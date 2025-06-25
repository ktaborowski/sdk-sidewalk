#
# Copyright (c) 2023 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

# Zephyr CMake project
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sidewalk_sid_end_device)

# Application source files
target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
src/main.c
src/sidewalk.c
src/sidewalk_events.c
)

target_sources_ifdef(CONFIG_SIDEWALK_FILE_TRANSFER app PRIVATE 
    src/sbdt/scratch_buffer.c
)
target_sources_ifdef(CONFIG_SIDEWALK_FILE_TRANSFER_DFU app PRIVATE
    src/sbdt/dfu_file_transfer.c
)

if(CONFIG_SID_END_DEVICE_SENSOR_MONITORING)
    target_sources(app PRIVATE
        src/sensor_monitoring/app_buttons.c
        src/sensor_monitoring/app_leds.c
        src/sensor_monitoring/app_sensor.c
        src/sensor_monitoring/app_tx.c
        src/sensor_monitoring/app_rx.c
        src/sensor_monitoring/app.c
    )
elseif(CONFIG_SID_END_DEVICE_DUT)
    target_sources(app PRIVATE
        src/cli/app.c
    )
else()
    target_sources(app PRIVATE
        src/hello/app.c
    )
endif()

if(CONFIG_SID_END_DEVICE_CLI)
    target_sources(app PRIVATE
        src/cli/app_dut.c
        src/cli/app_shell.c
    )
    target_sources_ifdef(CONFIG_SIDEWALK_FILE_TRANSFER_SHELL app PRIVATE
        src/cli/sbdt_shell.c
        src/cli/sbdt_shell_events.c
        src/cli/sbdt_shell_file_transfer.c
    )
    target_sources_ifdef(CONFIG_SIDEWALK_ON_DEV_CERT app PRIVATE 
        src/cli/sid_on_dev_cert_cli.c
    )
endif()

# Sidewalk library
add_library(sidewalk_lib STATIC IMPORTED GLOBAL)
if (CONFIG_SIDEWALK_USE_PREBUILTS)
if(CONFIG_CPU_CORTEX_M4)
    set(CORTEX_DIR cortex-m4)
else()
    set(CORTEX_DIR cortex-m33)
endif()

if(CONFIG_SIDEWALK_SUBGHZ_SUPPORT)
    set(TRANSPORT_DIR lora_fsk)
else()
    set(TRANSPORT_DIR ble_only)
endif()

set(SID_LIB_DIR ${ZEPHYR_BASE}/../sidewalk/lib/${CORTEX_DIR}/${TRANSPORT_DIR})
set_target_properties(sidewalk_lib PROPERTIES IMPORTED_LOCATION             ${SID_LIB_DIR}/libsid_protocol_all.a)
set_target_properties(sidewalk_lib PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${SID_LIB_DIR})
endif() # CONFIG_SIDEWALK_USE_PREBUILTS

target_link_libraries(app PUBLIC sidewalk_lib)
