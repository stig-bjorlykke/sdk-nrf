/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <modem/nrf_modem_lib.h>
#include <modem/modem_info.h>
#include <modem/pdn.h>
#include <modem/uicc_lwm2m.h>
#include <net/lwm2m_client_utils.h>

#include "slm_at_host.h"
#include "slm_settings.h"
#include "slm_util.h"
#include "slm_at_lwm2m.h"

LOG_MODULE_REGISTER(slm_lwm2m, CONFIG_SLM_LOG_LEVEL);

#define IMEI_LEN 15
#define ICCID_LEN 20
static uint8_t imei_buf[IMEI_LEN + 1];
static uint8_t iccid_buf[ICCID_LEN + 1];
#define ENDPOINT_NAME_LEN (IMEI_LEN + 14 + 1)
static uint8_t endpoint_name[ENDPOINT_NAME_LEN + 1];
static struct lwm2m_ctx client;

/* Todo: Document event level */
enum lwm2m_event_level {
	LWM2M_EVENT_NONE,
	LWM2M_EVENT_FOTA,
	LWM2M_EVENT_CLIENT,
	LWM2M_EVENT_PDN,
	LWM2M_EVENT_LAST,
};

static bool connected;
static bool auto_connected;
static bool no_serv_suspended;
static int lwm2m_event_level;

static void slm_lwm2m_rd_client_start(void);
static void slm_lwm2m_rd_client_stop(void);
extern int slm_lwm2m_init_device(char *serial_num);

K_SEM_DEFINE(link_up_sem, 0, 1);

/* Callback handler triggered when the modem should be put in a certain functional mode.
 * Handler is called pre provisioning of DTLS credentials when the modem should be put in offline
 * mode, and when the modem should return to normal mode after provisioning has been carried out.
 */
static int slm_modem_mode_request_cb(enum lte_lc_func_mode new_mode, void *user_data)
{
	ARG_UNUSED(user_data);
	int ret;

	k_sem_reset(&link_up_sem);

	ret = slm_util_at_printf("AT+CFUN=%u", new_mode);
	if (ret < 0) {
		LOG_ERR("Failed to set modem mode (%d)", ret);
		return ret;
	}

	if (new_mode == 1) {
		/* Wait for link up. */
		k_sem_take(&link_up_sem, K_FOREVER);
	}

	return 0;
}

static void slm_lwm2m_event(int type, int event)
{
	if (type <= lwm2m_event_level) {
		rsp_send("\r\n#XLWM2MEVT: %d,%d\r\n", type, event);
	}
}

/* Automatically start/stop when the default PDN connection goes up/down. */
static void slm_pdp_ctx_event_cb(uint8_t cid, enum pdn_event event, int reason)
{
	ARG_UNUSED(cid);
	ARG_UNUSED(reason);

	switch (event) {
	case PDN_EVENT_ACTIVATED:
		LOG_INF("Connection up");
		k_sem_give(&link_up_sem);

		if (IS_ENABLED(CONFIG_SLM_LWM2M_AUTO_STARTUP) && !auto_connected) {
			LOG_INF("LTE connected, auto-start LwM2M engine");
			slm_lwm2m_rd_client_start();
			auto_connected = true;
		} else if (connected && no_serv_suspended) {
			LOG_INF("LTE connected, resuming LwM2M engine");
			lwm2m_engine_resume();
		}
		no_serv_suspended = false;
		break;
	case PDN_EVENT_DEACTIVATED:
	case PDN_EVENT_NETWORK_DETACH:
		LOG_INF("Connection down");
		if (connected) {
			LOG_INF("LTE not connected, suspending LwM2M engine");
			lwm2m_engine_pause();
			no_serv_suspended = true;
		}
		break;
	default:
		LOG_INF("PDN connection event %d", event);
		break;
	}

	slm_lwm2m_event(LWM2M_EVENT_PDN, event);
}

void client_acknowledge(void)
{
	lwm2m_acknowledge(&client);
}

static int slm_lwm2m_firmware_event_cb(struct lwm2m_fota_event *event)
{
	switch (event->id) {
	case LWM2M_FOTA_DOWNLOAD_START:
		/* FOTA download process started */
		LOG_INF("FOTA download started for instance %d", event->download_start.obj_inst_id);
		break;

	case LWM2M_FOTA_DOWNLOAD_FINISHED:
		/* FOTA download process finished */
		LOG_INF("FOTA download ready for instance %d, dfu_type %d",
			event->download_ready.obj_inst_id, event->download_ready.dfu_type);
		break;

	case LWM2M_FOTA_UPDATE_IMAGE_REQ:
		/* FOTA update new image */
		LOG_INF("FOTA update request for instance %d, dfu_type %d",
			event->update_req.obj_inst_id, event->update_req.dfu_type);
		break;

	case LWM2M_FOTA_UPDATE_MODEM_RECONNECT_REQ:
		/* FOTA requests modem re-initialization and client re-connection */
		/* Return -1 to cause normal system reboot */
		return -1;

	case LWM2M_FOTA_UPDATE_ERROR:
		/* FOTA process failed or was cancelled */
		LOG_ERR("FOTA failure %d by status %d", event->failure.obj_inst_id,
			event->failure.update_failure);
		break;
	}

	slm_lwm2m_event(LWM2M_EVENT_FOTA, event->id);

	return 0;
}

static void slm_lwm2m_rd_client_event_cb(struct lwm2m_ctx *client_ctx,
					 enum lwm2m_rd_client_event client_event)
{
	switch (client_event) {
	case LWM2M_RD_CLIENT_EVENT_NONE:
		LOG_INF("Invalid event");
		break;

	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_FAILURE:
		LOG_WRN("Bootstrap registration failure");
		break;

	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_REG_COMPLETE:
		LOG_INF("Bootstrap registration complete");
		connected = false;
		break;

	case LWM2M_RD_CLIENT_EVENT_BOOTSTRAP_TRANSFER_COMPLETE:
		LOG_INF("Bootstrap transfer complete");
		/* Workaround an issue with server being disabled after Register 4.03 Forbidden */
		/* lwm2m_server_reset_timestamps(); */
		break;

	case LWM2M_RD_CLIENT_EVENT_REGISTRATION_FAILURE:
		LOG_WRN("Registration failure");
		break;

	case LWM2M_RD_CLIENT_EVENT_REGISTRATION_COMPLETE:
		LOG_INF("Registration complete");
		connected = true;
		break;

	case LWM2M_RD_CLIENT_EVENT_REG_TIMEOUT:
		LOG_WRN("Registration timeout");
		break;

	case LWM2M_RD_CLIENT_EVENT_REG_UPDATE_COMPLETE:
		LOG_INF("Registration update complete");
		connected = true;
		break;

	case LWM2M_RD_CLIENT_EVENT_DEREGISTER_FAILURE:
		LOG_WRN("Deregister failure");
		connected = false;
		break;

	case LWM2M_RD_CLIENT_EVENT_DISCONNECT:
		LOG_INF("Disconnected");
		connected = false;
		break;

	case LWM2M_RD_CLIENT_EVENT_QUEUE_MODE_RX_OFF:
		LOG_INF("Queue mode RX window closed");
		break;

	case LWM2M_RD_CLIENT_EVENT_ENGINE_SUSPENDED:
		LOG_INF("Engine suspended");
		break;

	case LWM2M_RD_CLIENT_EVENT_NETWORK_ERROR:
		LOG_WRN("Network error");
		break;

	case LWM2M_RD_CLIENT_EVENT_REG_UPDATE:
		LOG_INF("Registration update");
		break;

	case LWM2M_RD_CLIENT_EVENT_DEREGISTER:
		LOG_INF("Deregister");
		break;

	case LWM2M_RD_CLIENT_EVENT_SERVER_DISABLED:
		LOG_INF("Server disabled");
		break;
	}

	slm_lwm2m_event(LWM2M_EVENT_CLIENT, client_event);
}

int slm_at_lwm2m_init(void)
{
	int ret;
	struct modem_mode_change mode_change = {
		.cb = slm_modem_mode_request_cb,
		.user_data = NULL
	};

	lwm2m_modem_mode_init(&mode_change);

	pdn_default_ctx_cb_reg(slm_pdp_ctx_event_cb);

	if (IS_ENABLED(CONFIG_SLM_LWM2M_ENDPOINT_CLIENT_NAME_ICCID)) {
		/* Turn on SIM to read ICCID. */
		ret = slm_util_at_printf("AT+CFUN=41");
		if (ret < 0) {
			LOG_ERR("Failed to set modem mode (%d)", ret);
			return ret;
		}
	}

	ret = modem_info_init();
	if (ret < 0) {
		LOG_ERR("Unable to init modem_info (%d)", ret);
		return ret;
	}

	/* Query IMEI. */
	ret = modem_info_string_get(MODEM_INFO_IMEI, imei_buf, sizeof(imei_buf));
	if (ret < 0) {
		LOG_ERR("Unable to get IMEI");
		return ret;
	}

	if (IS_ENABLED(CONFIG_SLM_LWM2M_ENDPOINT_CLIENT_NAME_ICCID)) {
		/* Query ICCID. */
		ret = modem_info_string_get(MODEM_INFO_ICCID, iccid_buf, sizeof(iccid_buf));
		if (ret < 0) {
			LOG_ERR("Unable to get ICCID");
			return ret;
		}

		iccid_buf[strlen(iccid_buf) - 1] = '\0'; /* Remove checksum digit. */
		snprintk(endpoint_name, sizeof(endpoint_name), "%s", iccid_buf);
	} else {
		/* Use IMEI as unique endpoint name. */
		snprintk(endpoint_name, sizeof(endpoint_name), "%s%s", "urn:imei:", imei_buf);
	}

	slm_lwm2m_init_device(imei_buf);
	lwm2m_init_security(&client, endpoint_name);

	if (sizeof(CONFIG_SLM_LWM2M_PSK) > 1) {
		/* Write hard-coded PSK key to the engine. First security instance is the right
		 * one, because in bootstrap mode, it is the bootstrap PSK. In normal mode, it is
		 * the server key.
		 */
		lwm2m_security_set_psk(0, CONFIG_SLM_LWM2M_PSK,
				       sizeof(CONFIG_SLM_LWM2M_PSK), true,
				       endpoint_name);
	}

	if (IS_ENABLED(CONFIG_LWM2M_CLIENT_UTILS_FIRMWARE_UPDATE_OBJ_SUPPORT)) {
		lwm2m_init_firmware_cb(slm_lwm2m_firmware_event_cb);

		ret = lwm2m_init_image();
		if (ret < 0) {
			LOG_ERR("Failed to setup image properties (%d)", ret);
			return 0;
		}
	}

	/* Disable unnecessary time updates. */
	lwm2m_update_device_service_period(0);

	return 0;
}

int slm_at_lwm2m_uninit(void)
{
	connected = false;
	auto_connected = false;
	no_serv_suspended = false;

	slm_lwm2m_rd_client_stop();

	return 0;
}

static void slm_lwm2m_rd_client_start_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	uint32_t flags = 0;

	if (lwm2m_security_needs_bootstrap()) {
		flags |= LWM2M_RD_CLIENT_FLAG_BOOTSTRAP;
	}

	LOG_INF("Starting LwM2M client");

	lwm2m_rd_client_start(&client, endpoint_name, flags, slm_lwm2m_rd_client_event_cb, NULL);
}

static K_WORK_DEFINE(slm_lwm2m_rd_client_start_work, slm_lwm2m_rd_client_start_work_fn);

static void slm_lwm2m_rd_client_start(void)
{
	k_work_submit(&slm_lwm2m_rd_client_start_work);
}

static void slm_lwm2m_rd_client_stop_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	LOG_INF("Stopping LwM2M client");

	lwm2m_rd_client_stop(&client, slm_lwm2m_rd_client_event_cb, false);
}

static K_WORK_DEFINE(slm_lwm2m_rd_client_stop_work, slm_lwm2m_rd_client_stop_work_fn);

static void slm_lwm2m_rd_client_stop(void)
{
	k_work_submit(&slm_lwm2m_rd_client_stop_work);
}

/* AT#XLWM2M="connect" */
SLM_AT_CMD_CUSTOM(xlwm2m_connect, "AT#XLWM2M=\"connect\"", do_lwm2m_connect);
static int do_lwm2m_connect(enum at_cmd_type, const struct at_param_list *, uint32_t)
{
	slm_lwm2m_rd_client_start();

	return 0;
}

/* AT#XLWM2M="disconnect" */
SLM_AT_CMD_CUSTOM(xlwm2m_disconnect, "AT#XLWM2M=\"disconnect\"", do_lwm2m_disconnect);
static int do_lwm2m_disconnect(enum at_cmd_type, const struct at_param_list *, uint32_t)
{
	LOG_INF("Stopping LwM2M client");

	return lwm2m_rd_client_stop(&client, slm_lwm2m_rd_client_event_cb, true);
}

/* AT#XLWM2M="suspend" */
SLM_AT_CMD_CUSTOM(xlwm2m_suspend, "AT#XLWM2M=\"suspend\"", do_lwm2m_suspend);
static int do_lwm2m_suspend(enum at_cmd_type, const struct at_param_list *, uint32_t)
{
	return lwm2m_engine_pause();
}

/* AT#XLWM2M="resume" */
SLM_AT_CMD_CUSTOM(xlwm2m_resume, "AT#XLWM2M=\"resume\"", do_lwm2m_resume);
static int do_lwm2m_resume(enum at_cmd_type, const struct at_param_list *, uint32_t)
{
	return lwm2m_engine_resume();
}

/* AT#XLWM2M="update" */
SLM_AT_CMD_CUSTOM(xlwm2m_update, "AT#XLWM2M=\"update\"", do_lwm2m_update);
static int do_lwm2m_update(enum at_cmd_type, const struct at_param_list *, uint32_t)
{
	lwm2m_rd_client_update();

	return 0;
}

SLM_AT_CMD_CUSTOM(xlwm2mevt, "AT#XLWM2MEVT", handle_at_lwm2m_event);
static int handle_at_lwm2m_event(enum at_cmd_type cmd_type, const struct at_param_list *param_list,
				 uint32_t param_count)
{
	int event_level;
	int err = 0;

	switch (cmd_type) {
	case AT_CMD_TYPE_SET_COMMAND:
		/* Set LwM2M event reporting level. */
		err = at_params_int_get(param_list, 1, &event_level);
		if (err || (event_level < LWM2M_EVENT_NONE) || (event_level >= LWM2M_EVENT_LAST)) {
			err = -EINVAL;
		} else {
			lwm2m_event_level = event_level;
		}
		break;

	case AT_CMD_TYPE_READ_COMMAND:
		rsp_send("\r\n#XLWM2MEVT: %d\r\n", lwm2m_event_level);
		break;

	case AT_CMD_TYPE_TEST_COMMAND:
		rsp_send("\r\n#XLWM2MEVT: (0,1,2,3)\r\n");
		break;

	default:
		break;
	}

	return err;
}

/* AT#XLWM2M="uicc" */
SLM_AT_CMD_CUSTOM(xlwm2m_uicc, "AT#XLWM2M=\"uicc\"", do_lwm2m_uicc);
static int do_lwm2m_uicc(enum at_cmd_type, const struct at_param_list *, uint32_t)
{
	/* Read UICC LwM2M bootstrap record. */
	char buffer[UICC_RECORD_BUFFER_MAX];
	int ret;

	ret = uicc_lwm2m_bootstrap_read(buffer, sizeof(buffer));
	if (ret > 0) {
		rsp_send("\r\n#XLWM2M: \"uicc\",");
		data_send(buffer, ret);
		rsp_send("\r\n");
		return 0;
	}

	return -1;
}
