#!/usr/bin/env bash
set -e

OUTDIR=build-tachyon/build
mkdir -p "$OUTDIR"

MODE=${1-}
if [ "$MODE" = "cleanbuild" ]; then
    shift
fi

export $(dpkg-architecture -aarm64)
export CROSS_COMPILE=aarch64-linux-gnu-
export MAKEFLAGS="-j$(nproc)"

# Enable ccache to wrap the compiler
export CC="ccache ${CROSS_COMPILE}gcc"
export CXX="ccache ${CROSS_COMPILE}g++"
export CCACHE_DIR="${HOME}/.ccache"
ccache --max-size 10G || true

# If requested (cleanbuild) or missing output config, regenerate it.
# Note: scripts/config does not honor KCONFIG_CONFIG, so we must pass --file.
if [ "$MODE" = "cleanbuild" ] || [ ! -f "$OUTDIR/.config" ]; then
    # Clean build artifacts without deleting source changes
    make ARCH=arm64 mrproper

    rm -rf CONFIGS/arm64-config.flavour.qcom

    debian/rules clean
    debian/rules genconfigs

    if [ -f "CONFIGS/arm64-config.flavour.qcom" ]; then
        echo "Generated CONFIGS/arm64-config.flavour.qcom, copy to $OUTDIR/.config"
        cp CONFIGS/arm64-config.flavour.qcom "$OUTDIR/.config"
    else
        echo "Error: CONFIGS/arm64-config.flavour.qcom not found!"
        exit 1
    fi

    # FIXME: remove this
    # Ensure source tree stays clean for out-of-tree build.
    rm -f .config
fi

scripts/config --file "$OUTDIR/.config" --set-str CONFIG_SYSTEM_TRUSTED_KEYS ""
scripts/config --file "$OUTDIR/.config" --set-str CONFIG_SYSTEM_REVOCATION_KEYS ""

OUTPUT_DIR="docker/build"
DEB_DIR="$(cd "${OUTDIR}/.." && pwd)"
mkdir -p "${OUTPUT_DIR}"
BUILD_STAMP="${OUTPUT_DIR}/.deb-build-start"
touch "${BUILD_STAMP}"

make O="$OUTDIR" ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE}" olddefconfig
make O="$OUTDIR" ARCH=arm64 CROSS_COMPILE="${CROSS_COMPILE}" bindeb-pkg

mapfile -t debs < <(find "${DEB_DIR}" -maxdepth 1 -type f -name 'linux-image-*.deb' -newer "${BUILD_STAMP}" -print)
if [ ${#debs[@]} -eq 0 ]; then
    echo "Error: no new linux-image debs found in ${DEB_DIR}." >&2
    exit 1
fi
cp -f "${debs[@]}" "${OUTPUT_DIR}/"
rm -f "${BUILD_STAMP}"
