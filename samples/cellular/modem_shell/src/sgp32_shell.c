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

#define UICC_SELECT_IPAE_APPLET    "AT+CSIM=44,\"01A4040C10D276000118FB01FF34100089C003080900\""
#define UICC_SELECT_GATEWAY_APPLET "AT+CSIM=42,\"01A4040C10D276000118FB01FF34100089C0031D09\""
#define UICC_SELECT_ISD_APPLET     "AT+CSIM=44,\"01A4040C10A0000005591010FFFFFFFF890000010000\""
#define UICC_SELECT_TAC_APPLET     "AT+CSIM=40,\"01A4040C0FA00000003053F12816620101654E53\""

static uint8_t response[24 + 256*2 + 1];

static int exec_command_ret_sw(const char *command, uint8_t *sw1, uint8_t *sw2)
{
	int err;

	mosh_print("%s", command);
	err = nrf_modem_at_cmd(response, sizeof(response), "%s", command);
	if (err) {
		mosh_error("Failed to exec %s, error: %d", command, err);
		return err;
	}
	mosh_print("%s", response);

	if (sw1 && sw2) {
		/* Response is "+CSIM: xxx,"dataSWSW"\r\nOK\r\n" */
		size_t len = strlen(response);
		if (len < 12) {
			mosh_error("Response too short for SW1/SW2");
			*sw1 = *sw2 = 0;
			return -EINVAL;
		}
		hex2bin(&response[len - 11], 2, sw1, sizeof(*sw1));
		hex2bin(&response[len - 9], 2, sw2, sizeof(*sw2));
	}

	return 0;
}

static int exec_command(const char *command)
{
	uint8_t sw1, sw2;
	char cmd[64];

	if (exec_command_ret_sw(command, &sw1, &sw2) != 0) {
		return -EINVAL;
	}

	while (sw1 == 0x61 || sw1 == 0x6C) { /* 0x61 = Response Ready */
		/* Read Response */
		snprintf(cmd, sizeof(cmd), "AT+CSIM=10,\"01C00000%02X\"", sw2);

		if (exec_command_ret_sw(cmd, &sw1, &sw2) != 0) {
			return -EINVAL;
		}
	}

	return 0;
}

static int cmd_gd_trigger(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D eIM trigger");

	exec_command(UICC_OPEN_CHANNEL);
	if (exec_command(UICC_SELECT_IPAE_APPLET) == 0) {
		/* Trigger polling */
		exec_command("AT+CSIM=10,\"81A0000000\"");
	}
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
	if (exec_command(UICC_SELECT_IPAE_APPLET) == 0) {
		exec_command(cmd);
	}
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
	if (exec_command(UICC_SELECT_GATEWAY_APPLET) == 0) {
		exec_command(cmd);
	}
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
	if (exec_command(UICC_SELECT_GATEWAY_APPLET) == 0) {
		exec_command(cmd);
	}
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmg_gd_euicc_memory_meset(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D IPAe eUICC memory reset");

	exec_command(UICC_OPEN_CHANNEL);
	if (exec_command(UICC_SELECT_IPAE_APPLET) == 0) {
		/* Send EuiccMemoryReset */
		exec_command("AT+CSIM=24,\"81E2910007BF64048202010400\"");
	}
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmg_gd_exec_fallback(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D Gateway execute fallback mechanism");

	exec_command(UICC_OPEN_CHANNEL);
	if (exec_command(UICC_SELECT_GATEWAY_APPLET) == 0) {
		/* Send ExecuteFallbackMechanism */
		exec_command("AT+CSIM=22,\"81E2910006BF5D038001FF00\"");
	}
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_gd_return_from_fallback(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D Gateway return from fallback");

	exec_command(UICC_OPEN_CHANNEL);
	if (exec_command(UICC_SELECT_GATEWAY_APPLET) == 0) {
		/* Send ReturnFromFallback */
		exec_command("AT+CSIM=22,\"81E2910006BF5E038001FF00\"");
	}
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_tac_trigger(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("TAC poll triggered");

	exec_command(UICC_OPEN_CHANNEL);
	if (exec_command(UICC_SELECT_TAC_APPLET) == 0) {
		/* Run Local Polling */
		exec_command("AT+CSIM=16,\"8100000003400101\"");
	}
	exec_command(UICC_CLOSE_CHANNEL);
	/* STATUS command */
	exec_command("AT+CSIM=10,\"80F2000000\"");

	return 0;
}

static int cmd_euicc_info1(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD EUICCInfo1Request");

	exec_command(UICC_OPEN_CHANNEL);
	if (exec_command(UICC_SELECT_ISD_APPLET) == 0) {
		/* EUICCInfo1 */
		exec_command("AT+CSIM=18,\"81E2910003BF200000\"");
	}
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_euicc_info2(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD EUICCInfo2Request");

	exec_command(UICC_OPEN_CHANNEL);
	if (exec_command(UICC_SELECT_ISD_APPLET) == 0) {
		/* EUICCInfo2 */
		exec_command("AT+CSIM=18,\"81E2910003BF220000\"");
	}
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_get_certs(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD GetCertsRequestRequest");

	exec_command(UICC_OPEN_CHANNEL);
	if (exec_command(UICC_SELECT_ISD_APPLET) == 0) {
		/* GetCertsRequest */
		exec_command("AT+CSIM=18,\"81E2910003BF560000\"");
	}
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_get_conn_params(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD GetConnectivityParametersRequest");

	exec_command(UICC_OPEN_CHANNEL);
	if (exec_command(UICC_SELECT_ISD_APPLET) == 0) {
		/* GetConnectivityParameters */
		exec_command("AT+CSIM=18,\"81E2910003BF5F0000\"");
	}
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_get_eim_config(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD GetEimConfigurationDataRequest");

	exec_command(UICC_OPEN_CHANNEL);
	if (exec_command(UICC_SELECT_ISD_APPLET) == 0) {
		/* GetEIMConfigData */
		exec_command("AT+CSIM=18,\"81E2910003BF550000\"");
	}
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_notifications_list(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD RetrieveNotificationsListRequest");

	exec_command(UICC_OPEN_CHANNEL);
	if (exec_command(UICC_SELECT_ISD_APPLET) == 0) {
		/* RetrieveNotificationsListRequest */
		exec_command("AT+CSIM=16,\"81E2910003BF2B0000\"");
	}
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

static int cmd_profile_info_list(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD ProfileInfoListRequest");

	exec_command(UICC_OPEN_CHANNEL);
	if (exec_command(UICC_SELECT_ISD_APPLET) == 0) {
		/* ProfileInfoListRequest */
		exec_command("AT+CSIM=18,\"81E2910003BF2D0000\"");
	}
	exec_command(UICC_CLOSE_CHANNEL);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_isd,
	SHELL_CMD(info1, NULL, "EUICCInfo1Request", cmd_euicc_info1),
	SHELL_CMD(info2, NULL, "EUICCInfo2Request", cmd_euicc_info2),
	SHELL_CMD(get_certs, NULL, "GetCertsRequest", cmd_get_certs),
	SHELL_CMD(get_conn_param, NULL, "GetConnectivityParametersRequest", cmd_get_conn_params),
	SHELL_CMD(get_eim_config, NULL, "GetEimConfigurationDataRequest", cmd_get_eim_config),
	SHELL_CMD(notif_list, NULL, "RetrieveNotificationsListRequest", cmd_notifications_list),
	SHELL_CMD(profile_info_list, NULL, "ProfileInfoListRequest", cmd_profile_info_list),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_gd,
	SHELL_CMD(exec_fallback, NULL, "Gateway Execute Fallback Mechanism", cmg_gd_exec_fallback),
	SHELL_CMD(memory_reset, NULL, "IPAe eUICC Memory Reset", cmg_gd_euicc_memory_meset),
	SHELL_CMD(return_fallback, NULL, "Gateway Return From Fallback", cmd_gd_return_from_fallback),
	SHELL_CMD(set_eim_address, NULL, "Set IPAe eIM Address", cmd_gd_set_eim_address),
	SHELL_CMD(set_timer_value, NULL, "Set Gateway Timer Value", cmd_gd_set_timer_value),
	SHELL_CMD(set_trigger_mechanism, NULL, "Set Gateway Trigger Mechanism", cmd_gd_set_trigger),
	SHELL_CMD(trigger, NULL, "IPAe eIM Trigger", cmd_gd_trigger),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_tac,
	SHELL_CMD(trigger, NULL, "TAC Trigger", cmd_tac_trigger),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sgp32,
	SHELL_CMD(g+d, &sub_gd, "G+D commands", mosh_print_help_shell),
	SHELL_CMD(isd, &sub_isd, "ISD (Issuer Security Domain) commands", mosh_print_help_shell),
	SHELL_CMD(tac, &sub_tac, "Thales commands", mosh_print_help_shell),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sgp32, &sub_sgp32, "Commands for SGP.32", mosh_print_help_shell);
