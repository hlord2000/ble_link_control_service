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
#include <zephyr/fs/fs.h>

#include "link_control.h"
#include "link_control_service.h"
#include "zephyr/bluetooth/gap.h"

LOG_MODULE_REGISTER(link_control_peripheral);

/* Define maximum supported AD data length */
#if defined(CONFIG_BT_CTLR_ADV_DATA_LEN_MAX)
#define BT_AD_DATA_LEN_MAX CONFIG_BT_CTLR_ADV_DATA_LEN_MAX
#else
#define BT_AD_DATA_LEN_MAX 31U
#endif

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

struct conn_adv_pair {
    struct bt_conn *conn;             /* Connection associated with this pair */
    struct bt_le_ext_adv *adv;        /* Advertising set for this pair */
    uint8_t adv_index;                /* Index of advertising set */
    uint8_t phy;                      /* Whether this pair uses coded PHY */
    int8_t rssi;                      /* Last known RSSI value */
	int8_t conn_tx_power;             /* TX power for connection */
	int8_t adv_tx_power;              /* TX power for advertising */
    bool active;                      /* Whether this pair is currently in use */
    struct k_sem conn_sem;            /* Semaphore for connection state */
};

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LCS_VAL),
};

static K_SEM_DEFINE(ble_init_ok, 0, 1);

#define NUM_CONN_ADV_PAIRS CONFIG_BT_MAX_CONN

static struct conn_adv_pair conn_adv_pairs[CONFIG_BT_MAX_CONN] = {0};

static int init_conn_adv_pair(struct conn_adv_pair *pairs, uint8_t phy, int8_t conn_tx_power, int8_t adv_tx_power) {
	struct conn_adv_pair *pair = NULL;

	uint8_t index;
	for (index = 0; index < NUM_CONN_ADV_PAIRS; index++) {
		if (pairs[index].active == false) {
	  		pair = &pairs[index];
			break;
	  	}
	}
	if (pair == NULL) {
		LOG_ERR("No free conn_adv_pair found");
		return -1;
	}

    pair->conn = NULL;
    pair->adv = NULL;
    pair->adv_index = index;
    pair->phy = phy;
    pair->rssi = 0;
	pair->conn_tx_power = conn_tx_power;
	pair->adv_tx_power = adv_tx_power;
    pair->active = true;
    k_sem_init(&pair->conn_sem, 0, 1);
    return 0;
}

static int free_conn_adv_pair(struct conn_adv_pair **pairs, uint8_t index) {
	pairs[index]->active = false;
	return 0;
}

static void start_pair_advertising(struct conn_adv_pair *pair) {
    int err;
    struct bt_le_adv_param adv_param = {
        .id = BT_ID_DEFAULT,
        .sid = pair->adv_index,
        .secondary_max_skip = 0,
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        .options = BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_EXT_ADV,
        .peer = NULL,
    };

	if (pair->phy == BT_GAP_LE_PHY_CODED) {
		adv_param.options |= BT_LE_ADV_OPT_CODED;
		adv_param.options |= BT_LE_ADV_OPT_NO_2M;
	}

	if (pair->adv != NULL) {
        err = bt_le_ext_adv_delete(pair->adv);
        if (err) {
            LOG_ERR("Failed to delete advertising set %d (err %d)", 
                    pair->adv_index, err);
            return;
        }
        pair->adv = NULL;
    }

    err = bt_le_ext_adv_create(&adv_param, NULL, &pair->adv);
    if (err) {
        LOG_ERR("Failed to create advertising set %d (err %d)", pair->adv_index, err);
        return;
    }
	
    err = bt_le_ext_adv_set_data(pair->adv, 
                                ad, ARRAY_SIZE(ad),
                                NULL, 0);
    if (err) {
        LOG_ERR("Failed to set advertising data for set %d (err %d)", pair->adv_index, err);
        return;
    }

    /* Start advertising */
    err = bt_le_ext_adv_start(pair->adv, BT_LE_EXT_ADV_START_DEFAULT);
    if (err) {
        LOG_ERR("Failed to start advertising set %d (err %d)", pair->adv_index, err);
        return;
    }

    LOG_INF("Started advertising set %d on %d PHY", pair->adv_index, 
            pair->phy);

	err = set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_ADV, pair->adv_index, pair->adv_tx_power);
	if (err) {
		LOG_ERR("Failed to set TX power (err %d)", err);
		return;
	}
}

static void stop_pair_advertising(struct conn_adv_pair *pair) {
    if (pair->adv == NULL) {
        return;
    }

    int err = bt_le_ext_adv_stop(pair->adv);
    if (err) {
        LOG_ERR("Failed to stop advertising set %d (err %d)", 
                pair->adv_index, err);
    } else {
        LOG_DBG("Stopped advertising set %d", pair->adv_index);
    }
}

#if 0
static void connected(struct bt_conn *conn, uint8_t err) {
	struct bt_conn_info info;
    char addr[BT_ADDR_LE_STR_LEN];

    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Connected to %s", addr);

	if(bt_conn_get_info(conn, &info) < 0) {
		LOG_ERR("Could not get connection info");
		return;
	}
	bool is_coded = (info.le.phy->rx_phy == BT_GAP_LE_PHY_CODED);
	LOG_INF("This connection coded phy? %d", is_coded);

	/* Stop advertising for PHY connection comes from */
    for (int i = 0; i < CONFIG_BT_EXT_ADV_MAX_ADV_SET; i++) {
        stop_advertising(i);
    }

	int conn_idx = get_free_connection_slot(connections);
	if (conn_idx == -1) {
		LOG_ERR("No available connection.");
		return;
	}

    /* Set TX power for connection */
    uint16_t conn_handle;
    if (bt_hci_get_conn_handle(connections[conn_idx], &conn_handle) == 0) {
        set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, current_tx_power);
    }
    
    k_sem_give(&ble_connected);
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected from %s (reason: %u)", addr, reason);

	
	int conn_idx = get_connection_slot_idx(connections, conn);
	if (conn_idx == -1) {
		LOG_ERR("Connection not found.");
		return;
	}

	bt_conn_unref(connections[conn_idx]);
	connections[conn_idx] = NULL;

    /* Restart advertising on both sets */
    start_advertising(0, LEGACY_ADVERTISING);
    start_advertising(1, EXTENDED_ADVERTISING);
    k_sem_take(&ble_connected, K_MSEC(50));
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
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
#if IS_ENABLED(CONFIG_BT_USER_PHY_UPDATE)
    .le_phy_updated = le_phy_updated,
#endif
};
#endif

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
	init_conn_adv_pair(conn_adv_pairs, BT_GAP_LE_PHY_1M, 0, 0);
	init_conn_adv_pair(conn_adv_pairs, BT_GAP_LE_PHY_CODED, 0, 0);
	start_pair_advertising(&conn_adv_pairs[0]);
	start_pair_advertising(&conn_adv_pairs[1]);
    
    return 0;
}

#if 0
void ble_write_thread(void) {
    k_sem_take(&ble_init_ok, K_FOREVER);

    while (true) {
        k_sem_take(&ble_connected, K_FOREVER);
        
        if (current_conn) {
            uint16_t conn_handle;
            if (bt_hci_get_conn_handle(current_conn, &conn_handle) == 0) {
                int8_t rssi;
                int err = read_conn_rssi(conn_handle, &rssi);
                if (err == 0) {
                    update_peripheral_rssi(current_conn, rssi);
                    LOG_INF("Connection RSSI: %d dBm", rssi);
                }
            }
        }

        k_sem_give(&ble_connected);
        k_msleep(CONFIG_LCS_RSSI_INTERVAL_MS);
    }
}

K_THREAD_DEFINE(ble_write_thread_id, CONFIG_LCS_PERIPHERAL_STACK_SIZE, ble_write_thread, 
                NULL, NULL, NULL, CONFIG_LCS_PERIPHERAL_THREAD_PRIORITY, 0, 0);
#endif

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
