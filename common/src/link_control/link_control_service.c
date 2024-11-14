#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bt_lcs, LOG_LEVEL_DBG);

#include "link_control/link_control.h"
#include "link_control/link_control_service.h"

// Variable declarations
static int8_t tx_power_value = 0;
static int8_t rssi_value = 0;
int8_t current_tx_power = 0;
static int8_t down_local_rssi = 0;
static uint8_t down_xcvr = 0;
static int8_t up_local_rssi = 0;
static uint8_t up_xcvr = 0;
static uint8_t peripheral_data = 0;

// Function declarations for downstream characteristics
static ssize_t read_down_local_rssi(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &down_local_rssi, sizeof(down_local_rssi));
}

static void down_local_rssi_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    bool notifications_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Down local RSSI notifications %s", notifications_enabled ? "enabled" : "disabled");
}

static ssize_t read_down_xcvr(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &down_xcvr, sizeof(down_xcvr));
}

static ssize_t write_down_xcvr(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset + len > sizeof(down_xcvr)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    memcpy(&down_xcvr + offset, buf, len);
    return len;
}

static void down_xcvr_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    bool notifications_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Down XCVR notifications %s", notifications_enabled ? "enabled" : "disabled");
}

// Original functions
static ssize_t read_tx_power(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &tx_power_value, sizeof(tx_power_value));
}

static ssize_t write_tx_power(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset + len > sizeof(tx_power_value)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(&tx_power_value + offset, buf, len);
    current_tx_power = tx_power_value;

    uint16_t conn_handle;
    bt_hci_get_conn_handle(conn, &conn_handle);
    set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, tx_power_value);

    LOG_INF("Set tx power to %d", tx_power_value);

    return len;
}

void update_peripheral_rssi(struct bt_conn *conn, int16_t new_rssi) {
        /*
    if (atomic_get(&peripheral_rssi_notif_enabled)) {
        peripheral_rssi_value = new_rssi;
        bt_gatt_notify_uuid(conn, BT_UUID_LCS_RSSI_PERIPHERAL, lcs_svc.attrs,
                &peripheral_rssi_value, sizeof(peripheral_rssi_value));
    }
        */
}

int8_t peripheral_rssi_value;

#if defined(CONFIG_BT_CENTRAL)
// Central-specific functions and variables
static int8_t central_rssi_value = 0;
static int8_t down_remote_rssi = 0;

static ssize_t read_down_remote_rssi(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &down_remote_rssi, sizeof(down_remote_rssi));
}

static void down_remote_rssi_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    bool notifications_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Down remote RSSI notifications %s", notifications_enabled ? "enabled" : "disabled");
}

static ssize_t read_up_local_rssi(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &up_local_rssi, sizeof(up_local_rssi));
}

static void up_local_rssi_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    bool notifications_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Up local RSSI notifications %s", notifications_enabled ? "enabled" : "disabled");
}

static ssize_t read_up_xcvr(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &up_xcvr, sizeof(up_xcvr));
}

static ssize_t write_up_xcvr(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset + len > sizeof(up_xcvr)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    memcpy(&up_xcvr + offset, buf, len);
    return len;
}

static void up_xcvr_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    bool notifications_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Up XCVR notifications %s", notifications_enabled ? "enabled" : "disabled");
}

static ssize_t read_peripheral_data(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                  void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &peripheral_data, sizeof(peripheral_data));
}

static void peripheral_data_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    bool notifications_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Peripheral data notifications %s", notifications_enabled ? "enabled" : "disabled");
}

static ssize_t read_rssi_central(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &peripheral_rssi_value, sizeof(peripheral_rssi_value));
}

extern struct k_timer central_rssi_timer;
atomic_t central_rssi_notif_enabled;
static void rssi_central_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    atomic_set(&central_rssi_notif_enabled, value == BT_GATT_CCC_NOTIFY);
    if (atomic_get(&central_rssi_notif_enabled)) {
        k_timer_start(&central_rssi_timer, K_MSEC(500), K_MSEC(500));
    } else {
        k_timer_stop(&central_rssi_timer);
    }
    LOG_INF("RSSI notifications %s", central_rssi_notif_enabled ? "enabled" : "disabled");
}
#endif

BT_GATT_SERVICE_DEFINE(lcs_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_LCS),

    // downstream connection characteristics (both devices)
    BT_GATT_CHARACTERISTIC(BT_UUID_LCS_DOWN_LOCAL_RSSI,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ,
                          read_down_local_rssi, NULL, &down_local_rssi),
    BT_GATT_CCC(down_local_rssi_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    BT_GATT_CHARACTERISTIC(BT_UUID_LCS_DOWN_XCVR,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                          read_down_xcvr, write_down_xcvr, &down_xcvr),
    BT_GATT_CCC(down_xcvr_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

#if defined(CONFIG_BT_CENTRAL)
    // central-only downstream characteristics
    BT_GATT_CHARACTERISTIC(BT_UUID_LCS_DOWN_REMOTE_RSSI,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ,
                          read_down_remote_rssi, NULL, &down_remote_rssi),
    BT_GATT_CCC(down_remote_rssi_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    // upstream connection characteristics
    BT_GATT_CHARACTERISTIC(BT_UUID_LCS_UP_LOCAL_RSSI,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ,
                          read_up_local_rssi, NULL, &up_local_rssi),
    BT_GATT_CCC(up_local_rssi_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    BT_GATT_CHARACTERISTIC(BT_UUID_LCS_UP_XCVR,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                          read_up_xcvr, write_up_xcvr, &up_xcvr),
    BT_GATT_CCC(up_xcvr_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    // peripheral data reflection
    BT_GATT_CHARACTERISTIC(BT_UUID_LCS_PERIPHERAL_DATA,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ,
                          read_peripheral_data, NULL, &peripheral_data),
    BT_GATT_CCC(peripheral_data_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
#endif
);
