#!/bin/bash
# One-command setup for PeerTalk tooling
#
# Installs system dependencies (jq, build tools) and Python environment.
# Works on Linux (apt/dnf) and macOS (homebrew).

set -e

cd "$(dirname "$0")"

echo "PeerTalk Tools Setup"
echo "===================="
echo ""

# Detect OS
OS="unknown"
PKG_MGR=""
if [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
    if command -v brew &> /dev/null; then
        PKG_MGR="brew"
    fi
elif [[ -f /etc/debian_version ]]; then
    OS="debian"
    PKG_MGR="apt"
elif [[ -f /etc/redhat-release ]]; then
    OS="redhat"
    PKG_MGR="dnf"
fi

echo "Detected: $OS (package manager: ${PKG_MGR:-none})"
echo ""

# Function to check and install a package
install_pkg() {
    local cmd="$1"
    local apt_pkg="${2:-$1}"
    local brew_pkg="${3:-$1}"
    local dnf_pkg="${4:-$apt_pkg}"

    if command -v "$cmd" &> /dev/null; then
        echo "  ✓ $cmd"
        return 0
    fi

    echo "  ✗ $cmd not found"

    case "$PKG_MGR" in
        brew)
            echo "    Installing via homebrew..."
            brew install "$brew_pkg"
            ;;
        apt)
            echo "    Install with: sudo apt install $apt_pkg"
            MISSING_PKGS="$MISSING_PKGS $apt_pkg"
            ;;
        dnf)
            echo "    Install with: sudo dnf install $dnf_pkg"
            MISSING_PKGS="$MISSING_PKGS $dnf_pkg"
            ;;
        *)
            echo "    Please install $cmd manually"
            ;;
    esac
}

# Required dependencies (minimal Docker-based workflow)
echo "Checking required dependencies..."
MISSING_PKGS=""

install_pkg "jq" "jq" "jq" "jq"
install_pkg "python3" "python3" "python3" "python3"

# Optional: Only needed for native POSIX development (Docker has these)
echo ""
echo "Checking optional dependencies (for native POSIX builds)..."
echo "Note: These are already in Docker - only install if building POSIX code natively"

install_pkg "make" "make" "make" "make"
install_pkg "gcc" "build-essential" "gcc" "gcc"
install_pkg "lcov" "lcov" "lcov" "lcov"
install_pkg "ctags" "universal-ctags" "universal-ctags" "ctags"
install_pkg "clang-format" "clang-format" "clang-format" "clang-tools-extra"

# Prompt for apt/dnf install if packages missing
if [[ -n "$MISSING_PKGS" && "$PKG_MGR" != "brew" ]]; then
    echo ""
    echo "Missing packages:$MISSING_PKGS"
    echo ""
    if [[ "$PKG_MGR" == "apt" ]]; then
        echo "Install all with:"
        echo "  sudo apt update && sudo apt install$MISSING_PKGS"
    elif [[ "$PKG_MGR" == "dnf" ]]; then
        echo "Install all with:"
        echo "  sudo dnf install$MISSING_PKGS"
    fi
    echo ""
    read -p "Continue anyway? [y/N] " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

echo ""

# Check for Python 3 (should be installed by now, but verify)
if ! command -v python3 &> /dev/null; then
    echo "ERROR: Python 3 is required but not installed."
    echo ""
    echo "Install it with:"
    echo "  Ubuntu/Debian: sudo apt install python3 python3-venv"
    echo "  macOS:         brew install python3"
    echo "  Fedora:        sudo dnf install python3"
    exit 1
fi

# Create Python virtual environment
if [ ! -d ".venv" ]; then
    echo "Creating Python virtual environment..."
    python3 -m venv .venv
else
    echo "Virtual environment already exists"
fi

# Activate and install dependencies
source .venv/bin/activate

echo "Installing Python dependencies..."
pip install -q --upgrade pip
pip install -q -r requirements.txt

# Check if books directory has content
BOOKS_DIR="../books"
if [ -d "$BOOKS_DIR" ] && [ "$(ls -A $BOOKS_DIR 2>/dev/null)" ]; then
    echo "Found books directory with $(ls -1 $BOOKS_DIR | wc -l) reference files"
else
    echo "Note: books/ directory is empty or missing"
fi

# Check for Docker (optional, for Mac cross-compilation)
echo ""
echo "Checking Docker (optional, for Mac cross-compilation)..."
if command -v docker &> /dev/null; then
    echo "  ✓ docker"
    if docker compose version &> /dev/null; then
        echo "  ✓ docker compose"
    elif docker-compose version &> /dev/null; then
        echo "  ✓ docker-compose (legacy)"
    else
        echo "  ✗ docker compose not found"
        echo "    Mac cross-compilation requires Docker Compose"
    fi
else
    echo "  ✗ docker not found (optional)"
    echo "    Install Docker for Mac cross-compilation with Retro68"
    echo "    https://docs.docker.com/get-docker/"
fi

# Make scripts executable
chmod +x build/*.sh 2>/dev/null || true
chmod +x ../.claude/hooks/*.sh 2>/dev/null || true

echo ""
echo "========================================"
echo "Setup complete!"
echo "========================================"
echo ""
echo "Minimal requirements installed:"
echo "  ✓ jq (required for hooks)"
echo "  ✓ python3 (required for validators)"
echo ""
echo "Docker-based workflow (recommended):"
echo "  1. Build Docker image:"
echo "     ./scripts/docker-build.sh"
echo ""
echo "  2. Use skills (Docker handles compilation):"
echo "     /session next        # Find next available session"
echo "     /build test          # Build and run POSIX tests"
echo "     /build all           # Build for all platforms (POSIX + Mac)"
echo "     /setup-machine       # Onboard new Classic Mac hardware"
echo ""
echo "Native POSIX development (optional):"
echo "  If you installed make/gcc/lcov/ctags, you can build POSIX code natively"
echo "  Otherwise, hooks gracefully skip (all tools are in Docker)"
echo ""
echo "To activate Python environment:"
echo "  source tools/.venv/bin/activate"
