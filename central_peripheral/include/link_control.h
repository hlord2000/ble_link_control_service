#ifndef LINK_CONTROL_H__
#define LINK_CONTROL_H__

#include <stdint.h>
#include <zephyr/bluetooth/bluetooth.h>

extern int8_t current_tx_power;

// Set the TX power level for a connection
int set_tx_power(uint8_t handle_type, uint16_t handle, int8_t tx_pwr_lvl);

// Read connection RSSI
int read_conn_rssi(uint16_t handle, int8_t *rssi);

// Change the connection interval
int change_connection_interval(struct bt_conn *conn, uint16_t interval_us);

// Update the connection PHY
int update_phy(struct bt_conn *conn, uint8_t phy);

#endif
