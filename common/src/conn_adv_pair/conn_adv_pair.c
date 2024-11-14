#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <sdc_hci_vs.h>

#include <zephyr/logging/log.h>

#include "link_control/link_control.h"
#include "conn_adv_pair/conn_adv_pair.h"

LOG_MODULE_REGISTER(conn_adv_pair);

struct conn_adv_pair conn_adv_pairs[NUM_CONN_ADV_PAIRS];

int init_conn_adv_pair(struct conn_adv_pair *pairs, 
					   uint8_t phy, 
					   int8_t conn_tx_power, 
					   int8_t adv_tx_power,
					   const struct bt_le_ext_adv_cb *cb,
				   	   const struct bt_data *ad, size_t ad_len, 
					   const struct bt_data *sd, size_t sd_len) {

	int err;
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
	pair->conn_index = -1;
    pair->adv_index = index;
	pair->adv_cb = cb;
    pair->phy = phy;
    pair->rssi = 0;
	pair->conn_tx_power = conn_tx_power;
	pair->adv_tx_power = adv_tx_power;
    pair->active = true;
    k_sem_init(&pair->conn_sem, 0, 1);

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
            return -1;
        }
		LOG_INF("Advertising set %d deleted", pair->adv_index);
        pair->adv = NULL;
    }

    err = bt_le_ext_adv_create(&adv_param, cb, &pair->adv);
    if (err) {
        LOG_ERR("Failed to create advertising set %d (err %d)", pair->adv_index, err);
        return -1;
    }

    err = bt_le_ext_adv_set_data(pair->adv, 
                                ad, ad_len,
                                sd, sd_len);
    if (err) {
        LOG_ERR("Failed to set advertising data for set %d (err %d)", pair->adv_index, err);
        return -1;
    }

    return 0;
}

int free_conn_adv_pair(struct conn_adv_pair **pairs, uint8_t index) {
	pairs[index]->active = false;
	return 0;
}

int conn_adv_pair_get_index(struct conn_adv_pair *pair) {
	for (uint8_t index = 0; index < NUM_CONN_ADV_PAIRS; index++) {
		if (pair == &conn_adv_pairs[index]) {
	  		return index;
		}
	}
	return -1;
}

int conn_adv_pair_get_index_conn(struct bt_conn *conn) {
	for (uint8_t index = 0; index < NUM_CONN_ADV_PAIRS; index++) {
		if (bt_conn_index(conn) == bt_conn_index(conn_adv_pairs[index].conn)) {
	  		return index;
		}
	}
	return -1;
}

void stop_pair_advertising(struct conn_adv_pair *pair) {
    int err = bt_le_ext_adv_stop(pair->adv);
    if (err) {
        LOG_ERR("Failed to stop advertising set %d (err %d)", 
                pair->adv_index, err);
    } else {
        LOG_INF("Stopped advertising set %d", pair->adv_index);
    }
}

void start_pair_advertising(struct conn_adv_pair *pair) {
    int err;

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


