#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bt_lcs, LOG_LEVEL_DBG);

#include "link_control.h"
#include "link_control_service.h"

static int8_t peripheral_tx_power = 0;
static ssize_t read_tx_power_peripheral(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &peripheral_tx_power, sizeof(peripheral_tx_power));
}

static ssize_t write_tx_power_peripheral(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset + len > sizeof(peripheral_tx_power)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(&peripheral_tx_power + offset, buf, len);
	current_tx_power = peripheral_tx_power;

	uint16_t conn_handle;
	bt_hci_get_conn_handle(conn, &conn_handle);
	set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, peripheral_tx_power);

	LOG_INF("Set tx power to %d", peripheral_tx_power);

    return len;
}

static int8_t central_tx_power = 0;
static ssize_t read_tx_power_central(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &peripheral_tx_power, sizeof(peripheral_tx_power));
}

static ssize_t write_tx_power_central(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset + len > sizeof(peripheral_tx_power)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(&peripheral_tx_power + offset, buf, len);
	current_tx_power = peripheral_tx_power;

	uint16_t conn_handle;
	bt_hci_get_conn_handle(conn, &conn_handle);
	set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, peripheral_tx_power);

	LOG_INF("Set tx power to %d", peripheral_tx_power);

    return len;
}

static int8_t peripheral_rssi_value = 0;
static ssize_t read_rssi_peripheral(struct bt_conn *conn, const struct bt_gatt_attr *attr,
						 void *buf, uint16_t len, uint16_t offset) {
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &peripheral_rssi_value, sizeof(peripheral_rssi_value));
}

bool peripheral_rssi_notif_enabled = false;
static void rssi_peripheral_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    peripheral_rssi_notif_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("RSSI notifications %s", peripheral_rssi_notif_enabled ? "enabled" : "disabled");
}

static int8_t central_rssi_value = 0;
static ssize_t read_rssi_central(struct bt_conn *conn, const struct bt_gatt_attr *attr,
						 void *buf, uint16_t len, uint16_t offset) {
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &peripheral_rssi_value, sizeof(peripheral_rssi_value));
}


extern struct k_timer central_rssi_timer;
bool central_rssi_notif_enabled = false;
static void rssi_central_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    central_rssi_notif_enabled = (value == BT_GATT_CCC_NOTIFY);
	if (central_rssi_notif_enabled) {
		k_timer_start(&central_rssi_timer, K_MSEC(500), K_MSEC(500));
	}
	else {
		k_timer_stop(&central_rssi_timer);
	}
    LOG_INF("RSSI notifications %s", central_rssi_notif_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(lcs_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_LCS),
    BT_GATT_CHARACTERISTIC(BT_UUID_LCS_TX_PWR_PERIPHERAL,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_tx_power_peripheral, write_tx_power_peripheral, &peripheral_tx_power),
	BT_GATT_CHARACTERISTIC(BT_UUID_LCS_RSSI_PERIPHERAL,
						   BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
						   BT_GATT_PERM_READ,
						   read_rssi_peripheral, NULL, &peripheral_rssi_value),
	BT_GATT_CCC(rssi_peripheral_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(BT_UUID_LCS_TX_PWR_CENTRAL,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_tx_power_central, write_tx_power_central, &central_tx_power),
	BT_GATT_CHARACTERISTIC(BT_UUID_LCS_RSSI_CENTRAL,
						   BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
						   BT_GATT_PERM_READ,
						   read_rssi_central, NULL, &central_rssi_value),
	BT_GATT_CCC(rssi_central_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

void update_peripheral_rssi(struct bt_conn *conn, int16_t new_rssi) {
	if (peripheral_rssi_notif_enabled) {
		peripheral_rssi_value = new_rssi;
		bt_gatt_notify_uuid(conn, BT_UUID_LCS_RSSI_PERIPHERAL, lcs_svc.attrs, \
				&peripheral_rssi_value, sizeof(peripheral_rssi_value));
	}
}

void update_central_rssi(struct bt_conn *conn, int16_t new_rssi) {
	if (central_rssi_notif_enabled) {
		central_rssi_value = new_rssi;
		bt_gatt_notify_uuid(conn, BT_UUID_LCS_RSSI_CENTRAL, lcs_svc.attrs, &central_rssi_value, \
				sizeof(central_rssi_value));
	}
}

