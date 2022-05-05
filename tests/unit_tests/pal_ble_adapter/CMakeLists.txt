#
# Copyright (c) 2022 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-4-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sidewalk_test_ble_adapter)

cmock_handle(${ZEPHYR_BASE}/include/settings/settings.h)
cmock_handle(${ZEPHYR_BASE}/../sidewalk/include/sid_ble_advert.h)

# add test file
target_sources(app PRIVATE src/main.c)

# generate runner for the test
test_runner_generate(src/main.c)