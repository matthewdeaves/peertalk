#!/bin/bash
# Build or pull the PeerTalk Docker development environment
#
# This creates a container with:
# - Retro68 cross-compilation toolchain
# - m68k-apple-macos-gcc (for MacTCP, AppleTalk)
# - powerpc-apple-macos-gcc (for Open Transport)
# - All required build tools
#
# Usage:
#   ./scripts/docker-build.sh         # Pull from Docker Hub (fast)
#   ./scripts/docker-build.sh --build # Build locally (30-60 min)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
IMAGE_NAME="mndeaves/peertalk:latest"

cd "$PROJECT_DIR"

# Check for --build flag
if [[ "$1" == "--build" ]]; then
    echo "Building PeerTalk development container locally..."
    echo "This may take 30-60 minutes (Retro68 compilation)"
    echo ""

    # Check for required file
    if [[ ! -f "resources/retro68/MPW_Interfaces.zip" ]]; then
        echo "ERROR: resources/retro68/MPW_Interfaces.zip not found"
        echo "This file contains Apple's Universal Interfaces (MacTCP.h, etc.)"
        echo "It must be present before building the container."
        exit 1
    fi

    # Build using docker compose
    if command -v docker-compose >/dev/null 2>&1; then
        docker-compose -f docker/docker-compose.yml build
    elif command -v docker >/dev/null 2>&1; then
        docker compose -f docker/docker-compose.yml build
    else
        echo "ERROR: Docker not found"
        exit 1
    fi
else
    echo "Pulling PeerTalk development container from Docker Hub..."
    echo ""

    docker pull "$IMAGE_NAME"
fi

echo ""
echo "Container ready!"
echo ""
echo "To start a development shell:"
echo "  ./scripts/docker-shell.sh"
echo ""
echo "To compile a file directly:"
echo "  ./scripts/docker-compile.sh src/mactcp/tcp_mactcp.c"

if [[ "$1" != "--build" ]]; then
    echo ""
    echo "To build locally instead of pulling:"
    echo "  ./scripts/docker-build.sh --build"
fi
