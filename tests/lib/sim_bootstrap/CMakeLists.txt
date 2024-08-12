#
# Copyright (c) 2024 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sim_bootstrap)

test_runner_generate(src/main.c)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE
  ${app_sources}
  #${ZEPHYR_NRF_MODULE_DIR}/lib/pkcs15/src/asn1_decode.c
  #${ZEPHYR_NRF_MODULE_DIR}/lib/pkcs15/src/pkcs15_decode.c
  #${ZEPHYR_NRF_MODULE_DIR}/lib/sim_bootstrap/src/sim_bootstrap.c
)

zephyr_include_directories(
  ${ZEPHYR_NRF_MODULE_DIR}/lib/pkcs15/include/
  ${ZEPHYR_NRF_MODULE_DIR}/lib/sim_bootstrap/include/
  ${ZEPHYR_NRFXLIB_MODULE_DIR}/nrf_modem/include/
)
