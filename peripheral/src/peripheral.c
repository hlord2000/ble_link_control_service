#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/settings/settings.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#if defined(CONFIG_FILE_SYSTEM)
#include <zephyr/fs/fs.h>
#endif

#include "link_control.h"
#include "link_control_service.h"

LOG_MODULE_REGISTER(link_control_peripheral);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define STACKSIZE 1024 
#define PRIORITY 7

int8_t current_tx_power;
static K_SEM_DEFINE(ble_init_ok, 0, 1);
static K_SEM_DEFINE(ble_connected, 0, 1);
struct bt_conn *current_conn;

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LCS_VAL),
};

static struct bt_le_ext_adv *adv;

static void start_advertising(void) {
	int err;
	struct bt_le_adv_param adv_param = {
		.id = BT_ID_DEFAULT,
		.sid = 0U,
		.secondary_max_skip = 0U,
		.options = (BT_LE_ADV_OPT_EXT_ADV |
			    BT_LE_ADV_OPT_CONNECTABLE |
			    BT_LE_ADV_OPT_CODED),
		.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
		.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
		.peer = NULL,
	};

	LOG_INF("Creating a Coded PHY connectable non-scannable advertising set");
	err = bt_le_ext_adv_create(&adv_param, NULL, &adv);
	if (err) {
		LOG_ERR("Failed to create Coded PHY extended advertising set (err %d)", err);
		return;
	}

	LOG_INF("Setting extended advertising data");
	err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("Failed to set extended advertising data (err %d)", err);
		return;
	}

	LOG_INF("Starting Extended Advertising (connectable non-scannable)");
	err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		LOG_ERR("Failed to start extended advertising set (err %d)", err);
		return;
	}

    set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, 0, current_tx_power);
}

static struct bt_gatt_exchange_params exchange_params;

static void exchange_func(struct bt_conn *conn, uint8_t err,
    struct bt_gatt_exchange_params *params)
{
    if (!err) {
        uint32_t bt_max_send_len = bt_gatt_get_mtu(conn) - 3;
        LOG_INF("max send len is %d", bt_max_send_len);
    }
}

static void connected(struct bt_conn *conn, uint8_t err) {
    char addr[BT_ADDR_LE_STR_LEN];

    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Connected %s", addr);

    err = bt_le_ext_adv_stop(adv);
    if (err) {
        LOG_ERR("Failed to stop advertising, err: %d", err);
    }

    current_conn = bt_conn_ref(conn);
    uint16_t conn_handle;
    bt_hci_get_conn_handle(current_conn, &conn_handle);
    set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, current_tx_power);

	exchange_params.func = exchange_func;
	err = bt_gatt_exchange_mtu(current_conn, &exchange_params);
	if (err) {
		LOG_ERR("MTU exchange failed (err %d)", err);
	}
    
    k_sem_give(&ble_connected);
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected: %s (reason %u)", addr, reason);

    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    start_advertising();
    k_sem_take(&ble_connected, K_MSEC(50));
}

#if IS_ENABLED(CONFIG_BT_USER_PHY_UPDATE)
static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param) {
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("LE PHY Updated: %s Tx 0x%x, Rx 0x%x", addr, param->tx_phy, param->rx_phy);
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
#if IS_ENABLED(CONFIG_BT_USER_PHY_UPDATE)
    .le_phy_updated = le_phy_updated,
#endif
};

int main(void) {
    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return 0;
    }

    LOG_INF("Bluetooth initialized");
    k_sem_give(&ble_init_ok);

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    start_advertising();
    return 0;
}

void ble_write_thread(void) {
    k_sem_take(&ble_init_ok, K_FOREVER);

    while (true) {
        k_sem_take(&ble_connected, K_FOREVER);
        
        uint16_t conn_handle;
        bt_hci_get_conn_handle(current_conn, &conn_handle);
        
        int8_t rssi;
        int err = read_conn_rssi(conn_handle, &rssi);
        if (err == 0) {
            update_rssi(current_conn, rssi);
			LOG_INF("RSSI: %i", rssi);
        }

        k_sem_give(&ble_connected);
        k_msleep(CONFIG_LCS_RSSI_INTERVAL_MS);
    }
}

K_THREAD_DEFINE(ble_write_thread_id, STACKSIZE, ble_write_thread, NULL, NULL, NULL, PRIORITY, 0, 0);

#if defined(CONFIG_FILE_SYSTEM)
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
	SHELL_CMD(remove_logs, NULL, "Removes all logs", cmd_remove_logs),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(link_control, &link_control_cmds, "Link Control commands", NULL);
#endif
