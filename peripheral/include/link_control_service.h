#ifndef LINK_CONTROL_SERVICE_H__
#define LINK_CONTROL_SERVICE_H__

#include <zephyr/types.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#define BT_UUID_LCS_VAL \
    BT_UUID_128_ENCODE(0x430EBAD0, 0x5C25, 0x469E, 0xA162, 0xA1C9DC50A8FD)
#define BT_UUID_LCS_TX_PWR_VAL \
    BT_UUID_128_ENCODE(0x430EBAD1, 0x5C25, 0x469E, 0xA162, 0xA1C9DC50A8FD)
#define BT_UUID_LCS_RSSI_VAL \
    BT_UUID_128_ENCODE(0x430EBAD2, 0x5C25, 0x469E, 0xA162, 0xA1C9DC50A8FD)
#define BT_UUID_LCS_THROUGHPUT_VAL \
	BT_UUID_128_ENCODE(0x430EBAD3, 0x5C25, 0x469E, 0xA162, 0xA1C9DC50A8FD)
#define BT_UUID_LCS           BT_UUID_DECLARE_128(BT_UUID_LCS_VAL)
#define BT_UUID_LCS_TX_PWR    BT_UUID_DECLARE_128(BT_UUID_LCS_TX_PWR_VAL)
#define BT_UUID_LCS_RSSI      BT_UUID_DECLARE_128(BT_UUID_LCS_RSSI_VAL)
#define BT_UUID_LCS_THROUGHPUT BT_UUID_DECLARE_128(BT_UUID_LCS_THROUGHPUT_VAL)

void update_rssi(struct bt_conn *conn, int16_t new_rssi);

#endif
