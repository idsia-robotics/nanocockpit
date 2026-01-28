# GAP8 applications

## Build instructions

The GAP8 code is composed of reusable components, under `lib/`, and example applications, under `examples/`. The examples show how to use individual components of NanoCockpit (e.g., the co-routines, the CPX low-level API or the CPX Wi-Fi streamer) and entire applications (e.g., PULP-Frontnet).

GAP SDK v3.8.1 is required to build this project, newer versions might work but have not been tested. Configure your computer to use the GAP SDK Docker container as described [here](../docker/gapsdk).

Select the example that you want to build, for example the Wi-Fi streamer:
```sh
$ cd src/gap/examples/streamer
```

Compile and run the code over JTAG:

```shell
$ gap8 3.8.1
> make all run
```

Flash the code:

```shell
> make flash
```
