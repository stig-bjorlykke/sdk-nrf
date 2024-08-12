/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "asn1_decode.h"

static void copy_to_hex(uint8_t *dst, const uint8_t *src, size_t len)
{
	static uint8_t hex_chr[] = {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
	};
	int ofs = 0;

	for (size_t i = 0; i < len; i++) {
		dst[ofs++] = hex_chr[(src[i] & 0xF0) >> 4];
		dst[ofs++] = hex_chr[(src[i] & 0x0F)];
	}
	dst[ofs] = '\0';
}

bool asn1_dec_head(asn1_ctx_t *ctx, uint8_t *tag, size_t *len)
{
	uint32_t hlen = 2; /* Minimum two bytes for header */

	if (ctx->error || ((ctx->offset + hlen) > ctx->length)) {
		/* Error detected or out of data (happens at end of sequence) */
		return false;
	}

	*tag = ctx->asnbuf[ctx->offset++];
	*len = ctx->asnbuf[ctx->offset++];

	if ((*tag & 0x1F) == 0x1F) {
		/* Extended tag number is unsupported */
		ctx->error = true;
		return false;
	}

	if (*len & 0x80) {
		int n = *len & 0x7F;

		hlen += n;
		if (n > 3 || (ctx->offset + hlen) > ctx->length) {
			/* Unsupported header length or out of data (header is past buffer) */
			ctx->error = true;
			return false;
		}

		*len = 0;
		for (int i = 0; i < n; i++) {
			*len = (*len << 8) + ctx->asnbuf[ctx->offset++];
		}
	}

	if ((ctx->offset + *len) > ctx->length) {
		/* Out of data (value is past buffer) */
		ctx->error = true;
		return false;
	}

	return true;
}

void asn1_dec_octet_string(asn1_ctx_t *ctx, size_t len, uint8_t *value, size_t max_len)
{
	if (len >= max_len) {
		/* OCTET STRING too long for buffer */
		ctx->error = true;
		return;
	}

	copy_to_hex(value, &ctx->asnbuf[ctx->offset], len);
	ctx->offset += len;
}

void asn1_dec_sequence(asn1_ctx_t *ctx, size_t len, void *data, asn1_sequence_func_t sequence_func)
{
	/* Create a subset from the buffer */
	asn1_ctx_t seq_ctx = {
		.asnbuf = &ctx->asnbuf[ctx->offset],
		.length = len
	};

	sequence_func(&seq_ctx, data);
	ctx->offset += len;

	/* Copy error from subset */
	ctx->error = seq_ctx.error;
}

void asn1_dec_skip(asn1_ctx_t *ctx, size_t len)
{
	ctx->offset += len;
}
