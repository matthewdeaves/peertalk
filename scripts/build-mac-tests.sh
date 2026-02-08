#!/bin/bash
# Build PeerTalk test applications for Classic Mac platforms
#
# Usage: ./scripts/build-mac-tests.sh [mactcp|ot] [all|test|perf]
#
# Platforms:
#   mactcp - 68k MacTCP build (System 6.0.8 - 7.5.5)
#   ot     - PPC Open Transport build (System 7.6.1+)
#
# Targets:
#   all   - Build library, test app, and performance tests (default)
#   test  - Build library and basic test app only
#   perf  - Build library and performance tests only
#
# Output:
#   build/mac/libpeertalk_<platform>.a    - Static library
#   build/mac/test_mactcp.bin             - Basic test app
#   build/mac/test_latency.bin            - Latency measurement
#   build/mac/test_throughput.bin         - Throughput measurement
#   build/mac/test_stress.bin             - Stress testing
#   build/mac/test_discovery.bin          - Discovery testing
#
# Example:
#   ./scripts/build-mac-tests.sh mactcp        # Build everything for MacTCP
#   ./scripts/build-mac-tests.sh mactcp perf   # Build only perf tests
#   ./scripts/build-mac-tests.sh ot test       # Build only basic test for OT

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

PLATFORM="${1:-mactcp}"
TARGET="${2:-all}"

# Validate platform
case "$PLATFORM" in
    mactcp|ot)
        ;;
    *)
        echo "Usage: $0 [mactcp|ot] [all|test|perf]"
        echo ""
        echo "Platforms:"
        echo "  mactcp - 68k MacTCP build (System 6.0.8 - 7.5.5)"
        echo "  ot     - PPC Open Transport build (System 7.6.1+)"
        echo ""
        echo "Targets:"
        echo "  all   - Build library, test app, and performance tests (default)"
        echo "  test  - Build library and basic test app only"
        echo "  perf  - Build library and performance tests only"
        exit 1
        ;;
esac

# Validate target
case "$TARGET" in
    all|test|perf)
        ;;
    *)
        echo "Invalid target: $TARGET"
        echo "Valid targets: all, test, perf"
        exit 1
        ;;
esac

cd "$PROJECT_DIR"

# Use docker compose
DOCKER_CMD="docker compose"
if ! command -v docker >/dev/null 2>&1; then
    echo "ERROR: docker not found"
    exit 1
fi

echo "========================================="
echo "Building PeerTalk Tests for $PLATFORM"
echo "Target: $TARGET"
echo "========================================="

# Determine make targets
MAKE_TARGETS=""
case "$TARGET" in
    all)
        MAKE_TARGETS="test perf_tests"
        ;;
    test)
        MAKE_TARGETS="test"
        ;;
    perf)
        MAKE_TARGETS="perf_tests"
        ;;
esac

$DOCKER_CMD -f docker/docker-compose.yml run --rm peertalk-dev bash -c "
    set -e
    cd /workspace

    # Clean previous build
    make -f Makefile.retro68 PLATFORM=$PLATFORM clean 2>/dev/null || true

    # Build library and tests
    echo 'Building library...'
    make -f Makefile.retro68 PLATFORM=$PLATFORM all

    echo ''
    echo 'Building test applications...'
    make -f Makefile.retro68 PLATFORM=$PLATFORM $MAKE_TARGETS
"

echo ""
echo "========================================="
echo "Build Complete!"
echo "========================================="
echo ""
echo "Output files:"
ls -la build/mac/*.bin 2>/dev/null || echo "No .bin files found"
echo ""
echo "Next steps:"
echo "1. Deploy to Mac: mcp__classic-mac-hardware__upload_file"
echo "   Example: upload_file(machine='performa6200', local_path='build/mac/test_throughput.bin', remote_path='test_throughput.bin')"
echo ""
echo "2. Or use the /deploy skill:"
echo "   /deploy performa6200"
echo ""
echo "3. On the Mac:"
echo "   - Use BinUnpk to extract the .bin file"
echo "   - Double-click to run the test"
echo "   - Press any key to exit when done"
echo ""
echo "4. Fetch logs:"
echo "   /fetch-logs performa6200"
