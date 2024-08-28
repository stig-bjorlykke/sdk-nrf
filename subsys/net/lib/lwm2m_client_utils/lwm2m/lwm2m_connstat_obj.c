/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/lwm2m.h>
#include <lwm2m_object.h>
#include <lwm2m_engine.h>
#include <nrf_modem_at.h>

LOG_MODULE_REGISTER(lwm2m_connstat, CONFIG_LWM2M_CLIENT_UTILS_LOG_LEVEL);

#define CONNSTAT_VERSION_MAJOR 1
#define CONNSTAT_VERSION_MINOR 0
#define CONNSTAT_MAX_ID 9

/* Connection Statistics resource IDs */
#define CONNSTAT_SMS_TX_COUNTER_ID     0
#define CONNSTAT_SMS_RX_COUNTER_ID     1
#define CONNSTAT_TX_DATA_ID            2
#define CONNSTAT_RX_DATA_ID            3
#define CONNSTAT_MAX_MSG_SIZE_ID       4
#define CONNSTAT_AVG_MSG_SIZE_ID       5
#define CONNSTAT_START_ID              6
#define CONNSTAT_STOP_ID               7
#define CONNSTAT_COLLECTION_PERIOD_ID  8

/*
 * Calculate resource instances as follows:
 * start with CONNSTAT_MAX_ID
 * subtract EXEC resources (2)
 */
#define RESOURCE_INSTANCE_COUNT (CONNSTAT_MAX_ID - 2)

/* resource state variables */
static uint32_t sms_tx_counter;
static uint32_t sms_rx_counter;
static uint32_t tx_data;
static uint32_t rx_data;
static uint32_t max_msg_size;
static uint32_t avg_msg_size;
static uint32_t collection_period;

static void connstat_work_handler(struct k_work *work);

/* Delayed work that is used to stop statistics. */
static K_WORK_DELAYABLE_DEFINE(connstat_work, connstat_work_handler);

static struct lwm2m_engine_obj connstat;
static struct lwm2m_engine_obj_field fields[] = {
	OBJ_FIELD_DATA(CONNSTAT_SMS_TX_COUNTER_ID, R_OPT, U32),
	OBJ_FIELD_DATA(CONNSTAT_SMS_RX_COUNTER_ID, R_OPT, U32),
	OBJ_FIELD_DATA(CONNSTAT_TX_DATA_ID, R_OPT, U32),
	OBJ_FIELD_DATA(CONNSTAT_RX_DATA_ID, R_OPT, U32),
	OBJ_FIELD_DATA(CONNSTAT_MAX_MSG_SIZE_ID, R_OPT, U32),
	OBJ_FIELD_DATA(CONNSTAT_AVG_MSG_SIZE_ID, R_OPT, U32),
	OBJ_FIELD_EXECUTE_OPT(CONNSTAT_START_ID),
	OBJ_FIELD_EXECUTE_OPT(CONNSTAT_STOP_ID),
	OBJ_FIELD_DATA(CONNSTAT_COLLECTION_PERIOD_ID, RW_OPT, U32)
};

static struct lwm2m_engine_obj_inst inst;
static struct lwm2m_engine_res res[CONNSTAT_MAX_ID];
static struct lwm2m_engine_res_inst res_inst[RESOURCE_INSTANCE_COUNT];

static void connstat_exec(int command)
{
	nrf_modem_at_printf("AT%%XCONNSTAT=%d", command);
}

static void connstat_work_handler(struct k_work *work)
{
	LOG_INF("Stop collecting connectivity statistics");
	connstat_exec(0);
}

static void *connstat_resource_read_cb(uint16_t obj_inst_id, uint16_t res_id,
				       uint16_t res_inst_id, size_t *data_len)
{
	static uint32_t last_update;
	uint32_t now = k_uptime_get_32();
	void *data = NULL;
	int err;

	/* Update values if older than 1 second. */
	if (now - last_update > 1) {
		err = nrf_modem_at_scanf("AT%XCONNSTAT?", "%%XCONNSTAT: %d,%d,%d,%d,%d,%d",
					 &sms_tx_counter, &sms_rx_counter, &tx_data, &rx_data,
					 &max_msg_size, &avg_msg_size);
		if (err < 6) {
			LOG_ERR("Failed to read connectivity statistics");
		}
		last_update = now;
	}

	err = lwm2m_get_res_buf(&LWM2M_OBJ(7, obj_inst_id, res_id), &data, NULL, NULL, NULL);
	if (err) {
		LOG_ERR("Failed to read resource data");
	}

	return data;
}

static int lwm2m_connstat_start_cb(uint16_t obj_inst_id, uint8_t *args, uint16_t args_len)
{
	LOG_INF("Start collecting connectivity statistics");
	connstat_exec(1);

	if (collection_period) {
		k_work_schedule(&connstat_work, K_SECONDS(collection_period));
	}

	return 0;
}

static int lwm2m_connstat_stop_cb(uint16_t obj_inst_id, uint8_t *args, uint16_t args_len)
{
	LOG_INF("Stop collecting connectivity statistics");
	k_work_cancel_delayable(&connstat_work);
	connstat_exec(0);

	return 0;
}

static struct lwm2m_engine_obj_inst *connstat_create(uint16_t obj_inst_id)
{
	int i = 0, j = 0;

	init_res_instance(res_inst, ARRAY_SIZE(res_inst));

	/* initialize instance resource data */
	INIT_OBJ_RES_DATA(CONNSTAT_SMS_TX_COUNTER_ID, res, i, res_inst, j,
			  &sms_tx_counter, sizeof(sms_tx_counter));
	INIT_OBJ_RES_DATA(CONNSTAT_SMS_RX_COUNTER_ID, res, i, res_inst, j,
			  &sms_rx_counter, sizeof(sms_rx_counter));
	INIT_OBJ_RES_DATA(CONNSTAT_TX_DATA_ID, res, i, res_inst, j, &tx_data, sizeof(tx_data));
	INIT_OBJ_RES_DATA(CONNSTAT_RX_DATA_ID, res, i, res_inst, j, &rx_data, sizeof(rx_data));
	INIT_OBJ_RES_DATA(CONNSTAT_MAX_MSG_SIZE_ID, res, i, res_inst, j,
			  &max_msg_size, sizeof(max_msg_size));
	INIT_OBJ_RES_DATA(CONNSTAT_AVG_MSG_SIZE_ID, res, i, res_inst, j,
			  &avg_msg_size, sizeof(avg_msg_size));
	INIT_OBJ_RES_EXECUTE(CONNSTAT_START_ID, res, i, lwm2m_connstat_start_cb);
	INIT_OBJ_RES_EXECUTE(CONNSTAT_STOP_ID, res, i, lwm2m_connstat_stop_cb);
	INIT_OBJ_RES_DATA(CONNSTAT_COLLECTION_PERIOD_ID, res, i, res_inst, j,
			  &collection_period, sizeof(collection_period));

	inst.resources = res;
	inst.resource_count = i;

	LOG_DBG("Create LwM2M connection statistics instance: %d", obj_inst_id);

	return &inst;
}

static int lwm2m_init_connstat_cb(void)
{
	/* create object */
	lwm2m_create_object_inst(&LWM2M_OBJ(7, 0));
	lwm2m_register_read_callback(&LWM2M_OBJ(7, 0, 0), connstat_resource_read_cb);
	lwm2m_register_read_callback(&LWM2M_OBJ(7, 0, 1), connstat_resource_read_cb);
	lwm2m_register_read_callback(&LWM2M_OBJ(7, 0, 2), connstat_resource_read_cb);
	lwm2m_register_read_callback(&LWM2M_OBJ(7, 0, 3), connstat_resource_read_cb);
	lwm2m_register_read_callback(&LWM2M_OBJ(7, 0, 4), connstat_resource_read_cb);
	lwm2m_register_read_callback(&LWM2M_OBJ(7, 0, 5), connstat_resource_read_cb);

	return 0;
}

static int lwm2m_connstat_init(void)
{
	connstat.obj_id = LWM2M_OBJECT_CONNECTIVITY_STATISTICS_ID;
	connstat.version_major = CONNSTAT_VERSION_MAJOR;
	connstat.version_minor = CONNSTAT_VERSION_MINOR;
	connstat.is_core = true;
	connstat.fields = fields;
	connstat.field_count = ARRAY_SIZE(fields);
	connstat.max_instance_count = 1U;
	connstat.create_cb = connstat_create;
	lwm2m_register_obj(&connstat);

	LOG_DBG("Init LwM2M connectivity statistics object");
	return 0;
}

LWM2M_OBJ_INIT(lwm2m_connstat_init);
LWM2M_APP_INIT(lwm2m_init_connstat_cb);
