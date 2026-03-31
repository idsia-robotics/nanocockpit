# ESP-IDF container image
# Source: https://docs.espressif.com/projects/esp-idf/en/v5.3.3/esp32/get-started/index.html

ARG UBUNTU_VERSION=22.04
FROM ubuntu:$UBUNTU_VERSION

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get upgrade -y
RUN apt-get install -y \
    bison \
    ccache \
    cmake \
    dfu-util \
    flex \
    git \
    gperf \
    libffi-dev \
    libssl-dev \
    libusb-1.0-0 \
    ninja-build \
    python3 \
    python3-pip \
    python3-setuptools \
    python3-venv \
    wget

ARG ESP_IDF_VERSION
RUN echo "ESP IDF version: $ESP_IDF_VERSION"
RUN mkdir /esp && \
    cd /esp && \
    git clone -b v$ESP_IDF_VERSION --recursive https://github.com/espressif/esp-idf.git

ENV IDF_TOOLS_PATH="/esp/idf_tools"

RUN cd /esp/esp-idf && \
    ./install.sh esp32

RUN mkdir -p /module/data/
WORKDIR /module/data
