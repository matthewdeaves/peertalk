#!/bin/bash
# ADSP userFlags Validator - Warns when userFlags accessed without clearing
#
# This is a WARNING hook (exit 0), not a blocking hook.
# It reminds developers about the critical requirement to clear userFlags.
#
# Verified in Programming With AppleTalk (Lines 5780-5782):
# "Once the attention message has been received, you must set the userFlags to zero.
#  This allows another attention message, or other unsolicited connection event, to occur.
#  Failure to clear the userFlags will result in your connection hanging."
#
# Claude Code hooks receive JSON on stdin, not environment variables.

set -e

# Require jq for JSON parsing
if ! command -v jq >/dev/null 2>&1; then
    echo "[userflags] jq not found - install with: sudo apt install jq"
    exit 0
fi

# Hook logging setup
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HOOK_LOG_DIR="$SCRIPT_DIR/../logs"
HOOK_LOG="$HOOK_LOG_DIR/hooks.log"
mkdir -p "$HOOK_LOG_DIR"

log_hook() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] [userflags] $1" >> "$HOOK_LOG"
}

# Read JSON input from stdin
INPUT=$(cat)

# Extract file path and new content from JSON
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')
NEW_CONTENT=$(echo "$INPUT" | jq -r '.tool_input.new_string // empty')

# Only check AppleTalk code
if [[ ! "$FILE_PATH" =~ appletalk ]]; then
    exit 0
fi

if [[ -z "$NEW_CONTENT" ]]; then
    exit 0
fi

# Check for userFlags access (reading the field)
if echo "$NEW_CONTENT" | grep -qE -- '->userFlags|\.userFlags'; then
    log_hook "Checking userFlags in $FILE_PATH"
    # Check if it's being cleared properly somewhere in the same edit
    # Look for pattern: userFlags = 0 or userFlags=0
    if ! echo "$NEW_CONTENT" | grep -qE 'userFlags\s*=\s*0'; then
        log_hook "WARNING: userFlags accessed but not cleared in $FILE_PATH"
        echo ""
        echo "⚠️  WARNING: ADSP userFlags Access Detected"
        echo "==========================================="
        echo ""
        echo "CRITICAL: userFlags MUST be cleared after reading!"
        echo ""
        echo "From Programming With AppleTalk (Lines 5780-5782):"
        echo "  'Failure to clear the userFlags will result in"
        echo "   your connection hanging.'"
        echo ""
        echo "Required pattern in userRoutine:"
        echo "  uint8_t flags = ccb->userFlags;"
        echo "  ccb->userFlags = 0;  /* Clear IMMEDIATELY */"
        echo "  /* Now process the captured flags */"
        echo ""
        echo "Next steps:"
        echo "  1. Verify your code follows the pattern above"
        echo "  2. If correct: Continue (this may be a false positive)"
        echo "  3. If missing clear: Add 'ccb->userFlags = 0;' immediately after read"
        echo "  4. Reference: .claude/rules/appletalk.md 'CRITICAL: userFlags Must Be Cleared'"
        echo ""
        # Warning only, don't block
    else
        log_hook "OK: userFlags properly cleared in $FILE_PATH"
    fi
fi

exit 0
