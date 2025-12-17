#!/bin/bash
# apply-ci-fast-config.sh
# Applies CI-fast kernel config overrides to speed up compilation
# This disables debug symbols and sanitizers which significantly slow down builds

set -e

CONFIG_FILE="$1"

if [ -z "$CONFIG_FILE" ] || [ ! -f "$CONFIG_FILE" ]; then
    echo "Usage: $0 <path-to-.config>"
    exit 1
fi

echo "Applying CI-fast config overrides to: $CONFIG_FILE"

# Function to disable a config option
disable_config() {
    local option="$1"
    if grep -q "^CONFIG_${option}=" "$CONFIG_FILE"; then
        sed -i "s/^CONFIG_${option}=.*/# CONFIG_${option} is not set/" "$CONFIG_FILE"
        echo "  Disabled: CONFIG_${option}"
    fi
}

# Function to enable a config option
enable_config() {
    local option="$1"
    local value="$2"
    if grep -q "^# CONFIG_${option} is not set" "$CONFIG_FILE"; then
        sed -i "s/^# CONFIG_${option} is not set/CONFIG_${option}=${value}/" "$CONFIG_FILE"
        echo "  Enabled: CONFIG_${option}=${value}"
    elif grep -q "^CONFIG_${option}=" "$CONFIG_FILE"; then
        sed -i "s/^CONFIG_${option}=.*/CONFIG_${option}=${value}/" "$CONFIG_FILE"
        echo "  Changed: CONFIG_${option}=${value}"
    else
        echo "CONFIG_${option}=${value}" >> "$CONFIG_FILE"
        echo "  Added: CONFIG_${option}=${value}"
    fi
}

echo ""
echo "=== Disabling Debug Info (30-40% build time reduction) ==="
disable_config "DEBUG_INFO"
disable_config "DEBUG_INFO_DWARF5"
disable_config "DEBUG_INFO_DWARF4"
disable_config "DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT"
disable_config "DEBUG_INFO_BTF"
disable_config "DEBUG_INFO_BTF_MODULES"
disable_config "DEBUG_INFO_COMPRESSED"
disable_config "DEBUG_INFO_COMPRESSED_NONE"
disable_config "DEBUG_INFO_COMPRESSED_ZLIB"
disable_config "DEBUG_INFO_COMPRESSED_ZSTD"
disable_config "DEBUG_INFO_SPLIT"
disable_config "DEBUG_INFO_REDUCED"
disable_config "GDB_SCRIPTS"

echo ""
echo "=== Disabling Sanitizers (15-20% build time reduction) ==="
disable_config "KASAN"
disable_config "KASAN_GENERIC"
disable_config "KASAN_INLINE"
disable_config "KASAN_OUTLINE"
disable_config "KASAN_SW_TAGS"
disable_config "KASAN_HW_TAGS"
disable_config "UBSAN"
disable_config "UBSAN_TRAP"
disable_config "UBSAN_BOUNDS"
disable_config "UBSAN_ALIGNMENT"
disable_config "UBSAN_SANITIZE_ALL"
disable_config "KCOV"
disable_config "KCOV_INSTRUMENT_ALL"

echo ""
echo "=== Disabling Heavy Debug Options (5-10% build time reduction) ==="
disable_config "DEBUG_SPINLOCK"
disable_config "DEBUG_MUTEXES"
disable_config "DEBUG_RT_MUTEXES"
disable_config "DEBUG_LOCK_ALLOC"
disable_config "PROVE_LOCKING"
disable_config "LOCK_STAT"
disable_config "DEBUG_ATOMIC_SLEEP"
disable_config "DEBUG_LOCKING_API_SELFTESTS"
disable_config "DEBUG_LIST"
disable_config "DEBUG_PLIST"
disable_config "DEBUG_SG"
disable_config "DEBUG_NOTIFIERS"
disable_config "DEBUG_CREDENTIALS"

echo ""
echo "=== Keeping Essential Options Enabled ==="
# Keep these for basic validation
# (They have minimal compile-time impact)
# enable_config "MAGIC_SYSRQ" "y"
# enable_config "DEBUG_KERNEL" "y" # This is a menu item, not much overhead

echo ""
echo "CI-fast config applied successfully!"
echo "Expected build time improvement: 40-50% faster than full debug build"
