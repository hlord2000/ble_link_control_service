#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bt_lcs, LOG_LEVEL_DBG);

#include "link_control.h"
#include "link_control_service.h"

static int8_t tx_power_value = 0;
static int8_t rssi_value = 0;

static ssize_t read_tx_power(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &tx_power_value, sizeof(tx_power_value));
}

static ssize_t write_tx_power(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
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

static ssize_t read_rssi(struct bt_conn *conn, const struct bt_gatt_attr *attr,
						 void *buf, uint16_t len, uint16_t offset) {
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &rssi_value, sizeof(rssi_value));
}

volatile bool rssi_notif_enabled = false;

static void rssi_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    rssi_notif_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("RSSI notifications %s", rssi_notif_enabled ? "enabled" : "disabled");
}

volatile bool throughput_notif_enabled = false;

static void throughput_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	throughput_notif_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("Throughput notifications %s", value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(lcs_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_LCS),
    BT_GATT_CHARACTERISTIC(BT_UUID_LCS_TX_PWR,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                           read_tx_power, write_tx_power, &tx_power_value),
	BT_GATT_CHARACTERISTIC(BT_UUID_LCS_RSSI,
						   BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
						   BT_GATT_PERM_READ,
						   read_rssi, NULL, &rssi_value),
	BT_GATT_CCC(rssi_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_LCS_THROUGHPUT, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(throughput_ccc_cfg_changed,
		    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

void update_rssi(struct bt_conn *conn, int16_t new_rssi) {
	if (rssi_notif_enabled) {
		rssi_value = new_rssi;
		bt_gatt_notify_uuid(conn, BT_UUID_LCS_RSSI, lcs_svc.attrs, &rssi_value, sizeof(rssi_value));
	}
}

K_MUTEX_DEFINE(notify_mutex);
volatile bool notification_in_progress = false;

static void notification_cb(struct bt_conn *conn, void *user_data) {
    k_mutex_unlock(&notify_mutex);
    notification_in_progress = false;
}

void throughput_test(void) {
    while(true) {
        if (throughput_notif_enabled) {
            uint8_t throughput[CONFIG_BT_L2CAP_TX_MTU - 3] = "Throughput test";
			// Increase a 32 bit value at the end of this array to test throughput
			static uint32_t throughput_test_value = 0;
			throughput[sizeof(throughput) - 4] = (throughput_test_value >> 24) & 0xFF;
			throughput[sizeof(throughput) - 3] = (throughput_test_value >> 16) & 0xFF;
			throughput[sizeof(throughput) - 2] = (throughput_test_value >> 8) & 0xFF;
			throughput[sizeof(throughput) - 1] = throughput_test_value & 0xFF;
			throughput_test_value++;

            // Wait for mutex before sending
            k_mutex_lock(&notify_mutex, K_FOREVER);
            notification_in_progress = true;
            
            struct bt_gatt_notify_params params = {
                .uuid = BT_UUID_LCS_THROUGHPUT,
                .attr = &lcs_svc.attrs[5], // Adjust index as needed
                .data = throughput,
                .len = sizeof(throughput),
                .func = notification_cb,
                .user_data = NULL,
            };

            int err = bt_gatt_notify_cb(current_conn, &params);
            
            if (err) {
                LOG_ERR("Failed to send notification (err %d)", err);
                k_mutex_unlock(&notify_mutex);
                notification_in_progress = false;
            }
        }
    }
}

K_THREAD_DEFINE(throughput_thread, 1024, throughput_test, NULL, NULL, NULL, 1, 0, 0);
