#!/bin/bash
# Check prerequisites for PeerTalk build system
# Returns 0 if all OK, 1 if missing required tools

set -e

MISSING_REQUIRED=0
MISSING_OPTIONAL=0

echo "Checking PeerTalk build prerequisites..."
echo

# Check POSIX tools (required)
echo "=== Required Tools ==="
if ! which gcc >/dev/null 2>&1; then
    echo "❌ Install gcc: sudo apt install build-essential"
    MISSING_REQUIRED=1
else
    echo "✓ gcc found: $(gcc --version | head -1)"
fi

if ! which make >/dev/null 2>&1; then
    echo "❌ Install make: sudo apt install make"
    MISSING_REQUIRED=1
else
    echo "✓ make found: $(make --version | head -1)"
fi

echo

# Check Mac build environment
echo "=== Mac Build Environment ==="
if [[ -z "$RETRO68" ]]; then
    if ! docker compose -f docker/docker-compose.yml ps >/dev/null 2>&1; then
        echo "❌ Neither \$RETRO68 nor Docker available"
        echo "   Setup Docker: Run ./scripts/docker-build.sh"
        MISSING_REQUIRED=1
    else
        echo "✓ Docker available for Mac builds"
    fi
else
    echo "✓ RETRO68=$RETRO68"
fi

echo

# Check quality tools (optional but recommended)
echo "=== Optional Quality Tools ==="
if which lcov >/dev/null 2>&1; then
    echo "✓ lcov found (for coverage reports)"
else
    echo "⚠️  lcov not found - Install: sudo apt install lcov"
    MISSING_OPTIONAL=1
fi

if which clang-format >/dev/null 2>&1; then
    echo "✓ clang-format found"
else
    echo "⚠️  clang-format not found - Install: sudo apt install clang-format"
    MISSING_OPTIONAL=1
fi

if which ctags >/dev/null 2>&1; then
    echo "✓ ctags found"
else
    echo "⚠️  ctags not found - Install: sudo apt install universal-ctags"
    MISSING_OPTIONAL=1
fi

if which python3 >/dev/null 2>&1; then
    echo "✓ python3 found (for ISR validator)"
else
    echo "⚠️  python3 not found - Install: sudo apt install python3"
    MISSING_OPTIONAL=1
fi

echo

# Summary
if [[ $MISSING_REQUIRED -eq 0 ]]; then
    echo "✅ All required tools present"
    if [[ $MISSING_OPTIONAL -gt 0 ]]; then
        echo "⚠️  Some optional tools missing (see above)"
    fi
    exit 0
else
    echo "❌ Missing required tools - please install them first"
    exit 1
fi
