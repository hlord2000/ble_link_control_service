#ifndef LINK_CONTROL_SERVICE_H__
#define LINK_CONTROL_SERVICE_H__

#include <stdint.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#define BT_UUID_LCS_VAL \
    BT_UUID_128_ENCODE(0x430EBAD0, 0x5C25, 0x469E, 0xA162, 0xA1C9DC50A8FD)
#define BT_UUID_LCS_TX_PWR_PERIPHERAL_VAL \
    BT_UUID_128_ENCODE(0x430EBAD1, 0x5C25, 0x469E, 0xA162, 0xA1C9DC50A8FD)
#define BT_UUID_LCS_RSSI_PERIPHERAL_VAL \
    BT_UUID_128_ENCODE(0x430EBAD2, 0x5C25, 0x469E, 0xA162, 0xA1C9DC50A8FD)
#define BT_UUID_LCS_TX_PWR_CENTRAL_VAL \
    BT_UUID_128_ENCODE(0x430EBAD3, 0x5C25, 0x469E, 0xA162, 0xA1C9DC50A8FD)
#define BT_UUID_LCS_RSSI_CENTRAL_VAL \
    BT_UUID_128_ENCODE(0x430EBAD4, 0x5C25, 0x469E, 0xA162, 0xA1C9DC50A8FD)
#define BT_UUID_LCS                      BT_UUID_DECLARE_128(BT_UUID_LCS_VAL)
#define BT_UUID_LCS_TX_PWR_PERIPHERAL    BT_UUID_DECLARE_128(BT_UUID_LCS_TX_PWR_PERIPHERAL_VAL)
#define BT_UUID_LCS_RSSI_PERIPHERAL      BT_UUID_DECLARE_128(BT_UUID_LCS_RSSI_PERIPHERAL_VAL)
#define BT_UUID_LCS_TX_PWR_CENTRAL       BT_UUID_DECLARE_128(BT_UUID_LCS_TX_PWR_CENTRAL_VAL)
#define BT_UUID_LCS_RSSI_CENTRAL         BT_UUID_DECLARE_128(BT_UUID_LCS_RSSI_CENTRAL_VAL)

void update_peripheral_rssi(struct bt_conn *conn, int16_t new_rssi);
void update_central_rssi(struct bt_conn *conn, int16_t new_rssi);

extern atomic_t peripheral_rssi_notif_enabled;
extern atomic_t central_rssi_notif_enabled;

#endif
