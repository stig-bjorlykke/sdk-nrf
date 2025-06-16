/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/shell/shell.h>
#include <zephyr/net/socket.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <nrf_modem_at.h>

#include "mosh_defines.h"
#include "mosh_print.h"

#define UICC_OPEN_CHANNEL  "AT+CSIM=10,\"0070000001\""
#define UICC_CLOSE_CHANNEL "AT+CSIM=8,\"00708001\""

#define UICC_SELECT_IPAE_APPLET    "AT+CSIM=44,\"01A4040010D276000118FB01FF34100089C003080900\""
#define UICC_SELECT_GATEWAY_APPLET "AT+CSIM=42,\"01A4040010D276000118FB01FF34100089C0031D09\""
#define UICC_SELECT_TAC_APPLET     "AT+CSIM=40,\"01A4040C0FA00000003053F12816620101654E53\""

static void exec_command(const char *command)
{
	uint8_t response[255];
	int err;

	mosh_print("%s", command);
	err = nrf_modem_at_cmd(response, sizeof(response), "%s", command);
	if (err) {
		mosh_error("Failed to exec %s, error: %d", command, err);
	}
	mosh_print("%s", response);
}

static int cmd_gd_trigger(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D eIM trigger");

	exec_command(UICC_OPEN_CHANNEL);
	exec_command(UICC_SELECT_IPAE_APPLET);
	exec_command("AT+CSIM=10,\"81A0000000\""); /* Trigger polling */
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_gd_set_eim_address(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t ip_address;
	char cmd[64];

	if (argc < 2) {
		struct addrinfo hints = { .ai_family = AF_INET };
		struct addrinfo *result;
		int err;

		err = getaddrinfo("https.prp.eim.gdiotsuite.com", NULL, &hints, &result);
		if (err) {
			mosh_error("Failed to resolve G+D eIM address: %d / %d", err, errno);
			return -EINVAL;
		}

		mosh_print("Resolved G+D eIM address to %s",
			   inet_ntoa(((struct sockaddr_in *)result->ai_addr)->sin_addr));
		ip_address = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
		freeaddrinfo(result);
	} else {
		if (inet_pton(AF_INET, argv[1], &ip_address) != 1) {
			mosh_error("Invalid IP address: %s", argv[1]);
			return -EINVAL;
		}

		mosh_print("Set G+D eIM address to %s", argv[1]);
	}

	snprintf(cmd, sizeof(cmd), "AT+CSIM=28,\"81E2910009A6073E0521%08X\"", ntohl(ip_address));

	exec_command(UICC_OPEN_CHANNEL);
	exec_command(UICC_SELECT_IPAE_APPLET);
	exec_command(cmd);
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_gd_set_trigger(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		mosh_print("Usage: gd %s <trigger_mechanism>", argv[0]);
		mosh_print(" 0 - none");
		mosh_print(" 1 - timer");
		mosh_print(" 2 - reset");
		return -EINVAL;
	}

	uint32_t trigger = atol(argv[1]);
	char cmd[64];

	if (trigger > 2) {
		mosh_error("Invalid trigger value: %s, must be between 0 and 2", argv[1]);
		return -EINVAL;
	}

	mosh_print("Set G+D Gateway trigger mechanism to %s",
		   trigger == 0 ? "none" : (trigger == 1 ? "timer" : "reset"));
	snprintf(cmd, sizeof(cmd), "AT+CSIM=18,\"81E29100049F0201%02X\"", trigger);

	exec_command(UICC_OPEN_CHANNEL);
	exec_command(UICC_SELECT_GATEWAY_APPLET);
	exec_command(cmd);
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_gd_set_timer_value(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		mosh_print("Usage: gd %s <hours> <minutes>", argv[0]);
		return -EINVAL;
	}

	uint32_t hours = atol(argv[1]);
	uint32_t minutes = 0;

	if (hours > 0x7FFF) {
		mosh_error("Invalid hours: %s, must be between 0 and %u", argv[1], 0x7FFF);
		return -EINVAL;
	}

	if (argc > 2) {
		minutes = atol(argv[2]);
		if (minutes > 59) {
			mosh_error("Invalid minutes: %s, must be between 0 and 59", argv[2]);
			return -EINVAL;
		}
	}

	uint32_t timer_value = (hours << 8) + minutes;
	char cmd[64];

	mosh_print("Set G+D Gateway timer value to %d hours %d minutes", hours, minutes);
	snprintf(cmd, sizeof(cmd), "AT+CSIM=22,\"81E29100069F0103%06X\"", timer_value);

	exec_command(UICC_OPEN_CHANNEL);
	exec_command(UICC_SELECT_GATEWAY_APPLET);
	exec_command(cmd);
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmg_gd_euicc_memory_meset(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D IPAe eUICC memory reset");

	exec_command(UICC_OPEN_CHANNEL);
	exec_command(UICC_SELECT_IPAE_APPLET);
	exec_command("AT+CSIM=24,\"81E2910007BF640482020104\""); /* Send EuiccMemoryReset */
	exec_command("AT+CSIM=10,\"01C0000009\"");               /* Read response */
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmg_gd_exec_fallback(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D Gateway execute fallback mechanism");

	exec_command(UICC_OPEN_CHANNEL);
	exec_command(UICC_SELECT_GATEWAY_APPLET);
	exec_command("AT+CSIM=22,\"81E2910006BF5D038001FF\""); /* Send ExecuteFallbackMechanism */
	exec_command("AT+CSIM=10,\"01C0000006\"");             /* Read response */
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_gd_return_from_fallback(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D Gateway return from fallback");

	exec_command(UICC_OPEN_CHANNEL);
	exec_command(UICC_SELECT_GATEWAY_APPLET);
	exec_command("AT+CSIM=22,\"81E2910006BF5E038001FF\""); /* Send ReturnFromFallback */
	exec_command("AT+CSIM=10,\"01C0000006\"");             /* Read response */
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_tac_trigger(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("TAC poll triggered");

	exec_command(UICC_OPEN_CHANNEL);
	exec_command(UICC_SELECT_TAC_APPLET);
	exec_command("AT+CSIM=16,\"8100000003400101\""); /* Run Local Polling */
	exec_command(UICC_CLOSE_CHANNEL);
	exec_command("AT+CSIM=10,\"80F2000000\"");       /* STATUS command */

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_gd,
	SHELL_CMD(exec_fallback, NULL, "Execute Fallback Mechanism", cmg_gd_exec_fallback),
	SHELL_CMD(memory_reset, NULL, "eUICC Memory Reset", cmg_gd_euicc_memory_meset),
	SHELL_CMD(return_fallback, NULL, "Return From Fallback", cmd_gd_return_from_fallback),
	SHELL_CMD(set_eim_address, NULL, "Set eIM Address", cmd_gd_set_eim_address),
	SHELL_CMD(set_timer_value, NULL, "Set Timer Value", cmd_gd_set_timer_value),
	SHELL_CMD(set_trigger_mechanism, NULL, "Set Trigger Mechanism", cmd_gd_set_trigger),
	SHELL_CMD(trigger, NULL, "eIM Trigger", cmd_gd_trigger),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sgp32,
	SHELL_CMD(g+d, &sub_gd, "G+D commands", mosh_print_help_shell),
	SHELL_CMD(tac_trigger, NULL, "TAC trigger", cmd_tac_trigger),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sgp32, &sub_sgp32, "Commands for SGP.32", mosh_print_help_shell);
