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
#include <netdb.h>
#include <arpa/inet.h>
#include <nrf_modem_at.h>

#include "mosh_defines.h"
#include "mosh_print.h"

#define UICC_SELECT_IPAE_APPLET    "AT+CSIM=44,\"01A4040C10D276000118FB01FF34100089C003080900\""
#define UICC_SELECT_GATEWAY_APPLET "AT+CSIM=42,\"01A4040C10D276000118FB01FF34100089C0031D09\""
#define UICC_SELECT_ISD_APPLET     "AT+CSIM=44,\"01A4040C10A0000005591010FFFFFFFF890000010000\""
#define UICC_SELECT_TAC_APPLET     "AT+CSIM=36,\"01A404000DA00000084455F1279166010101\""

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
	char cmd[512];

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

static int exec_applet_command(const char *select_applet, const char *command)
{
	char cmd[512];

	exec_command("AT+CSIM=10,\"0070000001\""); /* Open channel */
	if (exec_command(select_applet) == 0) {
		snprintf(cmd, sizeof(cmd), "AT+CSIM=%d,\"%s\"", strlen(command), command);
		exec_command(cmd);
	}
	exec_command("AT+CSIM=8,\"00708001\""); /* Close channel */

	return 0;
}

static int exec_isd_ipad_command(const char *command)
{
	char cmd[512];

	exec_command("AT+CSIM=10,\"0070000001\""); /* Open channel */

	/* TERMINAL CAPABILITY with IPAd */
	exec_command("AT+CSIM=24,\"81AA000007A9058100840101\"");

	if (exec_command(UICC_SELECT_ISD_APPLET) == 0) {
		snprintf(cmd, sizeof(cmd), "AT+CSIM=%d,\"%s\"", strlen(command), command);
		exec_command(cmd);
	}

	/* IPAe activate */
	exec_command("AT+CSIM=26,\"81E2910007BF42048002078000\"");

	exec_command("AT+CSIM=8,\"00708001\""); /* Close channel */

	return 0;
}

static int cmd_gd_trigger(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D eIM trigger");
	exec_applet_command(UICC_SELECT_IPAE_APPLET, "81A0000000");

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

	snprintf(cmd, sizeof(cmd), "81E2910009A6073E0521%08X00", ntohl(ip_address));
	exec_applet_command(UICC_SELECT_IPAE_APPLET, cmd);

	return 0;
}

static char applet_cmd[2048];
static char gd_eim_config[2][4][512] = {
	{
	"81E21100F0BF57820385A08203813082037D801F312E332E362E312E342E312E31313739312E3130342E362E312E312E312E32811C6E67696E782E7072702E65696D2E6764696F7473756974652E636F6D820101830100A55BA059301306072A8648CE3D020106082A8648CE3D03010703420004220992323F91254F7ED527A30D82BB7BE68FD5259D9122CBEC6F85459AE234FC3F5007E5F191E587F086648449A872AD6BDABCD3A48F92F7BC5A31658265A862A68202D3A18202CF30820275A00302010202110093C9E096C07C2D1F31E5EBA4DFAF939E300A06082A8648CE3D040302308180310B300906035504061302444531",
	"81E21101F010300E0603550408130742617661726961310F300D060355040713064D756E696368312D302B060355040A1324472B44204D6F62696C6520536563757269747920544344204962657269612C20532E4C2E311F301D060355040313167072702E65696D2E6764696F7473756974652E636F6D301E170D3235303932323135303035305A170D3237303932333031303035305A308186310B30090603550406130244453110300E0603550408130742617661726961310F300D060355040713064D756E696368312D302B060355040A1324472B44204D6F62696C6520536563757269747920544344204962657269612C20",
	"81E21102F0532E4C2E312530230603550403131C6E67696E782E7072702E65696D2E6764696F7473756974652E636F6D3059301306072A8648CE3D020106082A8648CE3D0301070342000452B5D22560734731CCC3A4521574C146D7CFBE9F4CFDE63C5C36DE70119088C48B05C1250B30DF7A5E8FACE1AD2C8CB4713575F621747E0579277A4FD09C39B8A381C73081C4300E0603551D0F0101FF040403020780301D0603551D250416301406082B0601050507030106082B06010505070302300C0603551D130101FF04023000301D0603551D0E04160414D99959A3C7EA3BEAC75599766228E46229AD4857301F0603551D2304",
	"81E29103BA18301680143748833E38E7A9EEB47A64C2752B378489CF1FE830450603551D11043E303C821C68747470732E7072702E65696D2E6764696F7473756974652E636F6D821C6E67696E782E7072702E65696D2E6764696F7473756974652E636F6D300A06082A8648CE3D040302034800304502210099881C5BE1C5F7A6060BE582D73992381B32B4E1EEB77BEB2A463CAA3CE0EC000220073A4AD2235393D788C15ECEAB6DD8613A3F87D519F7F6AD7BAC6513F539B7608702038800"
	},
	{
	"81E21100F0BF57820385A08203813082037D801F312E332E362E312E342E312E31313739312E3130342E362E312E312E312E32811C6E67696E782E7072702E65696D2E6764696F7473756974652E636F6D820101830100A55BA059301306072A8648CE3D020106082A8648CE3D03010703420004A4ED6940C49F499EEDEA652E1CCE6BBFC1761903D663141A15F3929AB74F3F01FB296BEC82153BD01F783FC4DB5621012B5FACE561473CCD214837D61C1C021DA68202D3A18202CF30820275A00302010202110093C9E096C07C2D1F31E5EBA4DFAF939E300A06082A8648CE3D040302308180310B300906035504061302444531",
	"81E21101F010300E0603550408130742617661726961310F300D060355040713064D756E696368312D302B060355040A1324472B44204D6F62696C6520536563757269747920544344204962657269612C20532E4C2E311F301D060355040313167072702E65696D2E6764696F7473756974652E636F6D301E170D3235303932323135303035305A170D3237303932333031303035305A308186310B30090603550406130244453110300E0603550408130742617661726961310F300D060355040713064D756E696368312D302B060355040A1324472B44204D6F62696C6520536563757269747920544344204962657269612C20",
	"81E21102F0532E4C2E312530230603550403131C6E67696E782E7072702E65696D2E6764696F7473756974652E636F6D3059301306072A8648CE3D020106082A8648CE3D0301070342000452B5D22560734731CCC3A4521574C146D7CFBE9F4CFDE63C5C36DE70119088C48B05C1250B30DF7A5E8FACE1AD2C8CB4713575F621747E0579277A4FD09C39B8A381C73081C4300E0603551D0F0101FF040403020780301D0603551D250416301406082B0601050507030106082B06010505070302300C0603551D130101FF04023000301D0603551D0E04160414D99959A3C7EA3BEAC75599766228E46229AD4857301F0603551D2304",
	"81E29103BA18301680143748833E38E7A9EEB47A64C2752B378489CF1FE830450603551D11043E303C821C68747470732E7072702E65696D2E6764696F7473756974652E636F6D821C6E67696E782E7072702E65696D2E6764696F7473756974652E636F6D300A06082A8648CE3D040302034800304502210099881C5BE1C5F7A6060BE582D73992381B32B4E1EEB77BEB2A463CAA3CE0EC000220073A4AD2235393D788C15ECEAB6DD8613A3F87D519F7F6AD7BAC6513F539B7608702038800"
	}
};

static int cmg_gd_eim_config(const struct shell *shell, size_t argc, char **argv)
{
	int config = 0;

	if (argc >= 2) {
		config = atoi(argv[1]);
		if (config < 1 || config > 2) {
			mosh_error("Invalid config: %s, must be 1 or 2", argv[1]);
			return -EINVAL;
		}
	}

	mosh_print("G+D IPAe eIM config %u", config);
	config -= 1; /* Adjust for zero-based index */

	/* EuiccMemoryResetRequest */
	exec_applet_command(UICC_SELECT_IPAE_APPLET, "81E2910007BF64048202010400");

	/* Select G+D EIM config */
	exec_command("AT+CSIM=10,\"0070000001\""); /* Open channel */
	if (exec_command(UICC_SELECT_IPAE_APPLET) == 0) {
		for (int i = 0; i < ARRAY_SIZE(gd_eim_config[config]); i++) {
			snprintf(applet_cmd, sizeof(applet_cmd), "AT+CSIM=%d,\"%s\"", strlen(gd_eim_config[config][i]), gd_eim_config[config][i]);
			exec_command(applet_cmd);
		}
	}
	exec_command("AT+CSIM=8,\"00708001\""); /* Close channel */

	return 0;
}

static int cmd_gd_set_trigger(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		mosh_print("Usage: g+d %s <trigger_mechanism>", argv[0]);
		mosh_print(" 0 - none");
		mosh_print(" 1 - timer");
		mosh_print(" 2 - reset");
		mosh_print(" 3 - timer + reset");
		mosh_print(" 4 - mcc/mnc");
		mosh_print(" 5 - timer + mcc/mnc");
		mosh_print(" 6 - reset + mcc/mnc");
		mosh_print(" 7 - timer + reset + mcc/mnc");
		return -EINVAL;
	}

	uint32_t trigger = atol(argv[1]);
	char cmd[64];

	if (trigger > 7) {
		mosh_error("Invalid trigger value: %s, must be between 0 and 7", argv[1]);
		return -EINVAL;
	}

	mosh_print("Set G+D Gateway trigger mechanism to %u (0x%02X)", trigger, trigger);
	snprintf(cmd, sizeof(cmd), "81E29100049F0201%02X00", trigger);
	exec_applet_command(UICC_SELECT_GATEWAY_APPLET, cmd);

	return 0;
}

static int cmd_gd_set_timer_value(const struct shell *shell, size_t argc, char **argv)
{
	if (argc < 2) {
		mosh_print("Usage: g+d %s <hours> <minutes>", argv[0]);
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
	snprintf(cmd, sizeof(cmd), "81E29100069F0103%06X00", timer_value);
	exec_applet_command(UICC_SELECT_GATEWAY_APPLET, cmd);

	return 0;
}

static int cmg_gd_euicc_memory_meset(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D IPAe eUICC memory reset");
	exec_applet_command(UICC_SELECT_IPAE_APPLET, "81E2910007BF64048202010400");

	return 0;
}

static int cmg_gd_exec_fallback(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D Gateway execute fallback mechanism");
	exec_applet_command(UICC_SELECT_GATEWAY_APPLET, "81E2910006BF5D038001FF00");

	return 0;
}

static int cmd_gd_return_from_fallback(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("G+D Gateway return from fallback");
	exec_applet_command(UICC_SELECT_GATEWAY_APPLET, "81E2910006BF5E038001FF00");

	return 0;
}

static int cmd_tac_trigger(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("TAC poll triggered");
	exec_applet_command(UICC_SELECT_TAC_APPLET, "0100000003400101");
	exec_command("AT+CSIM=8,\"80F2000C\""); /* STATUS command */

	return 0;
}

static int cmd_isd_command(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD Command");
	if (argc < 2) {
		mosh_print("Usage: isd %s <SIM APDU>", argv[0]);
		return -1;
	}

	exec_isd_ipad_command(argv[1]);

	return 0;
}

static int cmd_euicc_info1(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD EUICCInfo1Request");
	exec_isd_ipad_command("81E2910003BF200000");

	return 0;
}

static int cmd_euicc_info2(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD EUICCInfo2Request");
	exec_isd_ipad_command("81E2910003BF220000");

	return 0;
}

static int cmd_get_certs(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD GetCertsRequest");
	exec_isd_ipad_command("81E2910003BF560000");

	return 0;
}

static int cmd_get_conn_params(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD GetConnectivityParametersRequest");
	exec_isd_ipad_command("81E2910003BF5F0000");

	return 0;
}

static int cmd_get_eim_config(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD GetEimConfigurationDataRequest");
	exec_isd_ipad_command("81E2910003BF550000");

	return 0;
}

static int cmd_get_rat(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD GetRatRequest");
	exec_isd_ipad_command("81E2910003BF430000");

	return 0;
}

static int cmd_notifications_list(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD RetrieveNotificationsListRequest");
	exec_isd_ipad_command("81E2910003BF2B0000");

	return 0;
}

static int cmd_profile_info_list(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD ProfileInfoListRequest");
	exec_isd_ipad_command("81E2910003BF2D0000");

	return 0;
}

static int cmd_ipae_activate(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("ISD IPAe activate");
	exec_applet_command(UICC_SELECT_ISD_APPLET, "81E2910007BF42048002078000");

	return 0;
}

static int cmd_kigen_read_debug_log(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("Kigen Read Debug Log");
	exec_applet_command(UICC_SELECT_ISD_APPLET, "81E2910006FF7E03DF330000");

	return 0;
}

static int cmd_kigen_eim_config(const struct shell *shell, size_t argc, char **argv)
{
	char eim_config1[] =
		"81E29100E9BF5781E5A081E23081DF801765696D2D73746167696E672D302E6B6967656E2E636F6D"
		"820102830100A55BA059301306072A8648CE3D020106082A8648CE3D03010703420004D1716C1592"
		"4F9C13FCFC246249574833C91DE9787028307F8D7E9C941C21DC4FCD70C8D4F530EF69446AA83E9C"
		"949BEDB4B4F517A9167B9CAE50192B6289CC45A65BA059301306072A8648CE3D020106082A8648CE"
		"3D03010703420004552477D4734320982C8E0C545997652F95F5567715D9C23D2AE554A2493F0D2F"
		"ADB71FEEBD07A2F48AA5FCF0178D07E2A3AA853F9F6976C04B877D25655D44678702";
	char eim_support[] = "06C0";
	char eim_config2[] = "8900";
	char eim_config[sizeof(eim_config1) + sizeof(eim_support) + sizeof(eim_config2)];

	if (argc < 2) {
		mosh_print("Kigen EIM config HTTP and CoAP", argv[1]);
	} else if (strcmp(argv[1], "http") == 0) {
		mosh_print("Kigen EIM config HTTP", argv[1]);
		sprintf(eim_support, "0780");
	} else if (strcmp(argv[1], "coap") == 0) {
		mosh_print("Kigen EIM config CoAP", argv[1]);
		sprintf(eim_support, "0640");
	} else {
		mosh_error("Usage: kigen eim_config <http|coap>");
		return 0;
	}

	/* EuiccMemoryResetRequest */
	exec_applet_command(UICC_SELECT_ISD_APPLET, "81E2910007BF640482020204");

	/* Select Kigen EIM config */
	snprintf(eim_config, sizeof(eim_config), "%s%s%s", eim_config1, eim_support, eim_config2);
	exec_applet_command(UICC_SELECT_ISD_APPLET, eim_config);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_isd,
	SHELL_CMD(command, NULL, "ISD command", cmd_isd_command),
	SHELL_CMD(info1, NULL, "EUICCInfo1Request", cmd_euicc_info1),
	SHELL_CMD(info2, NULL, "EUICCInfo2Request", cmd_euicc_info2),
	SHELL_CMD(ipae_activate, NULL, "IPAe activate", cmd_ipae_activate),
	SHELL_CMD(get_certs, NULL, "GetCertsRequest", cmd_get_certs),
	SHELL_CMD(get_conn_param, NULL, "GetConnectivityParametersRequest", cmd_get_conn_params),
	SHELL_CMD(get_eim_config, NULL, "GetEimConfigurationDataRequest", cmd_get_eim_config),
	SHELL_CMD(get_rat, NULL, "GetRatRequest", cmd_get_rat),
	SHELL_CMD(notif_list, NULL, "RetrieveNotificationsListRequest", cmd_notifications_list),
	SHELL_CMD(profile_info_list, NULL, "ProfileInfoListRequest", cmd_profile_info_list),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_kigen,
	SHELL_CMD(eim_config, NULL, "eIM config (HTTP and/or CoAP)", cmd_kigen_eim_config),
	SHELL_CMD(read_debug, NULL, "Read Debug Log", cmd_kigen_read_debug_log),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_gd,
	SHELL_CMD(eim_config, NULL, "eIM Config", cmg_gd_eim_config),
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
	SHELL_CMD(kigen, &sub_kigen, "Kigen commands", mosh_print_help_shell),
	SHELL_CMD(tac, &sub_tac, "Thales commands", mosh_print_help_shell),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(sgp32, &sub_sgp32, "Commands for SGP.32", mosh_print_help_shell);
