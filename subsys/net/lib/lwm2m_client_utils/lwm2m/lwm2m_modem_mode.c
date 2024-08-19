/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <net/lwm2m_client_utils.h>
#include <modem/lte_lc.h>

LOG_MODULE_REGISTER(lwm2m_modem_mode, CONFIG_LWM2M_CLIENT_UTILS_LOG_LEVEL);

static int lwm2m_modem_mode_cb(enum lte_lc_func_mode new_mode, void *user_data)
{
	int ret;

	if (IS_ENABLED(CONFIG_LTE_LINK_CONTROL)) {
		enum lte_lc_func_mode fmode;

		if (lte_lc_func_mode_get(&fmode)) {
			LOG_ERR("Failed to read modem functional mode");
			ret = -EFAULT;
			return ret;
		}

		/* Return success if the modem is in the required functional mode. */
		if (fmode == new_mode) {
			LOG_DBG("Modem already in requested state %d", new_mode);
			return 0;
		}

		if (new_mode == LTE_LC_FUNC_MODE_NORMAL) {
			/* I need to use the blocking call, because in next step
			* LwM2M engine would create socket and call connect()
			*/
			ret = lte_lc_connect();

			if (ret) {
				LOG_ERR("lte_lc_connect() failed %d", ret);
			}
			LOG_INF("Modem connection restored");
		} else {
			ret = lte_lc_func_mode_set(new_mode);
			if (ret == 0) {
				LOG_DBG("Modem set to requested state %d", new_mode);
			}
		}
	} else {
		ret = -ENOTSUP;
	}

	return ret;
}

static struct modem_mode_change mm = {
	.cb = lwm2m_modem_mode_cb,
	.user_data = NULL
};

const struct modem_mode_change *lwm2m_modem_mode(void)
{
	return &mm;
}

void lwm2m_modem_mode_init(struct modem_mode_change *mmode)
{
	if (mmode) {
		mm.cb = mmode->cb;
		mm.user_data = mmode->user_data;
	} else {
		/* Restore the default if not a callback function */
		mm.cb = lwm2m_modem_mode_cb;
		mm.user_data = NULL;
	}
}