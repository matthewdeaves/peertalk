#!/bin/bash
# Coverage Threshold Check - Verify test coverage after test runs
#
# Checks if coverage meets the 10% minimum threshold from CLAUDE.md.
# Runs lcov inside Docker where it's installed, not on the host.
# This is an informational hook - it warns but doesn't block.
#
# Claude Code hooks receive JSON on stdin, not environment variables.

set -e

# Require jq for JSON parsing
if ! command -v jq >/dev/null 2>&1; then
    echo "[coverage] jq not found - install with: sudo apt install jq"
    exit 0
fi

# Hook logging setup
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOOK_LOG_DIR="$SCRIPT_DIR/../logs"
HOOK_LOG="$HOOK_LOG_DIR/hooks.log"
mkdir -p "$HOOK_LOG_DIR"

log_hook() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] [coverage] $1" >> "$HOOK_LOG"
}

# Read JSON input from stdin
INPUT=$(cat)

# Extract command from JSON
COMMAND=$(echo "$INPUT" | jq -r '.tool_input.command // empty')

# Only run after test commands
if [[ ! "$COMMAND" =~ (make.*test|pytest|\.\/test_|ctest|gcov|lcov) ]]; then
    exit 0
fi

log_hook "Test command detected: $COMMAND"

# Find project directory from cwd in the hook input
CWD=$(echo "$INPUT" | jq -r '.cwd // empty')
if [[ -z "$CWD" ]]; then
    CWD="$(pwd)"
fi

# Look for coverage data on the host (visible via Docker volume mount)
COVERAGE_FILE=""
for candidate in "build/coverage/coverage.info" "coverage.info" "coverage/lcov.info" "lcov.info"; do
    if [[ -f "$CWD/$candidate" ]]; then
        COVERAGE_FILE="$candidate"
        break
    fi
done

if [[ -z "$COVERAGE_FILE" ]]; then
    log_hook "No coverage data found - run 'make docker-coverage' first"
    exit 0
fi

# Check for Docker (lcov runs in the container, not on the host)
DOCKER_COMPOSE="$CWD/docker/docker-compose.yml"
if [[ ! -f "$DOCKER_COMPOSE" ]]; then
    echo "[coverage] No docker/docker-compose.yml found - skipping"
    exit 0
fi

if ! docker compose version >/dev/null 2>&1; then
    echo "[coverage] Docker not available - skipping coverage check"
    exit 0
fi

# Run lcov --summary inside Docker (where lcov is installed)
SUMMARY=$(cd "$CWD" && docker compose -f docker/docker-compose.yml run --rm -T \
    --user "$(id -u):$(id -g)" peertalk-dev \
    lcov --summary "/workspace/$COVERAGE_FILE" 2>&1) || {
    log_hook "lcov failed inside container"
    exit 0
}

# Parse coverage percentage from lcov output
COVERAGE=$(echo "$SUMMARY" | \
    grep -E "lines\.*:" | \
    head -1 | \
    awk -F: '{print $2}' | \
    awk '{print $1}' | \
    tr -d '%')

if [[ -z "$COVERAGE" ]]; then
    log_hook "Could not parse coverage from lcov output"
    exit 0
fi

# 10% threshold from CLAUDE.md Code Quality Gates
THRESHOLD=10.0

# Compare using awk (available everywhere, no bc dependency)
BELOW=$(awk "BEGIN {print ($COVERAGE < $THRESHOLD) ? 1 : 0}")

if [[ "$BELOW" -eq 1 ]]; then
    log_hook "WARNING: Coverage ${COVERAGE}% < ${THRESHOLD}%"
    echo ""
    echo "[coverage] Coverage below threshold"
    echo "           Current:  ${COVERAGE}%"
    echo "           Required: ${THRESHOLD}%"
    echo ""
    echo "           Per CLAUDE.md Code Quality Gates, add tests before committing."
    echo ""
    echo "Next steps:"
    echo "  1. Add tests to tests/test_*.c for new/changed code"
    echo "  2. Run: /build test (to update coverage)"
    echo "  3. Commit when coverage >= 10%"
    echo ""
else
    log_hook "OK: Coverage ${COVERAGE}% >= ${THRESHOLD}%"
    echo "[coverage] ${COVERAGE}% (threshold: ${THRESHOLD}%)"
fi

exit 0
