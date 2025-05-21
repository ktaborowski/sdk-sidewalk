/*
 * Copyright (c) 2019-2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * This file has private functions needed by the driver
 */

#ifndef HALO_LR1110_RADIO_H
#define HALO_LR1110_RADIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lr11xx_config.h>
#include "lr11xx_system.h"

typedef struct
{
    lr11xx_wifi_channel_mask_t channel_mask;
    uint8_t                    max_result;
} wifi_configuration_scan_base_t;

typedef struct
{
    wifi_configuration_scan_base_t base;
    lr11xx_wifi_signal_type_scan_t signal_type;
    lr11xx_wifi_mode_t             scan_mode;
    uint8_t                        nb_scan_per_channel;
    uint16_t                       timeout_per_scan;
    bool                           abort_on_timeout;
} wifi_configuration_scan_t;

void* lr11xx_get_drv_ctx(void);

#ifdef __cplusplus
}
#endif

#endif /* HALO_LR1110_RADIO_H */
