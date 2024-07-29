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
static struct bt_conn *current_conn;

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

static void connected(struct bt_conn *conn, uint8_t err) {
    char addr[BT_ADDR_LE_STR_LEN];

    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Connected %s", addr);

    err = bt_le_adv_stop();
    if (err) {
        LOG_ERR("Failed to stop advertising, err: %d", err);
    }

    current_conn = bt_conn_ref(conn);
    uint16_t conn_handle;
    bt_hci_get_conn_handle(current_conn, &conn_handle);
    set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, current_tx_power);
    
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
        k_msleep(500);
    }
}

K_THREAD_DEFINE(ble_write_thread_id, STACKSIZE, ble_write_thread, NULL, NULL, NULL, PRIORITY, 0, 0);
