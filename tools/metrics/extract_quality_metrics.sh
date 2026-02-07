#!/bin/bash
# Extract code quality metrics
# Output: JSON to stdout

set -e

# Count files > 500 lines
LARGE_FILES=0
if [ -d "src" ] || [ -d "include" ]; then
    LARGE_FILES=$(find src include -name "*.c" -o -name "*.h" 2>/dev/null | \
      while read f; do
        if [ -f "$f" ]; then
            LINES=$(wc -l < "$f")
            if [ "$LINES" -gt 500 ]; then
              echo "$f"
            fi
        fi
      done | wc -l || echo "0")
fi

# Count TODOs and FIXMEs
TODO_COUNT=0
FIXME_COUNT=0
if [ -d "src" ] || [ -d "include" ]; then
    TODO_COUNT=$(grep -r "TODO" --include="*.c" --include="*.h" src include 2>/dev/null | wc -l || echo "0")
    FIXME_COUNT=$(grep -r "FIXME" --include="*.c" --include="*.h" src include 2>/dev/null | wc -l || echo "0")
fi

# Compiler warnings (would need to be passed from CI)
COMPILER_WARNINGS=0

cat <<EOF
{
  "files_over_500_lines": $LARGE_FILES,
  "todo_count": $TODO_COUNT,
  "fixme_count": $FIXME_COUNT,
  "compiler_warnings": $COMPILER_WARNINGS
}
EOF
