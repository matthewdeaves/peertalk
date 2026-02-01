#!/bin/bash
# Quick Compile Check - Immediate feedback after file edits
#
# Runs a fast syntax check on edited C files to catch errors early.
# Uses Docker container if Retro68 not installed locally.
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

# Check for local Retro68 or Docker availability
RETRO68_LOCAL="${RETRO68:-}"
DOCKER_COMPOSE="$PROJECT_DIR/docker/docker-compose.yml"
USE_DOCKER=false

# Determine if we need Retro68 (Mac-specific code)
NEEDS_RETRO68=false
if [[ "$FILE_PATH" == *"/mactcp/"* ]] || [[ "$FILE_PATH" == *"/opentransport/"* ]] || [[ "$FILE_PATH" == *"/appletalk/"* ]]; then
    NEEDS_RETRO68=true
fi

# Decide whether to use Docker
if [[ "$NEEDS_RETRO68" == "true" ]]; then
    if [[ -z "$RETRO68_LOCAL" ]] || [[ ! -d "$RETRO68_LOCAL" ]]; then
        # No local Retro68, try Docker (prefer modern "docker compose" over deprecated "docker-compose")
        if [[ -f "$DOCKER_COMPOSE" ]] && docker compose version >/dev/null 2>&1; then
            USE_DOCKER=true
        else
            echo "[compile] Skipping Mac check - no Retro68 or Docker available"
            exit 0
        fi
    fi
fi

# Get relative path for Docker (container mounts project at /workspace)
REL_PATH="${FILE_PATH#$PROJECT_DIR/}"

# Run syntax check based on platform
run_compile() {
    local compiler="$1"
    local includes="$2"

    if [[ "$USE_DOCKER" == "true" ]]; then
        # Use Docker container (modern "docker compose" command)
        cd "$PROJECT_DIR"
        docker compose -f docker/docker-compose.yml run --rm -T peertalk-dev \
            $compiler -fsyntax-only -Wall $includes "/workspace/$REL_PATH" 2>&1
    else
        # Use local compiler
        $compiler -fsyntax-only -Wall $includes "$FILE_PATH" 2>&1
    fi
}

if [[ "$FILE_PATH" == *"/mactcp/"* ]] || [[ "$FILE_PATH" == *"/appletalk/"* ]]; then
    # 68k code
    if [[ "$USE_DOCKER" == "true" ]]; then
        OUTPUT=$(run_compile "m68k-apple-macos-gcc" "-I/workspace/include -I/opt/Retro68-build/toolchain/universal/CIncludes") || {
            log_hook "ERROR: Syntax errors in $FILE_PATH"
            echo ""
            echo "[compile] ⚠️  Syntax errors in $(basename "$FILE_PATH"):"
            echo "$OUTPUT" | head -20
            echo ""
            echo "Next step: Fix the errors above and save again (hook will re-run automatically)"
            exit 0
        }
    else
        OUTPUT=$(run_compile "$RETRO68_LOCAL/bin/m68k-apple-macos-gcc" "-I$PROJECT_DIR/include -I$RETRO68_LOCAL/universal/CIncludes") || {
            echo ""
            echo "[compile] ⚠️  Syntax errors in $(basename "$FILE_PATH"):"
            echo "$OUTPUT" | head -20
            echo ""
            echo "Next step: Fix the errors above and save again (hook will re-run automatically)"
            exit 0
        }
    fi
elif [[ "$FILE_PATH" == *"/opentransport/"* ]]; then
    # PPC code
    if [[ "$USE_DOCKER" == "true" ]]; then
        OUTPUT=$(run_compile "powerpc-apple-macos-gcc" "-I/workspace/include -I/opt/Retro68-build/toolchain/universal/CIncludes") || {
            echo ""
            echo "[compile] ⚠️  Syntax errors in $(basename "$FILE_PATH"):"
            echo "$OUTPUT" | head -20
            echo ""
            echo "Next step: Fix the errors above and save again (hook will re-run automatically)"
            exit 0
        }
    else
        OUTPUT=$(run_compile "$RETRO68_LOCAL/bin/powerpc-apple-macos-gcc" "-I$PROJECT_DIR/include -I$RETRO68_LOCAL/universal/CIncludes") || {
            echo ""
            echo "[compile] ⚠️  Syntax errors in $(basename "$FILE_PATH"):"
            echo "$OUTPUT" | head -20
            echo ""
            echo "Next step: Fix the errors above and save again (hook will re-run automatically)"
            exit 0
        }
    fi
else
    # POSIX code - use local gcc
    if ! command -v gcc >/dev/null 2>&1; then
        echo "[compile] gcc not found"
        exit 0
    fi

    OUTPUT=$(gcc -fsyntax-only -Wall -I"$PROJECT_DIR/include" "$FILE_PATH" 2>&1) || {
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
