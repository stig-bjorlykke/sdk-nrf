/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdint.h>
#include <stdbool.h>

#ifndef SIM_BOOTSTRAP_H__
#define SIM_BOOTSTRAP_H__

/** SIM record max size is 256 bytes. The buffer size needed for the AT response is
 * (256 * 2) + 4 bytes for SW + 1 byte for NUL. Using 516 bytes is adequate to read
 * a full UICC record.
 */
#define SIM_RECORD_BUFFER_MAX ((256 * 2) + 4 + 1)

/**
 * @brief Read SIM bootstrap record.
 *
 * @param[inout]  p_buffer     Buffer to store SIM bootstrap record. This buffer is also used
 *                             internally by the function reading the AT response, so it must be
 *                             twice the size of expected LwM2M content + 4 bytes for UICC SW.
 * @param[in]     buffer_size  Total size of buffer
 *
 * @return Length of SIM bootstrap record, -errno on error.
 */
int sim_bootstrap_read(uint8_t *p_buffer, int buffer_size);

#endif /* SIM_BOOTSTRAP_H__ */
