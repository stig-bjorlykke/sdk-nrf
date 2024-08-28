/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/ztest.h>
#include <zephyr/fff.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/lwm2m.h>
#include <net/lwm2m_client_utils.h>

#include "stubs.h"

ZTEST_SUITE(lwm2m_client_utils_connstat, NULL, NULL, NULL, NULL, NULL);

static lwm2m_engine_get_data_cb_t sms_tx_counter_cb;
static lwm2m_engine_get_data_cb_t sms_rx_counter_cb;
static lwm2m_engine_execute_cb_t start_cb;
static lwm2m_engine_execute_cb_t stop_cb;

static int lwm2m_register_read_callback_copy(const struct lwm2m_obj_path *path,
					     lwm2m_engine_get_data_cb_t cb)
{
	if (path->obj_id == 7 && path->obj_inst_id == 0 && path->res_id == 0) {
		sms_tx_counter_cb = cb;
	} else if (path->obj_id == 7 && path->obj_inst_id == 0 && path->res_id == 1) {
		sms_rx_counter_cb = cb;
	}

	return 0;
}

static int lwm2m_register_exec_callback_copy(const struct lwm2m_obj_path *path,
					     lwm2m_engine_execute_cb_t cb)
{
	if (path->obj_id == 7 && path->obj_inst_id == 0 && path->res_id == 6) {
		start_cb = cb;
	} else if (path->obj_id == 7 && path->obj_inst_id == 0 && path->res_id == 7) {
		stop_cb = cb;
	}

	return 0;
}

/* Stubbed */

static void setup(void)
{
	/* Register resets */
	DO_FOREACH_FAKE(RESET_FAKE);

	/* Reset common FFF internal structures */
	FFF_RESET_HISTORY();

	lwm2m_register_read_callback_fake.custom_fake = lwm2m_register_read_callback_copy;
	lwm2m_register_exec_callback_fake.custom_fake = lwm2m_register_exec_callback_copy;
	call_lwm2m_init_callbacks();
}

/*
 * Tests for initializing the module
 */

ZTEST(lwm2m_client_utils_connstat, test_connstat_start)
{
	setup();

	start_cb(0, NULL, 0);
	zassert_equal(nrf_modem_at_printf_fake.call_count, 1, "nrf_modem_at_printf not called");
}
