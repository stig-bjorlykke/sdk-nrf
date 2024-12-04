/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/hash_function.h>
#include <zephyr/logging/log.h>
#include <modem/uicc_lwm2m.h>
#include "lwm2m_message_handling.h"
#include "lwm2m_rw_oma_tlv.h"
#include "lwm2m_bootstrap_from_sim.h"
#include "lwm2m_security.h"

LOG_MODULE_REGISTER(lwm2m_bootstrap, CONFIG_LWM2M_CLIENT_UTILS_LOG_LEVEL);

#define HASH_KEY "hash"
#define BOOTSTRAP_KEY "bootstrap"
#define SETTINGS_SUBTREE_NAME "lwm2m:uicc"
#define SETTINGS_HASH_NAME SETTINGS_SUBTREE_NAME "/" HASH_KEY
#define SETTINGS_BOOTSTRAP_NAME SETTINGS_SUBTREE_NAME "/" BOOTSTRAP_KEY

static int current_hash;

/* Todo: Improve this. */
static uint8_t *bootstrap;
static ssize_t bootstrap_len;

static int settings_load_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	ARG_UNUSED(len);
	ssize_t sz = 0;

	if (strcmp(key, HASH_KEY) == 0) {
		sz = read_cb(cb_arg, &current_hash, sizeof(current_hash));
	} else if (strcmp(key, BOOTSTRAP_KEY) == 0) {
		bootstrap = k_malloc(len);
		bootstrap_len = len - 1;
		sz = read_cb(cb_arg, bootstrap, len);
	}

	if (sz < 0) {
		LOG_ERR("Settings read %s failed: %d", key, sz);
	}

	return 0;
}

static int settings_init(void)
{
	static struct settings_handler sh = {
		.name = SETTINGS_SUBTREE_NAME,
		.h_set = settings_load_cb,
	};
	int err;

	err = settings_subsys_init();
	if (err) {
		LOG_ERR("Settings init failed: %d", err);
		return err;
	}

	err = settings_register(&sh);
	if (err && err != -EEXIST) {
		LOG_ERR("Settings register failed: %d", err);
		return err;
	}

	err = settings_load_subtree(SETTINGS_SUBTREE_NAME);
	if (err) {
		LOG_ERR("Settings load subtree failed: %d", err);
		return err;
	}

	return 0;
}

static void validate_security_identity(const char *endpoint)
{
	char identity[CONFIG_LWM2M_SECURITY_KEY_SIZE] = { 0 };
	uint8_t mode = 0;
	int ret;

	if (!IS_ENABLED(CONFIG_LWM2M_DTLS_SUPPORT) || strlen(endpoint) == 0) {
		return;
	}

	/* Use client endpoint name if identity does not exists. */
	ret = lwm2m_get_u8(&LWM2M_OBJ(LWM2M_OBJECT_SECURITY_ID, 0, SECURITY_MODE_ID), &mode);
	if (ret || mode != SEC_MODE_PSK) {
		return;
	}

	ret = lwm2m_get_opaque(&LWM2M_OBJ(LWM2M_OBJECT_SECURITY_ID, 0, SECURITY_CLIENT_PK_ID),
			       identity, sizeof(identity));
	if (ret || strlen(identity) > 0) {
		return;
	}

	LOG_INF("Set security identity from endpoint: %s", endpoint);
	lwm2m_set_opaque(&LWM2M_OBJ(LWM2M_OBJECT_SECURITY_ID, 0, SECURITY_CLIENT_PK_ID),
			 (void *)endpoint, strlen(endpoint));
}

static int write_tlv_object(struct lwm2m_ctx *ctx, uint16_t object_id,
			    uint8_t *p_object, uint16_t object_len)
{
	struct coap_packet in_cpkt = {
		.data = p_object,
		.max_len = object_len,
	};
	struct lwm2m_message *msg;
	int err;

	msg = lwm2m_get_message(ctx);
	msg->in.in_cpkt = &in_cpkt;
	msg->in.reader = &oma_tlv_reader;
	msg->path.obj_id = object_id;
	msg->path.level = LWM2M_PATH_LEVEL_OBJECT;

	err = do_write_op_tlv(msg);

	lwm2m_reset_message(msg, true);

	return err;
}

static uint16_t get_uint16(uint8_t *p_buffer, uint16_t offset)
{
	return (p_buffer[offset] << 8) + p_buffer[offset + 1];
}

static int lwm2m_ef_decode(struct lwm2m_ctx *ctx, uint8_t *p_buffer, int buffer_size)
{
	uint16_t num_objects, objects_size, object_id, object_len;
	uint8_t version_major, version_minor;
	uint16_t offset = 0;
	int err;

	if (buffer_size < 4) {
		return -EINVAL;
	}

	num_objects = get_uint16(p_buffer, offset);
	offset += 2;

	objects_size = get_uint16(p_buffer, offset);
	offset += 2;

	if (buffer_size < objects_size + 4) {
		return -EINVAL;
	}

	for (int i = 0; i < num_objects; i++) {
		if (buffer_size < offset + 6) {
			return -EINVAL;
		}

		object_id = get_uint16(p_buffer, offset);
		offset += 2;

		version_major = p_buffer[offset++];
		if (version_major > 0) {
			version_minor = p_buffer[offset++];
		} else {
			version_minor = 0;
		}

		object_len = get_uint16(p_buffer, offset);
		offset += 2;

		if (buffer_size < offset + object_len) {
			return -EINVAL;
		}

		LOG_INF("Object ID: %u, version: %u.%u, length: %u",
			object_id, version_major, version_minor, object_len);

		err = write_tlv_object(ctx, object_id, &p_buffer[offset], object_len);
		if (err) {
			LOG_ERR("Failed to write object %u, err %d", object_id, err);
			return err;
		}

		offset += object_len;
	}

	return num_objects;
}

int lwm2m_bootstrap_from_sim(struct lwm2m_ctx *ctx, char *endpoint)
{
	uint8_t uicc_record[UICC_RECORD_BUFFER_MAX];
	int length = 0, num_objects = 0;
	int hash, err;

	err = settings_init();
	if (err) {
		return err;
	}

	/* 1. Bootstrap from SIM. */
	if (IS_ENABLED(CONFIG_UICC_LWM2M)) {
		length = uicc_lwm2m_bootstrap_read(uicc_record, sizeof uicc_record);
	}

	/* 2. Bootstrap from nRF Cloud. */
	if (length <= 0 && bootstrap_len > 0) {
		length = hex2bin(bootstrap, bootstrap_len, uicc_record, UICC_RECORD_BUFFER_MAX);
		k_free(bootstrap);
		bootstrap = NULL;
	}

	/* 3. Bootstrap from static config. */
	if (length <= 0 && strlen(CONFIG_LWM2M_CLIENT_UTILS_BOOTSTRAP_FROM_SIM_DATA)) {
		length = hex2bin(CONFIG_LWM2M_CLIENT_UTILS_BOOTSTRAP_FROM_SIM_DATA,
				 strlen(CONFIG_LWM2M_CLIENT_UTILS_BOOTSTRAP_FROM_SIM_DATA),
				 uicc_record, UICC_RECORD_BUFFER_MAX);
	}

	hash = sys_hash32(uicc_record, length);
	if (hash == current_hash) {
		/* No changes in SIM content. */
		return 0;
	}

	if (length > 4) {
		/* Content length is header (4 bytes) + objects size */
		int content_length = 4 + (uicc_record[2] << 8) + uicc_record[3];

		if (content_length < length) {
			length = content_length;
		}
	}

	if (length > 0) {
		ctx->bootstrap_mode = true;
		num_objects = lwm2m_ef_decode(ctx, uicc_record, length);

		if (num_objects) {
			validate_security_identity(endpoint);
			ctx->load_credentials(ctx);
		}

		settings_save_one(SETTINGS_HASH_NAME, &hash, sizeof(hash));
	} else {
		settings_delete(SETTINGS_HASH_NAME);
	}

	return num_objects;
}
