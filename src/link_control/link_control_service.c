#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bt_lcs, LOG_LEVEL_DBG);

#include "link_control.h"
#include "link_control_service.h"

int8_t tx_power_val = 20;

// Forward define the read and write functions
static ssize_t read_tx_power(struct bt_conn *conn, const struct bt_gatt_attr *attr,
							void *buf, uint16_t len, uint16_t offset);
static ssize_t write_tx_power(struct bt_conn *conn, const struct bt_gatt_attr *attr,
							 const void *buf, uint16_t len, uint16_t offset, uint8_t flags);

BT_GATT_SERVICE_DEFINE(lcs_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_LCS),
    BT_GATT_CHARACTERISTIC(BT_UUID_LCS_TX_PWR,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_tx_power, write_tx_power, &tx_power_val),
);

static ssize_t read_tx_power(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &tx_power_val, sizeof(tx_power_val));
}

static ssize_t write_tx_power(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset + len > sizeof(tx_power_val)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(&tx_power_val + offset, buf, len);

	uint16_t conn_handle;
	bt_hci_get_conn_handle(conn, &conn_handle);
	set_tx_power(BT_HCI_VS_LL_HANDLE_TYPE_CONN, conn_handle, tx_power_val);

	LOG_INF("Set tx power to %d", tx_power_val);

    return len;
}

