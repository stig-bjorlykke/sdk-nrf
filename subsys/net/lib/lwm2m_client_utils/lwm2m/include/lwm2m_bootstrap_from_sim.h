/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef LWM2M_CLIENT_UTILS_BOOTSTRAP_FROM_SIM_H__
#define LWM2M_CLIENT_UTILS_BOOTSTRAP_FROM_SIM_H__

#include <zephyr/net/lwm2m.h>

int lwm2m_bootstrap_from_sim(struct lwm2m_ctx *ctx, char *endpoint);

#endif /* LWM2M_CLIENT_UTILS_BOOTSTRAP_FROM_SIM_H__ */
