#!/bin/bash
# Minimal setup for Docker-based PeerTalk development
#
# Installs only what must run on the host:
#   - jq (hooks parse JSON)
#   - python3 (MCP server, validators)
#   - Docker (all builds happen here)

set -e

cd "$(dirname "$0")"

echo "PeerTalk Minimal Setup"
echo "======================"
echo ""
echo "Philosophy: ALL builds happen in Docker"
echo "Host only needs: jq, python3, Docker"
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

# Required dependencies
echo "Checking required dependencies..."
MISSING_PKGS=""

install_pkg "jq" "jq" "jq" "jq"
install_pkg "python3" "python3" "python3" "python3"

# Prompt for apt/dnf install if packages missing
if [[ -n "$MISSING_PKGS" && "$PKG_MGR" != "brew" ]]; then
    echo ""
    echo "Missing packages:$MISSING_PKGS"
    echo ""
    if [[ "$PKG_MGR" == "apt" ]]; then
        echo "Install with:"
        echo "  sudo apt update && sudo apt install$MISSING_PKGS"
    elif [[ "$PKG_MGR" == "dnf" ]]; then
        echo "Install with:"
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

echo ""
echo "Checking Docker (required for all builds)..."
if command -v docker &> /dev/null; then
    echo "  ✓ docker"

    # Check if Docker daemon is running
    if ! docker info &> /dev/null; then
        echo "  ✗ Docker daemon not running"
        echo ""
        echo "ERROR: Docker is installed but not running"
        echo "Please start Docker Desktop or Docker daemon"
        exit 1
    fi

    if docker compose version &> /dev/null 2>&1; then
        echo "  ✓ docker compose"
    elif docker-compose version &> /dev/null 2>&1; then
        echo "  ⚠ docker-compose (legacy) - 'docker compose' preferred"
    else
        echo "  ✗ docker compose not found"
        echo ""
        echo "ERROR: Docker Compose is required for all builds"
        echo "Install Docker Desktop: https://docs.docker.com/get-docker/"
        exit 1
    fi
else
    echo "  ✗ docker not found"
    echo ""
    echo "ERROR: Docker is required for all builds (POSIX + Mac)"
    echo "Install Docker Desktop: https://docs.docker.com/get-docker/"
    echo "  https://docs.docker.com/get-docker/"
    exit 1
fi

# Make scripts executable (ignore errors if directories don't exist)
[ -d "build" ] && chmod +x build/*.sh 2>/dev/null || true
[ -d "../.claude/hooks" ] && chmod +x ../.claude/hooks/*.sh 2>/dev/null || true
[ -d "../scripts" ] && chmod +x ../scripts/*.sh 2>/dev/null || true

echo ""
echo "========================================"
echo "Step 2: Docker Environment"
echo "========================================"
echo ""
echo "Building Docker container with:"
echo "  - Retro68 (Mac cross-compiler)"
echo "  - gcc, make (POSIX builds)"
echo "  - lcov, ctags, clang-format (quality tools)"
echo ""

# Build/pull Docker image
cd ..
if ./scripts/docker-build.sh; then
    echo "  ✓ Docker image ready"
else
    echo "  ✗ Docker setup failed"
    exit 1
fi

echo ""
echo "========================================"
echo "Step 3: MCP Server Configuration"
echo "========================================"
echo ""

# Setup MCP configuration
if [ ! -f ".mcp.json" ]; then
    if [ -f ".mcp.json.example" ]; then
        echo "Copying MCP configuration..."
        cp .mcp.json.example .mcp.json
        echo "  ✓ .mcp.json created"
    else
        echo "  ⚠ .mcp.json.example not found (MCP server won't be available)"
    fi
else
    echo "  ✓ .mcp.json already exists"
fi

echo ""
echo "========================================"
echo "Setup Complete!"
echo "========================================"
echo ""
echo "Installed on host:"
echo "  ✓ jq (hooks)"
echo "  ✓ python3 (MCP server, validators)"
echo "  ✓ Docker (build environment)"
echo "  ✓ Docker image (Retro68 + build tools)"
echo "  ✓ MCP server configuration"
echo ""
echo "IMPORTANT: Restart Claude Code completely to load MCP servers"
echo "           (Exit and reopen, not just reload)"
echo ""
echo "After restarting Claude Code:"
echo "  /test-machine --list   # Verify MCP is loaded"
echo "  /setup-machine         # Add your Classic Mac hardware"
echo "  /session next          # Start development"
echo "  /build test            # Build + test (in Docker)"
echo ""
echo "To activate Python environment:"
echo "  source tools/.venv/bin/activate"
