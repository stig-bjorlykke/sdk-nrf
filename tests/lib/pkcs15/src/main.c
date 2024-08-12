/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "unity.h"
#include "pkcs15_decode.h"

/* EF(ODF) object */
char ef_odf_bytes[] = {
	0xa7, 0x06, 0x30, 0x04, 0x04, 0x02, 0x64, 0x30
};

/* EF(DODF) object */
char ef_dodf_bytes[] = {
	0xa1, 0x27, 0x30, 0x00, 0x30, 0x11, 0x0c, 0x0f,
	0x4c, 0x77, 0x4d, 0x32, 0x4d, 0x20, 0x42, 0x6f,
	0x6f, 0x74, 0x73, 0x74, 0x72, 0x61, 0x70, 0xa1,
	0x10, 0x30, 0x0e, 0x06, 0x06, 0x06, 0x04, 0x67,
	0x2b, 0x09, 0x01, 0x30, 0x04, 0x04, 0x02, 0x64,
	0x32, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

void test_pkcs15_ef_odf_path_decode(void)
{
	pkcs15_object_t object = {};
	bool success;

	success = pkcs15_ef_odf_path_decode(ef_odf_bytes, sizeof(ef_odf_bytes), &object);

	TEST_ASSERT_EQUAL(true, success);
	TEST_ASSERT_EQUAL_STRING("6430", object.path);
}

void test_pkcs15_ef_dodf_path_decode(void)
{
	pkcs15_object_t object = {};
	bool success;

	success = pkcs15_ef_dodf_path_decode(ef_dodf_bytes, sizeof(ef_dodf_bytes), &object);

	TEST_ASSERT_EQUAL(true, success);
	TEST_ASSERT_EQUAL_STRING("6432", object.path);
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
