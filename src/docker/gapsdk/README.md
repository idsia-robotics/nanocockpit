# GAP SDK Docker image

## Available versions

Currently, the following versions are supported and GitLab CI automatically builds the corresponding images for Ubuntu 22.04 with both `amd64` and `arm64` architectures:
* `3.6` and `3.8.1`, built by the `master` branch of this repo
* `4.22.0`, built by the `ubuntu22.04-4.22.0` branch of this repo

Branches that build a number of other 4.x.x versions and images for Ubuntu 18.04 are also provided for reference, but are not actively supported.
The images are configured to support GAP SDK configs for two boards.

## Available configurations
Images built by GitLab CI are configured to support three GAP SDK configs:
* `ai_deck`
* `gapuino`
* `gapuino_v3`

To enable further configs, add them to `GAP_CONFIGS` in `.gitlab-ci.yml` or build a custom image locally with `docker build`.

## Build image
Pre-built images are available on [GitLab](https://gitlab.com/EliaCereda/gapsdk/container_registry/1864311), you can skip this step and proceed.
The next step will use the pre-built images automatically.
In alternative, you can build a custom image locally in case you need to customize it.
To do so, open a terminal in this directory and build an image with the following `docker build` command, supplying the desired `GAP_SDK_VERSION`.

```shell
$ export UBUNTU_VERSION=22.04 GAP_SDK_VERSION=3.8.1 GAP_TOOLCHAIN_VERSION=24.02 GAP_CONFIGS=ai_deck,gapuino,gapuino_v3
$ DOCKER_BUILDKIT=1 \
  docker build \
    --pull \
    --cache-from "registry.gitlab.com/eliacereda/gapsdk:$UBUNTU_VERSION-3.8.1" \
    --build-arg BUILDKIT_INLINE_CACHE=1 \
    --build-arg UBUNTU_VERSION --build-arg GAP_SDK_VERSION --build-arg GAP_TOOLCHAIN_VERSION --build-arg GAP_CONFIGS \
    -t "registry.gitlab.com/eliacereda/gapsdk:$UBUNTU_VERSION-$GAP_SDK_VERSION" .
```

## Shell alias

Add the following alias to your `.bashrc` or `.zshrc` (depending on your shell). It will allow you to launch a GAP SDK Docker container with the shortcut `gap8`:

```shell
gap8() {
    # Usage: gap8 [GAP_SDK_VERSION [GAP_CONFIG [UBUNTU_VERSION]]]
    GAP_SDK_VERSION=${1:-3.8.1} GAP_CONFIG=${2:-ai_deck} UBUNTU_VERSION=${3:-22.04}

    docker run \
        --rm -it \
        -v "${PWD}:/module/data/" \
        -P \
        --device-cgroup-rule="c 189:* rmw" -v /dev/bus:/dev/bus:ro -v /dev/serial:/dev/serial:ro \
        registry.gitlab.com/eliacereda/gapsdk:${UBUNTU_VERSION}-${GAP_SDK_VERSION} \
        /bin/bash -c "
            echo \"export PS1='\e[1;32m[gap8:${UBUNTU_VERSION}-${GAP_SDK_VERSION}-${GAP_CONFIG} \w]\$ \e[0m'\" >> /root/.bashrc; \
            export GAPY_OPENOCD_CABLE=interface/ftdi/olimex-arm-usb-ocd-h.cfg; \
            source /gap_sdk/configs/${GAP_CONFIG}.sh; \
            cd /module/data/; \
            bash
        "
}
```

## Usage

After setting up the `gap8` alias, enter the GAP SDK container and get ready to run your first GAP8 program:

```shell
$ gap8 3.8.1
> cd /gap_sdk
```

This will open a container with GAP SDK v3.8.1 targeting the Bitcraze AI-deck board configuration.
At `/gap_sdk` you can find the root of the GAP SDK, with source code and many examples, cloned from the [official repo](https://github.com/GreenWaves-Technologies/gap_sdk/tree/release-v3.8.1). Let's try running the Hello World:

```shell
> cd /gap_sdk/examples/pmsis/helloworld
> ll
/gap_sdk/examples/pmsis/helloworld total 24
drwxr-xr-x 2 root root 4096 Jan 12  2023 ./
drwxr-xr-x 6 root root 4096 Jan 12  2023 ../
-rw-r--r-- 1 root root  297 Jan 12  2023 Makefile
-rw-r--r-- 1 root root  233 Jan 12  2023 README.md
-rw-r--r-- 1 root root 1626 Jan 12  2023 helloworld.c
-rw-r--r-- 1 root root  241 Jan 12  2023 testset.cfg
```

### Run in GVSOC
The GAP SDK includes GVSOC, an accurate simulator of the GAP8 system-on-chip that speeds up development and debugging. You can run the example in GVSOC, by simply typing:
```shell
> make all run platform=gvsoc
[...]

	 *** PMSIS HelloWorld ***

Entering main controller
[32 0] Hello World!
Cluster master core entry
[0 2] Hello World!
[0 7] Hello World!
[0 0] Hello World!
[0 4] Hello World!
[0 1] Hello World!
[0 5] Hello World!
[0 6] Hello World!
[0 3] Hello World!
Cluster master core exit
Test success !
```

### Run on GAP8 via JTAG
Running the example on the actual board requires a JTAG programmer such as the [Olimex ARM-USB-OCD-H](https://duckduckgo.com/?q=olimex+arm+usb+ocd+h&ia=web#:~:text=JTAG%20%E2%80%BA%20ARM%2DUSB%2DOCD%2DH-,ARM%2DUSB%2DOCD%2DH,-%2D%20Olimex).

For instructions on how to physically connect the programmer to an AI-deck board, refer to [Bitcraze documentation](https://www.bitcraze.io/documentation/repository/aideck-gap8-examples/master/infrastructure/jtag-programmer/).
Note that the software setup describe in their documentation is not necessary and already taken care by our Docker container: running the Hello World on an actual board only requires setting the `platform=board` parameter. 

Alternatively, the `platform` parameter can be omitted entirely because `board` is the default value:
```shell
> make all run
[...]
Open On-Chip Debugger 0.10.0+dev-dirty (2023-01-12-15:12)
Licensed under GNU GPL v2
For bug reports, read
	http://openocd.org/doc/doxygen/bugs.html
DEPRECATED! use 'adapter speed' not 'adapter_khz'
DEPRECATED! use 'adapter driver' not 'interface'
Warn : Interface already configured, ignoring
TARGET create
Info : core 0 selected
0
Info : gap8_adv_debug_itf tap selected
Info : adv_dbg_unit debug unit selected
Info : Option 7 is passed to adv_dbg_unit debug unit
GAP8 INIT TARGET
Info : clock speed 1500 kHz
Info : JTAG tap: gap8.cpu tap/device found: 0x149511c3 (mfg: 0x0e1 (Wintec Industries), part: 0x4951, ver: 0x1)
GAP8 examine target
Init jtag
Initialising GAP8 JTAG TAP
Info : adv debug unit is configured with option BOOT MODE JTAG
Info : adv debug unit is configured with option ADBG_USE_HISPEED
Info : gdb port disabled
Loading binary through JTAG
Info : tcl server disabled
Info : telnet server disabled


	*** PMSIS HelloWorld ***

Entering main controller
[32 0] Hello World!
Cluster master core entry
[0 6] Hello World!
[0 4] Hello World!
[0 5] Hello World!
[0 3] Hello World!
[0 7] Hello World!
[0 1] Hello World!
[0 2] Hello World!
[0 0] Hello World!
Cluster master core exit
Test success !
```

With `make run` on a physical board, OpenOCD loads the executable code on the physical GAP8 through the JTAG link, executes it once and shows the printed output (through a mechanism called JTAG semi-hosting). 

### Program on GAP8 Flash memory
In alternative, you can load the code on the Flash memory aboard the AI-deck, so that it will be executed automatically every time it starts up. This does not make much sense with the Hello World, because it's output is not visible. Anyways, you can flash it using the command:

```shell
> make all flash
[...]
Open On-Chip Debugger 0.10.0+dev-dirty (2023-01-12-15:12)
Licensed under GNU GPL v2
For bug reports, read
	http://openocd.org/doc/doxygen/bugs.html
DEPRECATED! use 'adapter speed' not 'adapter_khz'
DEPRECATED! use 'adapter driver' not 'interface'
Warn : Interface already configured, ignoring
TARGET create
Info : core 0 selected
Info : gap8_adv_debug_itf tap selected
Info : adv_dbg_unit debug unit selected
Info : Option 7 is passed to adv_dbg_unit debug unit
GAP8 INIT TARGET
Info : clock speed 1500 kHz
Info : JTAG tap: gap8.cpu tap/device found: 0x149511c3 (mfg: 0x0e1 (Wintec Industries), part: 0x4951, ver: 0x1)
GAP8 examine target
Init jtag
Initialising GAP8 JTAG TAP
Info : adv debug unit is configured with option BOOT MODE JTAG
Info : adv debug unit is configured with option ADBG_USE_HISPEED
Info : gdb port disabled
--------------------------
begining flash session (hyperflash)
--------------------------
load flasher to L2 memory
Loading binary through JTAG
Warn : Burst read timed out
Instruct flasher to begin flash per se
device struct address is 470093656
going to wait on addr GAP_RDY
wait on gap_rdy done witg buff ptr 0 469828864
loading image with addr 469828864 addr_min 469828864 and size 28704
load image done
flasher is done, exiting
--------------------------
flasher is done!
--------------------------
--------------------------
Reset CONFREG to 0
--------------------------
Info : JTAG tap: gap8.cpu tap/device found: 0x149511c3 (mfg: 0x0e1 (Wintec Industries), part: 0x4951, ver: 0x1)
GAP8 examine target
RESET: jtag boot mode=3
DEPRECATED! use 'adapter [de]assert' not 'jtag_reset'
```
