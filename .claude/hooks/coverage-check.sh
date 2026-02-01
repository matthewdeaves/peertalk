#!/bin/bash
# Coverage Threshold Check - Verify test coverage after test runs
#
# Checks if coverage meets the 10% minimum threshold from CLAUDE.md.
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

# Find project directory from cwd in the hook input
CWD=$(echo "$INPUT" | jq -r '.cwd // empty')
if [[ -z "$CWD" ]]; then
    CWD="$(pwd)"
fi

COVERAGE_FILE=""

# Look for coverage data in common locations
for candidate in "coverage.info" "coverage/lcov.info" "build/coverage.info" "lcov.info"; do
    if [[ -f "$CWD/$candidate" ]]; then
        COVERAGE_FILE="$CWD/$candidate"
        break
    fi
done

if [[ -z "$COVERAGE_FILE" ]]; then
    # No coverage data yet - might be first test run
    exit 0
fi

# Check for lcov
if ! command -v lcov >/dev/null 2>&1; then
    echo "[coverage] lcov not installed - skipping coverage check"
    exit 0
fi

# Get coverage percentage
COVERAGE=$(lcov --summary "$COVERAGE_FILE" 2>&1 | \
    grep -E "lines\.*:" | \
    head -1 | \
    awk -F: '{print $2}' | \
    awk '{print $1}' | \
    tr -d '%')

if [[ -z "$COVERAGE" ]]; then
    echo "[coverage] Could not parse coverage data"
    exit 0
fi

# 10% threshold from CLAUDE.md Code Quality Gates
THRESHOLD=10.0

# Compare (using bc for floating point)
if command -v bc >/dev/null 2>&1; then
    if (( $(echo "$COVERAGE < $THRESHOLD" | bc -l) )); then
        log_hook "WARNING: Coverage ${COVERAGE}% < ${THRESHOLD}%"
        echo ""
        echo "[coverage] ⚠️  Coverage below threshold"
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
        echo "[coverage] ✓ ${COVERAGE}% (threshold: ${THRESHOLD}%)"
    fi
else
    # No bc, just report the number
    log_hook "Coverage ${COVERAGE}% (bc not available for comparison)"
    echo "[coverage] ${COVERAGE}% (threshold: ${THRESHOLD}%)"
fi

exit 0
