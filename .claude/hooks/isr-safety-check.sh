#!/bin/bash
# ISR Safety Gate - BLOCKS edits that introduce ISR safety violations
#
# This hook runs before Edit operations on Mac networking code.
# It checks the new content for forbidden function calls in callback contexts.
#
# Claude Code hooks receive JSON on stdin, not environment variables.
#
# Exit 0: Allow the edit
# Exit 1: Block the edit (violation found)

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

# Extract file path and new content from JSON
# Edit tool uses 'new_string', Write tool uses 'content'
FILE_PATH=$(echo "$INPUT" | jq -r '.tool_input.file_path // empty')
NEW_CONTENT=$(echo "$INPUT" | jq -r '.tool_input.new_string // .tool_input.content // empty')

# Skip if no file path
if [[ -z "$FILE_PATH" ]]; then
    exit 0
fi

# Only check Mac networking code paths
if [[ ! "$FILE_PATH" =~ (mactcp|opentransport|appletalk) ]]; then
    exit 0
fi

# Skip if no new content
if [[ -z "$NEW_CONTENT" ]]; then
    exit 0
fi

log_hook "Checking: $FILE_PATH"

# Path to forbidden calls database
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
FORBIDDEN_FILE="$PROJECT_DIR/tools/validators/forbidden_calls.txt"

if [[ ! -f "$FORBIDDEN_FILE" ]]; then
    # No database, can't check - allow
    exit 0
fi

# Check for callback function patterns that indicate ISR context
# These patterns match the signatures documented in:
#   - mactcp.md: TCPNotifyProcPtr, UDPNotifyProcPtr
#   - opentransport.md: OTNotifyProcPtr
#   - appletalk.md: ADSPCompletionUPP, ADSPConnectionEventUPP
CALLBACK_PATTERNS=(
    "_asr\s*\("
    "_notifier\s*\("
    "_completion\s*\("
    "_callback\s*\("
    "pascal\s+void.*StreamPtr"       # MacTCP TCP/UDP ASR
    "pascal\s+void.*OTEventCode"     # Open Transport notifier
    "pascal\s+void.*DSPPBPtr"        # ADSP ioCompletion
    "pascal\s+void.*TPCCB"           # ADSP userRoutine
)

HAS_CALLBACK=false
for pattern in "${CALLBACK_PATTERNS[@]}"; do
    if echo "$NEW_CONTENT" | grep -qE "$pattern"; then
        HAS_CALLBACK=true
        break
    fi
done

if [[ "$HAS_CALLBACK" != "true" ]]; then
    # No callback functions in this edit, no need to check ISR safety
    log_hook "PASS: No callbacks in $FILE_PATH"
    exit 0
fi

log_hook "Callback detected in $FILE_PATH, checking for violations..."

# Check for forbidden calls in the new content
VIOLATIONS=""

while IFS='|' read -r func category reason; do
    # Skip comments and empty lines
    [[ "$func" =~ ^# ]] && continue
    [[ -z "$func" ]] && continue

    # Trim whitespace
    func=$(echo "$func" | xargs)
    reason=$(echo "$reason" | xargs)

    # Check if function is called (word boundary + parenthesis)
    if echo "$NEW_CONTENT" | grep -qE "\b${func}\s*\("; then
        VIOLATIONS="${VIOLATIONS}  - ${func}: ${reason}\n"
    fi
done < "$FORBIDDEN_FILE"

if [[ -n "$VIOLATIONS" ]]; then
    log_hook "BLOCKED: Violations in $FILE_PATH"
    echo ""
    echo "BLOCKED: ISR Safety Violations Detected"
    echo "========================================"
    echo ""
    echo "The following forbidden calls were found in callback code:"
    echo -e "$VIOLATIONS"
    echo ""
    echo "Next steps:"
    echo "  1. Review patterns: .claude/rules/isr-safety.md"
    echo "  2. Check all violations: /check-isr $FILE_PATH"
    echo "  3. Fix violations using safe alternatives below"
    echo "  4. Re-attempt your edit"
    echo ""
    echo "Common fixes:"
    echo "  memcpy/BlockMove -> pt_memcpy_isr() (see CLAUDE.md 'ISR-Safe Queue Push')"
    echo "  malloc/NewPtr    -> Pre-allocated buffers in context struct"
    echo "  TickCount        -> Set timestamp=0, let main loop timestamp later"
    echo "  Sync network     -> Use async version with completion callback"
    echo "  printf/logging   -> Set flag, log from main loop"
    echo ""
    echo "Reference: Inside Macintosh Volume VI Table B-3 (lines 224396-224607)"
    exit 1
fi

log_hook "PASS: No violations in $FILE_PATH"
exit 0
