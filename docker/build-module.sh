#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   bash docker/build-module.sh <module-dir>
# Example:
#   bash docker/build-module.sh sound/soc/qcom/qdsp6


MODDIR="${1-}"
if [[ -z "$MODDIR" ]]; then
	echo "Usage: bash docker/build-module.sh <module-dir>" >&2
	exit 2
fi
if [[ ! -d "$MODDIR" ]]; then
	echo "Error: module dir not found: $MODDIR" >&2
	exit 1
fi

OUTDIR="${OUTDIR:-build-tachyon/build}"
mkdir -p "$OUTDIR"

# dpkg-architecture prints shell exports; we intentionally export them.
export $(dpkg-architecture -aarm64)
export CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
export MAKEFLAGS="${MAKEFLAGS:--j$(nproc)}"

export CC="ccache ${CROSS_COMPILE}gcc"
export CXX="ccache ${CROSS_COMPILE}g++"
export CCACHE_DIR="${HOME}/.ccache"
ccache --max-size 10G >/dev/null 2>&1 || true

if [[ ! -f "$OUTDIR/.config" ]]; then
	echo "Error: missing $OUTDIR/.config (run: bash docker/build-kernel.sh cleanbuild)" >&2
	exit 1
fi

# Prepare step only if needed (keeps incremental builds fast)
# autoconf.h is a good indicator that 'prepare' has run for this output dir.
if [[ ! -f "$OUTDIR/include/generated/autoconf.h" ]]; then
	echo "Preparing build directory: $OUTDIR"
	make O="$OUTDIR" ARCH=arm64 CROSS_COMPILE="$CROSS_COMPILE" olddefconfig
	make O="$OUTDIR" ARCH=arm64 CROSS_COMPILE="$CROSS_COMPILE" prepare modules_prepare
fi

make O="$OUTDIR" ARCH=arm64 CROSS_COMPILE="$CROSS_COMPILE" M="$MODDIR" modules
find "$OUTDIR/$MODDIR" -maxdepth 1 -type f -name "*.ko" -print || true
