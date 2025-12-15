# Crazyflie NanoCockpit
Performance-optimized Application Framework for AI-based Autonomous Nanorobotics

## Features
* Camera configuration and acquisition
* State-estimation forwarding to GAP8
* Camera and state-estimation streaming over Wi-Fi

## Structure
This repository is structured with one sub-directory for each component of the system, matched with the hardware components in a Crazyflie drone:
* `stm32`: code for the Crazyflie's main STM32 MCU, further divided in `crazyflie-firmware` (original Crazyflie firmware, with minor additions) and `app` (user application code).
* `nina`: code for the ESP32 NINA module on the AI-deck. Handles Wi-Fi communication, streams data received from GAP8 to the host computer.
* `gap`: code for the GAP8 SoC on the AI-deck. Handles camera configuration and acquisition, receives state estimation from STM32 over UART, streams over SPI to ESP32.
* `client`: Python code for the host computer. Provides ROS and GUI clients for the camera streamer, off-board inference and remote control of the drone.

## Getting started

The following instructions allow you build and deploy a demo application build on top of the NanoCockpit framework to a Crazyflie drone with AI-deck.
The application implements the PULP-Frontnet-based CNN tested in the _Human pose estimation_ experiment, including on-board inference on the GAP8 SoC and closed-loop control on the STM32 MCU.
More detailed information about each component is available in the respective subfolders.

#### Setup virtual environment

```shell
$ python3.9 -m venv venv
$ source venv/bin/activate
$ pip install -r requirements.txt
$ pip install src/client/aideck_cpx_streamer
$ pip install src/client/crazyflie-clients-python
```

#### STM32

```shell
$ source venv/bin/activate
$ cd src/stm32/app
$ make all -j8
$ make cload
```

#### GAP8

GAP SDK v3.8.1 is required to build this application. Configure your computer to use the GAP SDK Docker container as described [here](src/docker/gapsdk).
Then activate the GAP SDK container:

```shell
$ source venv/bin/activate
$ cd src/gap
$ gap8 3.8.1
```

```
> make clean
> make all flash
```

#### NINA ESP32

ESP-IDF v4.4.5 is required to build this application. Configure your computer to use the ESP-IDF Docker container as described [here](src/docker/espidf).
Then activate the ESP-IDF container:

```shell
$ source venv/bin/activate
$ cd src/nina
$ esp 4.4.5
``` 

```shell
> idf.py clean; idf.py menuconfig; idf.py all
> openocd -f interface/ftdi/olimex-arm-usb-ocd-h.cfg -f board/esp-wroom-32.cfg -c "adapter_khz 20000"  -c 'program_esp build/partition_table/partition-table.bin 0x8000 verify' -c 'program_esp build/bootloader/bootloader.bin 0x1000 verify' -c 'program_esp build/aideck_cpx_streamer.bin 0x10000 verify reset exit'
```

#### Crazyflie Client

```shell
$ source venv/bin/activate
$ cfclient
```

## Literature review
In our paper, we review the body of work on nanorobotics over the last five years and demonstrate both the high research interest in the topic and the Crazyflie's prominent status as de-facto standard robot platform.
The data to reproduce our analysis is available in `docs/literature_review` as a resource to other researchers that approach the nano-drone field.
We search the Elsevier Scopus citation database using the query in [scopus_query.txt](docs/literature_review/scopus_query.txt), resulting in the 554 papers reported in [scopus_dataset.csv](docs/literature_review/scopus_dataset.csv), with our manual annotations with information about the target drone platform.

## Publications
If you use NanoCockpit in an academic context, we kindly ask you to cite the following publication:
* E. Cereda, D. Palossi, and A. Giusti, ‘NanoCockpit: Performance-optimized Application Framework for AI-based Autonomous Nanorobotics’, arXiv pre-print ####.#####, 2025 [arXiv](https://arxiv.org/abs/####.#####).
  
```bibtex
@inproceedings{cereda2025nanocockpit,
  author={Cereda, Elia and Giusti, Alessandro and Palossi, Daniele},
  booktitle={}, 
  title={NanoCockpit: Performance-optimized Application Framework for AI-based Autonomous Nanorobotics}, 
  year={2025},
  volume={},
  number={},
  pages={},
  note={[Under review]}
}
```

## Contributors
Elia Cereda<sup>1</sup>,
Alessandro Giusti<sup>1</sup>,
and Daniele Palossi<sup>1,2</sup>.

<sup>1 </sup>Dalle Molle Institute for Artificial Intelligence (IDSIA), USI and SUPSI, Switzerland.<br>
<sup>2 </sup>Integrated Systems Laboratory (IIS) of ETH Zürich, Switzerland.<br>
