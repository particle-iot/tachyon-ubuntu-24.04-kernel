FROM ubuntu:24.04 AS base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt update && apt install -y \
    curl dkms build-essential bc ca-certificates gawk xz-utils cpio rsync \
    bison flex libssl-dev libelf-dev dwarves llvm clang lld rustc cargo \
    git wget fakeroot devscripts debhelper libncurses-dev libudev-dev \
    libpci-dev libiberty-dev python3 python3-pip unzip locales elfutils dwarfdump \
    gcc gcc-13 g++ g++-13 gcc-aarch64-linux-gnu gcc-13-aarch64-linux-gnu \
    gcc-arm-linux-gnueabihf gcc-13-arm-linux-gnueabihf gcc-powerpc64le-linux-gnu \
    gcc-13-powerpc64le-linux-gnu gcc-riscv64-linux-gnu gcc-13-riscv64-linux-gnu \
    gcc-s390x-linux-gnu gcc-13-s390x-linux-gnu gcc-x86-64-linux-gnu gcc-13-x86-64-linux-gnu


# Stage 2
FROM base AS setup


RUN apt install -y \
    ccache rustc cargo rust-src bindgen-0.65 \
    && rm -rf /var/lib/apt/lists/*

RUN locale-gen en_US.UTF-8 && update-locale LANG=en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

# Use default account with UID/GID matching host user
RUN echo "ubuntu:ubuntu" | chpasswd

USER ubuntu

WORKDIR /home/ubuntu/workspace

CMD ["/bin/bash"]
