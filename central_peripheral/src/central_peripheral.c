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
#include <zephyr/fs/fs.h>

#include "link_control.h"
#include "link_control_service.h"

LOG_MODULE_REGISTER(link_control_central);

static void start_scan(void);
static struct bt_uuid_128 discover_uuid = BT_UUID_INIT_128(0);
static struct bt_conn *central_conn;
struct bt_conn *peripheral_conn;
static uint16_t tx_power_handle;
static uint16_t rssi_handle;
static struct bt_gatt_subscribe_params subscribe_params;

#define NAME_LEN 256
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LCS_VAL),
};

static void start_advertising(void) {
    int err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }
    set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, current_tx_power);
}

struct link_control_handles {
    uint16_t tx_power_handle;
};

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

    if (peripheral_conn) {
        return;
    }

	/*
    if (type != BT_GAP_ADV_TYPE_ADV_IND &&
        type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND &&
        type != BT_GAP_ADV_TYPE_SCAN_RSP &&
		type != BT_GAP_ADV_TYPE_EXT_ADV) {
        return;
    }
	*/

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

		struct bt_conn_le_create_param *conn_params = BT_CONN_LE_CREATE_PARAM(
			BT_CONN_LE_OPT_CODED | BT_CONN_LE_OPT_NO_1M,
			BT_GAP_SCAN_FAST_INTERVAL,
			BT_GAP_SCAN_FAST_INTERVAL);


        err = bt_conn_le_create(addr, conn_params,
                    BT_LE_CONN_PARAM_DEFAULT, &peripheral_conn);
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

    err = bt_le_scan_start(BT_LE_SCAN_CODED_ACTIVE, device_found);
    if (err < 0) {
        LOG_ERR("Scanning failed to start (err %d)", err);
        return;
    }
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

int write_tx_power_peripheral(int8_t tx_power_value)
{
    int err;
	
	if (peripheral_conn != NULL) {
		write_params.data = &tx_power_value;
		write_params.length = sizeof(tx_power_value);
		write_params.handle = tx_power_handle;
		write_params.offset = 0;
		write_params.func = write_func;

		err = bt_gatt_write(peripheral_conn, &write_params);
		if (err) {
			LOG_ERR("Write failed (err %d)", err);
			return err;
		}
	} else {
		return -EINVAL;
	}
	return 0;
}

static uint8_t rssi_notify_cb(struct bt_conn *conn,
                              struct bt_gatt_subscribe_params *params,
                              const void *data, uint16_t length)
{
    if (!data) {
		#if 0
        LOG_INF("Unsubscribed from RSSI notifications");
        params->value_handle = 0;
		#endif
        return BT_GATT_ITER_STOP;
    }

	int8_t rssi = *(int8_t *)data;

    LOG_INF("Received RSSI notification: %d", rssi);
	update_peripheral_rssi(central_conn, rssi);

    return BT_GATT_ITER_CONTINUE;
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
		memcpy(&discover_uuid, BT_UUID_LCS_TX_PWR_PERIPHERAL, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            LOG_ERR("Discover failed (err %d)", err);
        }
    } else if (bt_uuid_cmp(discover_params.uuid, BT_UUID_LCS_TX_PWR_PERIPHERAL) == 0) {
        // TX Power characteristic found
        tx_power_handle = bt_gatt_attr_value_handle(attr);
        LOG_INF("TX Power characteristic handle: %u", tx_power_handle);

		memcpy(&discover_uuid, BT_UUID_LCS_RSSI_PERIPHERAL, sizeof(discover_uuid));
		discover_params.uuid = &discover_uuid.uuid;
        discover_params.start_handle = attr->handle + 1;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            LOG_ERR("Discover failed (err %d)", err);
        }
    } else if (bt_uuid_cmp(discover_params.uuid, BT_UUID_LCS_RSSI_PERIPHERAL) == 0) {
		rssi_handle = bt_gatt_attr_value_handle(attr);
		LOG_INF("RSSI characteristic handle: %u", rssi_handle);

		struct bt_uuid_16 ccc_uuid = BT_UUID_INIT_16(0);
		memcpy(&ccc_uuid, BT_UUID_GATT_CCC, sizeof(ccc_uuid));

		discover_params.uuid = &ccc_uuid.uuid;
		discover_params.start_handle = attr->handle + 2;
		discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
		subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);

		err = bt_gatt_discover(conn, &discover_params);
		if (err) {
			LOG_ERR("Discover failed (err %d)", err);
		}
	} else {
		subscribe_params.notify = rssi_notify_cb;
		subscribe_params.value = BT_GATT_CCC_NOTIFY;
		subscribe_params.ccc_handle = attr->handle;

		err = bt_gatt_subscribe(conn, &subscribe_params);
		if (err && err != -EALREADY) {
			LOG_ERR("Subscribe failed (err %d)", err);
		} else {
			LOG_INF("[SUBSCRIBED]");
		}

		return BT_GATT_ITER_STOP;
	}

    return BT_GATT_ITER_STOP;
}

void get_central_rssi_work_handler(struct k_work *item) {
	int err;
	int8_t rssi;
	uint16_t conn_handle;

	bt_hci_get_conn_handle(central_conn, &conn_handle);

	err = read_conn_rssi(conn_handle, &rssi);
	LOG_INF("Central RSSI: %d", rssi);

	//update_central_rssi(central_conn, rssi);
}

K_WORK_DEFINE(central_rssi_work, get_central_rssi_work_handler);

void get_rssi(struct k_timer *timer_id) {
	if (central_conn != NULL) {
		k_work_submit(&central_rssi_work);
	}
}

K_TIMER_DEFINE(central_rssi_timer, get_rssi, NULL);

static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err < 0) {
        LOG_ERR("Failed to connect to %s (%u)", addr, err);
        bt_conn_unref(peripheral_conn);
        peripheral_conn = NULL;
        start_scan();
        return;
    }

    LOG_INF("Connected: %s", addr);
    if (conn == peripheral_conn) {
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
    } else {
		if (central_conn == NULL) {
			LOG_INF("Connected to central");
			central_conn = conn;
		}
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];


    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

	if (conn == peripheral_conn) {
		bt_conn_unref(peripheral_conn);
		peripheral_conn = NULL;

		start_scan();
	} else if (conn == central_conn) {
		k_timer_stop(&central_rssi_timer);
	}
}

#if IS_ENABLED(CONFIG_BT_USER_PHY_UPDATE)
static void le_phy_updated(struct bt_conn *conn,
               struct bt_conn_le_phy_info *param)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("LE PHY Updated: %s Tx 0x%x, Rx 0x%x", addr, param->tx_phy,
           param->rx_phy);
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
#if IS_ENABLED(CONFIG_BT_USER_PHY_UPDATE)
    .le_phy_updated = le_phy_updated,
#endif
};

static int cmd_set_peripheral_tx(const struct shell *shell, size_t argc, char **argv)
{
    int8_t tx_power;
    if (argc != 2) {
        shell_error(shell, "Usage: set_peripheral_tx <power_level>");
        return -EINVAL;
    }
    tx_power = (int8_t)atoi(argv[1]);
    if (!peripheral_conn) {
        shell_error(shell, "No active connection");
        return -ENOEXEC;
    }
    write_tx_power_peripheral(tx_power);
    return 0;
}

static int cmd_set_central_tx(const struct shell *shell, size_t argc, char **argv)
{
    int err;
    uint16_t conn_handle;
    if (argc != 2) {
        shell_error(shell, "Usage: set_central_tx <power_level>");
        return -EINVAL;
    }
    current_tx_power = (int8_t)atoi(argv[1]);
    if (!peripheral_conn) {
        shell_error(shell, "No active connection");
        return -ENOEXEC;
    }
    err = bt_hci_get_conn_handle(peripheral_conn, &conn_handle);
    if (err) {
        shell_error(shell, "Failed to get connection handle (err %d)", err);
        return err;
    }
    err = set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, current_tx_power);
    if (err) {
        shell_error(shell, "Failed to set central TX power (err %d)", err);
        return err;
    }
    shell_print(shell, "Central TX power set to %d", current_tx_power);
    return 0;
}

#if IS_ENABLED(CONFIG_BT_USER_PHY_UPDATE)
static int cmd_set_phy(const struct shell *shell, size_t argc, char **argv)
{
    int err;
    uint8_t phy;
    if (argc != 2) {
        shell_error(shell, "Usage: set_phy <1m|2m|coded>");
        return -EINVAL;
    }
    if (!peripheral_conn) {
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
    err = update_phy(peripheral_conn, phy);
    if (err) {
        shell_error(shell, "Failed to update PHY (err %d)", err);
        return err;
    }
    shell_print(shell, "PHY update initiated to %s", argv[1]);
    return 0;
}
#endif

#if 0
static int cmd_remove_logs(const struct shell *shell, size_t argc, char **argv) {
    int res;
    struct fs_dir_t dirp;
    struct fs_dirent entry;

	char dir_path[64] = "/lfs1";

    fs_dir_t_init(&dirp);

    res = fs_opendir(&dirp, dir_path);
    if (res) {
        shell_error(shell, "Error opening directory %s [%d]\n", dir_path, res);
        return res;
    }

    shell_print(shell, "Clearing directory %s\n", dir_path);
    for (;;) {
        char file_path[PATH_MAX] = { 0 };

        res = fs_readdir(&dirp, &entry);

        /* entry.name[0] == 0 means end-of-dir */
        if (res || entry.name[0] == 0) {
            break;
        }

        /* Delete file or sub directory */
        snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry.name);
        res = fs_unlink(file_path);
        if (res) {
            shell_error(shell, "Error deleting file/dir [%d]\n", res);
            fs_closedir(&dirp);
            return res;
        }
    }

    fs_closedir(&dirp);
    shell_print(shell, "Directory %s cleared successfully\n", dir_path);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(link_control_cmds,
    SHELL_CMD(set_peripheral_tx, NULL, "Set peripheral TX power", cmd_set_peripheral_tx),
    SHELL_CMD(set_central_tx, NULL, "Set central TX power", cmd_set_central_tx),
#if IS_ENABLED(CONFIG_BT_USER_PHY_UPDATE)
    SHELL_CMD(set_phy, NULL, "Set PHY (1m, 2m, or coded)", cmd_set_phy),
#endif
	SHELL_CMD(remove_logs, NULL, "Removes all logs", cmd_remove_logs),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(link_control, &link_control_cmds, "Link Control commands", NULL);
#endif

int main(void)
{
    int err;

    err = bt_enable(NULL);
    if (err < 0) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }
    LOG_INF("Bluetooth initialized");

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

	start_advertising();
	LOG_INF("Advertising started");

    start_scan();
	LOG_INF("Scanning started");

    return 0;
}
