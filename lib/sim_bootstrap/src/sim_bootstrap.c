/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include <sim_bootstrap.h>
#include <nrf_modem_at.h>
#include <pkcs15_decode.h>

static uint8_t hex_to_nibble(uint8_t src)
{
	uint8_t dst;

	if (src >= 'A' && src <= 'F') {
		dst = (src - 'A' + 10);
	} else if (src >= 'a' && src <= 'f') {
		dst = (src - 'a' + 10);
	} else if (src >= '0' && src <= '9') {
		dst = (src - '0');
	} else {
		/* Illegal hex digit */
		dst = 0;
	}

	return dst;
}

static int hex_to_bin(uint8_t *dst, const uint8_t *src, int len)
{
	int ofs = 0;

	for (int i = 0; i < len; i += 2) {
		dst[ofs++] = (hex_to_nibble(src[i]) << 4) + hex_to_nibble(src[i + 1]);
	}

	return ofs;
}

static int csim_send(const uint8_t *p_csim_command, uint8_t *p_response, int buffer_size)
{
	uint8_t csim_fmt[20];
	int length, rc;

	/* Create csim_fmt based on buffer size */
	rc = snprintf(csim_fmt, sizeof(csim_fmt), "+CSIM: %%d,\"%%%ds\"", buffer_size - 1);
	if (rc >= (int)sizeof(csim_fmt)) {
		return -EINVAL;
	}

	/* Use p_response buffer for both command and response */
	rc = snprintf(p_response, buffer_size, "AT+CSIM=%u,\"%s\"",
		      (unsigned int)strlen(p_csim_command), p_csim_command);
	if (rc >= buffer_size) {
		return -EINVAL;
	}

	/* Send AT command. */
	rc = nrf_modem_at_scanf(p_response, csim_fmt, &length, p_response);
	if (rc < 0) {
		return rc;
	}

	/* Check for success and trailing status word 9000 in response */
	if ((rc != 2) || (length < 4) || (memcmp(&p_response[length - 4], "9000", 4) != 0)) {
		return -EINVAL;
	}

	/* Remove status word from response */
	return length - 4;
}

static int csim_read_file(const uint8_t *p_path, uint8_t *p_response, int buffer_size)
{
	uint8_t csim_select[] = "01A40804047FFF****00";
	uint8_t csim_read[] = "01B0000000";
	int length;

	/* Select path */
	memcpy(&csim_select[14], p_path, 4);
	length = csim_send(csim_select, p_response, buffer_size);
	if (length <= 0) {
		return length;
	}

	/* Check buffer size, needs to be max*2 + 4 bytes for SW for AT response */
	if (buffer_size < SIM_RECORD_BUFFER_MAX) {
		/* Expected maximum response length: 1-255, 0=256 */
		snprintf(&csim_read[8], 3, "%2.2X", (uint8_t)((buffer_size - 4) / 2));
	}

	/* Read path */
	length = csim_send(csim_read, p_response, buffer_size);
	if (length <= 0) {
		return length;
	}

	/* Convert from hex to binary (inplace) */
	length = hex_to_bin(p_response, p_response, length);

	return length;
}

static int sim_bootstrap_read_records(uint8_t *p_buffer, int buffer_size)
{
	pkcs15_object_t pkcs15_object;
	int length;

	/* Read EF(ODF) */
	length = csim_read_file("5031", p_buffer, buffer_size);
	if (length <= 0) {
		return length;
	}

	/* Decode PKCS #15 EF(ODF) Path */
	memset(&pkcs15_object, 0, sizeof(pkcs15_object));
	if (!pkcs15_ef_odf_path_decode(p_buffer, length, &pkcs15_object)) {
		return -ENOENT;
	}

	/* Check if EF(DODF) Path is found */
	if (pkcs15_object.path[0] == 0) {
		return -ENOENT;
	}

	/* Read EF(DODF) */
	length = csim_read_file(pkcs15_object.path, p_buffer, buffer_size);
	if (length <= 0) {
		return length;
	}

	/* Decode PKCS #15 EF(DODF) Path */
	memset(&pkcs15_object, 0, sizeof(pkcs15_object));
	if (!pkcs15_ef_dodf_path_decode(p_buffer, length, &pkcs15_object)) {
		return -ENOENT;
	}

	/* Check if EF(DODF-bootstrap) Path is found */
	if (pkcs15_object.path[0] == 0) {
		return -ENOENT;
	}

	/* Read EF(DODF-bootstrap) */
	length = csim_read_file(pkcs15_object.path, p_buffer, buffer_size);

	return length;
}

int sim_bootstrap_read(uint8_t *p_buffer, int buffer_size)
{
	int length;

	/* Open a logical channel 1 */
	length = csim_send("0070000001", p_buffer, buffer_size);
	if (length <= 0) {
		return length;
	}

	/* Select PKCS#15 on channel 1 using the default AID */
	length = csim_send("01A404040CA000000063504B43532D313500", p_buffer, buffer_size);

	if (length > 0) {
		/* Read bootstrap records */
		length = sim_bootstrap_read_records(p_buffer, buffer_size);
	}

	/* Close the logical channel (using separate buffer to keep content from last file) */
	uint8_t close_response[21];
	int close_length;

	close_length = csim_send("01708001", close_response, sizeof(close_response));
	if (length >= 0 && close_length < 0) {
		return close_length;
	}

	return length;
}

