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

# Apply CI-fast config for PR builds (not for tags or main branch)
# This disables debug info and sanitizers for faster compilation
if [ -n "$CIRCLE_PULL_REQUEST" ] || [ -n "$CI_FAST_BUILD" ]; then
    echo "========================================="
    echo "CI-FAST BUILD MODE ENABLED"
    echo "  Reason: PR build detected"
    echo "  Disabling: DEBUG_INFO, KASAN, UBSAN"
    echo "  Expected: 40-50% faster build"
    echo "========================================="

    # Find all generated .config files and apply overrides
    for config in debian/build/build-*/.config; do
        if [ -f "$config" ]; then
            echo "Applying CI-fast overrides to: $config"
            $DIR/ci-scripts/apply-ci-fast-config.sh "$config"
        fi
    done
else
    echo "========================================="
    echo "FULL DEBUG BUILD MODE"
    echo "  Reason: Tag or main branch build"
    echo "  Including: Full debug symbols, sanitizers"
    echo "========================================="
fi

debian/rules binary-particle meta-particle binary-indep binary-perarch

mkdir $DIR/debs
cp ../*.deb $DIR/debs/

exit 0

