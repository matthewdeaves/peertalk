#!/bin/bash
# Build PeerTalk for all platforms
#
# Usage:
#   ./tools/build/build_all.sh         # Build all platforms
#   ./tools/build/build_all.sh posix   # Build POSIX only
#   ./tools/build/build_all.sh mactcp  # Build 68k/MacTCP only
#   ./tools/build/build_all.sh ot      # Build PPC/OT only

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Check for local Retro68 or use Docker
RETRO68="${RETRO68:-}"
USE_DOCKER=false

if [[ -z "$RETRO68" ]] || [[ ! -d "$RETRO68" ]]; then
    if [[ -f "$PROJECT_DIR/docker/docker-compose.yml" ]] && command -v docker >/dev/null 2>&1; then
        USE_DOCKER=true
        echo "Using Docker for Retro68 builds"
    else
        echo "Warning: Retro68 not found and Docker not available"
        echo "Set RETRO68 environment variable or install Docker"
    fi
fi

# Platform to build (default: all)
PLATFORM="${1:-all}"

cd "$PROJECT_DIR"

echo "Building PeerTalk"
echo "================="
echo "Project: $PROJECT_DIR"
echo "Retro68: $RETRO68"
echo "Platform: $PLATFORM"
echo ""

build_posix() {
    echo "[POSIX] Building..."
    if [[ -f "Makefile" ]]; then
        make clean >/dev/null 2>&1 || true
        make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
        echo "[POSIX] ✓ Complete"
    else
        echo "[POSIX] ⚠ No Makefile found - skipping"
    fi
}

build_mactcp() {
    echo "[68k/MacTCP] Building..."

    if [[ ! -f "Makefile.retro68" ]]; then
        echo "[68k/MacTCP] ⚠ No Makefile.retro68 found - skipping"
        return 0
    fi

    if [[ "$USE_DOCKER" == "true" ]]; then
        docker compose -f docker/docker-compose.yml run --rm peertalk-dev \
            make -f Makefile.retro68 PLATFORM=mactcp
    elif [[ -d "$RETRO68" ]]; then
        make -f Makefile.retro68 clean >/dev/null 2>&1 || true
        make -f Makefile.retro68 PLATFORM=mactcp RETRO68="$RETRO68"
    else
        echo "[68k/MacTCP] ⚠ Retro68 not available - skipping"
        return 0
    fi
    echo "[68k/MacTCP] ✓ Complete"
}

build_ot() {
    echo "[PPC/OT] Building..."

    if [[ ! -f "Makefile.retro68" ]]; then
        echo "[PPC/OT] ⚠ No Makefile.retro68 found - skipping"
        return 0
    fi

    if [[ "$USE_DOCKER" == "true" ]]; then
        docker compose -f docker/docker-compose.yml run --rm peertalk-dev \
            make -f Makefile.retro68 PLATFORM=ot
    elif [[ -d "$RETRO68" ]]; then
        make -f Makefile.retro68 clean-ppc >/dev/null 2>&1 || true
        make -f Makefile.retro68 PLATFORM=ot RETRO68="$RETRO68"
    else
        echo "[PPC/OT] ⚠ Retro68 not available - skipping"
        return 0
    fi
    echo "[PPC/OT] ✓ Complete"
}

build_appletalk() {
    echo "[AppleTalk] Building..."

    if [[ ! -f "Makefile.retro68" ]]; then
        echo "[AppleTalk] ⚠ No Makefile.retro68 found - skipping"
        return 0
    fi

    if [[ "$USE_DOCKER" == "true" ]]; then
        docker compose -f docker/docker-compose.yml run --rm peertalk-dev \
            make -f Makefile.retro68 PLATFORM=appletalk
    elif [[ -d "$RETRO68" ]]; then
        make -f Makefile.retro68 PLATFORM=appletalk RETRO68="$RETRO68"
    else
        echo "[AppleTalk] ⚠ Retro68 not available - skipping"
        return 0
    fi
    echo "[AppleTalk] ✓ Complete"
}

case "$PLATFORM" in
    posix)
        build_posix
        ;;
    mactcp|68k)
        build_mactcp
        ;;
    ot|ppc|opentransport)
        build_ot
        ;;
    appletalk|at)
        build_appletalk
        ;;
    all)
        build_posix
        echo ""
        build_mactcp
        echo ""
        build_ot
        echo ""
        build_appletalk
        ;;
    *)
        echo "Unknown platform: $PLATFORM"
        echo "Usage: $0 [posix|mactcp|ot|appletalk|all]"
        exit 1
        ;;
esac

echo ""
echo "Build complete!"
