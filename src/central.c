#include <zephyr/types.h>
#include <stdlib.h>
#include <stddef.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include "link_control.h"
#include "link_control_service.h"

LOG_MODULE_REGISTER(link_control_central);

#define RSSI_LOG_RATE_MS 500

struct rssi_reading {
	int8_t rssi;
	uint32_t timestamp;
};

struct rssi_reading rssi_buf[1024];

K_MUTEX_DEFINE(lcs_conn_mutex);

static void start_scan(void);
static struct bt_uuid_128 discover_uuid = BT_UUID_INIT_128(0);
static struct bt_conn *default_conn;
static uint16_t tx_power_handle;
#define NAME_LEN 256

struct link_control_handles {
    uint16_t tx_power_handle;
    // Add other characteristic handles as needed
};

static struct link_control_handles lc_handles;

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
                    BT_LE_CONN_PARAM(400, 600, 0, 1500), 
					&default_conn);
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

static void write_func(struct bt_conn *conn, uint8_t err,
                       struct bt_gatt_write_params *params)
{
    if (err) {
        LOG_ERR("Write failed (err %u)", err);
    } else {
        LOG_INF("Write successful");
    }
}

static void write_tx_power(struct bt_conn *conn, int8_t tx_power_value)
{
    int err;

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

	char str[BT_UUID_STR_LEN];

	bt_uuid_to_str(attr->uuid, str, sizeof(str));
	LOG_INF("UUID: %s", str);

    if (bt_uuid_cmp(discover_params.uuid, BT_UUID_LCS) == 0) {
        // Service discovered, now discover characteristics
		memcpy(&discover_uuid, BT_UUID_LCS_TX_PWR, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            LOG_ERR("Discover failed (err %d)", err);
        }
    } else if (bt_uuid_cmp(discover_params.uuid, BT_UUID_LCS_TX_PWR) == 0) {
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

	uint16_t conn_handle;

    err = bt_hci_get_conn_handle(default_conn, &conn_handle);
    if (err) {
		LOG_ERR("Failed to get connection handle (err %d)", err);
    }

    err = set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, 20);
    if (err) {
		LOG_ERR("Failed to set central connection TX power (err %d)", err);
    }

    LOG_INF("Connected: %s", addr);
	memcpy(&discover_uuid, BT_UUID_LCS, sizeof(discover_uuid));
	discover_params.uuid = &discover_uuid.uuid;
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

static int cmd_set_peripheral_tx(const struct shell *shell, size_t argc, char **argv)
{
    int8_t tx_power;
    if (argc != 2) {
        shell_error(shell, "Usage: set_peripheral_tx <power_level>");
        return -EINVAL;
    }
    tx_power = (int8_t)atoi(argv[1]);
    if (!default_conn) {
        shell_error(shell, "No active connection");
    }
    write_tx_power(default_conn, tx_power);
    return 0;
}

static int cmd_set_central_tx(const struct shell *shell, size_t argc, char **argv)
{
    int8_t tx_power;
    int err;
    uint16_t conn_handle;
    if (argc != 2) {
        shell_error(shell, "Usage: set_central_tx <power_level>");
        return -EINVAL;
    }
    tx_power = (int8_t)atoi(argv[1]);
    if (!default_conn) {
        shell_error(shell, "No active connection");
    }
    err = bt_hci_get_conn_handle(default_conn, &conn_handle);
    if (err) {
        shell_error(shell, "Failed to get connection handle (err %d)", err);
    }

    err = set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, tx_power);
    if (err) {
        shell_error(shell, "Failed to set central connection TX power (err %d)", err);
    }

    err = set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_SCAN, 0, tx_power);
    if (err) {
        shell_error(shell, "Failed to set central scan TX power (err %d)", err);
    }
    shell_print(shell, "Central TX power set to %d", tx_power);
    return 0;
}

static int cmd_set_phy(const struct shell *shell, size_t argc, char **argv)
{
    int err;
    uint8_t phy;
    if (argc != 2) {
        shell_error(shell, "Usage: set_phy <1m|2m|coded>");
        return -EINVAL;
    }
    if (!default_conn) {
        shell_error(shell, "No active connection");
        return -ENOEXEC;
    }
    if (strcmp(argv[1], "1m") == 0) {
        phy = BT_GAP_LE_PHY_1M;
    } else if (strcmp(argv[1], "2m") == 0) {
        phy = BT_GAP_LE_PHY_2M;
    } else if (strcmp(argv[1], "coded") == 0) {
        phy = BT_GAP_LE_PHY_CODED;
    } else {
        shell_error(shell, "Invalid PHY option. Use 1m, 2m, or coded.");
        return -EINVAL;
    }
    err = update_phy(default_conn, phy);
    if (err) {
        shell_error(shell, "Failed to update PHY (err %d)", err);
        return err;
    }
    shell_print(shell, "PHY update initiated to %s", argv[1]);
    return 0;
}

static int cmd_print_rssi_buf(const struct shell *shell, size_t argc, char **argv)
{
	for (int i = 0; i < 1024; i++) {
		if (rssi_buf[i].timestamp == 0) {
			break;
		}
		shell_print(shell, "RSSI: %d, Timestamp: %u", rssi_buf[i].rssi, rssi_buf[i].timestamp);
	}
	return 0;
}

uint32_t delay_ms;
static int cmd_capture_rssi(const struct shell *shell, size_t argc, char **argv) {

	if (argc != 2) {
		shell_error(shell, "Usage: capture_rssi <delay_ms>");
		return -EINVAL;
	}

	delay_ms = atoi(argv[1]);
	if (delay_ms < 0) {
		shell_error(shell, "Invalid delay value");
		return -EINVAL;
	}

	shell_print(shell, "Capturing RSSI in %d ms, please wait...", delay_ms);

	k_mutex_unlock(&lcs_conn_mutex);
	return 0;
}


SHELL_STATIC_SUBCMD_SET_CREATE(link_control_cmds,
    SHELL_CMD(set_peripheral_tx, NULL, "Set peripheral TX power", cmd_set_peripheral_tx),
    SHELL_CMD(set_central_tx, NULL, "Set central TX power", cmd_set_central_tx),
    SHELL_CMD(set_phy, NULL, "Set PHY (1m, 2m, or coded)", cmd_set_phy),
	SHELL_CMD(print_rssi_buf, NULL, "Print RSSI buffer", cmd_print_rssi_buf),
	SHELL_CMD(capture_rssi_delay, NULL, "Capture RSSI", cmd_capture_rssi),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(link_control, &link_control_cmds, "Link Control commands", NULL);

int main(void)
{
    int err;
	k_mutex_init(&lcs_conn_mutex);
	k_mutex_unlock(&lcs_conn_mutex);

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

    err = set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_SCAN, 0, 20);
    if (err) {
        LOG_ERR("Failed to set central scan TX power (err %d)", err);
    }

	LOG_INF("Waiting for connection");
	k_msleep(30000);

	uint16_t handle;
	bt_hci_get_conn_handle(default_conn, &handle);

	LOG_INF("Connection handle: %u", handle);
	for (int i = 0; i < 1024; i++) {
		read_conn_rssi(handle, &rssi_buf[i].rssi);
		rssi_buf[i].timestamp = k_uptime_get_32();
		LOG_INF("RSSI: %d, Timestamp: %u", rssi_buf[i].rssi, rssi_buf[i].timestamp);
		k_msleep(RSSI_LOG_RATE_MS);
	}

    return 0;
}
