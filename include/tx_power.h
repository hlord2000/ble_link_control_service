#ifndef TX_POWER_H__
#define TX_POWER_H__

#include <stdint.h>
int set_tx_power(uint8_t handle_type, uint16_t handle, int8_t tx_pwr_lvl);

#endif
