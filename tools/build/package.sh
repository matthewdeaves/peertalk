#!/bin/bash
# Package PeerTalk binaries for Mac transfer
#
# Creates .bin files that can be transferred to real Mac hardware
# using tools like Basilisk II, Mini vMac, or direct file transfer.
#
# Usage:
#   ./tools/build/package.sh              # Package all platforms
#   ./tools/build/package.sh mactcp       # Package 68k only
#   ./tools/build/package.sh ot           # Package PPC only

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

# RETRO68 can be set via environment variable or defaults to ~/Retro68
RETRO68="${RETRO68:-$HOME/Retro68}"
BUILD_DIR="$PROJECT_DIR/build"
PACKAGE_DIR="$PROJECT_DIR/packages"

PLATFORM="${1:-all}"

cd "$PROJECT_DIR"

echo "Packaging PeerTalk"
echo "=================="
echo ""

# Ensure build and package directories exist
mkdir -p "$BUILD_DIR" "$PACKAGE_DIR"

package_mactcp() {
    echo "[68k/MacTCP] Packaging..."

    # Look for built binary
    local binary=""
    for candidate in "$BUILD_DIR/PeerTalk-68k" "$BUILD_DIR/peertalk.68k" "$BUILD_DIR/PeerTalk"; do
        if [[ -f "$candidate" ]] || [[ -f "$candidate.bin" ]]; then
            binary="$candidate"
            break
        fi
    done

    if [[ -z "$binary" ]]; then
        echo "  ⚠ No 68k binary found in $BUILD_DIR"
        echo "    Run: ./tools/build/build_all.sh mactcp"
        return 1
    fi

    # Create MacBinary if not already
    if [[ -f "${binary}.bin" ]]; then
        cp "${binary}.bin" "$PACKAGE_DIR/PeerTalk-68k.bin"
    elif [[ -f "$binary" ]]; then
        # The binary should already be in MacBinary format from Retro68
        cp "$binary" "$PACKAGE_DIR/PeerTalk-68k.bin"
    fi

    if [[ -f "$PACKAGE_DIR/PeerTalk-68k.bin" ]]; then
        local size=$(stat -c%s "$PACKAGE_DIR/PeerTalk-68k.bin" 2>/dev/null || stat -f%z "$PACKAGE_DIR/PeerTalk-68k.bin")
        echo "  ✓ Created PeerTalk-68k.bin ($size bytes)"
    else
        echo "  ✗ Failed to create package"
        return 1
    fi
}

package_ot() {
    echo "[PPC/OT] Packaging..."

    # Look for built binary
    local binary=""
    for candidate in "$BUILD_DIR/PeerTalk-PPC" "$BUILD_DIR/peertalk.ppc" "$BUILD_DIR/PeerTalk"; do
        if [[ -f "$candidate" ]] || [[ -f "$candidate.bin" ]]; then
            binary="$candidate"
            break
        fi
    done

    if [[ -z "$binary" ]]; then
        echo "  ⚠ No PPC binary found in $BUILD_DIR"
        echo "    Run: ./tools/build/build_all.sh ot"
        return 1
    fi

    # Create MacBinary if not already
    if [[ -f "${binary}.bin" ]]; then
        cp "${binary}.bin" "$PACKAGE_DIR/PeerTalk-PPC.bin"
    elif [[ -f "$binary" ]]; then
        cp "$binary" "$PACKAGE_DIR/PeerTalk-PPC.bin"
    fi

    if [[ -f "$PACKAGE_DIR/PeerTalk-PPC.bin" ]]; then
        local size=$(stat -c%s "$PACKAGE_DIR/PeerTalk-PPC.bin" 2>/dev/null || stat -f%z "$PACKAGE_DIR/PeerTalk-PPC.bin")
        echo "  ✓ Created PeerTalk-PPC.bin ($size bytes)"
    else
        echo "  ✗ Failed to create package"
        return 1
    fi
}

create_readme() {
    cat > "$PACKAGE_DIR/README.txt" << 'EOF'
PeerTalk Mac Packages
=====================

This directory contains MacBinary-encoded PeerTalk binaries for
Classic Macintosh systems.

Files:
  PeerTalk-68k.bin  - For 68k Macs with MacTCP 2.1
                      Requires: System 6.0.8 - 7.5.5

  PeerTalk-PPC.bin  - For PowerPC Macs with Open Transport
                      Requires: System 7.6.1+ or Mac OS 8/9

Transfer Methods:
  1. FTP to Mac (if networking already works)
  2. Basilisk II / Mini vMac shared folder
  3. Serial transfer with Zmodem
  4. Write to HFS floppy/CD

After transfer:
  - Double-click to launch
  - If file type is lost, use ResEdit to set:
    Creator: PTLK
    Type: APPL

For more info: https://github.com/matthewdeaves/peertalk
EOF
    echo "  ✓ Created README.txt"
}

case "$PLATFORM" in
    mactcp|68k)
        package_mactcp
        ;;
    ot|ppc|opentransport)
        package_ot
        ;;
    all)
        package_mactcp || true
        echo ""
        package_ot || true
        echo ""
        create_readme
        ;;
    *)
        echo "Unknown platform: $PLATFORM"
        echo "Usage: $0 [mactcp|ot|all]"
        exit 1
        ;;
esac

echo ""
echo "Packages created in: $PACKAGE_DIR"
ls -la "$PACKAGE_DIR"/*.bin 2>/dev/null || echo "(no .bin files yet)"
