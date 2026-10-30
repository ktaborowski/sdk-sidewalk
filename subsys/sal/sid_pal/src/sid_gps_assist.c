/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* GPS assist NOSUPPORT defaults for the location platform abstraction layer.
 *
 * No GPS-assist back end is available, so GNSS with GPS assist (location
 * effort mode L6) cannot be served. The prebuilt Sidewalk libraries are built
 * with SID_SDK_CONFIG_ENABLE_GPS_ASSIST disabled and reference none of these
 * entry points today; a library built with it enabled resolves them here
 * instead of failing to link.
 *
 * Compiled only when nothing implements them, see CMakeLists.txt, so exactly
 * one definition of each symbol exists in any configuration.
 */

#include <zephyr/toolchain.h>

#include <sid_pal_gps_assist_ifc.h>

sid_error_t sid_pal_gps_assist_init(const struct sid_pal_gps_assist_config *config)
{
	ARG_UNUSED(config);
	return SID_ERROR_NOSUPPORT;
}

sid_error_t sid_pal_gps_assist_deinit(void)
{
	return SID_ERROR_NOSUPPORT;
}

sid_error_t sid_pal_gps_assist_inject_ephemeris(const uint8_t *data, size_t size)
{
	ARG_UNUSED(data);
	ARG_UNUSED(size);
	return SID_ERROR_NOSUPPORT;
}

sid_error_t sid_pal_gps_assist_start_acquisition(void)
{
	return SID_ERROR_NOSUPPORT;
}

sid_error_t sid_pal_gps_assist_abort_acquisition(void)
{
	return SID_ERROR_NOSUPPORT;
}

sid_error_t sid_pal_gps_assist_get_pseudoranges(
	struct sid_pal_gps_assist_pseudorange_result *result)
{
	ARG_UNUSED(result);
	return SID_ERROR_NOSUPPORT;
}

enum sid_pal_gps_assist_power_state sid_pal_gps_assist_get_power_state(void)
{
	return SID_PAL_GPS_ASSIST_POWER_OFF;
}
