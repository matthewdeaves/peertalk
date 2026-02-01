#!/bin/bash
# Quick Compile Check - Immediate feedback after file edits
#
# Runs syntax check on edited C files to catch errors early.
# ALL compilation happens in Docker (POSIX + Mac platforms).
# This is an informational hook - it warns but doesn't block.
#
# Claude Code hooks receive JSON on stdin, not environment variables.

set -e

# Require jq for JSON parsing
if ! command -v jq >/dev/null 2>&1; then
    echo "[compile] jq not found - install with: sudo apt install jq"
    exit 0
fi

# Hook logging setup
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOOK_LOG_DIR="$SCRIPT_DIR/../logs"
HOOK_LOG="$HOOK_LOG_DIR/hooks.log"
mkdir -p "$HOOK_LOG_DIR"

log_hook() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] [compile] $1" >> "$HOOK_LOG"
}

# Read JSON input from stdin
INPUT=$(cat)

# Extract file path from JSON (works for both Edit and Write tools)
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')

# Skip if no file path
if [[ -z "$FILE_PATH" ]]; then
    exit 0
fi

# Skip non-C/H files
if [[ ! "$FILE_PATH" =~ \.(c|h)$ ]]; then
    exit 0
fi

# Skip if file doesn't exist yet
if [[ ! -f "$FILE_PATH" ]]; then
    exit 0
fi

log_hook "Checking: $FILE_PATH"

# Get project root by looking for Makefile or include directory
PROJECT_DIR="$FILE_PATH"
while [[ "$PROJECT_DIR" != "/" ]]; do
    PROJECT_DIR="$(dirname "$PROJECT_DIR")"
    if [[ -f "$PROJECT_DIR/Makefile" ]] || [[ -d "$PROJECT_DIR/include" ]]; then
        break
    fi
done

if [[ "$PROJECT_DIR" == "/" ]]; then
    PROJECT_DIR="$(dirname "$FILE_PATH")"
fi

# Check for Docker (required for all builds)
DOCKER_COMPOSE="$PROJECT_DIR/docker/docker-compose.yml"

if [[ ! -f "$DOCKER_COMPOSE" ]]; then
    echo "[compile] No docker/docker-compose.yml found - skipping"
    exit 0
fi

if ! docker compose version >/dev/null 2>&1; then
    echo "[compile] Docker not available - skipping compile check"
    echo "[compile] Install Docker and run: ./scripts/docker-build.sh"
    exit 0
fi

# All compilation happens in Docker
USE_DOCKER=true

# Get relative path for Docker (container mounts project at /workspace)
REL_PATH="${FILE_PATH#$PROJECT_DIR/}"

# Run syntax check in Docker
run_compile() {
    local compiler="$1"
    local includes="$2"

    cd "$PROJECT_DIR"
    docker compose -f docker/docker-compose.yml run --rm -T peertalk-dev \
        $compiler -fsyntax-only -Wall $includes "/workspace/$REL_PATH" 2>&1
}

# Compile in Docker based on platform
if [[ "$FILE_PATH" == *"/mactcp/"* ]] || [[ "$FILE_PATH" == *"/appletalk/"* ]]; then
    # 68k code
    OUTPUT=$(run_compile "m68k-apple-macos-gcc" "-I/workspace/include -I/opt/Retro68-build/toolchain/universal/CIncludes") || {
        log_hook "ERROR: Syntax errors in $FILE_PATH"
        echo ""
        echo "[compile] ⚠️  Syntax errors in $(basename "$FILE_PATH"):"
        echo "$OUTPUT" | head -20
        echo ""
        echo "Next step: Fix the errors above and save again (hook will re-run automatically)"
        exit 0
    }
elif [[ "$FILE_PATH" == *"/opentransport/"* ]]; then
    # PPC code
    OUTPUT=$(run_compile "powerpc-apple-macos-gcc" "-I/workspace/include -I/opt/Retro68-build/toolchain/universal/CIncludes") || {
        echo ""
        echo "[compile] ⚠️  Syntax errors in $(basename "$FILE_PATH"):"
        echo "$OUTPUT" | head -20
        echo ""
        echo "Next step: Fix the errors above and save again (hook will re-run automatically)"
        exit 0
    }
else
    # POSIX code
    OUTPUT=$(run_compile "gcc" "-I/workspace/include") || {
        echo ""
        echo "[compile] ⚠️  Syntax errors in $(basename "$FILE_PATH"):"
        echo "$OUTPUT" | head -20
        echo ""
        echo "Next step: Fix the errors above and save again (hook will re-run automatically)"
        exit 0
    }
fi

log_hook "OK: $FILE_PATH"
echo "[compile] ✓ $(basename "$FILE_PATH") OK"
exit 0
