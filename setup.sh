#!/bin/bash
# PeerTalk Development Environment Setup
# One script to get everything working on Ubuntu 25
#
# Prerequisites already on this machine:
#   - gcc, cmake, make (system packages)
#   - Retro68 cross-compiler (~/Retro68-build/toolchain)
#   - MPW Interfaces (~/Retro68/InterfacesAndLibraries/MPW_Interfaces)
#   - boost, gmp, mpfr, mpc, bison, flex, texinfo (Retro68 deps)
#
# This script installs the remaining tools and configures the project.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RETRO68_TOOLCHAIN="${RETRO68_TOOLCHAIN:-$HOME/Retro68-build/toolchain}"
RETRO68_SRC="${RETRO68_SRC:-$HOME/Retro68}"
CLOG_DIR="${CLOG_DIR:-$HOME/clog}"

echo "PeerTalk Development Setup"
echo "=========================="
echo ""

# ── Check core prerequisites ─────────────────────────────────────

check_tool() {
    if command -v "$1" &>/dev/null; then
        echo "  [ok] $1 ($(command -v "$1"))"
        return 0
    else
        echo "  [!!] $1 not found"
        return 1
    fi
}

ensure_bashrc_export() {
    local var_name="$1" var_value="$2"
    if ! grep -q "export ${var_name}=" "$HOME/.bashrc" 2>/dev/null; then
        echo "" >> "$HOME/.bashrc"
        echo "# Added by peertalk/setup.sh" >> "$HOME/.bashrc"
        echo "export ${var_name}=\"${var_value}\"" >> "$HOME/.bashrc"
        echo "  [ok] Added ${var_name} to ~/.bashrc"
    else
        echo "  [ok] ${var_name} already in ~/.bashrc"
    fi
}

echo "Checking prerequisites..."
MISSING=0
check_tool gcc || MISSING=1
check_tool cmake || MISSING=1
check_tool make || MISSING=1
check_tool python3 || MISSING=1
check_tool uv || MISSING=1

if [ "$MISSING" -eq 1 ]; then
    echo ""
    echo "Missing core tools. Install with:"
    echo "  sudo apt install build-essential cmake python3 python3-venv"
    echo "  curl -LsSf https://astral.sh/uv/install.sh | sh"
    exit 1
fi

# ── Check clog dependency ─────────────────────────────────────────

echo ""
echo "Checking clog library..."
if [ -f "$CLOG_DIR/build/libclog.a" ]; then
    echo "  [ok] libclog.a found at $CLOG_DIR/build/libclog.a"
elif [ -f "$CLOG_DIR/CMakeLists.txt" ]; then
    echo "  [--] clog source found but not built. Building..."
    mkdir -p "$CLOG_DIR/build"
    (cd "$CLOG_DIR/build" && cmake .. && make)
    if [ -f "$CLOG_DIR/build/libclog.a" ]; then
        echo "  [ok] clog built successfully"
    else
        echo "  [!!] clog build failed"
        exit 1
    fi
else
    echo "  [!!] clog not found at $CLOG_DIR"
    echo "       Clone it: git clone <clog-repo> $CLOG_DIR"
    exit 1
fi

# ── Check Retro68 ────────────────────────────────────────────────

echo ""
echo "Checking Retro68 cross-compiler..."
if [ -x "$RETRO68_TOOLCHAIN/bin/m68k-apple-macos-gcc" ]; then
    echo "  [ok] m68k-apple-macos-gcc"
else
    echo "  [!!] Retro68 68k toolchain not found at $RETRO68_TOOLCHAIN"
    echo "       Build Retro68 first: https://github.com/autc04/Retro68"
    exit 1
fi

if [ -x "$RETRO68_TOOLCHAIN/bin/powerpc-apple-macos-gcc" ]; then
    echo "  [ok] powerpc-apple-macos-gcc"
else
    echo "  [--] powerpc-apple-macos-gcc not found (PPC builds unavailable)"
fi

# Verify toolchain files exist
if [ -f "$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake" ]; then
    echo "  [ok] 68k toolchain file (retro68.toolchain.cmake)"
else
    echo "  [!!] 68k toolchain file not found"
fi

if [ -f "$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake" ]; then
    echo "  [ok] PPC toolchain file (retroppc.toolchain.cmake)"
else
    echo "  [--] PPC toolchain file not found"
fi

# ── Check MPW Interfaces ────────────────────────────────────────

MPW_CINCLUDES="$RETRO68_SRC/InterfacesAndLibraries/MPW_Interfaces/Interfaces&Libraries/Interfaces/CIncludes"
MPW_ZIP="$RETRO68_SRC/resources/MPW_Interfaces.zip"
echo ""
echo "Checking MPW Interfaces..."
if [ -f "$MPW_CINCLUDES/MacTCP.h" ] && [ -f "$MPW_CINCLUDES/OpenTransport.h" ]; then
    echo "  [ok] MacTCP.h and OpenTransport.h found"
elif [ -f "$MPW_ZIP" ]; then
    echo "  [--] MPW Interfaces not found. Extracting from Retro68..."
    mkdir -p "$RETRO68_SRC/InterfacesAndLibraries"
    unzip -o "$MPW_ZIP" -d "$RETRO68_SRC/InterfacesAndLibraries/"
    if [ -f "$MPW_CINCLUDES/MacTCP.h" ] && [ -f "$MPW_CINCLUDES/OpenTransport.h" ]; then
        echo "  [ok] MPW Interfaces extracted successfully"
    else
        echo "  [!!] Extraction succeeded but headers not found at expected path"
        exit 1
    fi
else
    echo "  [!!] MPW Interfaces not found"
    echo "       Run: cd $RETRO68_SRC && ./setup.sh --mpw-only"
    exit 1
fi

# ── Install analysis tools ───────────────────────────────────────

echo ""
echo "Checking analysis tools..."
INSTALL_PKGS=""

for tool in cppcheck lcov pmccabe; do
    if command -v "$tool" &>/dev/null; then
        echo "  [ok] $tool"
    else
        echo "  [--] $tool (will install)"
        INSTALL_PKGS="$INSTALL_PKGS $tool"
    fi
done

if [ -n "$INSTALL_PKGS" ]; then
    echo ""
    echo "Installing:$INSTALL_PKGS"
    sudo apt install -y $INSTALL_PKGS
fi

# ── MCP server configuration ────────────────────────────────────

MCP_DIR="$SCRIPT_DIR/.claude/mcp-servers/classic-mac-hardware"
echo ""
echo "Checking MCP server..."
if [ -f "$MCP_DIR/server.py" ]; then
    echo "  [ok] server.py exists"
else
    echo "  [!!] MCP server not found at $MCP_DIR/server.py"
    exit 1
fi

# Verify uv can load the mcp package
if uv run --with mcp python3 -c "import mcp" 2>/dev/null; then
    echo "  [ok] mcp Python package available via uv"
else
    echo "  [--] Caching mcp package for uv..."
    uv run --with mcp python3 -c "import mcp; print('  [ok] mcp package cached')" 2>/dev/null \
        || echo "  [!!] Failed to install mcp package via uv"
fi

# Verify .mcp.json exists
if [ -f "$SCRIPT_DIR/.mcp.json" ]; then
    echo "  [ok] .mcp.json configured"
else
    echo "  [!!] .mcp.json not found — Claude Code won't see the MCP server"
    echo "       Create it at project root with classic-mac-hardware server config"
fi

if [ -f "$MCP_DIR/machines.json" ]; then
    echo "  [ok] machines.json configured"
    echo "       Machines:"
    python3 -c "
import json, sys
with open('$MCP_DIR/machines.json') as f:
    machines = json.load(f)
for mid, m in machines.items():
    methods = []
    if 'ftp' in m: methods.append('FTP')
    if 'launchappl' in m: methods.append('LaunchAPPL')
    print(f'         {mid}: {m[\"name\"]} ({m[\"platform\"]}) - {\", \".join(methods)}')
" 2>/dev/null || echo "       (could not parse machines.json)"
else
    echo "  [--] No machines.json — copy from old project or configure machines"
fi

# ── Environment variables ────────────────────────────────────────

echo ""
echo "Checking environment..."

SHELL_RC="$HOME/.bashrc"
EXPORT_LINE="export RETRO68=$RETRO68_TOOLCHAIN"

if grep -q "RETRO68=" "$SHELL_RC" 2>/dev/null; then
    echo "  [ok] RETRO68 already in $SHELL_RC"
else
    echo "  [--] Adding RETRO68 to $SHELL_RC"
    echo "" >> "$SHELL_RC"
    echo "# Retro68 cross-compiler" >> "$SHELL_RC"
    echo "$EXPORT_LINE" >> "$SHELL_RC"
    echo "export PATH=\"\$RETRO68/bin:\$PATH\"" >> "$SHELL_RC"
fi

ensure_bashrc_export "PEERTALK_DIR" "$SCRIPT_DIR"

export RETRO68="$RETRO68_TOOLCHAIN"
export PATH="$RETRO68/bin:$PATH"
export PEERTALK_DIR="$SCRIPT_DIR"

# ── Make scripts executable ──────────────────────────────────────

echo ""
echo "Ensuring scripts are executable..."
chmod +x "$SCRIPT_DIR/tools/autorun.sh" 2>/dev/null && echo "  [ok] tools/autorun.sh" || true

# ── Summary ──────────────────────────────────────────────────────

echo ""
echo "=========================="
echo "Setup complete!"
echo ""
echo "  POSIX builds:    gcc $(gcc -dumpversion)"
echo "  68k builds:      $RETRO68_TOOLCHAIN/bin/m68k-apple-macos-gcc"
if [ -x "$RETRO68_TOOLCHAIN/bin/powerpc-apple-macos-gcc" ]; then
echo "  PPC builds:      $RETRO68_TOOLCHAIN/bin/powerpc-apple-macos-gcc"
fi
echo "  MPW Interfaces:  $MPW_CINCLUDES"
echo "  clog library:    $CLOG_DIR/build/libclog.a"
echo "  MCP server:      $MCP_DIR/server.py"
echo ""
echo "Quick start:"
echo "  # POSIX build"
echo "  mkdir -p build && cd build"
echo "  cmake .. -DCLOG_DIR=$CLOG_DIR && make"
echo ""
echo "  # 68k build (MacTCP)"
echo "  mkdir -p build-m68k && cd build-m68k"
echo "  cmake .. -DCMAKE_TOOLCHAIN_FILE=\$RETRO68/m68k-apple-macos/cmake/retro68.toolchain.cmake \\"
echo "    -DPT_PLATFORM=MACTCP -DCLOG_DIR=$CLOG_DIR && make"
echo ""
echo "  # PPC build (Open Transport)"
echo "  mkdir -p build-ppc && cd build-ppc"
echo "  cmake .. -DCMAKE_TOOLCHAIN_FILE=\$RETRO68/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \\"
echo "    -DPT_PLATFORM=OT -DCLOG_DIR=$CLOG_DIR && make"
echo ""
echo "  # Autorun (unattended)"
echo "  ./tools/autorun.sh"
echo ""
