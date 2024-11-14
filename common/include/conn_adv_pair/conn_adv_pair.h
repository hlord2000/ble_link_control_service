#ifndef CONN_ADV_PAIR_H__
#define CONN_ADV_PAIR_H__

#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

struct conn_adv_pair {
    struct bt_conn *conn;                  /* Connection associated with this pair */
    struct bt_le_ext_adv *adv;             /* Advertising set for this pair */
	uint8_t conn_index;				       /* Index of connection */
    uint8_t adv_index;                     /* Index of advertising set */
	const struct bt_le_ext_adv_cb *adv_cb; /* Advertising callbacks */
    uint8_t phy;                           /* Whether this pair uses coded PHY */
    int8_t rssi;                           /* Last known RSSI value */
	int8_t conn_tx_power;                  /* TX power for connection */
	int8_t adv_tx_power;                   /* TX power for advertising */
    bool active;                           /* Whether this pair is currently in use */
    struct k_sem conn_sem;                 /* Semaphore for connection state */
};

#define NUM_CONN_ADV_PAIRS CONFIG_BT_MAX_CONN

extern struct conn_adv_pair conn_adv_pairs[NUM_CONN_ADV_PAIRS];

int init_conn_adv_pair(struct conn_adv_pair *pairs, 
					   uint8_t phy, 
					   int8_t conn_tx_power, 
					   int8_t adv_tx_power,
					   const struct bt_le_ext_adv_cb *cb,
				   	   const struct bt_data *ad, size_t ad_len,
					   const struct bt_data *sd, size_t sd_len);

int free_conn_adv_pair(struct conn_adv_pair **pairs, uint8_t index);

int conn_adv_pair_get_index(struct conn_adv_pair *pair);

int conn_adv_pair_get_index_conn(struct bt_conn *conn);

void stop_pair_advertising(struct conn_adv_pair *pair);

void start_pair_advertising(struct conn_adv_pair *pair);	

#endif
