/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/util.h>

#include "unity.h"

#include "sim_bootstrap.h"

struct at_csim_cmd_rsp {
	char *command;
	char *response;
};

static struct at_csim_cmd_rsp *at_csim_command;
static int at_csim_command_size;
static int at_csim_command_num;

static struct at_csim_cmd_rsp at_csim_application_not_found[] = {
	/* Open a logical channel 1 */
	{ "AT+CSIM=10,\"0070000001\"", "+CSIM: 6,\"019000\"" },
	/* Select PKCS#15 on channel 1 using the default AID */
	{ "AT+CSIM=36,\"01A404040CA000000063504B43532D313500\"", "+CSIM: 4,\"6A82\"" },
	/* Close the logical channel */
	{ "AT+CSIM=8,\"01708001\"", "+CSIM: 4,\"9000\"" }
};

static struct at_csim_cmd_rsp at_csim_bootstrap_not_found[] = {
	/* Open a logical channel 1 */
	{ "AT+CSIM=10,\"0070000001\"", "+CSIM: 6,\"019000\"" },
	/* Select PKCS#15 on channel 1 using the default AID */
	{ "AT+CSIM=36,\"01A404040CA000000063504B43532D313500\"",
	  "+CSIM: 62,\"621B8202412183025031A5038001718A01058B036F06028002003888009000\"" },
	/* Select EF(ODF) (path 5031) */
	{ "AT+CSIM=20,\"01A40804047FFF503100\"",
	  "+CSIM: 62,\"621B8202412183025031A5038001718A01058B036F06028002003888009000\"" },
	/* Read EF(ODF) */
	{ "AT+CSIM=10,\"01B0000080\"", "+CSIM: 42,\"A706300404024407A506300404024404FFFFFF9000\"" },
	/* Select (path 4407) */
	{ "AT+CSIM=20,\"01A40804047FFF440700\"",
	  "+CSIM: 62,\"621B8202412183024407A5038001718A01058B036F0602800200C888009000\"" },
	/* Read record 4407 (empty) */
	{ "AT+CSIM=10,\"01B0000080\"", "+CSIM: 42,\"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF9000\"" },
	/* Close the logical channel */
	{ "AT+CSIM=8,\"01708001\"", "+CSIM: 4,\"9000\"" }
};

static struct at_csim_cmd_rsp at_csim_success[] = {
	/* Open a logical channel 1 */
	{ "AT+CSIM=10,\"0070000001\"", "+CSIM: 6,\"019000\"" },
	/* Select PKCS#15 on channel 1 using the default AID */
	{ "AT+CSIM=36,\"01A404040CA000000063504B43532D313500\"",
	  "+CSIM: 82,\"622582027821840CA000000063504B43532D31358A01058B032F060CC60990014083010183010A9000\"" },
	/* Select EF(ODF) (path 5031) */
	{ "AT+CSIM=20,\"01A40804047FFF503100\"",
	  "+CSIM: 52,\"621682024121830250318A01058B036F06028002002088009000\"" },
	/* Read EF(ODF) */
	{ "AT+CSIM=10,\"01B0000080\"", "+CSIM: 20,\"A7063004040264309000\"" },
	/* Select EF(DODF) (path 6430) */
	{ "AT+CSIM=20,\"01A40804047FFF643000\"",
	  "+CSIM: 52,\"621682024121830264308A01058B036F06028002007888009000\"" },
	/* Read EF(DODF) */
	{ "AT+CSIM=10,\"01B0000080\"",
	  "+CSIM: 86,\"A127300030110C0F4C774D324D20426F6F747374726170A110300E06060604672B09013004040264329000\"" },
	/* Select EF(DODF-bootstrap) (path 6432) */
	{ "AT+CSIM=20,\"01A40804047FFF643200\"",
	  "+CSIM: 52,\"621682024121830264308A01058B036F06028002007888009000\"" },
	/* Read EF(DODF-bootstrap) */
	{ "AT+CSIM=10,\"01B0000080\"",
	  "+CSIM: 108,\"C80016626F6F74702E696F742E742D6D6F62696C652E636F6DC10101410100C80506313233343536C805096C776D326D406464699000\"" },
	/* Close the logical channel */
	{ "AT+CSIM=8,\"01708001\"", "+CSIM: 4,\"9000\"" }
};

int nrf_modem_at_scanf(const char *cmd, const char *fmt, ...)
{
	va_list args;
	int ret;

	/* Check expected command */
	TEST_ASSERT_EQUAL_STRING(at_csim_command[at_csim_command_num].command, cmd);

	/* Check expected format */
	if (at_csim_command_num == at_csim_command_size - 1) {
		/* Buffer is smaller on last command (close) */
		TEST_ASSERT_EQUAL_STRING("+CSIM: %d,\"%20s\"", fmt);
	} else {
		TEST_ASSERT_EQUAL_STRING("+CSIM: %d,\"%260s\"", fmt);
	}

	va_start(args, fmt);
	ret = vsscanf(at_csim_command[at_csim_command_num].response, fmt, args);
	va_end(args);

	at_csim_command_num++;

	return ret;
}

static uint8_t lwm2m_tlv[] = {
	0xC8, 0x00, 0x16, 0x62, 0x6F, 0x6F, 0x74, 0x70, 0x2E, 0x69, 0x6F, 0x74,
	0x2E, 0x74, 0x2D, 0x6D, 0x6F, 0x62, 0x69, 0x6C, 0x65, 0x2E, 0x63, 0x6F,
	0x6D, 0xC1, 0x01, 0x01, 0x41, 0x01, 0x00, 0xC8, 0x05, 0x06, 0x31, 0x32,
	0x33, 0x34, 0x35, 0x36, 0xC8, 0x05, 0x09, 0x6C, 0x77, 0x6D, 0x32, 0x6D,
	0x40, 0x64, 0x64, 0x69
};

void test_sim_bootstrap_read_application_not_found(void)
{
	uint8_t buffer[256 + 4 + 1]; /* 256 bytes content + 4 bytes SW + 1 byte NUL */

	at_csim_command = at_csim_application_not_found;
	at_csim_command_size = ARRAY_SIZE(at_csim_application_not_found);
	at_csim_command_num = 0;

	int ret = sim_bootstrap_read(buffer, sizeof(buffer));

	TEST_ASSERT_EQUAL(-EINVAL, ret);
	TEST_ASSERT_EQUAL(at_csim_command_size, at_csim_command_num);
}

void test_sim_bootstrap_read_bootstrap_not_found(void)
{
	uint8_t buffer[256 + 4 + 1]; /* 256 bytes content + 4 bytes SW + 1 byte NUL */

	at_csim_command = at_csim_bootstrap_not_found;
	at_csim_command_size = ARRAY_SIZE(at_csim_bootstrap_not_found);
	at_csim_command_num = 0;

	int ret = sim_bootstrap_read(buffer, sizeof(buffer));

	TEST_ASSERT_EQUAL(-ENOENT, ret);
	TEST_ASSERT_EQUAL(at_csim_command_size, at_csim_command_num);
}
void test_sim_bootstrap_read_success(void)
{
	uint8_t buffer[256 + 4 + 1]; /* 256 bytes content + 4 bytes SW + 1 byte NUL */

	at_csim_command = at_csim_success;
	at_csim_command_size = ARRAY_SIZE(at_csim_success);
	at_csim_command_num = 0;

	int ret = sim_bootstrap_read(buffer, sizeof(buffer));

	TEST_ASSERT_EQUAL(sizeof(lwm2m_tlv), ret);
	TEST_ASSERT_EQUAL_MEMORY(lwm2m_tlv, buffer, sizeof(lwm2m_tlv));
	TEST_ASSERT_EQUAL(at_csim_command_size, at_csim_command_num);
}

/* It is required to be added to each test. That is because unity's
 * main may return nonzero, while zephyr's main currently must
 * return 0 in all cases (other values are reserved).
 */
extern int unity_main(void);

int main(void)
{
	(void)unity_main();

	return 0;
}
