#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sidewalk_test_sid_ble_adapter)

get_filename_component(SIDEWALK_BASE ${CMAKE_CURRENT_LIST_DIR}/../../.. ABSOLUTE)

target_sources(app PRIVATE
	${SIDEWALK_BASE}/subsys/sal/sid_pal/src/sid_ble_adapter.c
	${SIDEWALK_BASE}/subsys/sal/sid_pal/src/sid_ble_adapter_callbacks.c
	${SIDEWALK_BASE}/subsys/sal/sid_pal/src/sid_ble_ama_service.c
	${SIDEWALK_BASE}/subsys/sal/sid_pal/src/sid_ble_connection.c
	${SIDEWALK_BASE}/subsys/sal/sid_pal/src/hci_utils.c
	src/main.c
)

target_compile_definitions(app PRIVATE
	CONFIG_BT_PERIPHERAL_PREF_MIN_INT=16
	CONFIG_BT_PERIPHERAL_PREF_MAX_INT=60
	CONFIG_BT_PERIPHERAL_PREF_LATENCY=0
	CONFIG_BT_PERIPHERAL_PREF_TIMEOUT=400
	CONFIG_SIDEWALK_BLE_CONN_WORKQ_STACK_SIZE=1536
	CONFIG_SIDEWALK_BLE_CONN_WORKQ_PRIORITY=9
	CONFIG_SIDEWALK_BLE_CONN_EVT_QUEUE_SIZE=8
)

target_include_directories(app PRIVATE
	${SIDEWALK_BASE}/subsys/sal/sid_pal/include
)

target_link_libraries(app PRIVATE sid_pal_ble_adapter_ifc)
