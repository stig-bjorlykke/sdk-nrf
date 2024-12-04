/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef LWM2M_SECURITY_H__
#define LWM2M_SECURITY_H__

/* LWM2M_OBJECT_SECURITY_ID */
#define SECURITY_SERVER_URI_ID 0
#define SECURITY_BOOTSTRAP_FLAG_ID 1
#define SECURITY_MODE_ID 2
#define SECURITY_CLIENT_PK_ID 3
#define SECURITY_SERVER_PK_ID 4
#define SECURITY_SECRET_KEY_ID 5
#define SECURITY_SHORT_SERVER_ID 10

/* LWM2M_OBJECT_SERVER_ID */
#define SERVER_SHORT_SERVER_ID 0
#define SERVER_LIFETIME_ID 1

enum security_mode {
	SEC_MODE_PSK = 0,
	SEC_MODE_CERTIFICATE = 2,
	SEC_MODE_NO_SEC = 3,
};

#endif /* LWM2M_SECURITY_H__ */
