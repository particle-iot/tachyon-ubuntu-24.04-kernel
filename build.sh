#!/bin/bash
#
# build.sh - Build Debian kernel packages for Tachyon Ubuntu 24.04
# This script builds kernel .deb packages inside a Docker container
#
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_DIR="$(basename "$SCRIPT_DIR")"

# Display usage information
show_help() {
    cat << EOF
========================================
Tachyon Ubuntu 24.04 Kernel Build
========================================

Usage: $0 [COMMAND] [OPTIONS]

COMMANDS:
  help, --help, -h              Show this help message
  clean                         Clean build artifacts and exit
  docker-build                  Build/rebuild the Docker image with dependencies
  (no command)                  Build kernel packages (default)

ENVIRONMENT VARIABLES:
  CLEAN_BUILD         Clean before building (default: false for host, true for Docker)
                      Set to "true" to force clean build
                      Example: CLEAN_BUILD=true $0

  JOBS                Number of parallel build jobs (default: 16 for host, nproc for Docker)
                      Example: JOBS=8 $0

  SKIP_SOURCE_CHECK   Skip source tree cleanliness check (default: true)
                      Set to "false" to enable strict checking
                      Example: SKIP_SOURCE_CHECK=false $0

EXAMPLES:
  $0                           # Build kernel (incremental)
  $0 clean                     # Clean build artifacts only
  $0 help                      # Show this help
  CLEAN_BUILD=true $0          # Force clean build
  JOBS=8 $0                    # Build with 8 parallel jobs
  CLEAN_BUILD=true JOBS=8 $0   # Clean build with 8 jobs

NOTES:
  - Build takes 1-2 hours depending on your machine
  - Requires Docker Desktop for Mac to be running
  - Output .deb files will be in parent directory
  - Incremental builds are faster but may have stale artifacts

TO INSTALL:
  After building, use ./install.sh to install packages:
    ./install.sh --adb install             # Install via ADB
    ./install.sh --device install          # Install on device

EOF
}

# Handle clean command
do_clean() {
    echo "=========================================="
    echo "Cleaning Kernel Build Artifacts"
    echo "=========================================="
    echo ""

    if [ -f /.dockerenv ]; then
        echo "Cleaning inside Docker container..."
        echo ""

        # Install minimal dependencies for clean
        SUDO_CMD=""
        if [ "$(id -u)" -ne 0 ] && command -v sudo &> /dev/null; then
            SUDO_CMD="sudo"
        fi

        $SUDO_CMD apt-get update -qq
        $SUDO_CMD env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
            build-essential fakeroot dpkg-dev bc kmod debhelper gawk > /dev/null

        echo "Running debian/rules clean..."
        fakeroot debian/rules clean 2>&1 | grep -v "dpkg-source: warning" | grep -v "debconf: delaying" || true

        echo "Running make mrproper..."
        make ARCH=arm64 mrproper 2>/dev/null || true

        echo ""
        echo "Removing generated .deb files..."
        rm -fv ../*.deb ../*.buildinfo ../*.changes || true

        echo ""
        echo "Clean completed successfully!"
    else
        # Running on host - use Docker to clean
        echo "Launching Docker container for clean..."
        echo ""

        if ! command -v docker &> /dev/null; then
            echo "ERROR: Docker is not installed or not in PATH"
            exit 1
        fi

        if ! docker info &> /dev/null; then
            echo "ERROR: Docker daemon is not running"
            exit 1
        fi

        PARENT_DIR="$(dirname "$SCRIPT_DIR")"

        docker run --rm \
            -v "$SCRIPT_DIR:/build" \
            -v "$PARENT_DIR:/output" \
            -w /build \
            --platform linux/arm64 \
            ubuntu:24.04 \
            bash -c './build.sh clean'

        echo ""
        echo "Clean completed successfully!"
    fi

    echo "=========================================="
    exit 0
}

# Parse command-line arguments
COMMAND=""

while [[ $# -gt 0 ]]; do
    case $1 in
        help|--help|-h)
            show_help
            exit 0
            ;;
        clean)
            COMMAND="clean"
            shift
            ;;
        docker-build)
            COMMAND="docker-build"
            shift
            ;;
        *)
            echo "ERROR: Unknown argument: $1"
            echo ""
            show_help
            exit 1
            ;;
    esac
done

# Execute command
case "$COMMAND" in
    clean)
        do_clean
        ;;
    docker-build)
        echo "=========================================="
        echo "Building Docker Image"
        echo "=========================================="
        echo ""
        echo "Building tachyon-kernel-builder:latest with all dependencies..."
        echo "This will take a few minutes."
        echo ""
        if docker build --platform linux/arm64 -t tachyon-kernel-builder:latest "$SCRIPT_DIR"; then
            echo ""
            echo "=========================================="
            echo "Docker image built successfully!"
            echo "=========================================="
            echo ""
            echo "You can now run ./build.sh to build the kernel"
        else
            echo ""
            echo "ERROR: Failed to build Docker image"
            exit 1
        fi
        exit 0
        ;;
    "")
        # No command - proceed with build
        ;;
esac

echo "=========================================="
echo "Tachyon Ubuntu 24.04 Kernel Build"
echo "=========================================="
echo ""
echo "Building kernel packages with Q6 ASM 5.4 API compatibility patch..."
echo "This will take a while (1-2 hours depending on your machine)"
echo ""

# Check if running inside Docker
if [ -f /.dockerenv ]; then
    echo "Running inside Docker container..."
    echo ""

    # Check if dependencies are already installed (custom image)
    if ! command -v dpkg-buildpackage &> /dev/null; then
        # Install build dependencies (only if not using custom image)
        echo "Installing build dependencies..."

        # Detect if sudo is needed (when not running as root)
        SUDO_CMD=""
        if [ "$(id -u)" -ne 0 ] && command -v sudo &> /dev/null; then
            SUDO_CMD="sudo"
        fi

        $SUDO_CMD apt-get update -qq
        $SUDO_CMD env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
            build-essential \
            fakeroot \
            dpkg-dev \
            bc \
            kmod \
            cpio \
            flex \
            bison \
            libssl-dev \
            libelf-dev \
            dwarves \
            rsync \
            git \
            zstd \
            debhelper \
            kernel-wedge \
            python3 \
            gawk \
            libcap-dev \
            libnewt-dev \
            libiberty-dev \
            default-jdk-headless \
            java-common \
            libdw-dev \
            libpci-dev \
            pkg-config \
            python3-dev \
            python3-setuptools \
            libunwind-dev \
            liblzma-dev \
            libaudit-dev \
            libudev-dev \
            uuid-dev \
            libnuma-dev \
            libtraceevent-dev \
            libtracefs-dev \
            dkms \
            clang-18 \
            rustc \
            rust-src \
            rustfmt \
            bindgen-0.65 \
            xmlto \
            sharutils \
            asciidoc \
            python3-docutils \
            > /dev/null
        echo ""
    else
        echo "Using pre-built Docker image with dependencies"
        echo ""
    fi

    echo "Git status:"
    git status --short sound/soc/qcom/qdsp6/ || true
    echo ""

    # Clean previous build artifacts (conditional based on CLEAN_BUILD)
    CLEAN_BUILD="${CLEAN_BUILD:-true}"
    if [ "$CLEAN_BUILD" = "true" ]; then
        echo "Cleaning previous build artifacts..."
        fakeroot debian/rules clean 2>/dev/null || true
        make ARCH=arm64 mrproper 2>/dev/null || true
        echo ""
    else
        echo "Skipping clean (CLEAN_BUILD=false - incremental build)"
        echo ""
    fi

    # Start the build
    echo "Starting kernel package build for ARM64..."
    echo "Build started at: $(date)"
    echo ""

    # Get number of parallel jobs
    JOBS="${JOBS:-$(nproc)}"

    # Determine if we should use -nc flag for incremental build
    NC_FLAG=""
    if [ "$CLEAN_BUILD" = "false" ]; then
        NC_FLAG="-nc"
        echo "Using incremental build (skipping dpkg clean phase)"
    fi

    # Build packages - native ARM64 build (not cross-compiling)
    # Skip source tree cleanliness check since Docker/git interaction causes false positives
    SKIP_SOURCE_CHECK="${SKIP_SOURCE_CHECK:-true}"
    if [ "$SKIP_SOURCE_CHECK" = "true" ]; then
        echo "Skipping source tree cleanliness check"
        export SKIP_SOURCE_CHECK=1
        MAKEFLAGS="-j${JOBS}" dpkg-buildpackage $NC_FLAG -us -uc -b -d
    else
        echo "Enabling source tree cleanliness check"
        unset SKIP_SOURCE_CHECK
        MAKEFLAGS="-j${JOBS}" dpkg-buildpackage $NC_FLAG -us -uc -b
    fi

    BUILD_EXIT=$?

    echo ""
    echo "=========================================="
    if [ $BUILD_EXIT -eq 0 ]; then
        echo "Build completed successfully!"
        echo "Build finished at: $(date)"
        echo ""
        echo "Kernel packages created in parent directory:"
        ls -lh ../*.deb 2>/dev/null | tail -10
    else
        echo "Build FAILED with exit code $BUILD_EXIT"
    fi
    echo "=========================================="

    exit $BUILD_EXIT

else
    # Running on host - launch Docker container
    echo "Launching Docker container for build..."
    echo ""

    # Check if Docker is available
    if ! command -v docker &> /dev/null; then
        echo "ERROR: Docker is not installed or not in PATH"
        echo "Please install Docker Desktop for Mac"
        exit 1
    fi

    # Check if Docker daemon is running
    if ! docker info &> /dev/null; then
        echo "ERROR: Docker daemon is not running"
        echo "Please start Docker Desktop"
        exit 1
    fi

    # Get absolute path to parent directory (where .deb files will be created)
    PARENT_DIR="$(dirname "$SCRIPT_DIR")"

    echo "Kernel source: $SCRIPT_DIR"
    echo "Output directory: $PARENT_DIR"
    echo ""

    # Check if custom Docker image exists, build if not
    DOCKER_IMAGE="tachyon-kernel-builder:latest"
    if ! docker image inspect "$DOCKER_IMAGE" &> /dev/null; then
        echo "Building custom Docker image with build dependencies..."
        echo "This will take a few minutes but only needs to be done once."
        echo ""
        if docker build --platform linux/arm64 -t "$DOCKER_IMAGE" "$SCRIPT_DIR"; then
            echo ""
            echo "Docker image built successfully!"
            echo ""
        else
            echo ""
            echo "ERROR: Failed to build Docker image"
            echo "Falling back to ubuntu:24.04 (will install dependencies each time)"
            echo ""
            DOCKER_IMAGE="ubuntu:24.04"
        fi
    else
        echo "Using cached Docker image: $DOCKER_IMAGE"
        echo ""
    fi

    echo "Starting Docker build (native ARM64)..."
    echo ""

    # Set default environment variables
    export CLEAN_BUILD="${CLEAN_BUILD:-false}"
    export JOBS="${JOBS:-16}"
    export SKIP_SOURCE_CHECK="${SKIP_SOURCE_CHECK:-true}"

    echo "Build configuration:"
    echo "  Clean build: $CLEAN_BUILD"
    echo "  Parallel jobs: $JOBS"
    echo "  Skip source check: $SKIP_SOURCE_CHECK"
    echo ""

    # Run build in container
    docker run --rm \
        -v "$SCRIPT_DIR:/build" \
        -v "$PARENT_DIR:/output" \
        -w /build \
        --platform linux/arm64 \
        -e CLEAN_BUILD="$CLEAN_BUILD" \
        -e JOBS="$JOBS" \
        -e SKIP_SOURCE_CHECK="$SKIP_SOURCE_CHECK" \
        "$DOCKER_IMAGE" \
        bash -c '
            # Run the build script again (this time inside Docker)
            ./build.sh

            # Copy .deb files to output directory
            if ls ../*.deb 1> /dev/null 2>&1; then
                echo ""
                echo "Copying packages to host output directory..."
                cp -v ../*.deb /output/
            fi
        '

    BUILD_EXIT=$?

    echo ""
    echo "=========================================="
    if [ $BUILD_EXIT -eq 0 ]; then
        echo "Docker build completed successfully!"
        echo ""
        echo "Kernel packages in: $PARENT_DIR"
        ls -lh "$PARENT_DIR"/*.deb 2>/dev/null | grep "$(date +%Y)" || ls -lh "$PARENT_DIR"/*.deb | tail -5
        echo ""
        echo "To install on device:"
        echo "  ./install.sh --adb install"
        echo ""
        echo "Or push and install manually:"
        echo "  adb push $PARENT_DIR/linux-*.deb /tmp/"
        echo "  adb shell 'sudo dpkg -i /tmp/linux-*.deb'"
        echo "  adb reboot"
    else
        echo "Docker build FAILED with exit code $BUILD_EXIT"
    fi
    echo "=========================================="

    exit $BUILD_EXIT
fi
