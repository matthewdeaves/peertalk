#!/bin/bash
# ISR Safety Gate - BLOCKS edits that introduce ISR safety violations
#
# This hook runs before Edit and Write operations on Mac networking code.
# It delegates to a Python validator that correctly scopes checks to
# callback function bodies only (notifiers, ASRs, completion routines).
#
# For Edit operations, the validator reconstructs the full file to check
# the complete callback context, not just the edited snippet.
#
# Claude Code hooks receive JSON on stdin, not environment variables.
#
# Exit 0: Allow the edit
# Exit 2: Block the edit (violation found)

set -e

# Require jq for JSON parsing
if ! command -v jq >/dev/null 2>&1; then
    echo "[isr-safety] jq not found - install with: sudo apt install jq"
    exit 0  # Don't block, just warn
fi

# Hook logging setup
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOOK_LOG_DIR="$SCRIPT_DIR/../logs"
HOOK_LOG="$HOOK_LOG_DIR/hooks.log"
mkdir -p "$HOOK_LOG_DIR"

log_hook() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] [isr-safety] $1" >> "$HOOK_LOG"
}

# Read JSON input from stdin
INPUT=$(cat)

# Extract file path from JSON
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')

# Skip if no file path
if [[ -z "$FILE_PATH" ]]; then
    exit 0
fi

# Only check Mac networking code paths
if [[ ! "$FILE_PATH" =~ (mactcp|opentransport|appletalk) ]]; then
    exit 0
fi

log_hook "Checking: $FILE_PATH"

# Require Python for the validator
if ! command -v python3 >/dev/null 2>&1; then
    echo "[isr-safety] python3 not found - skipping ISR safety check"
    exit 0
fi

# Run the Python validator (scopes checks to callback function bodies only)
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
VALIDATOR="$PROJECT_DIR/tools/validators/isr_safety_hook.py"

if [[ ! -f "$VALIDATOR" ]]; then
    log_hook "Validator not found: $VALIDATOR"
    exit 0
fi

VIOLATIONS=""
EXIT_CODE=0
VIOLATIONS=$(echo "$INPUT" | python3 "$VALIDATOR" 2>&1) || EXIT_CODE=$?

if [[ $EXIT_CODE -eq 1 ]]; then
    log_hook "BLOCKED: Violations in $FILE_PATH"
    echo "" >&2
    echo "BLOCKED: ISR Safety Violations Detected" >&2
    echo "========================================" >&2
    echo "" >&2
    echo "The following forbidden calls were found inside callback functions:" >&2
    echo "$VIOLATIONS" >&2
    echo "" >&2
    echo "Next steps:" >&2
    echo "  1. Review patterns: .claude/rules/isr-safety.md" >&2
    echo "  2. Check all violations: /check-isr $FILE_PATH" >&2
    echo "  3. Fix violations using safe alternatives below" >&2
    echo "  4. Re-attempt your edit" >&2
    echo "" >&2
    echo "Common fixes:" >&2
    echo "  memcpy/BlockMove -> pt_memcpy_isr() (see CLAUDE.md 'ISR-Safe Queue Push')" >&2
    echo "  malloc/NewPtr    -> Pre-allocated buffers in context struct" >&2
    echo "  TickCount        -> Set timestamp=0, let main loop timestamp later" >&2
    echo "  Sync network     -> Use async version with completion callback" >&2
    echo "  printf/logging   -> Set flag, log from main loop" >&2
    echo "" >&2
    echo "Reference: Inside Macintosh Volume VI Table B-3 (lines 224396-224607)" >&2
    exit 2
elif [[ $EXIT_CODE -ne 0 ]]; then
    # Python error (not a violation) - don't block, just log
    log_hook "Validator error (exit $EXIT_CODE) - allowing edit"
    exit 0
fi

log_hook "PASS: No violations in $FILE_PATH"
exit 0
