/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/ztest.h>
#include <zephyr/fff.h>

#include <sid_pal_ble_adapter_ifc.h>
#include <sid_ble_adapter_callbacks.h>
#include <sid_ble_advert.h>
#include <sid_ble_ama_service.h>
#include <sid_ble_connection.h>
#include <sid_ble_service.h>
#include <bt_app_callbacks.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>

#include <stdbool.h>
#include <errno.h>

DEFINE_FFF_GLOBALS;

/* Bluetooth host */
FAKE_VALUE_FUNC(int, sid_ble_bt_enable, bt_ready_cb_t);
FAKE_VALUE_FUNC(int, sid_ble_bt_disable);
FAKE_VOID_FUNC(bt_id_get, bt_addr_le_t *, size_t *);
FAKE_VALUE_FUNC(int, bt_id_create, bt_addr_le_t *, uint8_t *);
FAKE_VALUE_FUNC(int, bt_id_delete, uint8_t);
FAKE_VALUE_FUNC(int, bt_id_reset, uint8_t, bt_addr_le_t *, uint8_t *);
FAKE_VALUE_FUNC(int, bt_conn_cb_register, struct bt_conn_cb *);
FAKE_VOID_FUNC(bt_gatt_cb_register, struct bt_gatt_cb *);
FAKE_VALUE_FUNC(struct bt_conn *, bt_conn_ref, struct bt_conn *);
FAKE_VOID_FUNC(bt_conn_unref, struct bt_conn *);
FAKE_VALUE_FUNC(const bt_addr_le_t *, bt_conn_get_dst, const struct bt_conn *);
FAKE_VALUE_FUNC(int, bt_conn_disconnect, struct bt_conn *, uint8_t);
FAKE_VALUE_FUNC(int, bt_conn_get_info, const struct bt_conn *, struct bt_conn_info *);
FAKE_VALUE_FUNC(int, bt_conn_le_param_update, struct bt_conn *, const struct bt_le_conn_param *);
FAKE_VALUE_FUNC(int, bt_hci_get_conn_handle, const struct bt_conn *, uint16_t *);
FAKE_VALUE_FUNC(struct net_buf *, bt_hci_cmd_alloc, k_timeout_t);
FAKE_VALUE_FUNC(int, bt_hci_cmd_send_sync, uint16_t, struct net_buf *, struct net_buf **);
FAKE_VALUE_FUNC(void *, net_buf_simple_add, struct net_buf_simple *, size_t);
FAKE_VOID_FUNC(net_buf_unref, struct net_buf *);
FAKE_VALUE_FUNC(ssize_t, bt_gatt_attr_read_service, struct bt_conn *, const struct bt_gatt_attr *,
		void *, uint16_t, uint16_t);
FAKE_VALUE_FUNC(ssize_t, bt_gatt_attr_read_chrc, struct bt_conn *, const struct bt_gatt_attr *,
		void *, uint16_t, uint16_t);
FAKE_VALUE_FUNC(ssize_t, bt_gatt_attr_read_ccc, struct bt_conn *, const struct bt_gatt_attr *,
		void *, uint16_t, uint16_t);
FAKE_VALUE_FUNC(ssize_t, bt_gatt_attr_write_ccc, struct bt_conn *, const struct bt_gatt_attr *,
		const void *, uint16_t, uint16_t, uint8_t);

/* Sidewalk PAL modules outside of the test */
FAKE_VALUE_FUNC(int, sid_ble_advert_init);
FAKE_VALUE_FUNC(int, sid_ble_advert_deinit);
FAKE_VALUE_FUNC(int, sid_ble_advert_start);
FAKE_VALUE_FUNC(int, sid_ble_advert_stop);
FAKE_VALUE_FUNC(int, sid_ble_advert_update, uint8_t *, uint8_t);
FAKE_VALUE_FUNC(int, sid_ble_advert_params_set, sid_ble_advert_params_t *);
FAKE_VALUE_FUNC(int, sid_ble_advert_params_get, sid_ble_advert_params_t *);
FAKE_VOID_FUNC(sid_ble_advert_notify_connection);
FAKE_VALUE_FUNC(int, sid_ble_send_data, sid_ble_srv_params_t *, uint8_t *, uint16_t);

/* Sidewalk library callbacks */
FAKE_VOID_FUNC(lib_data_cb, sid_ble_cfg_service_identifier_t, uint8_t *, uint16_t);
FAKE_VOID_FUNC(lib_notify_cb, sid_ble_cfg_service_identifier_t, bool);
FAKE_VOID_FUNC(lib_conn_cb, bool, uint8_t *);
FAKE_VOID_FUNC(lib_ind_cb, bool);
FAKE_VOID_FUNC(lib_mtu_cb, uint16_t);
FAKE_VOID_FUNC(lib_adv_start_cb);

#define FFF_FAKES_LIST(FAKE)                                                                       \
	FAKE(sid_ble_bt_enable)                                                                    \
	FAKE(sid_ble_bt_disable)                                                                   \
	FAKE(bt_id_get)                                                                            \
	FAKE(bt_id_create)                                                                         \
	FAKE(bt_id_delete)                                                                         \
	FAKE(bt_id_reset)                                                                          \
	FAKE(bt_conn_cb_register)                                                                  \
	FAKE(bt_gatt_cb_register)                                                                  \
	FAKE(bt_conn_ref)                                                                          \
	FAKE(bt_conn_unref)                                                                        \
	FAKE(bt_conn_get_dst)                                                                      \
	FAKE(bt_conn_disconnect)                                                                   \
	FAKE(bt_conn_get_info)                                                                     \
	FAKE(bt_conn_le_param_update)                                                              \
	FAKE(bt_hci_get_conn_handle)                                                               \
	FAKE(bt_hci_cmd_alloc)                                                                     \
	FAKE(bt_hci_cmd_send_sync)                                                                 \
	FAKE(net_buf_simple_add)                                                                   \
	FAKE(net_buf_unref)                                                                        \
	FAKE(bt_gatt_attr_read_service)                                                            \
	FAKE(bt_gatt_attr_read_chrc)                                                               \
	FAKE(bt_gatt_attr_read_ccc)                                                                \
	FAKE(bt_gatt_attr_write_ccc)                                                               \
	FAKE(sid_ble_advert_init)                                                                  \
	FAKE(sid_ble_advert_deinit)                                                                \
	FAKE(sid_ble_advert_start)                                                                 \
	FAKE(sid_ble_advert_stop)                                                                  \
	FAKE(sid_ble_advert_update)                                                                \
	FAKE(sid_ble_advert_params_set)                                                            \
	FAKE(sid_ble_advert_params_get)                                                            \
	FAKE(sid_ble_advert_notify_connection)                                                     \
	FAKE(sid_ble_send_data)                                                                    \
	FAKE(lib_data_cb)                                                                          \
	FAKE(lib_notify_cb)                                                                        \
	FAKE(lib_conn_cb)                                                                          \
	FAKE(lib_ind_cb)                                                                           \
	FAKE(lib_mtu_cb)                                                                           \
	FAKE(lib_adv_start_cb)

/* Time the fake controller takes to report a link closed after the terminate request. */
#define LINK_CLOSE_DELAY_MS (10)

struct bt_conn {
	uint8_t dummy;
};

static const sid_pal_ble_adapter_callbacks_t lib_callbacks = {
	.data_callback = lib_data_cb,
	.notify_callback = lib_notify_cb,
	.conn_callback = lib_conn_cb,
	.ind_callback = lib_ind_cb,
	.mtu_callback = lib_mtu_cb,
	.adv_start_callback = lib_adv_start_cb,
};

static sid_pal_ble_adapter_interface_t ble_ifc;
static struct bt_conn_cb *sid_bt_conn_cb;
static bool adapter_initialized;

static struct bt_conn conn_a = { .dummy = 0xA1 };
static struct bt_conn conn_b = { .dummy = 0xB2 };
static const bt_addr_le_t addr_a = {
	.type = BT_ADDR_LE_RANDOM,
	.a = { { 0xA6, 0xA5, 0xA4, 0xA3, 0xA2, 0xA1 } },
};
static const bt_addr_le_t addr_b = {
	.type = BT_ADDR_LE_RANDOM,
	.a = { { 0xB6, 0xB5, 0xB4, 0xB3, 0xB2, 0xB1 } },
};

/* Links still up in the fake controller, closed in the after hook. */
static struct bt_conn *links_up[2];

static bool link_a_closed;
static bool link_a_closed_at_id_delete;
static struct k_work_delayable link_a_close_work;

static void bt_id_get_fake_ids_created(bt_addr_le_t *addrs, size_t *count)
{
	ARG_UNUSED(addrs);
	*count = BT_ID_SIDEWALK + 1;
}

static int bt_conn_get_info_fake_sidewalk(const struct bt_conn *conn, struct bt_conn_info *info)
{
	ARG_UNUSED(conn);
	info->id = BT_ID_SIDEWALK;
	info->state = BT_CONN_STATE_CONNECTED;
	info->le.timeout = CONFIG_BT_PERIPHERAL_PREF_TIMEOUT;
	return 0;
}

static const bt_addr_le_t *bt_conn_get_dst_fake_by_conn(const struct bt_conn *conn)
{
	return (conn == &conn_b) ? &addr_b : &addr_a;
}

static struct bt_conn *bt_conn_ref_pass(struct bt_conn *conn)
{
	return conn;
}

static void link_set(struct bt_conn *conn, bool up)
{
	for (size_t i = 0; i < ARRAY_SIZE(links_up); i++) {
		if (up && !links_up[i]) {
			links_up[i] = conn;
			return;
		}
		if (!up && links_up[i] == conn) {
			links_up[i] = NULL;
			return;
		}
	}
}

static void link_connect(struct bt_conn *conn)
{
	link_set(conn, true);
	sid_bt_conn_cb->connected(conn, BT_HCI_ERR_SUCCESS);
}

static void link_disconnect(struct bt_conn *conn, uint8_t reason)
{
	link_set(conn, false);
	sid_bt_conn_cb->disconnected(conn, reason);
}

/* Let the connection work queue handle the queued events. The ztest thread is cooperative. */
static void ble_conn_handler_run(void)
{
	k_sleep(K_MSEC(1));
}

static void link_a_close_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	link_a_closed = true;
	link_disconnect(&conn_a, BT_HCI_ERR_LOCALHOST_TERM_CONN);
}

/* The controller reports the link closed some time after the terminate request. */
static int bt_conn_disconnect_fake_close_later(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(reason);
	if (conn == &conn_a) {
		k_work_schedule(&link_a_close_work, K_MSEC(LINK_CLOSE_DELAY_MS));
	}
	return 0;
}

static int bt_id_delete_fake_record_link(uint8_t id)
{
	ARG_UNUSED(id);
	link_a_closed_at_id_delete = link_a_closed;
	return 0;
}

static const struct bt_gatt_attr *ama_write_attr_get(void)
{
	const struct bt_gatt_service_static *svc = sid_ble_get_ama_service();

	for (size_t i = 0; i < svc->attr_count; i++) {
		if (svc->attrs[i].write && svc->attrs[i].write != bt_gatt_attr_write_ccc) {
			return &svc->attrs[i];
		}
	}

	return NULL;
}

static void ama_write(struct bt_conn *conn)
{
	static uint8_t data[] = { 0x01, 0x02, 0x03 };
	const struct bt_gatt_attr *attr = ama_write_attr_get();

	zassert_not_null(attr, "AMA write characteristic not found");
	zassert_equal(attr->write(conn, attr, data, sizeof(data), 0, 0), sizeof(data));
}

static void adapter_init(void)
{
	zassert_equal(ble_ifc->init(NULL), SID_ERROR_NONE);
	adapter_initialized = true;

	/* The connection module registers its callbacks only once. */
	if (!sid_bt_conn_cb) {
		sid_bt_conn_cb = bt_conn_cb_register_fake.arg0_val;
	}
	zassert_not_null(sid_bt_conn_cb);
}

static void adapter_deinit(void)
{
	zassert_equal(ble_ifc->deinit(), SID_ERROR_NONE);
	adapter_initialized = false;
}

static void *suite_setup(void)
{
	k_work_init_delayable(&link_a_close_work, link_a_close_work_handler);
	zassert_equal(sid_pal_ble_adapter_create(&ble_ifc), SID_ERROR_NONE);

	return NULL;
}

static void before_test(void *fixture)
{
	ARG_UNUSED(fixture);

	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();

	link_a_closed = false;
	link_a_closed_at_id_delete = false;
	bt_id_get_fake.custom_fake = bt_id_get_fake_ids_created;
	bt_conn_get_info_fake.custom_fake = bt_conn_get_info_fake_sidewalk;
	bt_conn_get_dst_fake.custom_fake = bt_conn_get_dst_fake_by_conn;
	bt_conn_ref_fake.custom_fake = bt_conn_ref_pass;

	zassert_equal(ble_ifc->set_callback(&lib_callbacks), SID_ERROR_NONE);
	adapter_init();
}

static void after_test(void *fixture)
{
	ARG_UNUSED(fixture);

	k_work_cancel_delayable(&link_a_close_work);
	for (size_t i = 0; i < ARRAY_SIZE(links_up); i++) {
		if (links_up[i]) {
			link_disconnect(links_up[i], BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		}
	}
	ble_conn_handler_run();

	if (adapter_initialized) {
		adapter_deinit();
	}
	ble_conn_handler_run();
}

ZTEST_SUITE(sid_ble_adapter, NULL, suite_setup, before_test, after_test, NULL);

ZTEST(sid_ble_adapter, test_01_write_from_active_link_delivered)
{
	link_connect(&conn_a);
	ble_conn_handler_run();

	ama_write(&conn_a);
	zassert_equal(lib_data_cb_fake.call_count, 1);
	zassert_equal(lib_data_cb_fake.arg0_val, AMA_SERVICE);
	zassert_equal(lib_data_cb_fake.arg2_val, 3);
}

ZTEST(sid_ble_adapter, test_02_deinit_without_link)
{
	adapter_deinit();

	zassert_equal(bt_conn_disconnect_fake.call_count, 0);
	zassert_equal(bt_id_delete_fake.call_count, 1);
	zassert_equal(bt_id_delete_fake.arg0_val, BT_ID_SIDEWALK);
}

ZTEST(sid_ble_adapter, test_03_deinit_waits_for_link_closed)
{
	link_connect(&conn_a);
	ble_conn_handler_run();
	bt_conn_disconnect_fake.custom_fake = bt_conn_disconnect_fake_close_later;
	bt_id_delete_fake.custom_fake = bt_id_delete_fake_record_link;
	/* Another user (DFU, the application) keeps Bluetooth enabled. */
	sid_ble_bt_disable_fake.return_val = 1;

	adapter_deinit();

	zassert_equal(bt_conn_disconnect_fake.call_count, 1);
	zassert_equal(bt_conn_disconnect_fake.arg0_val, &conn_a);
	zassert_equal(bt_id_delete_fake.call_count, 1);
	zassert_true(link_a_closed_at_id_delete,
		     "Sidewalk identity deleted while its link was still up");
}

ZTEST(sid_ble_adapter, test_04_write_after_deinit_ignored)
{
	link_connect(&conn_a);
	ble_conn_handler_run();
	/* The link stays up after deinit, the terminate is not completed yet. */
	adapter_deinit();

	ama_write(&conn_a);
	zassert_equal(lib_data_cb_fake.call_count, 0, "Write after deinit reached Sidewalk");
}

ZTEST(sid_ble_adapter, test_05_write_from_link_before_reinit_ignored)
{
	link_connect(&conn_a);
	ble_conn_handler_run();
	/* The link outlives deinit, and init creates the Sidewalk identity again. */
	adapter_deinit();
	adapter_init();

	ama_write(&conn_a);
	zassert_equal(lib_data_cb_fake.call_count, 0,
		      "Write from a link of the previous init reached Sidewalk");
}

ZTEST(sid_ble_adapter, test_06_write_from_refused_link_ignored)
{
	link_connect(&conn_a);
	ble_conn_handler_run();
	/* B is refused, but can still write until the link is closed. */
	link_connect(&conn_b);
	ble_conn_handler_run();
	zassert_equal(bt_conn_disconnect_fake.arg0_val, &conn_b);

	ama_write(&conn_b);
	zassert_equal(lib_data_cb_fake.call_count, 0, "Write from a refused link reached Sidewalk");
}

ZTEST(sid_ble_adapter, test_07_deinit_disconnect_error_completes)
{
	int64_t start = 0;

	link_connect(&conn_a);
	ble_conn_handler_run();
	/* The terminate is not sent, so no disconnected event follows. */
	bt_conn_disconnect_fake.return_val = -EIO;

	start = k_uptime_get();
	adapter_deinit();
	zassert_true(k_uptime_get() - start < LINK_CLOSE_DELAY_MS,
		     "Deinit waited for a disconnect that was not requested");

	zassert_equal(bt_conn_disconnect_fake.call_count, 1);
	zassert_equal(sid_ble_advert_deinit_fake.call_count, 1);
	zassert_equal(bt_id_delete_fake.call_count, 1);
	zassert_equal(sid_ble_bt_disable_fake.call_count, 1);

	adapter_init();
}
