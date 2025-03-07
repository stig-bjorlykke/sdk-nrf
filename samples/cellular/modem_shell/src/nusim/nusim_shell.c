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

static uint8_t apdu_open_channel[] = {
	0x00, 0x70, 0x00, 0x00, 0x01
};

static uint8_t apdu_select_app[] = {
	0x01, 0xA4, 0x04, 0x00, 0x10, 0xF0, 0x00, 0x00,
	0x05, 0x59, 0x10, 0x10, 0xFF, 0xFF, 0xFF, 0xFF,
	0x89, 0x00, 0x00, 0x01, 0x00
};

static uint8_t apdu_read_eid[] = {
	0x81, 0xE2, 0x91, 0x00, 0x03, 0xFF, 0x71, 0x00
};

static uint8_t apdu_read_capa[] = {
	0x81, 0xE2, 0x91, 0x00, 0x03, 0xFF, 0x72, 0x00
};

static uint8_t apdu_read_iccid[] = {
	0x81, 0xE2, 0x91, 0x00, 0x03, 0xFF, 0x74, 0x00
};

static uint8_t apdu_close_channel[] = {
	0x00, 0x70, 0x80, 0x01, 0x00
};

struct cmd_apdu_t {
	const uint8_t *apdu;
	const uint16_t len;
	int (*decode_fn)(const uint8_t *apdu, uint16_t apdu_len, const struct cmd_apdu_t *cmd);
	const char *name;
};

static int decode_app_apdu(const uint8_t *apdu, uint16_t apdu_len, const struct cmd_apdu_t *cmd);
static int decode_apdu(const uint8_t *apdu, uint16_t apdu_len, const struct cmd_apdu_t *cmd);

struct cmd_apdu_t apdu_list[] = {
	{ apdu_open_channel,  sizeof apdu_open_channel,  NULL,            "Open Channel"     },
	{ apdu_select_app,    sizeof apdu_select_app,    decode_app_apdu, "Select nuSIM App" },
	{ apdu_read_eid,      sizeof apdu_read_eid,      decode_apdu,     "EID"              },
	{ apdu_read_iccid,    sizeof apdu_read_iccid,    decode_apdu,     "ICCID"            },
	{ apdu_read_capa,     sizeof apdu_read_capa,     decode_apdu,     "Capability"       },
	{ apdu_close_channel, sizeof apdu_close_channel, NULL,            "Close Channel"    },
};

static const char *solution_type_str(uint8_t solution_type)
{
	if (solution_type == 0x00) {
		return "Key Generate On Board";
	} else if (solution_type == 0x01) {
		return "Key Injection";
	} else {
		return "Unknown";
	}
}

static const char *life_cycle_str(uint8_t life_cycle)
{
	if (life_cycle == 0x00) {
		return "Idle";
	} else if (life_cycle == 0x01) {
		return "Ready";
	} else if (life_cycle == 0x02) {
		return "Initialized";
	} else if (life_cycle == 0x03) {
		return "Personalized";
	} else if (life_cycle >= 0x80 && life_cycle <= 0x8F) {
		return "Device Temp Locked";
	} else if (life_cycle >= 0xF0 && life_cycle <= 0xFF) {
		return "Device Locked";
	} else {
		return "Unknown";
	}
}

static const char *result_code_str(uint16_t result_code)
{
	switch (result_code) {
	case 701:
		return "Data format check failed in command data";
	case 702:
		return "Life cycle check failed";
	case 703:
		return "Read data from NVM check failed";
	case 704:
		return "Allocate memory failed";
	case 705:
		return "No profile";
	case 706:
		return "Key pair generate failed";
	case 707:
		return "Read certification failed";
	default:
		return "Unknown result code";
	}
}

static void convert_iccid(uint8_t *data, uint32_t data_len)
{
	uint8_t tmp;

	for (int i = 0; i < data_len; i += 2) {
		tmp = data[i];
		data[i] = data[i + 1];
		if (tmp != 'f' && tmp != 'F') {
			data[i + 1] = tmp;
		}
	}
}

static int decode_app_apdu(const uint8_t *apdu, uint16_t apdu_len, const struct cmd_apdu_t *cmd)
{
	/* Check if this is a select nuSIM App response. */
	if (!(apdu_len == 30 && apdu[0] == 0x6F && apdu[1] == 0x1C && apdu[2] == 0x84 &&
	      apdu[3] == 0x10 && memcmp(&apdu[4], &cmd->apdu[5], apdu[3]) == 0)) {
		mosh_error("%s: unknown APDU format", cmd->name);
		return -EINVAL;
	}

	mosh_print("Solution type: %s (0x%02x)", solution_type_str(apdu[25]), apdu[25]);
	mosh_print("Life cycle:    %s (0x%02x)", life_cycle_str(apdu[29]), apdu[29]);

	return 0;
}

static int decode_apdu(const uint8_t *apdu, uint16_t apdu_len, const struct cmd_apdu_t *cmd)
{
	/* Check if this is a known nuSIM ASN.1 response */
	if (!(apdu_len >= 7 && apdu[0] == cmd->apdu[5] && apdu[1] == cmd->apdu[6] &&
	      apdu_len == apdu[2] + 3)) {
		mosh_error("%s: unknown APDU format", cmd->name);
		return -EINVAL;
	}

	if (apdu[3] == 0x04 && apdu[4] == apdu[2] - 2) { /* Octet String */
		char hex_str[apdu[4] * 2 + 1];

		bin2hex(&apdu[5], apdu[4], hex_str, sizeof(hex_str));
		if (apdu[1] == 0x74) { /* ICCID */
			convert_iccid(hex_str, strlen(hex_str));
		}
		mosh_print("%s:%*c%s", cmd->name, 14 - strlen(cmd->name), ' ', hex_str);
	} else if (apdu[3] == 0x02 && apdu[4] == 0x02) { /* Integer length 2 */
		uint16_t result = apdu[5] << 8 | apdu[6];

		mosh_warn("%s:%*c%s (%d)", cmd->name, 14 - strlen(cmd->name), ' ',
			  result_code_str(result), result);
	} else {
		mosh_error("%s: unknown ASN.1 tag:%02X len:%u", cmd->name, apdu[3], apdu[4]);
		return -EINVAL;
	}

	return 0;
}

static int send_recv_apdu(const uint8_t *c_apdu, uint16_t c_apdu_len, uint8_t *r_apdu,
			  uint16_t *r_apdu_len, const struct cmd_apdu_t *cmd, bool show_apdu)
{
	char hex_str[COS_MAX_APDU_SIZE * 2 + 1];
	int8_t res;

	if (show_apdu) {
		bin2hex(c_apdu, c_apdu_len, hex_str, sizeof(hex_str));
		mosh_print("[C-APDU] %s: %s", cmd->name, hex_str);
	}

	res = rt_cos_api_send_recv_msg(c_apdu, c_apdu_len, r_apdu, r_apdu_len);
	if (res != COS_SUCCESS) {
		mosh_error("%s rt_cos_api res: %d", cmd->name, res);
		return -EINVAL;
	}

	if (show_apdu) {
		bin2hex(r_apdu, *r_apdu_len, hex_str, sizeof(hex_str));
		mosh_print("[R-APDU] %s: %s", cmd->name, hex_str);
	}

	return 0;
}

static int handle_apdu(const struct cmd_apdu_t *cmd, bool show_apdu)
{
	uint8_t r_apdu[COS_MAX_APDU_RESP_SIZE];
	uint16_t r_apdu_len;
	uint16_t sw;
	int err;

	err = send_recv_apdu(cmd->apdu, cmd->len, r_apdu, &r_apdu_len, cmd, show_apdu);

	/* Check for use of procedure bytes '61xx' and '6Cxx' */
	if (!err && r_apdu_len == 2 && (r_apdu[0] == 0x61 || r_apdu[0] == 0x6C)) {
		uint8_t f_apdu[] = { cmd->apdu[0], 0xC0, 0x00, 0x00, r_apdu[1] };

		err = send_recv_apdu(f_apdu, sizeof(f_apdu), r_apdu, &r_apdu_len, cmd, show_apdu);
	}

	if (err) {
		return err;
	}

	if (r_apdu_len < 2 || r_apdu_len > COS_MAX_APDU_RESP_SIZE) {
		mosh_error("%s: APDU length %d", cmd->name, r_apdu_len);
		return -EINVAL;
	}

	sw = r_apdu[r_apdu_len - 2] << 8 | r_apdu[r_apdu_len - 1];
	if (sw != 0x9000) {
		mosh_error("%s: APDU error %04x", cmd->name, sw);
		return -EINVAL;
	}

	if (cmd->decode_fn) {
		/* Do not include SW in R-APDU */
		return cmd->decode_fn(r_apdu, r_apdu_len - 2, cmd);
	}

	return 0;
}

static int cmd_nusim_status(const struct shell *shell, size_t argc, char **argv)
{
	bool show_apdu = false;

	if (argc > 1 && strcmp(argv[1], "-a") == 0) {
		show_apdu = true;
	}

	for (size_t i = 0; i < ARRAY_SIZE(apdu_list); i++) {
		handle_apdu(&apdu_list[i], show_apdu);
	}

	return 0;
}

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
	SHELL_CMD(status, NULL, "nuSIM status", cmd_nusim_status),
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
