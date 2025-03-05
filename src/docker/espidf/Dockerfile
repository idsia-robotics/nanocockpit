ARG UBUNTU_VERSION
FROM ubuntu:$UBUNTU_VERSION

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get upgrade -y
RUN apt-get install -y \
    bison \
    curl \
    flex \
    gcc \
    git \
    gperf \
    libncurses-dev \
    make \
    python2 \
    python-pip \
    python-setuptools \
    wget

# Install compiler toolchain
ARG ESP_TOOLCHAIN_VERSION="1.22.0-80-g6c4433a-5.2.0"
RUN echo "ESP toolchain version: $ESP_TOOLCHAIN_VERSION"

RUN mkdir /esp && \
    cd /esp && \
    curl -O "https://dl.espressif.com/dl/xtensa-esp32-elf-linux64-$ESP_TOOLCHAIN_VERSION.tar.gz" && \
    tar -xzf "xtensa-esp32-elf-linux64-$ESP_TOOLCHAIN_VERSION.tar.gz" && \
    rm "xtensa-esp32-elf-linux64-$ESP_TOOLCHAIN_VERSION.tar.gz"

ENV PATH="/esp/xtensa-esp32-elf/bin:$PATH"

# Install ESP SDK
ARG ESP_SDK_VERSION
RUN echo "ESP SDK version: $ESP_SDK_VERSION"

RUN cd /esp && \
    git clone -b v$ESP_SDK_VERSION --recursive https://github.com/espressif/esp-idf.git

ENV IDF_PATH='/esp/esp-idf'

RUN ln -s /usr/bin/python2 /usr/bin/python && \
    pip2 install \
        virtualenv==16.7.10 \
        future==0.15.2 \
        cryptography==2.1.4 \
        pyparsing==2.2.0 \
        pyserial==3.4 && \
    pip2 install -r $IDF_PATH/requirements.txt

RUN cd $IDF_PATH && \
    ./install.sh

# Install OpenOCD
RUN apt-get -y install \
    autoconf \
    automake \
    libtool \
    libusb-1.0 \
    make \
    pkg-config \
    texinfo

ARG ESP_OPENOCD_VERSION="0.10.0-esp32-20190313"
RUN echo "ESP OpenOCD version: $ESP_OPENOCD_VERSION"

RUN cd /esp && \
    curl -OL "https://github.com/espressif/openocd-esp32/releases/download/v$ESP_OPENOCD_VERSION/openocd-esp32-linux64-$ESP_OPENOCD_VERSION.tar.gz" && \
    tar -xzf "openocd-esp32-linux64-$ESP_OPENOCD_VERSION.tar.gz" && \
    rm "openocd-esp32-linux64-$ESP_OPENOCD_VERSION.tar.gz"

ENV PATH="/esp/openocd-esp32/bin:$PATH"

RUN mkdir -p /module/data/
WORKDIR /module/data
