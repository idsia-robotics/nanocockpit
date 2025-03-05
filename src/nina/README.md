# NINA CPX bridge

This is a minimal implementation of the CPX protocol for the ESP32, which allows communication with the drone over Wi-Fi.

## Build instructions

Configure the ESP-IDF Docker container as described [here](../docker/espidf).

Open the ESP-IDF container and build the ESP application
```shell
$ cd src/nina
$ esp 5.3.1
> idf.py clean; idf.py menuconfig; idf.py all
```

It is possible to set the Wi-Fi configuration inside Menuconfig. The supported options are:
* Station mode (STA, preferred): the ESP32 connects to an existing Wi-Fi network for which you provide SSID and password in menuconfig (default SSID: `drone_wifi`, password: `wifi-wifi`)
* Access Point mode (AP): the ESP32 creates its own Wi-Fi network for which the SSID is provided in `wifi.h` (default SSID: `crazyflie`)

In addition, `menuconfig` provides the option to configure the [mDNS](https://en.wikipedia.org/wiki/Multicast_DNS) hostname of the drone, which allows a computer to connect without knowning its IP. The default hostname is `aideck.local`.

Flash the code over JTAG:
```shell
> openocd -f interface/ftdi/olimex-arm-usb-ocd-h.cfg -f board/esp-wroom-32.cfg -c "adapter_khz 20000"  -c 'program_esp build/partition_table/partition-table.bin 0x8000 verify' -c 'program_esp build/bootloader/bootloader.bin 0x1000 verify' -c 'program_esp build/aideck_cpx_streamer.bin 0x10000 verify reset exit'
```

## Limitations

- Only the GAP<=>ESP32<=>Host route is supported in this version, other routes supported by the [official CPX implementation](https://github.com/bitcraze/aideck-esp-firmware) (e.g., with STM32) are out of scope for this project.
- Dynamic Wi-Fi configuration is not supported, the desired configuration must be set at build time with `make menuconfig`. AP mode only supports creating an open network at the moment, STA supports open (configure an empty password) and password-protected Wi-Fi networks, but not enterprise networks (i.e. username/password authentication).
