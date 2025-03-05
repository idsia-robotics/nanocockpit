# syntax = docker/dockerfile:1.3

ARG UBUNTU_VERSION

### BUILD GAP8 TOOLCHAIN ###
FROM ubuntu:$UBUNTU_VERSION AS toolchain-build

ARG DEBIAN_FRONTEND=noninteractive

# Install GAP toolchain dependencies
RUN apt-get update && apt-get install -y \
    autoconf \
    automake \
    autotools-dev \
    curl \
    libmpc-dev \
    libmpfr-dev \
    libgmp-dev \
    gawk \
    git \
    build-essential \
    bison \
    flex \
    texinfo \
    gperf \
    libtool \
    patchutils \
    bc \
    zlib1g-dev \
    # Extra
    jq

# Download GitHub SSH host keys
RUN mkdir -p /root/.ssh && \
    curl -L https://api.github.com/meta | jq -r '.ssh_keys | .[]' | sed -e 's/^/github.com /' > /root/.ssh/known_hosts

RUN --mount=type=ssh \
    git clone --no-checkout git@github.com:GreenWaves-Technologies/gap-riscv-gnu-toolchain.git && \
    cd gap-riscv-gnu-toolchain && \
    git remote add idsia git@github.com:idsia-robotics/gap-riscv-gnu-toolchain.git

ARG GAP_TOOLCHAIN_VERSION
RUN --mount=type=ssh \
    echo GAP toolchain version: $GAP_TOOLCHAIN_VERSION && \
    cd gap-riscv-gnu-toolchain && \
    git fetch --all --tags --force && \
    git checkout tags/release-v${GAP_TOOLCHAIN_VERSION}

# ADD patches/toolchain /tmp/patches/toolchain
# RUN cd gap-riscv-gnu-toolchain && \
#     git update-index --refresh && \
#     git apply -3 /tmp/patches/toolchain/*.patch && \
#     rm -rf /tmp/patches

RUN  --mount=type=ssh \
    cd gap-riscv-gnu-toolchain && \
    git submodule update --init --recursive

RUN cd gap-riscv-gnu-toolchain && \
    make -f Makefile.gap package_ubuntu -j$(nproc)

FROM ubuntu:$UBUNTU_VERSION AS toolchain

# Only publish the built tarball to the container registry
COPY --from=toolchain-build /gap-riscv-gnu-toolchain/gap-gnu-toolchain-ubuntu-stripped.tar.gz /gap-riscv-gnu-toolchain/
