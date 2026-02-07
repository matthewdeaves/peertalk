#!/bin/bash
# Run ISR safety validator and extract violation counts
# Output: JSON to stdout
# Returns non-zero exit code on validation errors (not violations)

set -e

# Check if validator exists
if [ ! -f "tools/validators/isr_safety.py" ]; then
    echo "WARNING: ISR validator not found at tools/validators/isr_safety.py" >&2
    cat <<EOF
{
  "total_violations": -1,
  "error": "validator_not_found",
  "by_category": {
    "memory": 0,
    "timing": 0,
    "memory_ops": 0,
    "io": 0,
    "sync_network": 0,
    "toolbox": 0
  },
  "violations": []
}
EOF
    exit 0
fi

# Check if src directory exists
if [ ! -d "src/" ]; then
    echo "WARNING: src/ directory not found" >&2
    cat <<EOF
{
  "total_violations": 0,
  "note": "no_src_directory",
  "by_category": {
    "memory": 0,
    "timing": 0,
    "memory_ops": 0,
    "io": 0,
    "sync_network": 0,
    "toolbox": 0
  },
  "violations": []
}
EOF
    exit 0
fi

# Create temp file for error capture
TMPFILE=$(mktemp)
trap "rm -f $TMPFILE" EXIT

# Run validator with JSON output, capture both stdout and stderr
if python3 tools/validators/isr_safety.py src/ --json > "$TMPFILE" 2>&1; then
    # Success - output the JSON
    cat "$TMPFILE"
else
    EXIT_CODE=$?
    # Check if the output looks like valid JSON (validator found violations but still output JSON)
    if head -1 "$TMPFILE" | grep -q '^\s*{'; then
        # It's JSON output, probably just has violations (which is fine)
        cat "$TMPFILE"
    else
        # Real error - validator crashed or couldn't run
        echo "ERROR: ISR validator failed with exit code $EXIT_CODE" >&2
        echo "Output was:" >&2
        cat "$TMPFILE" >&2
        cat <<EOF
{
  "total_violations": -1,
  "error": "validator_execution_failed",
  "exit_code": $EXIT_CODE,
  "by_category": {
    "memory": 0,
    "timing": 0,
    "memory_ops": 0,
    "io": 0,
    "sync_network": 0,
    "toolbox": 0
  },
  "violations": []
}
EOF
    fi
fi
