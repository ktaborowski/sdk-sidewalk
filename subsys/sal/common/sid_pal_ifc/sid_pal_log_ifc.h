/*
 * Copyright 2020-2024 Amazon.com, Inc. or its affiliates. All rights reserved.
 *
 * AMAZON PROPRIETARY/CONFIDENTIAL
 *
 * You may not use this file except in compliance with the terms and
 * conditions set forth in the accompanying LICENSE.TXT file.
 *
 * THESE MATERIALS ARE PROVIDED ON AN "AS IS" BASIS. AMAZON SPECIFICALLY
 * DISCLAIMS, WITH RESPECT TO THESE MATERIALS, ALL WARRANTIES, EXPRESS,
 * IMPLIED, OR STATUTORY, INCLUDING THE IMPLIED WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT.
 */

#ifndef SID_PAL_LOG_IFC_H
#define SID_PAL_LOG_IFC_H

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

#define SID_PAL_LOG_SEVERITY_ERROR   0
#define SID_PAL_LOG_SEVERITY_WARNING 1
#define SID_PAL_LOG_SEVERITY_INFO    2
#define SID_PAL_LOG_SEVERITY_DEBUG   3

#define SID_PAL_LOG_ERROR(...)    LOG_ERR(__VA_ARGS__)
#define SID_PAL_LOG_WARNING(...)  LOG_WRN(__VA_ARGS__)
#define SID_PAL_LOG_INFO(...)     LOG_INF(__VA_ARGS__)
#define SID_PAL_LOG_DEBUG(...)    LOG_DBG(__VA_ARGS__)

#define SID_PAL_LOG_HEXDUMP_ERROR(data, len)    LOG_HEXDUMP_ERR(data, len, "")
#define SID_PAL_LOG_HEXDUMP_WARNING(data, len)  LOG_HEXDUMP_WRN(data, len, "")
#define SID_PAL_LOG_HEXDUMP_INFO(data, len)     LOG_HEXDUMP_INF(data, len, "")
#define SID_PAL_LOG_HEXDUMP_DEBUG(data, len)    LOG_HEXDUMP_DBG(data, len, "")

#define SID_PAL_LOG_FLUSH()     log_flush()
#define SID_PAL_LOG_PUSH_STR(x) (x)

#endif /* SID_PAL_LOG_IFC_H */
