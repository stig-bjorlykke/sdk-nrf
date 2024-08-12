#
# Copyright (c) 2024 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_library()
zephyr_include_directories(include)

zephyr_library_sources(
    src/asn1_decode.c
    src/pkcs15_decode.c
)
