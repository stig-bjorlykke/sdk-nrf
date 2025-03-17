/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/shell/shell.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <nrf_modem_at.h>
#include <nrf_modem_gnss.h>

#include "mosh_defines.h"
#include "mosh_print.h"

static const char *ntn_init_commands[] = {
	"AT+CFUN=0",
	"AT+CFUN=12",
	"AT%CSUS=2",
	"AT+CGDCONT=0,\"IP\",\"internet.m2mportal.de\"",
	"AT%XSYSTEMMODE=0,1,1,0",
	"AT%XEPCO=0",
	"AT%XNTNFEAT=0,0",
	"AT%XBANDLOCK=2,\"10000000000000000000000000000000000000000000000000000000000000000\""
};

static bool m_initialized;
static double m_latitude;
static double m_longitude;
static float m_altitude;

int ntn_setgpspos(double latitude, double longitude, float altitude)
{
	int err;

	int at_lat = (int)((latitude + 90) * 1000);
	int at_long = (int)((longitude + 180) * 1000);
	int at_alt = (int)(altitude * 1000);

	m_latitude = latitude;
	m_longitude = longitude;
	m_altitude = altitude;

	/* Note: different order of latitude and longitude. */
	mosh_print("AT%%XSETGPSPOS=%d,%d,%d", at_long, at_lat, at_alt);
	err = nrf_modem_at_printf("AT%%XSETGPSPOS=%d,%d,%d", at_long, at_lat, at_alt);
	if (err) {
		mosh_error("Failed to set AT%%XSETGPSPOS, error: %d", err);
	}

	return err;
}

void ntn_location(struct nrf_modem_gnss_pvt_data_frame *pvt)
{
	if (pvt->flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID) {
		mosh_print("GNSS Latitude: %.6f, Longitude: %.6f, Altitude: %.2f",
			   pvt->latitude, pvt->longitude, (double)pvt->altitude);

		ntn_setgpspos(pvt->latitude, pvt->longitude, pvt->altitude);
	}
}

static int cmd_ntn_init(const struct shell *shell, size_t argc, char **argv)
{
	int err;

	for (int i = 0; i < ARRAY_SIZE(ntn_init_commands); i++) {
		mosh_print("%s", ntn_init_commands[i]);
		err = nrf_modem_at_printf("%s", ntn_init_commands[i]);
		if (err) {
			mosh_error("Failed to set %s, error: %d", ntn_init_commands[i], err);
			return err;
		}
	}

	if (argc > 1) {
		if (argv[1][0] == '0') {
			ntn_setgpspos(m_latitude, m_longitude, m_altitude);
		} else if (argv[1][0] == '1') {
			ntn_setgpspos(63.422428, 10.446461, 91.0);
		} else if (argv[1][0] == '2') {
			ntn_setgpspos(63.431702, 10.472007, 53.0);
		} else {
			mosh_error("Invalid static setting: %d", argv[1][0]);
			return -EINVAL;
		}
	}

	mosh_print("NTN modem configuration completed successfully");
	m_initialized = true;

	return 0;
}

static int cmd_ntn_status(const struct shell *shell, size_t argc, char **argv)
{
	mosh_print("NTN status: %s", m_initialized ? "Initialized" : "Not initialized");
	mosh_print("Latitude:   %.6f°", m_latitude);
	mosh_print("Longitude:  %.6f°", m_longitude);
	mosh_print("Altitude:   %.2f m", (double)m_altitude);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_ntn,
	SHELL_CMD(init, NULL, "NTN initialize", cmd_ntn_init),
	SHELL_CMD(status, NULL, "NTN status", cmd_ntn_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ntn, &sub_ntn, "Commands for NTN.", mosh_print_help_shell);
