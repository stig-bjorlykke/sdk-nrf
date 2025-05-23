/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/shell/shell.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <nrf_modem_at.h>

#include "mosh_defines.h"
#include "mosh_print.h"

/* Giesecke+Devrient */
static const char *gd_trigger_commands[] = {
	"AT+CSIM=10,\"0070000001\"",       /* Open channel */
	"AT+CSIM=44,\"01A4040010D276000118FB01FF34100089C003080900\"", /* Select IPAe Applet */
	"AT+CSIM=10,\"81A0000000\"",       /* Trigger Polling */
	"AT+CSIM=10,\"0070800100\"",       /* Close Channel on logical channel 1 */
};

static const char *gd_exec_fallback_commands[] = {
	"AT+CSIM=10,\"0070000001\"",             /* Open channel */
	"AT+CSIM=42,\"01A4040010D276000118FB01FF34100089C0031D09\"", /* Select Gateway Applet */
	"AT+CSIM=22,\"81E2910006BF5D038001FF\"", /* Send ExecuteFallbackMechanism */
	"AT+CSIM=10,\"01C0000006\"",             /* Read response */
	"AT+CSIM=10,\"0070800100\""              /* Close Channel on logical channel 1 */
};

static const char *gd_return_fallback_commands[] = {
	"AT+CSIM=10,\"0070000001\"",             /* Open channel */
	"AT+CSIM=42,\"01A4040010D276000118FB01FF34100089C0031D09\"", /* Select Gateway Applet */
	"AT+CSIM=22,\"81E2910006BF5E038001FF\"", /* Send ReturnFromFallback */
	"AT+CSIM=10,\"01C0000006\"",             /* Read response */
	"AT+CSIM=10,\"0070800100\""              /* Close Channel on logical channel 1 */
};

static const char *gd_euicc_memory_reset_commands[] = {
	"AT+CSIM=10,\"0070000001\"",               /* Open channel */
	"AT+CSIM=44,\"01A4040010D276000118FB01FF34100089C003080900\"", /* Select IPAe Applet */
	"AT+CSIM=24,\"80E2910007BF640482020104\"", /* Send EuiccMemoryReset */
	"AT+CSIM=10,\"01C0000009\"",               /* Read response */
	"AT+CSIM=10,\"0070800100\""                /* Close Channel on logical channel 1 */
};

/* Tales Adaptive Connect v2 */
static const char *tac_trigger_commands[] = {
	"AT+CSIM=10,\"0070000001\"",       /* Open channel */
	"AT+CSIM=40,\"01A4040C0FA00000003053F12816620101654E53\"", /* Select TACv2 applet with AID */
	"AT+CSIM=16,\"8100000003400101\"", /* Run Local Polling */
	"AT+CSIM=10,\"0070800100\"",       /* Close Channel on logical channel 1 */
	"AT+CSIM=10,\"80F2000000\""        /* STATUS command */
};

static void exec_commands(const char **commands, size_t count)
{
	uint8_t response[255];
	int err;

	for (size_t i = 0; i < count; i++) {
		mosh_print("%s", commands[i]);
		err = nrf_modem_at_cmd(response, sizeof(response), "%s", commands[i]);
		if (err) {
			mosh_error("Failed to exec %s, error: %d", commands[i], err);
		}
		mosh_print("%s", response);
	}
}

static int cmd_gd_trigger(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D trigger mechanism");
	exec_commands(gd_trigger_commands, ARRAY_SIZE(gd_trigger_commands));

	return 0;
}

static int cmg_gd_euicc_memory_meset(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D EUICC memory reset");
	exec_commands(gd_euicc_memory_reset_commands, ARRAY_SIZE(gd_euicc_memory_reset_commands));

	return 0;
}

static int cmg_gd_exec_fallback(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D execute fallback mechanism");
	exec_commands(gd_exec_fallback_commands, ARRAY_SIZE(gd_exec_fallback_commands));

	return 0;
}

static int cmd_gd_return_from_fallback(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D return from fallback");
	exec_commands(gd_return_fallback_commands, ARRAY_SIZE(gd_return_fallback_commands));

	return 0;
}

static int cmd_tac_trigger(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("TAC poll triggered");
	exec_commands(tac_trigger_commands, ARRAY_SIZE(tac_trigger_commands));

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_gd,
	SHELL_CMD(exec_fallback, NULL, "Execute Fallback Mechanism", cmg_gd_exec_fallback),
	SHELL_CMD(memory_reset, NULL, "EUICC Memory Reset", cmg_gd_euicc_memory_meset),
	SHELL_CMD(return_fallback, NULL, "Return From Fallback", cmd_gd_return_from_fallback),
	SHELL_CMD(trigger, NULL, "Trigger Mechanism", cmd_gd_trigger),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sgp32,
	SHELL_CMD(gd, &sub_gd, "Giesecke+Devrient commands", mosh_print_help_shell),
	SHELL_CMD(tac_trigger, NULL, "TAC trigger", cmd_tac_trigger),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sgp32, &sub_sgp32, "Commands for SGP.32", mosh_print_help_shell);
