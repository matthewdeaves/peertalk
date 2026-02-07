#!/bin/bash
# Build LaunchAPPLServer for Classic Mac platforms
#
# Usage: ./scripts/build-launcher.sh [mactcp|ot|both]
#
# Platforms:
#   mactcp - 68k MacTCP build (System 6.0.8 - 7.5.5)
#   ot     - PPC Open Transport build (System 7.6.1+)
#   both   - Build both platforms (default)
#
# Output:
#   LaunchAPPL-build/LaunchAPPLServer-MacTCP.bin
#   LaunchAPPL-build/LaunchAPPLServer-MacTCP.dsk
#   LaunchAPPL-build/LaunchAPPLServer-OpenTransport.bin
#   LaunchAPPL-build/LaunchAPPLServer-OpenTransport.dsk
#
# Note: LaunchAPPL source is inside the Docker container at /opt/Retro68/LaunchAPPL/
#       This script builds there and copies output to workspace.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PLATFORM="${1:-both}"

# Determine which platforms to build
BUILD_MACTCP=0
BUILD_OT=0

case "$PLATFORM" in
    mactcp)
        BUILD_MACTCP=1
        ;;
    ot)
        BUILD_OT=1
        ;;
    both)
        BUILD_MACTCP=1
        BUILD_OT=1
        ;;
    *)
        echo "Usage: $0 [mactcp|ot|both]"
        echo ""
        echo "Platforms:"
        echo "  mactcp - 68k MacTCP build (System 6.0.8 - 7.5.5)"
        echo "  ot     - PPC Open Transport build (System 7.6.1+)"
        echo "  both   - Build both platforms (default)"
        exit 1
        ;;
esac

cd "$PROJECT_DIR"

# Create output directory
mkdir -p LaunchAPPL-build

# Use docker compose
DOCKER_CMD="docker compose"
if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: docker not found"
    exit 1
fi

# Build MacTCP version (68k)
if [[ $BUILD_MACTCP -eq 1 ]]; then
    echo "========================================="
    echo "Building LaunchAPPLServer for MacTCP..."
    echo "========================================="

    $DOCKER_CMD -f docker/docker-compose.yml run --rm peertalk-dev bash -c "
        set -e

        # Build in container's LaunchAPPL source directory
        cd /opt/Retro68/LaunchAPPL
        rm -rf build-mactcp
        mkdir -p build-mactcp
        cd build-mactcp

        # Configure with CMake for 68k
        cmake .. \
            -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake \
            -DCMAKE_BUILD_TYPE=Release

        # Build
        make -j\$(nproc)

        # Copy to workspace
        cp Server/LaunchAPPLServer.bin /workspace/LaunchAPPL-build/LaunchAPPLServer-MacTCP.bin
        cp Server/LaunchAPPLServer.dsk /workspace/LaunchAPPL-build/LaunchAPPLServer-MacTCP.dsk

        echo ''
        echo 'Build complete!'
        ls -lh /workspace/LaunchAPPL-build/LaunchAPPLServer-MacTCP.*
    "

    echo ""
    echo "MacTCP build complete:"
    echo "  Binary: LaunchAPPL-build/LaunchAPPLServer-MacTCP.bin"
    echo "  Disk:   LaunchAPPL-build/LaunchAPPLServer-MacTCP.dsk"
    echo ""
fi

# Build Open Transport version (PPC)
if [[ $BUILD_OT -eq 1 ]]; then
    echo "================================================"
    echo "Building LaunchAPPLServer for Open Transport..."
    echo "================================================"

    $DOCKER_CMD -f docker/docker-compose.yml run --rm peertalk-dev bash -c "
        set -e

        # Build in container's LaunchAPPL source directory
        cd /opt/Retro68/LaunchAPPL
        rm -rf build-ppc
        mkdir -p build-ppc
        cd build-ppc

        # Configure with CMake for PPC
        cmake .. \
            -DCMAKE_TOOLCHAIN_FILE=/opt/Retro68-build/toolchain/powerpc-apple-macos/cmake/retro68.toolchain.cmake \
            -DCMAKE_BUILD_TYPE=Release

        # Build
        make -j\$(nproc)

        # Copy to workspace
        cp Server/LaunchAPPLServer.bin /workspace/LaunchAPPL-build/LaunchAPPLServer-OpenTransport.bin
        cp Server/LaunchAPPLServer.dsk /workspace/LaunchAPPL-build/LaunchAPPLServer-OpenTransport.dsk

        echo ''
        echo 'Build complete!'
        ls -lh /workspace/LaunchAPPL-build/LaunchAPPLServer-OpenTransport.*
    "

    echo ""
    echo "Open Transport build complete:"
    echo "  Binary: LaunchAPPL-build/LaunchAPPLServer-OpenTransport.bin"
    echo "  Disk:   LaunchAPPL-build/LaunchAPPLServer-OpenTransport.dsk"
    echo ""
fi

echo "========================================="
echo "All builds complete!"
echo "========================================="
echo ""
echo "Output files:"
ls -la LaunchAPPL-build/
echo ""
echo "Next steps:"
echo "1. Deploy .bin files to your Mac via FTP: /deploy <machine>"
echo "2. Use BinUnpk on the Mac to extract the application"
echo "3. Launch LaunchAPPLServer and enable TCP server on port 1984"
echo "4. Test connectivity with: /test-machine <machine-name>"
