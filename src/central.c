#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(link_control_central);
#include "link_control_service.h"

static void start_scan(void);
static struct bt_conn *default_conn;
#define NAME_LEN 256

static bool data_cb(struct bt_data *data, void *user_data) {
    int err;
    struct bt_uuid *uuid = user_data;
    switch (data->type) {
    case BT_DATA_UUID128_SOME:
    case BT_DATA_UUID128_ALL:
        err = bt_uuid_create(uuid, data->data, data->data_len);
        if (err < 0) {
            uuid = NULL;
            LOG_ERR("Unable to parse UUID from advertisement data");
            return false;
        }
        return true;
    default:
        return true;
    }
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
             struct net_buf_simple *ad)
{
    char addr_str[BT_ADDR_LE_STR_LEN];
    struct bt_uuid service_uuid;
    int err;

    if (default_conn) {
        return;
    }

    if (type != BT_GAP_ADV_TYPE_ADV_IND &&
        type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND &&
        type != BT_GAP_ADV_TYPE_SCAN_RSP) {
        return;
    }

    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    bt_data_parse(ad, data_cb, &service_uuid);

    if (!bt_uuid_cmp(&service_uuid, BT_UUID_LCS)) {
        char uuid_str[NAME_LEN];
        bt_uuid_to_str(&service_uuid, uuid_str, sizeof(uuid_str));
        LOG_INF("Found service: %s", uuid_str);

        err = bt_le_scan_stop();
        if (err) {
            LOG_ERR("Stop LE scan failed (err %d)", err);
            return;
        }

        err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
                    BT_LE_CONN_PARAM_DEFAULT, &default_conn);
        if (err < 0) {
            LOG_ERR("Create conn to %s failed (%d)", addr_str, err);
            start_scan();
        } else {
            LOG_INF("Connection initiated to %s", addr_str);
        }
    }
}

static void start_scan(void)
{
    int err;
    err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
    if (err < 0) {
        LOG_ERR("Scanning failed to start (err %d)", err);
        return;
    }
    LOG_INF("Scanning successfully started");
}

static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_write_params write_params;
static uint16_t tx_power_handle;

static void write_func(struct bt_conn *conn, uint8_t err,
                       struct bt_gatt_write_params *params)
{
    if (err) {
        LOG_ERR("Write failed (err %u)", err);
    } else {
        LOG_INF("Write successful");
    }
}

static void write_tx_power(struct bt_conn *conn)
{
    int err;
    uint8_t tx_power_value = 4; // Example value, adjust as needed

    write_params.data = &tx_power_value;
    write_params.length = sizeof(tx_power_value);
    write_params.handle = tx_power_handle;
    write_params.offset = 0;
    write_params.func = write_func;

    err = bt_gatt_write(conn, &write_params);
    if (err) {
        LOG_ERR("Write failed (err %d)", err);
    }
}

static uint8_t discover_func(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr,
                             struct bt_gatt_discover_params *params)
{
    int err;

    if (!attr) {
        LOG_INF("Discover complete");
        (void)memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    LOG_INF("[ATTRIBUTE] handle %u", attr->handle);

    if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_LCS)) {
        // Service discovered, now discover characteristics
        discover_params.uuid = BT_UUID_LCS_TX_PWR;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            LOG_ERR("Discover failed (err %d)", err);
        }
    } else if (!bt_uuid_cmp(discover_params.uuid, BT_UUID_LCS_TX_PWR)) {
        // TX Power characteristic found
        tx_power_handle = bt_gatt_attr_value_handle(attr);
        LOG_INF("TX Power characteristic handle: %u", tx_power_handle);
    }

    return BT_GATT_ITER_STOP;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err < 0) {
        LOG_ERR("Failed to connect to %s (%u)", addr, err);
        bt_conn_unref(default_conn);
        default_conn = NULL;
        start_scan();
        return;
    }

    if (conn != default_conn) {
        return;
    }

    LOG_INF("Connected: %s", addr);
	discover_params.uuid = BT_UUID_LCS;
	discover_params.func = discover_func;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type = BT_GATT_DISCOVER_PRIMARY;

	err = bt_gatt_discover(conn, &discover_params);
	if (err) {
		LOG_ERR("Discover failed(err %d)", err);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    if (conn != default_conn) {
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

    bt_conn_unref(default_conn);
    default_conn = NULL;

    start_scan();
}

static void le_phy_updated(struct bt_conn *conn,
			   struct bt_conn_le_phy_info *param)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("LE PHY Updated: %s Tx 0x%x, Rx 0x%x", addr, param->tx_phy,
	       param->rx_phy);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
	.le_phy_updated = le_phy_updated,
};

int main(void)
{
    int err;

    err = bt_enable(NULL);
    if (err < 0) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    LOG_INF("Bluetooth initialized");
    start_scan();

    return 0;
}
