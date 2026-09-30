/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <sid_ble_connection.h>
#include <sid_ble_adapter_callbacks.h>
#include <sid_ble_advert.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

#include <errno.h>
#include <hci_utils.h>
#include <bt_app_callbacks.h>

#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(sid_ble_conn, CONFIG_SIDEWALK_LOG_LEVEL);

static void ble_connect_cb(struct bt_conn *conn, uint8_t err);
static void ble_disconnect_cb(struct bt_conn *conn, uint8_t reason);
static void ble_mtu_cb(struct bt_conn *conn, uint16_t tx_mtu, uint16_t rx_mtu);
static void ble_conn_evt_handler(struct k_work *work);

enum ble_conn_evt_type {
	BLE_CONN_EVT_CONNECTED,
	BLE_CONN_EVT_DISCONNECTED,
	BLE_CONN_EVT_MTU,
};

struct ble_conn_evt {
	struct bt_conn *conn;
	atomic_val_t init_count;
	uint16_t context;
	enum ble_conn_evt_type type;
};

K_MSGQ_DEFINE(ble_conn_evt_msgq, sizeof(struct ble_conn_evt),
	      CONFIG_SIDEWALK_BLE_CONN_EVT_QUEUE_SIZE, 4);
static K_THREAD_STACK_DEFINE(ble_conn_workq_stack, CONFIG_SIDEWALK_BLE_CONN_WORKQ_STACK_SIZE);
static struct k_work_q ble_conn_workq;
static K_WORK_DEFINE(ble_conn_evt_work, ble_conn_evt_handler);
static atomic_t conn_init_count;

static K_MUTEX_DEFINE(conn_state_mutex);

static sid_ble_conn_data_t conn_data;
static atomic_ptr_t conn_data_ptr;

static struct bt_le_conn_param conn_params_next = {
	.interval_min = CONFIG_BT_PERIPHERAL_PREF_MIN_INT,
	.interval_max = CONFIG_BT_PERIPHERAL_PREF_MAX_INT,
	.latency = CONFIG_BT_PERIPHERAL_PREF_LATENCY,
	.timeout = CONFIG_BT_PERIPHERAL_PREF_TIMEOUT,
};
static struct bt_le_conn_param conn_params_prev = {
	.interval_min = CONFIG_BT_PERIPHERAL_PREF_MIN_INT,
	.interval_max = CONFIG_BT_PERIPHERAL_PREF_MAX_INT,
	.latency = CONFIG_BT_PERIPHERAL_PREF_LATENCY,
	.timeout = CONFIG_BT_PERIPHERAL_PREF_TIMEOUT,
};

static struct bt_conn_cb conn_callbacks = {
	.connected = ble_connect_cb,
	.disconnected = ble_disconnect_cb,
};

static struct bt_gatt_cb gatt_callbacks = { .att_mtu_updated = ble_mtu_cb };

/* Read the current connection parameters from the host stack. */
static int ble_conn_param_get(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	struct bt_conn_info info = { 0 };
	int err = bt_conn_get_info(conn, &info);
	if (err) {
		return err;
	}

	param->interval_max = info.le.interval_us / 1250U;
	param->interval_min = info.le.interval_us / 1250U;
	param->latency = info.le.latency;
	param->timeout = info.le.timeout;

	return 0;
}

/* Sidewalk connection on an initialized module. Other identities belong to the app or DFU. */
static bool ble_conn_is_valid(struct bt_conn *conn)
{
	struct bt_conn_info conn_info = {};

	if (!atomic_ptr_get(&conn_data_ptr) || !conn || bt_conn_get_info(conn, &conn_info) ||
	    conn_info.id != BT_ID_SIDEWALK) {
		return false;
	}

	return true;
}

/* Queue an event for the handler. The event holds its own reference to conn.
 * context is the disconnect reason or the MTU, depending on type.
 */
static void ble_conn_evt_post(enum ble_conn_evt_type type, struct bt_conn *conn, uint16_t context)
{
	struct ble_conn_evt evt = {
		.conn = bt_conn_ref(conn),
		.init_count = atomic_get(&conn_init_count),
		.context = context,
		.type = type,
	};

	if (!evt.conn) {
		LOG_ERR("Connection event %u dropped, no reference", type);
		return;
	}

	if (k_msgq_put(&ble_conn_evt_msgq, &evt, K_NO_WAIT)) {
		LOG_ERR("Connection event %u dropped, queue full", type);
		bt_conn_unref(evt.conn);
		return;
	}

	k_work_submit_to_queue(&ble_conn_workq, &ble_conn_evt_work);
}

/* Queue a new Sidewalk connection. Runs on the BT RX thread, so do no blocking work here. */
static void ble_connect_cb(struct bt_conn *conn, uint8_t conn_err)
{
	if (!ble_conn_is_valid(conn)) {
		return;
	}

	if (conn_err) {
		LOG_ERR("Connection failed (err %u)", conn_err);
		return;
	}

	sid_ble_advert_notify_connection();
	ble_conn_evt_post(BLE_CONN_EVT_CONNECTED, conn, 0);
}

/* Queue the end of a Sidewalk connection. Runs on the system workqueue. */
static void ble_disconnect_cb(struct bt_conn *conn, uint8_t reason)
{
	if (!ble_conn_is_valid(conn)) {
		return;
	}

	ble_conn_evt_post(BLE_CONN_EVT_DISCONNECTED, conn, reason);
}

/* Queue the ATT MTU of a Sidewalk connection. */
static void ble_mtu_cb(struct bt_conn *conn, uint16_t tx_mtu, uint16_t rx_mtu)
{
	if (!ble_conn_is_valid(conn)) {
		return;
	}

	ble_conn_evt_post(BLE_CONN_EVT_MTU, conn, MIN(tx_mtu, rx_mtu));
}

static void ble_conn_connected_handle(struct bt_conn *conn)
{
	const bt_addr_le_t *bt_addr_le = NULL;
	struct bt_le_conn_param conn_params = { 0 };
	int err = 0;

	/* Handle the new connection */
	k_mutex_lock(&conn_state_mutex, K_FOREVER);
	if (!atomic_ptr_get(&conn_data_ptr)) {
		err = -ENODEV;
	} else if (conn_data.conn) {
		err = -EALREADY;
	} else {
		/* Fill connection data */
		bt_addr_le = bt_conn_get_dst(conn);
		if (bt_addr_le) {
			memcpy(conn_data.addr, bt_addr_le->a.val, BT_ADDR_SIZE);
		} else {
			LOG_WRN("Connection bt address not found.");
			memset(conn_data.addr, 0x00, BT_ADDR_SIZE);
		}
		conn_data.conn = bt_conn_ref(conn);
	}
	k_mutex_unlock(&conn_state_mutex);

	switch (err) {
	case 0:
		/* Success */
		break;
	case -EALREADY:
		/* Refuse the link if the connection is taken */
		LOG_WRN("Sidewalk connection already active, reject the new one");
		err = bt_conn_disconnect(conn, BT_HCI_ERR_CONN_LIMIT_EXCEEDED);
		if (err) {
			LOG_ERR("bt_conn_disconnect failed with error: %d = %s", err,
				strerror(err));
		}
		return;
	default:
		LOG_WRN("Connection ignored (err %d)", err);
		return;
	}

	/* Inform adapter about new connection */
	sid_ble_adapter_conn_connected((const uint8_t *)conn_data.addr);

	/* Set connection parameters */
	k_mutex_lock(&conn_state_mutex, K_FOREVER);
	conn_params = conn_params_next;
	k_mutex_unlock(&conn_state_mutex);

	err = bt_conn_le_param_update(conn, &conn_params);
	if (err) {
		LOG_WRN("bt_conn_le_param_update failed with error: %d = %s", err, strerror(err));
	}

	LOG_INF("BT Connected");
}

static void ble_conn_disconnected_handle(struct bt_conn *conn, uint8_t reason)
{
	bool is_active_conn = false;
	int err = 0;

	/* Release the slot if conn holds it and keep its parameters, then tell Sidewalk. */
	k_mutex_lock(&conn_state_mutex, K_FOREVER);
	is_active_conn = (conn_data.conn == conn);
	if (is_active_conn) {
		err = ble_conn_param_get(conn, &conn_params_prev);
		bt_conn_unref(conn_data.conn);
		conn_data.conn = NULL;
	}
	k_mutex_unlock(&conn_state_mutex);

	if (!is_active_conn) {
		return;
	}

	if (err) {
		LOG_WRN("Connection param get failed (err=%d)", err);
	}

	sid_ble_adapter_conn_disconnected((const uint8_t *)conn_data.addr);

	LOG_INF("BT Disconnected Reason: 0x%x = %s", reason, HCI_err_to_str(reason));
}

/* Forward the ATT MTU of the active Sidewalk connection to Sidewalk. */
static void ble_conn_mtu_handle(struct bt_conn *conn, uint16_t mtu)
{
	if (!conn_data.conn || conn_data.conn == conn) {
		sid_ble_adapter_mtu_changed(mtu);
	}
}

/* Handle queued events in order. Drop events from before the last init or deinit. */
static void ble_conn_evt_handler(struct k_work *work)
{
	struct ble_conn_evt evt = { 0 };

	ARG_UNUSED(work);

	while (!k_msgq_get(&ble_conn_evt_msgq, &evt, K_NO_WAIT)) {
		if (atomic_ptr_get(&conn_data_ptr) &&
		    evt.init_count == atomic_get(&conn_init_count)) {
			switch (evt.type) {
			case BLE_CONN_EVT_CONNECTED:
				ble_conn_connected_handle(evt.conn);
				break;
			case BLE_CONN_EVT_DISCONNECTED:
				ble_conn_disconnected_handle(evt.conn, (uint8_t)evt.context);
				break;
			case BLE_CONN_EVT_MTU:
				ble_conn_mtu_handle(evt.conn, evt.context);
				break;
			default:
				break;
			}
		}
		bt_conn_unref(evt.conn);
	}
}

int sid_ble_conn_param_get(struct bt_le_conn_param *param)
{
	int err = 0;

	if (!param) {
		return -EINVAL;
	}

	k_mutex_lock(&conn_state_mutex, K_FOREVER);
	if (conn_data.conn) {
		err = ble_conn_param_get(conn_data.conn, param);
	} else {
		memcpy(param, &conn_params_prev, sizeof(*param));
	}
	k_mutex_unlock(&conn_state_mutex);

	if (err) {
		LOG_ERR("Connection param get failed (err=%d)", err);
	}

	return err;
}

int sid_ble_conn_param_update(const struct bt_le_conn_param *param)
{
	struct bt_conn *conn = NULL;
	int err = 0;

	if (!param) {
		return -EINVAL;
	}

	k_mutex_lock(&conn_state_mutex, K_FOREVER);
	memcpy(&conn_params_next, param, sizeof(struct bt_le_conn_param));
	if (conn_data.conn) {
		conn = bt_conn_ref(conn_data.conn);
	}
	k_mutex_unlock(&conn_state_mutex);

	if (conn) {
		err = bt_conn_le_param_update(conn, param);
		bt_conn_unref(conn);
		if (err) {
			LOG_WRN("bt_conn_le_param_update failed with error: %d = %s", err,
				strerror(err));
		}
	}

	return 0;
}

const sid_ble_conn_data_t *sid_ble_conn_data_get(void)
{
	return (const sid_ble_conn_data_t *)atomic_ptr_get(&conn_data_ptr);
}

void sid_ble_conn_init(void)
{
	static bool bt_conn_registered;
	static bool workq_started;

	/* Start the work queue and register the BT callbacks once,
	 * open a new init round on every call.
	 */
	if (!workq_started) {
		const struct k_work_queue_config workq_cfg = { .name = "sid_ble_conn" };

		k_work_queue_init(&ble_conn_workq);
		k_work_queue_start(&ble_conn_workq, ble_conn_workq_stack,
				   K_THREAD_STACK_SIZEOF(ble_conn_workq_stack),
				   K_PRIO_COOP(CONFIG_SIDEWALK_BLE_CONN_WORKQ_PRIORITY),
				   &workq_cfg);
		workq_started = true;
	}

	atomic_inc(&conn_init_count);
	atomic_ptr_set(&conn_data_ptr, &conn_data);

	if (!bt_conn_registered) {
		int e = bt_conn_cb_register(&conn_callbacks);
		switch (e) {
		case 0:
		case -EEXIST:
			break;
		default: {
			LOG_ERR("bt_conn_cb_register failed with error: %d = %s", e, strerror(e));
			return;
		}
		}
		bt_gatt_cb_register(&gatt_callbacks);
		bt_conn_registered = true;
	}
}

int sid_ble_conn_disconnect(void)
{
	struct bt_conn *conn = NULL;
	int err = 0;

	k_mutex_lock(&conn_state_mutex, K_FOREVER);
	if (conn_data.conn) {
		conn = bt_conn_ref(conn_data.conn);
	}
	k_mutex_unlock(&conn_state_mutex);

	if (!conn) {
		return -ENOENT;
	}

	err = bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	bt_conn_unref(conn);

	return err;
}

void sid_ble_conn_deinit(void)
{
	/* Refuse new connections first. Queued events are dropped by the handler. */
	atomic_ptr_clear(&conn_data_ptr);
	atomic_inc(&conn_init_count);

	/* The disconnected event may come after deinit (e.g. bt_disable), so drop the slot now. */
	k_mutex_lock(&conn_state_mutex, K_FOREVER);
	if (conn_data.conn) {
		bt_conn_unref(conn_data.conn);
		conn_data.conn = NULL;
	}
	k_mutex_unlock(&conn_state_mutex);
}
