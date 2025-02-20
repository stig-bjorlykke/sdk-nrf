#
# Copyright (c) 2025 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Set the default NCS static partition layout for the Thingy:91 X
if(SB_CONFIG_BOARD_THINGY91X_NRF9151_NS AND SB_CONFIG_THINGY91X_STATIC_PARTITIONS_FACTORY)
  if(SB_CONFIG_ONOMONDO_SOFTSIM)
    set(PM_STATIC_YML_FILE pm_static_thingy91x_nrf9151_softsim.yml CACHE INTERNAL "")
  elseif(SB_CONFIG_REDTEA_NUSIM)
    set(PM_STATIC_YML_FILE pm_static_thingy91x_nrf9151_nusim.yml CACHE INTERNAL "")
  endif()
endif()
