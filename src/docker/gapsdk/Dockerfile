# syntax = docker/dockerfile:1.3

## File-level parameters used in _any_ FROM clause
ARG UBUNTU_VERSION
ARG GAP_TOOLCHAIN_VERSION

# GAP toolchain containter
FROM registry.gitlab.com/eliacereda/gap-riscv-gnu-toolchain:$UBUNTU_VERSION-$GAP_TOOLCHAIN_VERSION AS toolchain

### BUILD GAP8 SDK ###
FROM ubuntu:$UBUNTU_VERSION AS sdk

ARG DEBIAN_FRONTEND=noninteractive

# Copy and install the GAP8 toolchain built in the previous stage
COPY --from=toolchain /gap-riscv-gnu-toolchain/gap-gnu-toolchain-ubuntu-stripped.tar.gz /tmp

RUN cd /usr/lib && \
    tar -xvf /tmp/gap-gnu-toolchain-ubuntu-stripped.tar.gz && \
    rm /tmp/gap-gnu-toolchain-ubuntu-stripped.tar.gz && \
    mv gap-gnu-toolchain-ubuntu-stripped gap_riscv_toolchain

# Install GAP SDK dependencies
RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    build-essential \
    cmake \
    curl \
    doxygen \
    git \
    gtkwave \
    libftdi-dev \
    libgraphicsmagick++1-dev \
    libsdl2-dev \
    libsdl2-ttf-dev \
    libsndfile1-dev \
    libtool \
    libusb-1.0-0-dev \
    nano \
    pkg-config \
    python3-pip \
    rsync \
    scons \
    texinfo \
    wget \
    \
    # gap_tools dependencies
    libopencv-dev \
    python3-opencv \
    \
    # Extra
    jq \
    nano

# Download GitHub SSH host keys
RUN mkdir -p /root/.ssh && \
    curl -L https://api.github.com/meta | jq -r '.ssh_keys | .[]' | sed -e 's/^/github.com /' > /root/.ssh/known_hosts

# Install GAP SDK
RUN --mount=type=ssh \
    git clone git@github.com:GreenWaves-Technologies/gap_sdk.git --no-checkout && \
    cd gap_sdk && \
    git remote add idsia git@github.com:idsia-robotics/gap_sdk.git

# Checkout the desired version
ARG GAP_SDK_VERSION
RUN --mount=type=ssh \
    echo GAP SDK version: $GAP_SDK_VERSION && \
    cd gap_sdk && \
    git fetch --all --tags --force && \
    git checkout tags/release-v${GAP_SDK_VERSION} && \
    git submodule update --init --recursive

# Patches GAP SDK for ubuntu22.04 and aarch64 support
ADD scripts /tmp/scripts
ADD patches/gapsdk /tmp/patches/gapsdk
RUN cd gap_sdk && \
    bash /tmp/scripts/apply_patches.sh && \
    rm -rf /tmp/patches

RUN cd gap_sdk && \
    pip3 install -U pip && \
    pip3 install -r requirements.txt

# Build the GAP SDK for all comma-separated configs listed in GAP_CONFIGS
ARG GAP_CONFIGS
RUN echo GAP configs: $GAP_CONFIGS && \
    cd gap_sdk && \
    bash /tmp/scripts/build.sh && \
    rm -rf /tmp/scripts

# TODO: not tested on aarch64, if enabled should be moved inside build.sh
# RUN pip3 install tensorflow==1.15.2
# RUN pip3 install h5py==2.10.0
# RUN cd gap_sdk/tools/nntool && pip3 install -r requirements.txt
# RUN /bin/bash -c "cd /gap_sdk && source configs/$GAP_CONFIG.sh && make nntool"

# Install extra command line utilities
# RUN apt-get update && apt-get install -y \
#     nano

RUN mkdir -p /module/data/
WORKDIR /module/data/
