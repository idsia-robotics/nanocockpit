# GAP8 applications

## Build instructions

The GAP8 code is composed of reusable components, under `lib/`, and example applications, under `examples/`. The examples show how to use individual components of NanoCockpit (e.g., the co-routines, the CPX low-level API or the CPX Wi-Fi streamer) and entire applications (e.g., PULP-Frontnet).

GAP SDK v3.8.1 is required to build this project, newer versions might work but have not been tested. Configure your computer to use the GAP SDK Docker container as described [here](../docker/gapsdk).

```sh
$ cd src/gap
$ gap8 3.8.1
```

Select the example that you want to build, for example the Wi-Fi streamer. Then, compile and run the code over JTAG:

```sh
> cd examples/streamer
> make all run
```

Flash the code:

```shell
> make flash
```

## Run on GVSOC

To allow testing without a physical AI-deck, the GAP8 SDK includes a GAP8 chip simulator, GVSOC.
E.g., in the PULP-Frontnet example, it’s possible to test the correctness of the CNN inference using the `NETWORK_TEST_INPUT` flag in the GAP8 [config.h](src/gap/examples/pulp-frontnet/config.h#L53) header.

Once the flag is enabled, PULP-Frontnet can run be on GVSOC with the commands: 
```sh
$ cd src/gap
$ gap8 3.8.1
> cd examples/pulp-frontnet
> make clean
> make all run platform=gvsoc
```

The code will run one inference of the PULP-Frontnet CNN on a hardcoded test image and verify that the output activations of each layer exactly match those of the Python version of the CNN.
