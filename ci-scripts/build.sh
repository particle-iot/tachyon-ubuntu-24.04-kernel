#!/bin/bash

set -e

DIR=$(realpath "$(dirname "${BASH_SOURCE[0]}")/..")

cd "$DIR"

export DEBIAN_FRONTEND=noninteractive

# install dependencies
apt-get update -y
apt-get upgrade -y
apt-get install -y devscripts equivs gcc-aarch64-linux-gnu git sudo
debian/rules clean
mk-build-deps --install --remove --root-cmd sudo -t 'apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends --yes'
git config --global --add safe.directory $DIR
git clean -fd

# build
export $(dpkg-architecture -aarm64)
export CROSS_COMPILE=aarch64-linux-gnu-
export MAKEFLAGS="-j$(nproc)"

debian/rules clean
debian/rules updateconfigs
debian/rules binary-qcom

mkdir $DIR/debs
cp ../*.deb $DIR/debs/

exit 0

