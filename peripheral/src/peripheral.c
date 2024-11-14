#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <zephyr/settings/settings.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/fs/fs.h>

#include "link_control/link_control.h"
#include "link_control/link_control_service.h"
#include "conn_adv_pair/conn_adv_pair.h"

LOG_MODULE_REGISTER(link_control_peripheral);

/* Define maximum supported AD data length */
#if defined(CONFIG_BT_CTLR_ADV_DATA_LEN_MAX)
#define BT_AD_DATA_LEN_MAX CONFIG_BT_CTLR_ADV_DATA_LEN_MAX
#else
#define BT_AD_DATA_LEN_MAX 31U
#endif

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

const static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LCS_VAL),
};

static K_SEM_DEFINE(ble_init_ok, 0, 1);

void adv_connected_cb(struct bt_le_ext_adv *adv, struct bt_le_ext_adv_connected_info *info) {
    uint8_t index = bt_le_ext_adv_get_index(adv);
    struct conn_adv_pair *pair = &conn_adv_pairs[index];
    
    pair->conn = info->conn;
    pair->conn_index = bt_conn_index(info->conn);

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(info->conn), addr, sizeof(addr));
    LOG_INF("Connected to %s with advertising set %d", addr, index);

	stop_pair_advertising(pair);

    uint16_t conn_handle;
    if (bt_hci_get_conn_handle(info->conn, &conn_handle) == 0) {
        set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, 
                    conn_handle, 
                    pair->conn_tx_power);
    }

	k_sem_give(&pair->conn_sem);
}

static const struct bt_le_ext_adv_cb adv_callbacks = {
	.connected = adv_connected_cb,
};

/* Real connection handling happens in the advertising callback */
static void connected(struct bt_conn *conn, uint8_t err) {
    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected from %s (reason: %u)", addr, reason);
	
	int8_t index =	conn_adv_pair_get_index_conn(conn);
	if (index == -1) {
		LOG_ERR("Connection not found.");
		return;
	}

	bt_conn_unref(conn);
	conn_adv_pairs[index].conn = NULL;

	start_pair_advertising(&conn_adv_pairs[index]);

    k_sem_take(&conn_adv_pairs[index].conn_sem, K_MSEC(50));
}

#if IS_ENABLED(CONFIG_BT_USER_PHY_UPDATE)
static void le_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param) {
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("PHY Updated for %s: TX: %s, RX: %s", 
            addr, 
            param->tx_phy == BT_GAP_LE_PHY_2M ? "2M" : 
            param->tx_phy == BT_GAP_LE_PHY_1M ? "1M" : "CODED",
            param->rx_phy == BT_GAP_LE_PHY_2M ? "2M" : 
            param->rx_phy == BT_GAP_LE_PHY_1M ? "1M" : "CODED");
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
#if IS_ENABLED(CONFIG_BT_USER_PHY_UPDATE)
    .le_phy_updated = le_phy_updated,
#endif
};
#endif

static void rssi_report_work_handler(struct k_work *work) {
	int err;
	int8_t rssi;
	uint16_t conn_handle;
	for (int i = 0; i < NUM_CONN_ADV_PAIRS; i++) {
		if (conn_adv_pairs[i].conn != NULL) {
	  		bt_hci_get_conn_handle(conn_adv_pairs[i].conn, &conn_handle);
			err = read_conn_rssi(conn_handle, &rssi);
			bt_gatt_notify_uuid(conn_adv_pairs[i].conn, BT_UUID_LCS_DOWN_LOCAL_RSSI, lcs_svc.attrs,
					&rssi, sizeof(rssi));
			LOG_INF("Connection %d on PHY %d RSSI: %d", conn_handle, conn_adv_pairs[i].phy, rssi);
		}
	}
}

K_WORK_DEFINE(rssi_report_work, rssi_report_work_handler);

static void rssi_report_timer_handler(struct k_timer *timer) {
	k_work_submit(&rssi_report_work);
}

K_TIMER_DEFINE(rssi_report_timer, rssi_report_timer_handler, NULL);

int main(void) {
    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth initialization failed (err %d)", err);
        return 0;
    }

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    LOG_INF("Bluetooth initialized");
    k_sem_give(&ble_init_ok);

	/* Create two connection/advertising pairs, one for CODED and one for 1M PHY */	
	init_conn_adv_pair(conn_adv_pairs, BT_GAP_LE_PHY_1M, 0, 0, &adv_callbacks, ad, ARRAY_SIZE(ad), NULL, 0);
	init_conn_adv_pair(conn_adv_pairs, BT_GAP_LE_PHY_CODED, 0, 0, &adv_callbacks, ad, ARRAY_SIZE(ad), NULL, 0);

	start_pair_advertising(&conn_adv_pairs[0]);
	start_pair_advertising(&conn_adv_pairs[1]);

	k_timer_start(&rssi_report_timer, K_NO_WAIT, K_MSEC(CONFIG_LCS_RSSI_INTERVAL_MS));

    return 0;
}

#if defined(CONFIG_LOG_BACKEND_FS)
static int cmd_remove_logs(const struct shell *shell, size_t argc, char **argv) {
    int res;
    struct fs_dir_t dirp;
    struct fs_dirent entry;
    char dir_path[64] = "/lfs1";

    fs_dir_t_init(&dirp);

    res = fs_opendir(&dirp, dir_path);
    if (res) {
        shell_error(shell, "Error opening directory %s (err %d)", dir_path, res);
        return res;
    }

    shell_print(shell, "Clearing directory %s", dir_path);
    
    while (true) {
        char file_path[PATH_MAX] = { 0 };
        
        res = fs_readdir(&dirp, &entry);
        if (res || entry.name[0] == 0) {
            break;
        }

        snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, entry.name);
        res = fs_unlink(file_path);
        if (res) {
            shell_error(shell, "Error deleting %s (err %d)", file_path, res);
            fs_closedir(&dirp);
            return res;
        }
    }

    fs_closedir(&dirp);
    shell_print(shell, "Directory %s cleared successfully", dir_path);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(link_control_cmds,
    SHELL_CMD(remove_logs, NULL, "Removes all logs", cmd_remove_logs),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(link_control, &link_control_cmds, "Link Control commands", NULL);
#endif
