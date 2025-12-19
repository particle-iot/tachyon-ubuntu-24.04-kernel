#!/usr/bin/env bash
set -euo pipefail

# install.sh - Install Ubuntu 24.04 kernel to Tachyon device
# Supports both local (on-device) and remote (via ADB) installation

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PARENT_DIR="$(dirname "$SCRIPT_DIR")"

# Installation mode: "device", "adb", or "ssh"
INSTALL_MODE=""
ADB_SERIAL="${ADB_SERIAL:-}"
ADB_CMD="adb"
SSH_HOST="${SSH_HOST:-}"
SSH_PASSWORD="${SSH_PASSWORD:-}"
SSH_CMD="ssh"
SCP_CMD="scp"
AUTO_CONFIRM="no"
AUTO_REBOOT="no"
DRY_RUN="no"
FORCE="no"

# Color output (must be defined before use in argument parsing)
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

show_help() {
    cat << EOF
Usage: $0 --device <command> [packages...]                            # Run on device (requires sudo)
       $0 --adb [-s ID] <command> [packages...]                       # Run via ADB from host
       $0 --ssh --ssh-host USER@HOST [--ssh-password PWD] <command>  # Run via SSH from host

Options:
    --device              Run installation locally on the device
    --adb                 Run installation remotely via ADB from host computer
    -s, --serial ID       Specify ADB device serial (optional, uses ADB_SERIAL env var)
    --ssh                 Run installation remotely via SSH from host computer
    --ssh-host USER@HOST  SSH connection string (e.g., root@192.168.86.42)
    --ssh-password PWD    SSH password (optional, uses SSH_PASSWORD env var or key auth)
    --yes, -y             Auto-confirm installation (skip confirmation prompt)
    --reboot, -r          Automatically reboot device after installation
    --dry-run, -n         Show what would be done without doing it
    --force, -f           Force installation (pass --force-all to dpkg)

Commands:
    help            Show this help message
    check           Verify device and check for kernel packages
    install         Install kernel packages to device (default action)

Arguments:
    packages...     URLs or file paths to kernel packages (optional)
                    Supports: linux-image, linux-modules, linux-tools, etc.
                    If not specified, auto-finds packages locally

Environment Variables:
    ADB_SERIAL      ADB device serial number (alternative to --serial flag)
    SSH_HOST        SSH connection string (alternative to --ssh-host flag)
    SSH_PASSWORD    SSH password (alternative to --ssh-password flag)

Examples:
    # Auto-find and install local packages
    $0 --adb install                      # Auto-find and install via ADB
    $0 --device install                   # Auto-find and install on device
    $0 --ssh --ssh-host root@192.168.86.42 --ssh-password particle install

    # Install from URLs
    $0 --adb -s a3c44498 install \\
        https://example.com/linux-image-6.8.0-1056-particle_arm64.deb \\
        https://example.com/linux-modules-6.8.0-1056-particle_arm64.deb \\
        https://example.com/linux-tools-particle_arm64.deb

    # Install from local files
    $0 --adb install \\
        ../linux-image-6.8.0-1056-particle_arm64.deb \\
        ../linux-modules-6.8.0-1056-particle_arm64.deb

    # Check device
    $0 --adb check                        # Check device via ADB
    $0 --adb --reboot install             # Install and auto-reboot

Package Search Order (when no arguments):
    1. Parent directory (../linux-*.deb)
    2. Current directory (./linux-*.deb)
    3. /tmp directory (on device when using --device mode)

EOF
    exit 0
}

# Parse arguments
COMMAND=""
PACKAGE_ARGS=()

while [[ $# -gt 0 ]]; do
    case $1 in
        --device)
            INSTALL_MODE="device"
            shift
            ;;
        --adb)
            INSTALL_MODE="adb"
            shift
            ;;
        --serial|-s)
            ADB_SERIAL="$2"
            shift 2
            ;;
        --ssh)
            INSTALL_MODE="ssh"
            shift
            ;;
        --ssh-host)
            SSH_HOST="$2"
            shift 2
            ;;
        --ssh-password)
            SSH_PASSWORD="$2"
            shift 2
            ;;
        --yes|-y)
            AUTO_CONFIRM="yes"
            shift
            ;;
        --reboot|-r)
            AUTO_REBOOT="yes"
            shift
            ;;
        --dry-run|-n)
            DRY_RUN="yes"
            shift
            ;;
        --force|-f)
            FORCE="yes"
            shift
            ;;
        help|check|install)
            COMMAND="$1"
            shift
            ;;
        *)
            # Collect package URLs or paths as positional arguments
            PACKAGE_ARGS+=("$1")
            shift
            ;;
    esac
done

# Default to help if no mode specified
if [ -z "$INSTALL_MODE" ]; then
    COMMAND="help"
fi

# Setup ADB command with serial if specified
if [ "$INSTALL_MODE" = "adb" ] && [ -n "$ADB_SERIAL" ]; then
    ADB_CMD="adb -s $ADB_SERIAL"
fi

# Setup SSH/SCP commands with password authentication if specified
if [ "$INSTALL_MODE" = "ssh" ]; then
    if [ -z "$SSH_HOST" ]; then
        error "SSH mode requires --ssh-host parameter (e.g., root@192.168.86.42)"
    fi

    # Check if sshpass is available for password authentication
    USE_SSHPASS=false
    if [ -n "$SSH_PASSWORD" ]; then
        if command -v sshpass &> /dev/null; then
            USE_SSHPASS=true
        else
            warn "sshpass not found - install it for automatic password authentication"
            warn "  macOS: brew install hudochenkov/sshpass/sshpass"
            warn "  Linux: sudo apt-get install sshpass"
            warn "You will be prompted for the password for each SSH/SCP operation"
        fi
    fi
fi

# Helper function to execute SSH commands
ssh_exec() {
    if [ "$USE_SSHPASS" = true ]; then
        sshpass -p "$SSH_PASSWORD" ssh -o StrictHostKeyChecking=no "$SSH_HOST" "$@"
    else
        ssh -o StrictHostKeyChecking=no "$SSH_HOST" "$@"
    fi
}

# Helper function to execute SCP commands
scp_exec() {
    local src="$1"
    local dst="$2"
    if [ "$USE_SSHPASS" = true ]; then
        sshpass -p "$SSH_PASSWORD" scp -o StrictHostKeyChecking=no "$src" "$dst"
    else
        scp -o StrictHostKeyChecking=no "$src" "$dst"
    fi
}

error() {
    echo -e "${RED}ERROR: $*${NC}" >&2
    exit 1
}

info() {
    echo -e "${GREEN}INFO: $*${NC}"
}

warn() {
    echo -e "${YELLOW}WARN: $*${NC}"
}

section() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$*${NC}"
    echo -e "${BLUE}========================================${NC}"
}

# Execute command on device (locally, via ADB, or via SSH)
run_on_device() {
    if [ "$INSTALL_MODE" = "device" ]; then
        "$@"
    elif [ "$INSTALL_MODE" = "adb" ]; then
        $ADB_CMD shell "$@"
    elif [ "$INSTALL_MODE" = "ssh" ]; then
        ssh_exec "$@"
    fi
}

# Execute command on device as root (locally, via ADB, or via SSH)
run_on_device_sudo() {
    if [ "$INSTALL_MODE" = "device" ]; then
        sudo "$@"
    elif [ "$INSTALL_MODE" = "adb" ]; then
        $ADB_CMD shell "sudo $*"
    elif [ "$INSTALL_MODE" = "ssh" ]; then
        # SSH: if connecting as root, no sudo needed; otherwise use sudo
        if [[ "$SSH_HOST" == root@* ]]; then
            ssh_exec "$*"
        else
            ssh_exec "sudo $*"
        fi
    fi
}

# Push file to device (for ADB and SSH modes, no-op for device mode)
push_to_device() {
    local src="$1"
    local dst="$2"
    if [ "$INSTALL_MODE" = "adb" ]; then
        $ADB_CMD push "$src" "$dst"
    elif [ "$INSTALL_MODE" = "ssh" ]; then
        scp_exec "$src" "$SSH_HOST:$dst"
    fi
}

find_packages_local() {
    # Look for packages locally in parent or current directory
    local PACKAGES=()

    # Check parent directory first
    if ls "$PARENT_DIR"/linux-image-*.deb &> /dev/null 2>&1; then
        PACKAGES+=("$PARENT_DIR"/linux-image-*.deb)
    fi
    if ls "$PARENT_DIR"/linux-modules-*.deb &> /dev/null 2>&1; then
        PACKAGES+=("$PARENT_DIR"/linux-modules-*.deb)
    fi
    if ls "$PARENT_DIR"/linux-headers-*.deb &> /dev/null 2>&1; then
        PACKAGES+=("$PARENT_DIR"/linux-headers-*.deb)
    fi

    # Check current directory if nothing found
    if [ ${#PACKAGES[@]} -eq 0 ]; then
        if ls "$SCRIPT_DIR"/linux-image-*.deb &> /dev/null 2>&1; then
            PACKAGES+=("$SCRIPT_DIR"/linux-image-*.deb)
        fi
        if ls "$SCRIPT_DIR"/linux-modules-*.deb &> /dev/null 2>&1; then
            PACKAGES+=("$SCRIPT_DIR"/linux-modules-*.deb)
        fi
        if ls "$SCRIPT_DIR"/linux-headers-*.deb &> /dev/null 2>&1; then
            PACKAGES+=("$SCRIPT_DIR"/linux-headers-*.deb)
        fi
    fi

    echo "${PACKAGES[@]}"
}

find_packages_device() {
    # Look for packages on device in /tmp
    # Search for linux-image, linux-modules, and linux-headers
    local PACKAGES=""
    local IMAGE_PKGS=$(run_on_device "ls /tmp/linux-image-*.deb 2>/dev/null || true")
    local MODULE_PKGS=$(run_on_device "ls /tmp/linux-modules-*.deb 2>/dev/null || true")
    local HEADER_PKGS=$(run_on_device "ls /tmp/linux-headers-*.deb 2>/dev/null || true")

    if [ -n "$IMAGE_PKGS" ]; then
        PACKAGES="$IMAGE_PKGS"
    fi
    if [ -n "$MODULE_PKGS" ]; then
        if [ -n "$PACKAGES" ]; then
            PACKAGES="$PACKAGES $MODULE_PKGS"
        else
            PACKAGES="$MODULE_PKGS"
        fi
    fi
    if [ -n "$HEADER_PKGS" ]; then
        if [ -n "$PACKAGES" ]; then
            PACKAGES="$PACKAGES $HEADER_PKGS"
        else
            PACKAGES="$HEADER_PKGS"
        fi
    fi

    echo "$PACKAGES"
}

check_device() {
    section "Checking Device"

    if [ "$INSTALL_MODE" = "adb" ]; then
        # Check ADB connectivity
        if ! command -v adb &> /dev/null; then
            error "adb command not found. Please install Android Platform Tools."
        fi

        # Start ADB server
        adb start-server &> /dev/null || true

        # Check device count
        local DEVICE_COUNT=$(adb devices -l | grep -v "List of devices" | grep -c "device" || true)
        if [ "$DEVICE_COUNT" -eq 0 ]; then
            error "No devices connected. Connect device and try again."
        fi

        if [ "$DEVICE_COUNT" -gt 1 ] && [ -z "$ADB_SERIAL" ]; then
            warn "Multiple devices connected:"
            adb devices -l
            error "Please specify device serial with --serial"
        fi

        # Verify device is accessible
        if ! $ADB_CMD shell "echo test" &> /dev/null; then
            error "Cannot communicate with device"
        fi
    elif [ "$INSTALL_MODE" = "ssh" ]; then
        # Check SSH connectivity
        if ! command -v ssh &> /dev/null; then
            error "ssh command not found. Please install OpenSSH client."
        fi

        if ! command -v scp &> /dev/null; then
            error "scp command not found. Please install OpenSSH client."
        fi

        # Verify device is accessible
        if ! ssh_exec "echo test" &> /dev/null; then
            error "Cannot communicate with device via SSH. Check hostname and credentials."
        fi
    fi

    # Get device info
    local MODEL=$(run_on_device "cat /proc/device-tree/model 2>/dev/null | tr -d '\0'" || echo "Unknown")
    local KERNEL=$(run_on_device "uname -r" || echo "Unknown")
    local DISTRO=$(run_on_device "lsb_release -d 2>/dev/null | cut -f2" || echo "Unknown")

    info "Device Information:"
    info "  Model: $MODEL"
    info "  Kernel: $KERNEL"
    info "  Distribution: $DISTRO"
    if [ -n "$ADB_SERIAL" ]; then
        info "  Serial: $ADB_SERIAL"
    fi
    if [ -n "$SSH_HOST" ]; then
        info "  SSH Host: $SSH_HOST"
    fi

    # Check for packages
    section "Checking for Packages"

    if [ "$INSTALL_MODE" = "device" ]; then
        local PACKAGES=($(find_packages_device))
        if [ ${#PACKAGES[@]} -eq 0 ]; then
            warn "No kernel packages found in /tmp/"
            info "Package files should be in /tmp/linux-*.deb"
        else
            info "Found ${#PACKAGES[@]} package(s) in /tmp/"
            for pkg in "${PACKAGES[@]}"; do
                info "  - $(basename "$pkg")"
            done
        fi
    else
        local PACKAGES=($(find_packages_local))
        if [ ${#PACKAGES[@]} -eq 0 ]; then
            error "No kernel packages found. Build kernel first with ./build.sh"
        fi
        info "Found ${#PACKAGES[@]} package(s) to install:"
        for pkg in "${PACKAGES[@]}"; do
            info "  - $(basename "$pkg")"
        done
    fi

    info "Device is ready for installation"
}

install_packages() {
    section "Installing Kernel Packages"

    local PACKAGES=()
    local TMP_DOWNLOAD_DIR=""

    # If package arguments were provided (URLs or paths), use them
    if [ ${#PACKAGE_ARGS[@]} -gt 0 ]; then
        info "Using provided package URLs/paths..."

        # Create temp directory for downloads
        if [ "$INSTALL_MODE" = "device" ]; then
            TMP_DOWNLOAD_DIR=$(mktemp -d)
        else
            TMP_DOWNLOAD_DIR=$(mktemp -d)
        fi

        # Download or copy each package
        local idx=0
        for pkg_input in "${PACKAGE_ARGS[@]}"; do
            idx=$((idx + 1))
            local pkg_name="package${idx}.deb"

            # Determine the package name from URL if possible
            if [[ "$pkg_input" =~ \.deb$ ]]; then
                pkg_name=$(basename "$pkg_input")
            fi

            if [[ "$pkg_input" =~ ^https?:// ]]; then
                info "Downloading: $(basename "$pkg_input")..."
                curl -L -o "$TMP_DOWNLOAD_DIR/$pkg_name" "$pkg_input" || error "Failed to download $pkg_input"

                # Validate downloaded file
                local file_size=$(stat -f%z "$TMP_DOWNLOAD_DIR/$pkg_name" 2>/dev/null || stat -c%s "$TMP_DOWNLOAD_DIR/$pkg_name" 2>/dev/null)
                if [ "$file_size" -lt 1000 ]; then
                    error "Downloaded file is too small (${file_size} bytes) - likely an error page. Check URL: $pkg_input"
                fi

                # Check if it's a valid .deb file
                if ! file "$TMP_DOWNLOAD_DIR/$pkg_name" 2>/dev/null | grep -q "Debian"; then
                    error "Downloaded file is not a valid Debian package. Check URL: $pkg_input"
                fi
            elif [ -f "$pkg_input" ]; then
                info "Copying: $(basename "$pkg_input")..."
                cp "$pkg_input" "$TMP_DOWNLOAD_DIR/$pkg_name"

                # Validate copied file
                if ! file "$pkg_input" 2>/dev/null | grep -q "Debian"; then
                    error "File is not a valid Debian package: $pkg_input"
                fi
            else
                error "Package not found: $pkg_input"
            fi

            PACKAGES+=("$TMP_DOWNLOAD_DIR/$pkg_name")
        done
    else
        # Auto-find packages (original behavior)
        if [ "$INSTALL_MODE" = "device" ]; then
            # On device, look for packages in /tmp
            PACKAGES=($(find_packages_device))
            if [ ${#PACKAGES[@]} -eq 0 ]; then
                error "No kernel packages found in /tmp/. Copy packages to /tmp/ first."
            fi
        else
            # Via ADB, look for packages locally and push them
            PACKAGES=($(find_packages_local))
            if [ ${#PACKAGES[@]} -eq 0 ]; then
                error "No kernel packages found. Build kernel first with ./build.sh"
            fi
        fi
    fi

    info "Packages to install:"
    for pkg in "${PACKAGES[@]}"; do
        info "  - $(basename "$pkg")"
    done
    echo ""

    # Confirmation prompt
    if [ "$AUTO_CONFIRM" != "yes" ]; then
        echo -n "Install kernel packages? [y/N] "
        read -r response
        if [[ ! "$response" =~ ^[Yy]$ ]]; then
            info "Installation cancelled"
            exit 0
        fi
    fi

    if [ "$DRY_RUN" = "yes" ]; then
        warn "DRY RUN: Would perform the following actions:"
        if [ "$INSTALL_MODE" = "adb" ] || [ "$INSTALL_MODE" = "ssh" ]; then
            warn "  1. Push ${#PACKAGES[@]} packages to /tmp/"
        fi
        warn "  2. Install packages with dpkg -i"
        if [ "$AUTO_REBOOT" = "yes" ]; then
            warn "  3. Reboot device"
        fi
        return 0
    fi

    # Push packages if using ADB or SSH
    if [ "$INSTALL_MODE" = "adb" ] || [ "$INSTALL_MODE" = "ssh" ]; then
        info "Pushing packages to device..."
        for pkg in "${PACKAGES[@]}"; do
            info "  Pushing $(basename "$pkg")..."
            push_to_device "$pkg" /tmp/ || error "Failed to push $(basename "$pkg")"
        done
        echo ""
    fi

    # Build dpkg command
    local PKG_NAMES=""
    for pkg in "${PACKAGES[@]}"; do
        PKG_NAMES="$PKG_NAMES /tmp/$(basename "$pkg")"
    done

    local DPKG_CMD="dpkg -i $PKG_NAMES"
    if [ "$FORCE" = "yes" ]; then
        DPKG_CMD="dpkg -i --force-all $PKG_NAMES"
        warn "Using --force-all with dpkg"
    fi

    # Install packages
    info "Installing packages..."
    if ! run_on_device_sudo $DPKG_CMD; then
        error "Package installation failed"
    fi

    info "Packages installed successfully!"
    echo ""

    # Show installed kernel version
    local NEW_KERNEL=$(run_on_device "dpkg -l | grep linux-image" | tail -1 || echo "Unknown")
    info "Installed kernel: $NEW_KERNEL"

    # Cleanup
    if [ "$INSTALL_MODE" = "adb" ] || [ "$INSTALL_MODE" = "ssh" ]; then
        info "Cleaning up temporary files on device..."
        for pkg in "${PACKAGES[@]}"; do
            run_on_device "rm -f /tmp/$(basename "$pkg")" || true
        done
    fi

    # Cleanup local temp directory if we downloaded packages
    if [ -n "$TMP_DOWNLOAD_DIR" ] && [ -d "$TMP_DOWNLOAD_DIR" ]; then
        info "Cleaning up downloaded packages..."
        rm -rf "$TMP_DOWNLOAD_DIR" || true
    fi
}

reboot_device() {
    if [ "$AUTO_REBOOT" = "yes" ]; then
        section "Rebooting Device"
        info "Rebooting device..."

        if [ "$INSTALL_MODE" = "device" ]; then
            sudo reboot || warn "Reboot command failed"
        elif [ "$INSTALL_MODE" = "adb" ]; then
            $ADB_CMD reboot || warn "Reboot command failed, device may already be rebooting"
        elif [ "$INSTALL_MODE" = "ssh" ]; then
            if [[ "$SSH_HOST" == root@* ]]; then
                ssh_exec "reboot" || warn "Reboot command failed, device may already be rebooting"
            else
                ssh_exec "sudo reboot" || warn "Reboot command failed, device may already be rebooting"
            fi
        fi

        info "Device is rebooting..."
    else
        echo ""
        warn "Installation complete. Reboot device to use new kernel:"
        if [ "$INSTALL_MODE" = "device" ]; then
            warn "  sudo reboot"
        elif [ "$INSTALL_MODE" = "adb" ]; then
            warn "  $ADB_CMD reboot"
        elif [ "$INSTALL_MODE" = "ssh" ]; then
            if [[ "$SSH_HOST" == root@* ]]; then
                warn "  ssh $SSH_HOST 'reboot'"
            else
                warn "  ssh $SSH_HOST 'sudo reboot'"
            fi
        fi
    fi
}

main() {
    # Default command is install
    if [ -z "$COMMAND" ]; then
        COMMAND="install"
    fi

    # Show help
    if [ "$COMMAND" = "help" ]; then
        show_help
    fi

    section "Tachyon Kernel Installation"
    info "Mode: $INSTALL_MODE"

    # Execute command
    case "$COMMAND" in
        check)
            check_device
            ;;
        install)
            check_device
            install_packages
            reboot_device
            ;;
        *)
            error "Unknown command: $COMMAND"
            ;;
    esac

    echo ""
    section "Installation Complete"
    if [ "$DRY_RUN" != "yes" ]; then
        info "Kernel packages installed successfully"
        if [ "$AUTO_REBOOT" = "yes" ]; then
            info "Device is rebooting with new kernel"
        else
            info "Reboot device to use new kernel"
        fi
    fi
}

# Check if script is being sourced or executed
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
    main "$@"
fi
