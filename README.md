Use nRF Connect SDK Version 2.7
To build, use the command: 

```
cd <peripheral/central_peripheral>
```

```
west build -b nrf52840dk/nrf52840
```

To build with nRF21540 support:

```
west build -b nrf52840dk/nrf52840 --shield nrf21540ek -- -DEXTRA_CONF_FILE="fem.conf"
```

OR

```
west build -b nrf21540dk/nrf52840 -- -DEXTRA_CONF_FILE="fem.conf"
```

To build with PHY control commands enabled:

```
west build -b nrf52840dk/nrf52840 -- -DEXTRA_CONF_FILE="phy_update.conf"
```
OR

```
west build -b nrf52840dk/nrf52840 --shield nrf21540ek -- -DEXTRA_CONF_FILE="fem.conf;phy_update.conf"
```
OR

```
west build -b nrf21540dk/nrf52840 -- -DEXTRA_CONF_FILE="fem.conf;phy_update.conf"
```

To build with file system logging enabled:
```
west build -b nrf52840dk/nrf52840 -p -- -DEXTRA_CONF_FILE="phy_update.conf;flash_logging.conf"
```

To use external flash, please refer to the spi2 node's pinctrl definitions. 
For the nRF21540-DK they are:

| Function | Pin   |
|----------|-------|
| SCK      | P1.10 |
| MOSI     | P1.11 |
| MISO     | P1.12 |
| CS       | P1.07 |

For the nRF52-DK (nRF52832) they are:

| Function | Pin   |
|----------|-------|
| SCK      | P0.25 |
| MOSI     | P0.23 |
| MISO     | P0.24 |
| CS       | P0.22 |

Then:

```
west flash
```

All logging is sent to both the UART (for configuration and benchtop testing) and a littlefs filesystem (for field testing)

Additional 'link_control' commands are available on the central:

- set_peripheral_tx: set transmit power of connected peripheral
- set_central_tx: set transmit power of central device
- set_phy: If user PHY update is enabled, switch connection between 1M, 2M, and coded PHY
- remove_logs: clears all logs in filesystem **NOTE THAT AFTER RUNNING REMOVE_LOGS, YOU MUST RESET THE BOARD TO BEGIN COLLECTING LOGS AGAIN**

To view logs in the file system, run the following commands:

```
fs cd lfs1
fs ls
fs cat log.<XXXX>
```
