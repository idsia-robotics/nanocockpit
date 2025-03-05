# STM32 firmware

PULP-Frontnet low-level controller for the follow-me application.

## Installation

Install a STM32 compiler toolchain using one of the methods described here: https://www.bitcraze.io/documentation/repository/crazyflie-firmware/master/building-and-flashing/build/

The Crazyflie mounts two microcontrollers (STM32 and NRF51) which must run matching firmware versions.
This repo contains our customized STM32 firmware, based on version 2021.06.
We don't modify the NRF51 firmware, the stock 2021.06 version is used.
The first time a new Crazyflie is used, it must be flashed to stock version 2021.06 from `cfclient` so that both microcontrollers are updated.
Instructions can be found in [Bitcraze documentation](https://www.bitcraze.io/documentation/repository/crazyflie-clients-python/master/userguides/userguide_client/#firmware-upgrade).
Then, STM32 is flashed to our custom firmware below, while NRF51 is never flashed again.

Configure the radio address and other parameters in `cfclient` as described [here](https://www.bitcraze.io/documentation/repository/crazyflie-clients-python/master/userguides/userguide_client/#firmware-configuration).
Selecting a unique radio address avoids conflicts in case multiple drones are used at the same time.

## Build instructions

Compile the code (note that you need to be inside the `app` directory for the app-layer to build correctly):

```shell
$ cd src/stm32/app
$ make clean all
```

Flash the code over the air with the Crazyradio (replace the `radio://` URI with your CF radio parameters):
```shell
$ make cload CLOAD_ARGS="-w radio://0/100/2M/E7E7E7E7E7"
```
