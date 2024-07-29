Use nRF Connect SDK Version 2.7
To build, use the command: 

```cd <peripheral/central>```

```west build -b nrf52840dk/nrf52840```

Or, to build with nRF21540 support:

```west build -b nrf52840dk/nrf52840 --shield nrf21540ek -- -DEXTRA_CONF_FILE="fem.conf"```

Then:

```west flash```

All logging is sent to both the UART (for configuration and benchtop testing) and a littlefs filesystem (for field testing)

Additional 'link_control' commands are available on the central:

- set_peripheral_tx: set transmit power of connected peripheral
- set_central_tx: set transmit power of central device
- set_phy: If user PHY update is enabled, switch connection between 1M, 2M, and coded PHY
- remove_logs: clears all logs in filesystem

To view logs in the file system, run the following commands:

```fs cd lfs1```
```fs ls```
```fs cat log.<XXXX>```
