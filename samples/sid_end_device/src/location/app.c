/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <app.h>
#include <sidewalk.h>
#include <app_ble_config.h>
#include <app_subGHz_config.h>
#include <sid_location.h>
#include <sid_hal_memory_ifc.h>
#include <sid_hal_reset_ifc.h>

#include <bt_app_callbacks.h>

#include <state_notifier/state_notifier.h>
#if defined(CONFIG_GPIO)
#include <state_notifier/notifier_gpio.h>
#endif
#if defined(CONFIG_LOG)
#include <state_notifier/notifier_log.h>
#endif

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <string.h>

#include <json_printer/sidTypes2str.h>

LOG_MODULE_REGISTER(app, CONFIG_SIDEWALK_LOG_LEVEL);

#if defined(CONFIG_SID_END_DEVICE_LOCATION_EFFORT_L1)
#define LOCATION_EFFORT_MODE SID_LOCATION_EFFORT_L1
#elif defined(CONFIG_SID_END_DEVICE_LOCATION_EFFORT_L3)
#define LOCATION_EFFORT_MODE SID_LOCATION_EFFORT_L3
#elif defined(CONFIG_SID_END_DEVICE_LOCATION_EFFORT_L4)
#define LOCATION_EFFORT_MODE SID_LOCATION_EFFORT_L4
#else
#error "No location effort mode selected"
#endif

/* Location methods the location library may use. Wi-Fi and GNSS scans need a
 * location PAL, provided either by the LR1110 transceiver or by the nRF70
 * companion IC (Wi-Fi only).
 */
#if defined(CONFIG_SIDEWALK_SUBGHZ_RADIO_LR1110_LOCATION)
#define LOCATION_METHOD_MASK SID_LOCATION_METHOD_ALL
#elif defined(CONFIG_SIDEWALK_NRF7X_LOCATION)
#define LOCATION_METHOD_MASK (SID_LOCATION_METHOD_BLE_GATEWAY | SID_LOCATION_METHOD_WIFI)
#else
#define LOCATION_METHOD_MASK SID_LOCATION_METHOD_BLE_GATEWAY
#endif

#define MAX_TIME_SYNC_INTERVALS 10

static void app_event_location_run(sidewalk_ctx_t *sid, void *ctx);

static sidewalk_ctx_t sid_ctx;

/* Accessed only from the Sidewalk thread, either from a Sidewalk event handler
 * or from a sid_api callback invoked by sid_process().
 */
static bool location_initialized;
static bool location_init_requested;

/* Set from the Sidewalk thread, cleared from the location callback. */
static atomic_t location_run_pending;

static void location_timer_cb(struct k_timer *timer_id)
{
	ARG_UNUSED(timer_id);

	int err = sidewalk_event_send(app_event_location_run, NULL, NULL);

	if (err) {
		LOG_ERR("Send location run event err %d", err);
	}
}

K_TIMER_DEFINE(location_timer, location_timer_cb, NULL);

static bool location_run_is_done(enum sid_location_status status)
{
	switch (status) {
	case SID_LOCATION_SEND_ONLY_DONE:
	case SID_LOCATION_SCAN_ONLY_DONE:
	case SID_LOCATION_SCAN_AND_SEND_DONE:
		return true;
	default:
		return false;
	}
}

static void on_location_update(const struct sid_location_result *const result, void *context)
{
	ARG_UNUSED(context);

	LOG_INF("Location update status %d, err %d (%s), effort mode %d, link type %d",
		(int)result->status, (int)result->err, SID_ERROR_T_STR(result->err),
		(int)result->mode, (int)result->link);

	if (result->size) {
		LOG_HEXDUMP_INF(result->payload, result->size, "Location payload");
	}

	if (location_run_is_done(result->status)) {
		atomic_clear(&location_run_pending);
#if defined(CONFIG_STATE_NOTIFIER)
		application_state_sending(&global_state_notifier, false);
#endif
	}
}

static struct sid_location_config location_config = {
	.sid_location_type_mask = LOCATION_METHOD_MASK,
	.max_effort = LOCATION_EFFORT_MODE,
	.manage_effort = IS_ENABLED(CONFIG_SID_END_DEVICE_LOCATION_MANAGE_EFFORT),
	.callbacks = {
		.on_update = on_location_update,
	},
};

static void app_event_location_init(sidewalk_ctx_t *sid, void *ctx)
{
	ARG_UNUSED(ctx);

	if (sid == NULL || sid->handle == NULL) {
		LOG_ERR("Sidewalk has to be started before location init");
		return;
	}

	if (location_initialized) {
		return;
	}

	sid_error_t e = sid_location_init(sid->handle, &location_config);

	if (e != SID_ERROR_NONE) {
		LOG_ERR("sid location init err %d (%s)", (int)e, SID_ERROR_T_STR(e));
		location_init_requested = false;
		return;
	}
	location_initialized = true;

	LOG_INF("Location initialized, uplink every %d s in effort mode %d",
		CONFIG_SID_END_DEVICE_LOCATION_INTERVAL_S, (int)LOCATION_EFFORT_MODE);

	k_timer_start(&location_timer, K_NO_WAIT,
		      K_SECONDS(CONFIG_SID_END_DEVICE_LOCATION_INTERVAL_S));
}

static void app_event_location_run(sidewalk_ctx_t *sid, void *ctx)
{
	ARG_UNUSED(ctx);

	if (sid == NULL || sid->handle == NULL || !location_initialized) {
		LOG_ERR("Location is not initialized");
		return;
	}

	/* A single run may outlive the uplink period, for example when the scan
	 * results are fragmented over LoRa. Skip the cycle instead of queuing
	 * a run the location library would reject.
	 */
	if (!atomic_cas(&location_run_pending, 0, 1)) {
		LOG_WRN("Previous location run not finished, skipping this cycle");
		return;
	}

	struct sid_location_run_config config = {
		.type = SID_LOCATION_SCAN_AND_SEND,
		.mode = LOCATION_EFFORT_MODE,
	};

	sid_error_t e = sid_location_run(sid->handle, &config, 0);

	if (e != SID_ERROR_NONE) {
		LOG_ERR("sid location run err %d (%s)", (int)e, SID_ERROR_T_STR(e));
		atomic_clear(&location_run_pending);
		return;
	}

	LOG_INF("Location scan and send started in effort mode %d", (int)LOCATION_EFFORT_MODE);
#if defined(CONFIG_STATE_NOTIFIER)
	application_state_sending(&global_state_notifier, true);
#endif
}

static void on_sidewalk_event(bool in_isr, void *context)
{
	ARG_UNUSED(in_isr);
	ARG_UNUSED(context);

	int err = sidewalk_event_send(sidewalk_event_process, NULL, NULL);

	if (err) {
		LOG_ERR("Send event err %d", err);
	}
}

static void on_sidewalk_msg_received(const struct sid_msg_desc *msg_desc, const struct sid_msg *msg,
				     void *context)
{
	ARG_UNUSED(context);

	LOG_INF("Message received (type: %d, id: %u, size: %u)", (int)msg_desc->type, msg_desc->id,
		msg->size);
#if defined(CONFIG_STATE_NOTIFIER)
	application_state_receiving(&global_state_notifier, true);
	application_state_receiving(&global_state_notifier, false);
#endif
}

static void on_sidewalk_msg_sent(const struct sid_msg_desc *msg_desc, void *context)
{
	ARG_UNUSED(context);

	LOG_INF("Message send success (type: %d, id: %u)", (int)msg_desc->type, msg_desc->id);
}

static void on_sidewalk_send_error(sid_error_t error, const struct sid_msg_desc *msg_desc,
				   void *context)
{
	ARG_UNUSED(context);

	LOG_ERR("Message send err %d (%s), id %u", (int)error, SID_ERROR_T_STR(error),
		msg_desc->id);
}

static void on_sidewalk_factory_reset(void *context)
{
	ARG_UNUSED(context);

	LOG_INF("Factory reset notification received from sid api");
	if (sid_hal_reset(SID_HAL_RESET_NORMAL)) {
		LOG_WRN("Cannot reboot");
	}
}

static bool sidewalk_is_ready(const struct sid_status *status)
{
	return status->state == SID_STATE_READY &&
	       status->detail.registration_status == SID_STATUS_REGISTERED &&
	       status->detail.time_sync_status == SID_STATUS_TIME_SYNCED;
}

static void on_sidewalk_status_changed(const struct sid_status *status, void *context)
{
	ARG_UNUSED(context);

	uint32_t link_mask = status->detail.link_status_mask;
	struct sid_status *new_status = sid_hal_malloc(sizeof(struct sid_status));

	if (!new_status) {
		LOG_ERR("Failed to allocate memory for new status value");
	} else {
		memcpy(new_status, status, sizeof(struct sid_status));
		int err = sidewalk_event_send(sidewalk_event_new_status, new_status, sid_hal_free);

		if (err) {
			LOG_ERR("Send event err %d", err);
		}
	}

#if defined(CONFIG_STATE_NOTIFIER)
	switch (status->state) {
	case SID_STATE_READY:
	case SID_STATE_SECURE_CHANNEL_READY:
		application_state_connected(&global_state_notifier, true);
		break;
	case SID_STATE_NOT_READY:
		application_state_connected(&global_state_notifier, false);
		break;
	case SID_STATE_ERROR:
		application_state_error(&global_state_notifier, true);
		break;
	}

	application_state_registered(&global_state_notifier,
				     status->detail.registration_status == SID_STATUS_REGISTERED);
	application_state_time_sync(&global_state_notifier,
				    status->detail.time_sync_status == SID_STATUS_TIME_SYNCED);
#endif /* CONFIG_STATE_NOTIFIER */

	LOG_INF("Device %sregistered, Time Sync %s, Link status: {BLE: %s, FSK: %s, LoRa: %s}",
		(SID_STATUS_REGISTERED == status->detail.registration_status) ? "Is " : "Un",
		(SID_STATUS_TIME_SYNCED == status->detail.time_sync_status) ? "Success" : "Fail",
		(link_mask & SID_LINK_TYPE_1) ? "Up" : "Down",
		(link_mask & SID_LINK_TYPE_2) ? "Up" : "Down",
		(link_mask & SID_LINK_TYPE_3) ? "Up" : "Down");

	/* The location library has to be initialized after sid_init(), and it
	 * needs a registered and time synced device to resolve a location.
	 */
	if (!location_init_requested && sidewalk_is_ready(status)) {
		int err = sidewalk_event_send(app_event_location_init, NULL, NULL);

		if (err) {
			LOG_ERR("Send location init event err %d", err);
		} else {
			location_init_requested = true;
		}
	}
}

static bool gatt_authorize(struct bt_conn *conn, const struct bt_gatt_attr *attr)
{
	struct bt_conn_info cinfo = {};
	int ret = bt_conn_get_info(conn, &cinfo);

	if (ret != 0) {
		LOG_ERR("Failed to get id of connection err %d", ret);
		return false;
	}

	if (cinfo.id == BT_ID_SIDEWALK) {
		if (sid_ble_bt_attr_is_SMP(attr)) {
			return false;
		}
	}

#if defined(CONFIG_SIDEWALK_DFU)
	if (cinfo.id == BT_ID_SMP_DFU) {
		if (sid_ble_bt_attr_is_SIDEWALK(attr)) {
			return false;
		}
	}
#endif /* CONFIG_SIDEWALK_DFU */
	return true;
}

static const struct bt_gatt_authorization_cb gatt_authorization_callbacks = {
	.read_authorize = gatt_authorize,
	.write_authorize = gatt_authorize,
};

static uint16_t default_sync_intervals_h[MAX_TIME_SYNC_INTERVALS] = { 2, 4, 8,
								      12 }; // default GCS intervals
static struct sid_time_sync_config default_time_sync_config = {
	.adaptive_sync_intervals_h = default_sync_intervals_h,
	.num_intervals = ARRAY_SIZE(default_sync_intervals_h),
};

void app_start(void)
{
#if defined(CONFIG_STATE_NOTIFIER)
#if defined(CONFIG_GPIO)
	state_watch_init_gpio(&global_state_notifier);
#endif
#if defined(CONFIG_LOG)
	state_watch_init_log(&global_state_notifier);
#endif
	application_state_working(&global_state_notifier, true);
#endif /* CONFIG_STATE_NOTIFIER */

	static struct sid_event_callbacks event_callbacks = {
		.context = &sid_ctx,
		.on_event = on_sidewalk_event,
		.on_msg_received = on_sidewalk_msg_received,
		.on_msg_sent = on_sidewalk_msg_sent,
		.on_send_error = on_sidewalk_send_error,
		.on_status_changed = on_sidewalk_status_changed,
		.on_factory_reset = on_sidewalk_factory_reset,
	};

	/* A device that reports its location is typically mobile, but the
	 * static type is kept here to match the other sample variants.
	 */
	struct sid_end_device_characteristics dev_ch = {
		.type = SID_END_DEVICE_TYPE_STATIC,
		.power_type = SID_END_DEVICE_POWERED_BY_BATTERY_AND_LINE_POWER,
		.qualification_id = 0x0001,
	};

	sid_ctx.config = (struct sid_config){
		.link_mask = 0,
		.dev_ch = dev_ch,
		.callbacks = &event_callbacks,
		.link_config = app_get_ble_config(),
		.sub_ghz_link_config = app_get_sub_ghz_config(),
		.log_config = NULL,
		.time_sync_config = &default_time_sync_config,
	};

	int err = bt_gatt_authorization_cb_register(&gatt_authorization_callbacks);

	if (err) {
		LOG_ERR("Registering GATT authorization callbacks failed (err %d)", err);
		return;
	}

	sidewalk_start(&sid_ctx);
	sidewalk_event_send(sidewalk_event_platform_init, NULL, NULL);
	sidewalk_event_send(sidewalk_event_autostart, NULL, NULL);
}
