#!/bin/bash
# Compile PeerTalk files using the Docker container
#
# Usage: ./scripts/docker-compile.sh <file.c> [platform]
#
# Platforms:
#   mactcp    - 68k MacTCP (default for src/mactcp/*)
#   ot        - PPC Open Transport (default for src/opentransport/*)
#   appletalk - 68k AppleTalk (default for src/appletalk/*)
#   posix     - Native gcc (default for src/posix/* and src/core/*)
#
# Examples:
#   ./scripts/docker-compile.sh src/mactcp/tcp_mactcp.c
#   ./scripts/docker-compile.sh src/core/protocol.c posix

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

FILE_PATH="$1"
PLATFORM="${2:-auto}"

if [[ -z "$FILE_PATH" ]]; then
    echo "Usage: $0 <file.c> [platform]"
    echo ""
    echo "Platforms: mactcp, ot, appletalk, posix, auto (default)"
    exit 1
fi

# Auto-detect platform from path
if [[ "$PLATFORM" == "auto" ]]; then
    if [[ "$FILE_PATH" == *"/mactcp/"* ]]; then
        PLATFORM="mactcp"
    elif [[ "$FILE_PATH" == *"/opentransport/"* ]]; then
        PLATFORM="ot"
    elif [[ "$FILE_PATH" == *"/appletalk/"* ]]; then
        PLATFORM="appletalk"
    else
        PLATFORM="posix"
    fi
fi

# Convert to path relative to project root
REL_PATH="${FILE_PATH#$PROJECT_DIR/}"
if [[ "$REL_PATH" == "$FILE_PATH" ]]; then
    # Path wasn't under project dir, use as-is
    REL_PATH="$FILE_PATH"
fi

cd "$PROJECT_DIR"

# Select compiler and includes based on platform
case "$PLATFORM" in
    mactcp|appletalk)
        COMPILER="m68k-apple-macos-gcc"
        INCLUDES="-I/workspace/include -I/opt/Retro68-build/toolchain/universal/CIncludes"
        ;;
    ot)
        COMPILER="powerpc-apple-macos-gcc"
        INCLUDES="-I/workspace/include -I/opt/Retro68-build/toolchain/universal/CIncludes"
        ;;
    posix)
        # Use native gcc, not Docker
        echo "[compile] Using native gcc for POSIX code"
        gcc -fsyntax-only -Wall -I"$PROJECT_DIR/include" "$FILE_PATH"
        echo "[compile] $REL_PATH OK"
        exit 0
        ;;
    *)
        echo "Unknown platform: $PLATFORM"
        exit 1
        ;;
esac

echo "[compile] Using $COMPILER for $PLATFORM"

# Run in Docker
if command -v docker-compose >/dev/null 2>&1; then
    docker-compose -f docker/docker-compose.yml run --rm -T peertalk-dev \
        $COMPILER -fsyntax-only -Wall $INCLUDES "/workspace/$REL_PATH"
elif command -v docker >/dev/null 2>&1; then
    docker compose -f docker/docker-compose.yml run --rm -T peertalk-dev \
        $COMPILER -fsyntax-only -Wall $INCLUDES "/workspace/$REL_PATH"
else
    echo "ERROR: Neither docker-compose nor docker compose found"
    exit 1
fi

echo "[compile] $REL_PATH OK"
