/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/ztest.h>
#include <zephyr/fff.h>

#include <sid_ble_connection.h>

#include <mock/ble_callbacks_mock.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <bt_app_callbacks.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, bt_conn_cb_register, struct bt_conn_cb *);
FAKE_VOID_FUNC(bt_gatt_cb_register, struct bt_gatt_cb *);
FAKE_VALUE_FUNC(struct bt_conn *, bt_conn_ref, struct bt_conn *);
FAKE_VOID_FUNC(bt_conn_unref, struct bt_conn *);
FAKE_VALUE_FUNC(const bt_addr_le_t *, bt_conn_get_dst, const struct bt_conn *);
FAKE_VALUE_FUNC(int, bt_conn_disconnect, struct bt_conn *, uint8_t);
FAKE_VALUE_FUNC(int, bt_conn_get_info, const struct bt_conn *, struct bt_conn_info *);
FAKE_VOID_FUNC(sid_ble_advert_notify_connection);
FAKE_VALUE_FUNC(int, bt_conn_le_param_update, struct bt_conn *, const struct bt_le_conn_param *);

#define FFF_FAKES_LIST(FAKE)                                                                       \
	FFF_FAKES_LIST_BLE_CALLBACKS(FAKE)                                                         \
	FAKE(bt_conn_cb_register)                                                                  \
	FAKE(bt_gatt_cb_register)                                                                  \
	FAKE(bt_conn_ref)                                                                          \
	FAKE(bt_conn_unref)                                                                        \
	FAKE(bt_conn_get_dst)                                                                      \
	FAKE(bt_conn_disconnect)                                                                   \
	FAKE(bt_conn_get_info)                                                                     \
	FAKE(sid_ble_advert_notify_connection)                                                     \
	FAKE(bt_conn_le_param_update)

#define CONNECTED (true)
#define DISCONNECTED (false)
#define ESUCCESS (0)

struct bt_conn {
	uint8_t dummy;
};

typedef struct {
	size_t num_calls;
	uint8_t *addr;
	bool state;
} connection_callback_test_t;

static connection_callback_test_t conn_cb_test;
static struct bt_conn_cb *sid_bt_conn_cb;
static struct bt_gatt_cb *sid_bt_gatt_cb;
static size_t conn_cb_register_count;
static size_t gatt_cb_register_count;

static void connection_callback_connected(const uint8_t *ble_addr)
{
	conn_cb_test.num_calls++;
	conn_cb_test.addr = (uint8_t *)ble_addr;
}

static void connection_callback_disconnected(const uint8_t *ble_addr)
{
	conn_cb_test.num_calls++;
	conn_cb_test.addr = (uint8_t *)ble_addr;
}

static int bt_conn_get_info_fake1(const struct bt_conn *a, struct bt_conn_info *b)
{
	b->id = BT_ID_SIDEWALK;
	return 0;
}

static uint16_t param_get_fake_interval = 24;
static uint16_t param_get_fake_latency = 1;
static uint16_t param_get_fake_timeout = 400;

static int bt_conn_get_info_fake_param_get(const struct bt_conn *a, struct bt_conn_info *b)
{
	b->id = BT_ID_SIDEWALK;
	b->le.interval_us = (uint32_t)param_get_fake_interval * 1250U;
	b->le.latency = param_get_fake_latency;
	b->le.timeout = param_get_fake_timeout;
	return 0;
}

static int bt_conn_get_info_fake_error(const struct bt_conn *a, struct bt_conn_info *b)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	return -ENOTCONN;
}

static int ref_balance;
static const struct bt_conn *foreign_conn;
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

static struct bt_conn *bt_conn_ref_tracked(struct bt_conn *conn)
{
	ref_balance++;
	return conn;
}

static void bt_conn_unref_tracked(struct bt_conn *conn)
{
	ARG_UNUSED(conn);
	ref_balance--;
}

static int bt_conn_get_info_fake_by_conn(const struct bt_conn *a, struct bt_conn_info *b)
{
	b->id = (a == foreign_conn) ? BT_ID_DEFAULT : BT_ID_SIDEWALK;
	return 0;
}

static const bt_addr_le_t *bt_conn_get_dst_fake_by_conn(const struct bt_conn *conn)
{
	return (conn == &conn_b) ? &addr_b : &addr_a;
}

/* Per-connection identity and address fakes. */
static void multi_conn_fakes_install(void)
{
	bt_conn_get_info_fake.custom_fake = bt_conn_get_info_fake_by_conn;
	bt_conn_get_dst_fake.custom_fake = bt_conn_get_dst_fake_by_conn;
}

/* Let the connection work queue handle the queued events. The ztest thread is cooperative. */
static void ble_conn_handler_run(void)
{
	k_sleep(K_MSEC(1));
}

static void *suite_setup(void)
{
	/* The module registers its callbacks only once, also across suite repeats. */
	if (!sid_bt_conn_cb) {
		sid_ble_conn_init();
		conn_cb_register_count = bt_conn_cb_register_fake.call_count;
		gatt_cb_register_count = bt_gatt_cb_register_fake.call_count;
		sid_bt_conn_cb = bt_conn_cb_register_fake.arg0_val;
		sid_bt_gatt_cb = bt_gatt_cb_register_fake.arg0_val;
	}

	return NULL;
}

static void before_test(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Drop the slot and any queued event left by the previous test. */
	sid_ble_conn_deinit();
	ble_conn_handler_run();

	FFF_FAKES_LIST(RESET_FAKE);
	FFF_RESET_HISTORY();
	memset(&conn_cb_test, 0x00, sizeof(conn_cb_test));
	ref_balance = 0;
	foreign_conn = NULL;
	bt_conn_ref_fake.custom_fake = bt_conn_ref_tracked;
	bt_conn_unref_fake.custom_fake = bt_conn_unref_tracked;

	sid_ble_conn_init();
}

ZTEST_SUITE(sid_ble_connection, NULL, suite_setup, before_test, NULL, NULL);

ZTEST(sid_ble_connection, test_01_sid_ble_conn_init)
{
	zassert_equal(conn_cb_register_count, 1);
	zassert_equal(gatt_cb_register_count, 1);
	zassert_not_null(sid_bt_conn_cb);
	zassert_not_null(sid_bt_conn_cb->connected);
	zassert_not_null(sid_bt_conn_cb->disconnected);
	zassert_not_null(sid_bt_gatt_cb);
	zassert_not_null(sid_bt_gatt_cb->att_mtu_updated);

	sid_ble_conn_init();
	zassert_equal(bt_conn_cb_register_fake.call_count, 0, "Callbacks registered again");
	zassert_equal(bt_gatt_cb_register_fake.call_count, 0, "Callbacks registered again");
}

ZTEST(sid_ble_connection, test_02_sid_ble_conn_data_get)
{
	const sid_ble_conn_data_t *params = NULL;

	sid_ble_conn_init();
	sid_ble_conn_deinit();
	sid_ble_conn_deinit();

	sid_ble_conn_init();
	params = sid_ble_conn_data_get();
	zassert_not_null(params);

	sid_ble_conn_deinit();
	params = sid_ble_conn_data_get();
	zassert_is_null(params);
}

ZTEST(sid_ble_connection, test_03_sid_ble_conn_positive)
{
	uint8_t test_no_error = BT_HCI_ERR_SUCCESS;
	uint8_t test_reason = BT_HCI_ERR_UNKNOWN_LMP_PDU;
	const sid_ble_conn_data_t *params = NULL;
	struct bt_conn test_conn = { .dummy = 0xDC };
	const bt_addr_le_t test_addr = {
		.type = BT_ADDR_LE_RANDOM,
		.a = { { 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 } },
	};

	bt_conn_get_dst_fake.return_val = &test_addr;
	int (*custom_fakes[])(const struct bt_conn *,
			      struct bt_conn_info *) = { bt_conn_get_info_fake1 };
	SET_CUSTOM_FAKE_SEQ(bt_conn_get_info, custom_fakes, 1);

	sid_ble_conn_deinit();
	sid_ble_conn_init();

	sid_bt_conn_cb->connected(&test_conn, test_no_error);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_conn_connected_fake.call_count, 1);
	zassert_mem_equal(test_addr.a.val, sid_ble_adapter_conn_connected_fake.arg0_val,
			  BT_ADDR_SIZE);

	params = sid_ble_conn_data_get();
	zassert_equal(params->conn, &test_conn);
	zassert_mem_equal(test_addr.a.val, params->addr, BT_ADDR_SIZE);

	sid_bt_conn_cb->disconnected(&test_conn, test_reason);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_conn_disconnected_fake.call_count, 1);
	zassert_mem_equal(test_addr.a.val, sid_ble_adapter_conn_disconnected_fake.arg0_val,
			  BT_ADDR_SIZE);
}

ZTEST(sid_ble_connection, test_04_sid_ble_set_conn_cb_positive)
{
	uint8_t test_no_error = BT_HCI_ERR_SUCCESS;
	uint8_t test_reason = BT_HCI_ERR_UNKNOWN_LMP_PDU;
	const sid_ble_conn_data_t *params = NULL;
	struct bt_conn test_conn = { .dummy = 0xDC };
	const bt_addr_le_t test_addr = {
		.type = BT_ADDR_LE_RANDOM,
		.a = { { 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 } },
	};

	bt_conn_get_dst_fake.return_val = &test_addr;
	int (*custom_fakes[])(const struct bt_conn *,
			      struct bt_conn_info *) = { bt_conn_get_info_fake1 };
	SET_CUSTOM_FAKE_SEQ(bt_conn_get_info, custom_fakes, 1);

	sid_ble_conn_init();
	sid_ble_adapter_conn_connected_fake.custom_fake = connection_callback_connected;
	sid_ble_adapter_conn_disconnected_fake.custom_fake = connection_callback_disconnected;

	sid_bt_conn_cb->connected(&test_conn, test_no_error);
	ble_conn_handler_run();
	zassert_mem_equal(test_addr.a.val, conn_cb_test.addr, BT_ADDR_SIZE);

	params = sid_ble_conn_data_get();
	zassert_equal(params->conn, &test_conn);
	zassert_mem_equal(test_addr.a.val, params->addr, BT_ADDR_SIZE);

	sid_bt_conn_cb->disconnected(&test_conn, test_reason);
	ble_conn_handler_run();
	zassert_equal(conn_cb_test.state, DISCONNECTED);
	zassert_mem_equal(test_addr.a.val, conn_cb_test.addr, BT_ADDR_SIZE);
}

ZTEST(sid_ble_connection, test_05_sid_ble_conn_cb_set_call_count)
{
	size_t conn_cb_cnt_expected = 0;
	uint8_t test_no_error = BT_HCI_ERR_SUCCESS;
	uint8_t test_error_timeout = BT_HCI_ERR_CONN_TIMEOUT;
	uint8_t test_reason = BT_HCI_ERR_UNKNOWN_LMP_PDU;
	const bt_addr_le_t test_addr;
	struct bt_conn test_conn = { .dummy = 0xDC };

	bt_conn_get_dst_fake.return_val = &test_addr;
	int (*custom_fakes[])(const struct bt_conn *,
			      struct bt_conn_info *) = { bt_conn_get_info_fake1 };
	SET_CUSTOM_FAKE_SEQ(bt_conn_get_info, custom_fakes, 1);

	sid_ble_conn_init();
	sid_ble_adapter_conn_connected_fake.custom_fake = connection_callback_connected;
	sid_ble_adapter_conn_disconnected_fake.custom_fake = connection_callback_disconnected;

	sid_bt_conn_cb->connected(&test_conn, test_no_error);
	ble_conn_handler_run();
	conn_cb_cnt_expected++;
	zassert_equal(conn_cb_test.num_calls, conn_cb_cnt_expected);

	sid_bt_conn_cb->disconnected(&test_conn, test_reason);
	ble_conn_handler_run();
	conn_cb_cnt_expected++;
	zassert_equal(conn_cb_test.num_calls, conn_cb_cnt_expected);

	sid_bt_conn_cb->connected(&test_conn, test_error_timeout);
	ble_conn_handler_run();

	bt_conn_get_dst_fake.return_val = NULL;
	sid_bt_conn_cb->connected(&test_conn, test_no_error);
	ble_conn_handler_run();
	conn_cb_cnt_expected++;
	zassert_equal(conn_cb_test.num_calls, conn_cb_cnt_expected);
}

ZTEST(sid_ble_connection, test_06_sid_ble_disconnected_wrong_conn)
{
	size_t conn_cb_cnt_expected = 0;
	struct bt_conn test_wrong_conn;
	uint8_t test_no_error = BT_HCI_ERR_SUCCESS;
	uint8_t test_reason = BT_HCI_ERR_UNKNOWN_LMP_PDU;
	const bt_addr_le_t test_addr;
	struct bt_conn test_conn = { .dummy = 0xDC };

	bt_conn_get_dst_fake.return_val = &test_addr;
	int (*custom_fakes[])(const struct bt_conn *,
			      struct bt_conn_info *) = { bt_conn_get_info_fake1 };
	SET_CUSTOM_FAKE_SEQ(bt_conn_get_info, custom_fakes, 1);

	sid_ble_conn_init();
	sid_ble_adapter_conn_connected_fake.custom_fake = connection_callback_connected;
	sid_ble_adapter_conn_disconnected_fake.custom_fake = connection_callback_disconnected;

	sid_bt_conn_cb->connected(&test_conn, test_no_error);
	ble_conn_handler_run();
	conn_cb_cnt_expected++;
	zassert_equal(conn_cb_test.num_calls, conn_cb_cnt_expected);

	sid_bt_conn_cb->disconnected(&test_wrong_conn, test_no_error);
	ble_conn_handler_run();
	zassert_equal(conn_cb_test.num_calls, conn_cb_cnt_expected);

	sid_bt_conn_cb->disconnected(&test_conn, test_reason);
	ble_conn_handler_run();
	conn_cb_cnt_expected++;
	zassert_equal(conn_cb_test.num_calls, conn_cb_cnt_expected);
}

ZTEST(sid_ble_connection, test_07_sid_ble_cb_set_before_init)
{
	size_t conn_cb_cnt_expected = 0;
	struct bt_conn test_conn = { .dummy = 0xDC };

	int (*custom_fakes[])(const struct bt_conn *,
			      struct bt_conn_info *) = { bt_conn_get_info_fake1 };
	SET_CUSTOM_FAKE_SEQ(bt_conn_get_info, custom_fakes, 1);

	sid_ble_conn_deinit();
	sid_ble_conn_init();
	sid_ble_adapter_conn_connected_fake.custom_fake = connection_callback_connected;
	sid_ble_adapter_conn_disconnected_fake.custom_fake = connection_callback_disconnected;

	sid_bt_conn_cb->connected(&test_conn, 0);
	ble_conn_handler_run();
	conn_cb_cnt_expected++;
	zassert_equal(conn_cb_test.num_calls, conn_cb_cnt_expected);

	sid_bt_conn_cb->disconnected(&test_conn, 19);
	ble_conn_handler_run();
	conn_cb_cnt_expected++;
	zassert_equal(conn_cb_test.num_calls, conn_cb_cnt_expected);
}

ZTEST(sid_ble_connection, test_08_sid_ble_conn_mtu_callback)
{
	struct bt_conn test_conn = { .dummy = 0xDC };

	sid_ble_conn_init();
	/* ATT reports the initial MTU before the connected callback runs. */
	bt_conn_get_info_fake.custom_fake = bt_conn_get_info_fake1;

	uint16_t tx_mtu = 32, rx_mtu = 44;

	sid_bt_gatt_cb->att_mtu_updated(&test_conn, tx_mtu, rx_mtu);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_mtu_changed_fake.call_count, 1);
	zassert_equal(sid_ble_adapter_mtu_changed_fake.arg0_val, tx_mtu);
}

ZTEST(sid_ble_connection, test_09_sid_ble_conn_mtu_callback_curent_connection)
{
	struct bt_conn curr_conn = { 0 };
	struct bt_conn unknow_conn = { 0 };

	sid_ble_conn_init();
	int (*custom_fakes[])(const struct bt_conn *,
			      struct bt_conn_info *) = { bt_conn_get_info_fake1 };
	SET_CUSTOM_FAKE_SEQ(bt_conn_get_info, custom_fakes, 1);

	sid_bt_conn_cb->connected(&curr_conn, 0);
	ble_conn_handler_run();

	uint16_t tx_mtu = 32, rx_mtu = 44;

	sid_bt_gatt_cb->att_mtu_updated(&curr_conn, tx_mtu, rx_mtu);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_mtu_changed_fake.call_count, 1);
	zassert_equal(sid_ble_adapter_mtu_changed_fake.arg0_val, tx_mtu);

	sid_bt_gatt_cb->att_mtu_updated(&unknow_conn, tx_mtu, rx_mtu);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_mtu_changed_fake.call_count, 1);
}

ZTEST(sid_ble_connection, test_10_sid_ble_conn_disconnect)
{
	struct bt_conn test_conn = { .dummy = 0xDC };
	const bt_addr_le_t test_addr = {
		.type = BT_ADDR_LE_RANDOM,
		.a = { { 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 } },
	};

	sid_ble_conn_deinit();
	sid_ble_conn_init();

	zassert_equal(sid_ble_conn_disconnect(), -ENOENT);

	sid_ble_conn_init();
	bt_conn_get_dst_fake.return_val = &test_addr;
	int (*custom_fakes[])(const struct bt_conn *,
			      struct bt_conn_info *) = { bt_conn_get_info_fake1 };
	SET_CUSTOM_FAKE_SEQ(bt_conn_get_info, custom_fakes, 1);

	sid_bt_conn_cb->connected(&test_conn, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();

	bt_conn_disconnect_fake.return_val = ESUCCESS;
	zassert_equal(sid_ble_conn_disconnect(), ESUCCESS);

	sid_bt_conn_cb->connected(&test_conn, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();
	bt_conn_disconnect_fake.return_val = -ENOTCONN;
	zassert_equal(sid_ble_conn_disconnect(), -ENOTCONN);
}

ZTEST(sid_ble_connection, test_11_sid_ble_disconnect_cb_still_cleans_up_when_conn_param_get_fails)
{
	uint8_t test_reason = BT_HCI_ERR_CONN_TIMEOUT;
	struct bt_conn test_conn = { .dummy = 0xAB };
	const bt_addr_le_t test_addr = {
		.type = BT_ADDR_LE_RANDOM,
		.a = { { 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 } },
	};

	sid_ble_conn_deinit();
	sid_ble_conn_init();
	bt_conn_get_dst_fake.return_val = &test_addr;

	int (*custom_fakes[])(const struct bt_conn *, struct bt_conn_info *) = {
		bt_conn_get_info_fake1,
		bt_conn_get_info_fake1,
		bt_conn_get_info_fake_error,
	};
	SET_CUSTOM_FAKE_SEQ(bt_conn_get_info, custom_fakes, 3);

	sid_bt_conn_cb->connected(&test_conn, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();
	sid_bt_conn_cb->disconnected(&test_conn, test_reason);
	ble_conn_handler_run();

	const sid_ble_conn_data_t *params = sid_ble_conn_data_get();
	zassert_not_null(params);
	zassert_is_null(params->conn);
}

ZTEST(sid_ble_connection, test_12_sid_ble_conn_param_get)
{
	struct bt_le_conn_param param_out = { 0 };

	zassert_equal(sid_ble_conn_param_get(NULL), -EINVAL);

	sid_ble_conn_deinit();
	sid_ble_conn_init();
	zassert_equal(sid_ble_conn_param_get(&param_out), ESUCCESS);

	struct bt_conn test_conn = { .dummy = 0xDC };
	const bt_addr_le_t test_addr = {
		.type = BT_ADDR_LE_RANDOM,
		.a = { { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06 } },
	};
	bt_conn_get_dst_fake.return_val = &test_addr;
	int (*custom_fakes[])(const struct bt_conn *,
			      struct bt_conn_info *) = { bt_conn_get_info_fake_param_get };
	SET_CUSTOM_FAKE_SEQ(bt_conn_get_info, custom_fakes, 1);

	sid_bt_conn_cb->connected(&test_conn, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();

	memset(&param_out, 0, sizeof(param_out));
	zassert_equal(sid_ble_conn_param_get(&param_out), ESUCCESS);
	zassert_equal(param_out.interval_min, param_get_fake_interval);
	zassert_equal(param_out.interval_max, param_get_fake_interval);
	zassert_equal(param_out.latency, param_get_fake_latency);
	zassert_equal(param_out.timeout, param_get_fake_timeout);
}

ZTEST(sid_ble_connection, test_13_sid_ble_conn_param_update)
{
	const struct bt_le_conn_param param_in = {
		.interval_min = 18,
		.interval_max = 24,
		.latency = 0,
		.timeout = 500,
	};

	zassert_equal(sid_ble_conn_param_update(NULL), -EINVAL);

	sid_ble_conn_deinit();
	sid_ble_conn_init();
	zassert_equal(sid_ble_conn_param_update(&param_in), ESUCCESS);

	struct bt_conn test_conn = { .dummy = 0xDD };
	const bt_addr_le_t test_addr = {
		.type = BT_ADDR_LE_RANDOM,
		.a = { { 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f } },
	};
	bt_conn_get_dst_fake.return_val = &test_addr;
	int (*custom_fakes[])(const struct bt_conn *,
			      struct bt_conn_info *) = { bt_conn_get_info_fake1 };
	SET_CUSTOM_FAKE_SEQ(bt_conn_get_info, custom_fakes, 1);

	sid_bt_conn_cb->connected(&test_conn, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();

	bt_conn_le_param_update_fake.call_count = 0;
	bt_conn_le_param_update_fake.return_val = ESUCCESS;
	zassert_equal(sid_ble_conn_param_update(&param_in), ESUCCESS);
	zassert_equal(bt_conn_le_param_update_fake.call_count, 1);
	zassert_equal(bt_conn_le_param_update_fake.arg0_val, &test_conn);
	zassert_equal(bt_conn_le_param_update_fake.arg1_val->interval_min, param_in.interval_min);
	zassert_equal(bt_conn_le_param_update_fake.arg1_val->interval_max, param_in.interval_max);

	bt_conn_le_param_update_fake.return_val = -EINVAL;
	zassert_equal(sid_ble_conn_param_update(&param_in), ESUCCESS);
	zassert_equal(bt_conn_le_param_update_fake.call_count, 2);
}

ZTEST(sid_ble_connection, test_14_sid_ble_conn_mtu_foreign_conn_ignored)
{
	struct bt_conn app_conn = { .dummy = 0xEE };

	multi_conn_fakes_install();
	foreign_conn = &app_conn;

	sid_bt_gatt_cb->att_mtu_updated(&app_conn, 247, 247);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_mtu_changed_fake.call_count, 0,
		      "MTU of a non-Sidewalk connection reported to Sidewalk");
}

ZTEST(sid_ble_connection, test_15_sid_ble_conn_second_conn_first_disconnects_first)
{
	multi_conn_fakes_install();

	sid_bt_conn_cb->connected(&conn_a, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();
	sid_bt_conn_cb->connected(&conn_b, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_conn_connected_fake.call_count, 1,
		      "Sidewalk notified about a second concurrent connection");
	zassert_equal(sid_ble_conn_data_get()->conn, &conn_a, "Active connection overwritten");
	zassert_mem_equal(sid_ble_conn_data_get()->addr, addr_a.a.val, BT_ADDR_SIZE);

	sid_bt_conn_cb->disconnected(&conn_a, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_conn_disconnected_fake.call_count, 1,
		      "Disconnect of the active connection not reported");
	zassert_mem_equal(sid_ble_adapter_conn_disconnected_fake.arg0_val, addr_a.a.val,
			  BT_ADDR_SIZE);
	zassert_is_null(sid_ble_conn_data_get()->conn);

	sid_bt_conn_cb->disconnected(&conn_b, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_conn_disconnected_fake.call_count, 1,
		      "Disconnect of the second connection reported to Sidewalk");
	zassert_equal(ref_balance, 0, "bt_conn reference leaked");
}

ZTEST(sid_ble_connection, test_16_sid_ble_conn_second_conn_disconnects_first)
{
	multi_conn_fakes_install();

	sid_bt_conn_cb->connected(&conn_a, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();
	sid_bt_conn_cb->connected(&conn_b, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();

	sid_bt_conn_cb->disconnected(&conn_b, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_conn_disconnected_fake.call_count, 0,
		      "Sidewalk told disconnected while the first connection is still up");
	zassert_equal(sid_ble_conn_data_get()->conn, &conn_a, "Active connection lost");

	sid_bt_conn_cb->disconnected(&conn_a, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_conn_disconnected_fake.call_count, 1,
		      "Disconnect of the active connection not reported");
	zassert_is_null(sid_ble_conn_data_get()->conn);
	zassert_equal(ref_balance, 0, "bt_conn reference leaked");
}

ZTEST(sid_ble_connection, test_17_sid_ble_conn_disconnect_event_after_deinit)
{
	multi_conn_fakes_install();

	sid_bt_conn_cb->connected(&conn_a, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();
	sid_ble_conn_deinit();
	sid_bt_conn_cb->disconnected(&conn_a, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	ble_conn_handler_run();

	zassert_equal(sid_ble_adapter_conn_disconnected_fake.call_count, 0,
		      "Sidewalk callback called after deinit");
	zassert_equal(ref_balance, 0, "bt_conn reference leaked");
}

ZTEST(sid_ble_connection, test_18_sid_ble_conn_reinit_without_disconnect_event)
{
	multi_conn_fakes_install();

	sid_bt_conn_cb->connected(&conn_a, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();
	sid_ble_conn_deinit();
	sid_ble_conn_init();

	zassert_is_null(sid_ble_conn_data_get()->conn, "Stale connection survived deinit");
	zassert_equal(ref_balance, 0, "bt_conn reference leaked");
}

ZTEST(sid_ble_connection, test_19_sid_ble_conn_connect_event_after_deinit)
{
	multi_conn_fakes_install();

	sid_ble_conn_deinit();
	sid_bt_conn_cb->connected(&conn_a, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_conn_connected_fake.call_count, 0,
		      "Sidewalk callback called after deinit");

	sid_ble_conn_init();
	zassert_is_null(sid_ble_conn_data_get()->conn, "Connection taken after deinit");
	zassert_equal(ref_balance, 0, "bt_conn reference leaked");
}

ZTEST(sid_ble_connection, test_20_sid_ble_conn_connect_event_queued_before_reinit)
{
	multi_conn_fakes_install();

	sid_bt_conn_cb->connected(&conn_a, BT_HCI_ERR_SUCCESS);
	sid_ble_conn_deinit();
	sid_ble_conn_init();
	ble_conn_handler_run();

	zassert_equal(sid_ble_adapter_conn_connected_fake.call_count, 0,
		      "Connect event from before re-init reached Sidewalk");
	zassert_is_null(sid_ble_conn_data_get()->conn, "Stale connection took the slot");
	zassert_equal(ref_balance, 0, "bt_conn reference leaked");
}

ZTEST(sid_ble_connection, test_21_sid_ble_conn_blocking_calls_deferred)
{
	multi_conn_fakes_install();

	sid_bt_conn_cb->connected(&conn_a, BT_HCI_ERR_SUCCESS);
	zassert_equal(bt_conn_le_param_update_fake.call_count, 0,
		      "Parameter update sent from the host callback");
	zassert_equal(sid_ble_adapter_conn_connected_fake.call_count, 0,
		      "Sidewalk called from the host callback");
	ble_conn_handler_run();
	zassert_equal(bt_conn_le_param_update_fake.call_count, 1);
	zassert_equal(sid_ble_adapter_conn_connected_fake.call_count, 1);

	sid_bt_conn_cb->connected(&conn_b, BT_HCI_ERR_SUCCESS);
	zassert_equal(bt_conn_disconnect_fake.call_count, 0,
		      "Second connection refused from the host callback");
	ble_conn_handler_run();
	zassert_equal(bt_conn_disconnect_fake.call_count, 1);
	zassert_equal(bt_conn_disconnect_fake.arg0_val, &conn_b);
	zassert_equal(bt_conn_disconnect_fake.arg1_val, BT_HCI_ERR_CONN_LIMIT_EXCEEDED);

	sid_bt_conn_cb->disconnected(&conn_b, BT_HCI_ERR_CONN_LIMIT_EXCEEDED);
	sid_bt_conn_cb->disconnected(&conn_a, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	ble_conn_handler_run();
	zassert_equal(ref_balance, 0, "bt_conn reference leaked");
}

ZTEST(sid_ble_connection, test_22_sid_ble_conn_events_handled_in_order)
{
	multi_conn_fakes_install();

	/* Initial MTU before connected, then disconnect of A before connect of B. */
	sid_bt_gatt_cb->att_mtu_updated(&conn_a, 23, 23);
	sid_bt_conn_cb->connected(&conn_a, BT_HCI_ERR_SUCCESS);
	sid_bt_conn_cb->disconnected(&conn_a, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	sid_bt_conn_cb->connected(&conn_b, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();

	zassert_equal(sid_ble_adapter_mtu_changed_fake.call_count, 1);
	zassert_equal(sid_ble_adapter_conn_connected_fake.call_count, 2);
	zassert_equal(sid_ble_adapter_conn_disconnected_fake.call_count, 1);
	zassert_equal(sid_ble_conn_data_get()->conn, &conn_b, "Events handled out of order");
	zassert_mem_equal(sid_ble_conn_data_get()->addr, addr_b.a.val, BT_ADDR_SIZE);

	sid_bt_conn_cb->disconnected(&conn_b, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	ble_conn_handler_run();
	zassert_equal(ref_balance, 0, "bt_conn reference leaked");
}

ZTEST(sid_ble_connection, test_23_sid_ble_conn_mtu_from_link_before_reinit_ignored)
{
	multi_conn_fakes_install();

	sid_bt_conn_cb->connected(&conn_a, BT_HCI_ERR_SUCCESS);
	ble_conn_handler_run();
	/* A outlives deinit, and init creates the Sidewalk identity again. */
	sid_ble_conn_deinit();
	sid_ble_conn_init();

	sid_bt_gatt_cb->att_mtu_updated(&conn_a, 247, 247);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_mtu_changed_fake.call_count, 0,
		      "MTU from a link of the previous init reached Sidewalk");

	sid_bt_conn_cb->disconnected(&conn_a, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	ble_conn_handler_run();
	zassert_equal(sid_ble_adapter_conn_disconnected_fake.call_count, 0);
	zassert_equal(ref_balance, 0, "bt_conn reference leaked");
}
