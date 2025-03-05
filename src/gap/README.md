# GAP8 camera streamer

## Build instructions

At least GAP SDK v3.8.1 is required to build this project. Configure your computer to use the GAP SDK Docker container as described [here](../docker/gapsdk).

Compile and run the code over JTAG:

```shell
$ cd src/gap
$ gap8 3.8.1
> make all run
```

Flash the code:

```shell
> make flash
```
