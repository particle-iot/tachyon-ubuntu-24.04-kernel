#!/bin/bash

set -e

DIR=$(realpath "$(dirname "${BASH_SOURCE[0]}")/..")

cd "$DIR"

export DEBIAN_FRONTEND=noninteractive

# Install kernel-specific build dependencies
# Note: Base packages (gcc, devscripts, etc.) are pre-installed in Docker image
# Only mk-build-deps is needed to install kernel-specific dependencies
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

# Apply CI-fast config for non-release builds
# - RELEASE builds (tags): Full debug symbols and sanitizers enabled
# - PR/branch builds: Fast build with debug info and sanitizers disabled
if [ -z "$CIRCLE_TAG" ]; then
    echo "========================================="
    echo "CI-FAST BUILD MODE ENABLED"
    echo "  Build type: PR or branch build"
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
    echo "  Build type: Release (tag: $CIRCLE_TAG)"
    echo "  Including: Full debug symbols, sanitizers"
    echo "========================================="
fi

debian/rules binary-particle meta-particle binary-indep binary-perarch

mkdir $DIR/debs
cp ../*.deb $DIR/debs/

exit 0

