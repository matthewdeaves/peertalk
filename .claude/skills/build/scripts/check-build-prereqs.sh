#!/bin/bash
# Check prerequisites for PeerTalk build system
#
# All builds run inside Docker, so the main requirement is Docker itself.
# This script checks Docker availability and image status.
#
# Usage: ./.claude/skills/build/scripts/check-build-prereqs.sh
#
# Returns 0 if all OK, 1 if missing required tools

set -e

MISSING_REQUIRED=0

echo "PeerTalk Build Prerequisites"
echo "============================"
echo ""

# Check Docker (required)
echo "=== Docker (Required) ==="
if ! command -v docker >/dev/null 2>&1; then
    echo "  [X] Docker not installed"
    echo "      Install: https://docs.docker.com/get-docker/"
    MISSING_REQUIRED=1
else
    echo "  [OK] docker: $(docker --version | head -1)"

    # Check if Docker daemon is running
    if ! docker info >/dev/null 2>&1; then
        echo "  [X] Docker daemon not running"
        echo "      Start: sudo systemctl start docker"
        MISSING_REQUIRED=1
    else
        echo "  [OK] Docker daemon running"
    fi
fi

if ! command -v docker >/dev/null 2>&1 || ! docker info >/dev/null 2>&1; then
    echo ""
    echo "[X] Docker is required but not available"
    exit 1
fi

echo ""

# Check Docker images
echo "=== Docker Images ==="

# Check for peertalk-dev (used by Makefile docker-* targets)
if docker images --format "{{.Repository}}:{{.Tag}}" | grep -q "^peertalk-dev:latest$"; then
    SIZE=$(docker images peertalk-dev:latest --format "{{.Size}}")
    echo "  [OK] peertalk-dev:latest ($SIZE) - used by make docker-test"
elif docker images --format "{{.Repository}}:{{.Tag}}" | grep -q "ghcr.io/matthewdeaves/peertalk-dev:develop"; then
    SIZE=$(docker images ghcr.io/matthewdeaves/peertalk-dev:develop --format "{{.Size}}")
    echo "  [OK] ghcr.io/matthewdeaves/peertalk-dev:develop ($SIZE)"
    echo "       Tip: Tag it for local use:"
    echo "       docker tag ghcr.io/matthewdeaves/peertalk-dev:develop peertalk-dev"
else
    echo "  [  ] peertalk-dev not found (needed for make docker-test)"
    echo "       Pull: docker pull ghcr.io/matthewdeaves/peertalk-dev:develop"
    echo "       Then: docker tag ghcr.io/matthewdeaves/peertalk-dev:develop peertalk-dev"
fi

# Check for POSIX image (lighter, used by build_all.sh)
if docker images --format "{{.Repository}}:{{.Tag}}" | grep -q "^peertalk-posix:latest$"; then
    SIZE=$(docker images peertalk-posix:latest --format "{{.Size}}")
    echo "  [OK] peertalk-posix:latest ($SIZE) - lightweight POSIX builds"
else
    echo "  [  ] peertalk-posix:latest not found (optional, for quick builds)"
    echo "       Build: docker build -t peertalk-posix -f docker/Dockerfile.posix ."
fi

echo ""

# Check docker compose
echo "=== Docker Compose ==="
if docker compose version >/dev/null 2>&1; then
    echo "  [OK] docker compose: $(docker compose version --short 2>/dev/null || echo 'available')"
else
    echo "  [X] docker compose not available"
    echo "      Docker Compose is required for Mac builds and integration tests"
    MISSING_REQUIRED=1
fi

echo ""

# Check for test integration containers
echo "=== Integration Test Images ==="
for img in peertalk-alice peertalk-bob peertalk-charlie peertalk-test; do
    if docker images --format "{{.Repository}}:{{.Tag}}" | grep -q "^${img}:latest$"; then
        SIZE=$(docker images ${img}:latest --format "{{.Size}}")
        echo "  [OK] ${img}:latest ($SIZE)"
    else
        echo "  [  ] ${img}:latest not built (run: make test-integration-docker)"
    fi
done

echo ""

# Summary
echo "=== Summary ==="
if [[ $MISSING_REQUIRED -eq 0 ]]; then
    echo "[OK] All required prerequisites met"
    echo ""
    echo "Quick start commands:"
    echo "  make docker-test          # Run POSIX tests"
    echo "  make docker-coverage      # Tests with coverage report"
    echo "  make docker-analyze       # Static analysis"
    echo "  make test-integration-docker  # Multi-peer network test"
    exit 0
else
    echo "[X] Missing required prerequisites - please install them first"
    exit 1
fi
