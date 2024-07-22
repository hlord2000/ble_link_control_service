#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/hci_vs.h>
#include <sdc_hci_vs.h>

LOG_MODULE_REGISTER(link_control, LOG_LEVEL_DBG);

int change_connection_interval(struct bt_conn *conn, uint16_t interval_us) {
	int err;
	struct net_buf *buf;

	sdc_hci_cmd_vs_conn_update_t *cmd_conn_update;

	buf = bt_hci_cmd_create(SDC_HCI_OPCODE_CMD_VS_CONN_UPDATE,
				sizeof(*cmd_conn_update));
	if (!buf) {
		LOG_ERR("Could not allocate command buffer");
		return -ENOMEM;
	}

	uint16_t conn_handle;

	err = bt_hci_get_conn_handle(conn, &conn_handle);
	if (err < 0) {
		LOG_ERR("Failed obtaining conn_handle (err %d)", err);
		return err;
	}

	cmd_conn_update = net_buf_add(buf, sizeof(*cmd_conn_update));
	cmd_conn_update->conn_handle         = conn_handle;
	cmd_conn_update->conn_interval_us    = interval_us;
	cmd_conn_update->conn_latency        = 0;
	cmd_conn_update->supervision_timeout = 300;

	err = bt_hci_cmd_send_sync(SDC_HCI_OPCODE_CMD_VS_CONN_UPDATE, buf, NULL);
	if (err < 0) {
		LOG_ERR("Update connection parameters failed (err %d)", err);
		return err;
	}

	return 0;
}

int update_phy(struct bt_conn *conn, uint8_t phy) {
    int err;
	struct bt_conn_le_phy_param preferred_phy;

	if (phy == BT_GAP_LE_PHY_CODED) {
		preferred_phy.options = BT_CONN_LE_PHY_OPT_CODED_S8;
		preferred_phy.pref_tx_phy = BT_GAP_LE_PHY_CODED;
		preferred_phy.pref_rx_phy = BT_GAP_LE_PHY_CODED;
	}
	else {
		preferred_phy.options = BT_CONN_LE_PHY_OPT_NONE;
		preferred_phy.pref_tx_phy = phy;
		preferred_phy.pref_rx_phy = phy;
	}

    err = bt_conn_le_phy_update(conn, &preferred_phy);
    if (err < 0) {
        LOG_ERR("bt_conn_le_phy_update() returned %d", err);
		return err;
    }
	return 0;
}

int read_conn_rssi(uint16_t handle, int8_t *rssi) {
	struct net_buf *buf, *rsp = NULL;
	struct bt_hci_cp_read_rssi *cp;
	struct bt_hci_rp_read_rssi *rp;

	int err;

	buf = bt_hci_cmd_create(BT_HCI_OP_READ_RSSI, sizeof(*cp));
	if (!buf) {
		LOG_ERR("Unable to allocate command buffer");
		return -ENOMEM;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(handle);

	err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
	if (err < 0) {
		uint8_t reason = rsp ?
			((struct bt_hci_rp_read_rssi *)rsp->data)->status : 0;
		LOG_ERR("Read RSSI err: %d reason 0x%02x", err, reason);
		return err;
	}

	rp = (void *)rsp->data;
	*rssi = rp->rssi;

	net_buf_unref(rsp);
	return 0;
}

int set_tx_power(uint8_t handle_type, uint16_t handle, int8_t tx_pwr_lvl) {
	struct bt_hci_cp_vs_write_tx_power_level *cp;
	struct bt_hci_rp_vs_write_tx_power_level *rp;
	struct net_buf *buf, *rsp = NULL;
	int err;

	buf = bt_hci_cmd_create(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL,
				sizeof(*cp));
	if (!buf) {
		LOG_ERR("Unable to allocate command buffer");
		return -ENOMEM;
	}

	cp = net_buf_add(buf, sizeof(*cp));
	cp->handle = sys_cpu_to_le16(handle);
	cp->handle_type = handle_type;
	cp->tx_power_level = tx_pwr_lvl;

	err = bt_hci_cmd_send_sync(BT_HCI_OP_VS_WRITE_TX_POWER_LEVEL,
				   buf, &rsp);
	if (err < 0) {
		uint8_t reason = rsp ?
			((struct bt_hci_rp_vs_write_tx_power_level *)
			  rsp->data)->status : 0;
		LOG_ERR("Set Tx power err: %d reason 0x%02x", err, reason);
		return err;
	}

	rp = (void *)rsp->data;
	tx_pwr_lvl = rp->selected_tx_power;

	net_buf_unref(rsp);
	return 0;
}
