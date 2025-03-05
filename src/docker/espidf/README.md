# ESP IDF Docker image

## Available versions

Currently, GitLab CI builds images based on Ubuntu 22.04 with ESP-IDF versions:
* `3.3.1` for the `amd64` architecture
* `4.4.5` for both `amd64` and `arm64`
* `5.0.2` for both `amd64` and `arm64`

## Build image

Pre-built images are available on [GitLab](https://gitlab.com/EliaCereda/espidf/container_registry/2311000), you can skip this step and proceed.
The next step will use the pre-built images automatically.
In alternative, you can build a custom image locally in case you need to customize it.
To do so, export the desired `ESP_IDF_VERSION`, move to the corresponding sub-directory and build an image with the following `docker build` command.

```shell
$ cd 3.3.1
$ export UBUNTU_VERSION=22.04 ESP_IDF_VERSION=3.3.1
$ DOCKER_BUILDKIT=1 \
  docker build \
    --pull \
    --cache-from "registry.gitlab.com/eliacereda/espidf:$UBUNTU_VERSION-$ESP_IDF_VERSION" \
    --build-arg BUILDKIT_INLINE_CACHE=1 \
    --build-arg UBUNTU_VERSION --build-arg ESP_IDF_VERSION \
    -t "registry.gitlab.com/eliacereda/espidf:$UBUNTU_VERSION-$ESP_IDF_VERSION" .
```

## Shell alias

Add the following alias to your `.bashrc` or `.zshrc` (depending on your shell). It will allow you to launch a ESP-IDF Docker container with the shortcut `esp`:

```shell
esp() {
    # Usage: esp [ESP_SDK_VERSION [UBUNTU_VERSION]]
    ESP_SDK_VERSION=${1:-3.3.1} UBUNTU_VERSION=${2:-22.04}

    docker run \
        --rm -it \
        -v "${PWD}:/module/data/" \
        -P \
        --device-cgroup-rule="c 189:* rmw" -v /dev/bus:/dev/bus:ro -v /dev/serial:/dev/serial:ro \
        registry.gitlab.com/eliacereda/espidf:${UBUNTU_VERSION}-${ESP_SDK_VERSION} \
        /bin/bash -c "
            echo \"export PS1='\e[1;34m[esp:${UBUNTU_VERSION}-${ESP_SDK_VERSION} \w]\$ \e[0m'\" >> /root/.bashrc; \
            source /esp/esp-idf/export.sh; \
            cd /module/data/; \
            bash
        "
}
```

## Usage

After setting up the `esp` alias, enter the ESP-IDF container and get ready to run your first program on ESP32:

```shell
$ esp 4.4.5
> cd /esp/esp-idf
```

This will open a container with ESP-IDF v4.4.5 targeting the NINA-W102 module on the Bitcraze AI-deck board.
At `/esp/esp-idf` you can find the root of ESP-IDF, with source code and many examples, cloned from the [official repo](https://github.com/espressif/esp-idf/tree/v4.4.5). Let's try running the Hello World:

```shell
> cd /esp/esp-idf/examples/get-started/hello_world
> ll
/esp/esp-idf/examples/get-started/hello_world total 28
drwxr-xr-x 3 root root 4096 Jun 19  2023 ./
drwxr-xr-x 5 root root 4096 Jun 19  2023 ../
-rw-r--r-- 1 root root  235 Jun 19  2023 CMakeLists.txt
-rw-r--r-- 1 root root  182 Jun 19  2023 Makefile
-rw-r--r-- 1 root root 2335 Jun 19  2023 README.md
-rw-r--r-- 1 root root  663 Jun 19  2023 example_test.py
drwxr-xr-x 2 root root 4096 Jun 19  2023 main/
-rw-r--r-- 1 root root    0 Jun 19  2023 sdkconfig.ci
```

### Program on ESP32 via JTAG
Running the example on the ESP32 aboard the AI-deck requires a JTAG programmer such as the [Olimex ARM-USB-OCD-H](https://duckduckgo.com/?q=olimex+arm+usb+ocd+h&ia=web#:~:text=JTAG%20%E2%80%BA%20ARM%2DUSB%2DOCD%2DH-,ARM%2DUSB%2DOCD%2DH,-%2D%20Olimex).
For instructions on how to physically connect the programmer to an AI-deck board, refer to [Bitcraze documentation](https://www.bitcraze.io/documentation/repository/aideck-gap8-examples/master/infrastructure/jtag-programmer/).

Build the example and program it to the Flash memory
```shell
> idf.py all
> openocd -f interface/ftdi/olimex-arm-usb-ocd-h.cfg -f board/esp-wroom-32.cfg -c "adapter_khz 20000"  -c 'program_esp build/partition_table/partition-table.bin 0x8000 verify' -c 'program_esp build/bootloader/bootloader.bin 0x1000 verify' -c 'program_esp build/hello_world.bin 0x10000 verify reset exit'
```

By default, the output of the program will be written to the EPS32 UART (pins 1-2 of the AI-deck's right expansion header). Connect a UART adapter or a logic analyzer to those pins and check that the expected output is printed:
```shell
[...]
(0) cpu_start: Starting scheduler on APP CPU.
Hello world!
This is esp32 chip with 2 CPU core(s), WiFi/BT/BLE, silicon revision v1.0, 2MB external flash
Minimum free heap size: 300936 bytes
Restarting in 10 seconds...
Restarting in 9 seconds...
Restarting in 8 seconds...
[...]
```
