/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/shell/shell.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <modem/nrf_modem_lib.h>

#include "mosh_defines.h"
#include "mosh_print.h"

#include "rtcosapi.h"

/* The XUICC Partition service ID defined in Partition YAML */
#define XUICC_SID 0x000000FA

static int cmd_nusim_apdu(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t c_apdu[COS_MAX_APDU_SIZE];
	uint8_t r_apdu[COS_MAX_APDU_RESP_SIZE];
	uint8_t r_apdu_hex[sizeof(r_apdu) * 2 + 1];
	uint16_t c_apdu_len;
	uint16_t r_apdu_len;
	int8_t res;

	if (argc < 2) {
		mosh_error("Usage: nusim apdu \"command APDU\"");
		return -EINVAL;
	}

	c_apdu_len = hex2bin(argv[1], strlen(argv[1]), c_apdu, sizeof(c_apdu));
	if (c_apdu_len <= 0) {
		mosh_error("Invalid command APDU format");
		return -EINVAL;
	}

	res = rt_cos_api_send_recv_msg(c_apdu, c_apdu_len, r_apdu, &r_apdu_len);
	if (res != COS_SUCCESS) {
		mosh_error("Command APDU error: %d", res);
		return -EINVAL;
	}

	bin2hex(r_apdu, r_apdu_len, r_apdu_hex, sizeof(r_apdu_hex));
	mosh_print("nuSIM response APDU (%d): %s", r_apdu_len, r_apdu_hex);

	return 0;
}

static int cmd_nusim_init(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t atr[64];
	uint8_t atr_hex[sizeof(atr) * 2 + 1];
	uint8_t atr_len;
	int8_t res;

	res = rt_cos_api_init(atr, &atr_len, XUICC_SID);
	if (res != COS_SUCCESS) {
		mosh_print("Init error: %d", res);
		return -EINVAL;
	}

	bin2hex(atr, atr_len, atr_hex, sizeof(atr_hex));
	mosh_print("nuSIM initialized (%d): %s", atr_len, atr_hex);

	return 0;
}

static int cmd_nusim_warm_reset(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("nuSIM warm reset");
	rt_cos_api_sytem_reset(WARM_RESET);

	return 0;
}

static int cmd_nusim_cold_reset(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("nuSIM cold reset");
	rt_cos_api_sytem_reset(COLD_RESET);

	return 0;
}

static int cmd_nusim_version(const struct shell *shell, size_t argc, char **argv)
{
	uint8_t ver_buf[64];
	uint16_t ver_len;
	int8_t res;

	res = rt_cos_api_get_ver(ver_buf, &ver_len);
	if (res != COS_SUCCESS) {
		mosh_print("Version error: %d", res);
		return -EINVAL;
	}

	mosh_print(ver_buf);

	return 0;
}

static int cmd_modem_init(const struct shell *shell, size_t argc, char **argv)
{
	int err;

	err = nrf_modem_lib_init();
	if (err) {
		mosh_error("Modem library init failed: %d", err);
		return err;
	}

	return 0;
}

static int cmd_modem_shutdown(const struct shell *shell, size_t argc, char **argv)
{
	int err;

	err = nrf_modem_lib_shutdown();
	if (err) {
		mosh_error("Modem library shutdown failed: %d", err);
		return err;
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_nusim_reset,
	SHELL_CMD(cold, NULL, "Cold reset", cmd_nusim_cold_reset),
	SHELL_CMD(warm, NULL, "Warm reset", cmd_nusim_warm_reset),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_nusim,
	SHELL_CMD(apdu, NULL, "nuSIM command APDU", cmd_nusim_apdu),
	SHELL_CMD(init, NULL, "nuSIM initialize", cmd_nusim_init),
	SHELL_CMD(reset, &sub_nusim_reset, "nuSIM reset", mosh_print_help_shell),
	SHELL_CMD(version, NULL, "nuSIM version", cmd_nusim_version),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_modem,
	SHELL_CMD(init, NULL, "Initialize modem", cmd_modem_init),
	SHELL_CMD(shutdown, NULL, "Shutdown modem", cmd_modem_shutdown),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(nusim, &sub_nusim, "Commands for nuSIM.", mosh_print_help_shell);
SHELL_CMD_REGISTER(modem, &sub_modem, "Commands for modem library.", mosh_print_help_shell);
