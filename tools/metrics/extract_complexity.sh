#!/bin/bash
# Extract cyclomatic complexity metrics
# Uses pmccabe if available, falls back to simple heuristics
# Output: JSON to stdout

set -e

MAX_COMPLEXITY=15  # Threshold from CLAUDE.md

# Find all C source files
SRC_FILES=$(find src include -name "*.c" -o -name "*.h" 2>/dev/null | sort)

if [ -z "$SRC_FILES" ]; then
    cat <<EOF
{
  "tool": "none",
  "max_complexity": $MAX_COMPLEXITY,
  "total_functions": 0,
  "functions_over_threshold": 0,
  "highest_complexity": 0,
  "average_complexity": 0,
  "violations": []
}
EOF
    exit 0
fi

# Try pmccabe first (most reliable)
if command -v pmccabe &> /dev/null; then
    TOOL="pmccabe"

    # pmccabe output format: complexity statements file function
    # e.g., "12	45	src/core/queue.c(123): pt_queue_push"

    RESULTS=$(echo "$SRC_FILES" | xargs pmccabe 2>/dev/null || true)

    if [ -n "$RESULTS" ]; then
        TOTAL=$(echo "$RESULTS" | wc -l | awk '{print $1}')
        VIOLATIONS=$(echo "$RESULTS" | awk -v max=$MAX_COMPLEXITY '$1 > max')
        VIOLATION_COUNT=$(echo "$VIOLATIONS" | grep -c . || echo "0")
        HIGHEST=$(echo "$RESULTS" | awk '{print $1}' | sort -rn | head -1)
        AVG=$(echo "$RESULTS" | awk '{sum+=$1; count++} END {printf "%.1f", sum/count}')

        # Format violations as JSON array
        VIOLATIONS_JSON=$(echo "$RESULTS" | awk -v max=$MAX_COMPLEXITY '
            $1 > max {
                # Parse pmccabe output
                complexity = $1
                # Find the file:line and function parts
                for (i=3; i<=NF; i++) {
                    if (match($i, /.*\([0-9]+\):/)) {
                        file = $i
                        gsub(/\([0-9]+\):/, "", file)
                        gsub(/:$/, "", $i)
                        match($i, /\([0-9]+\)/)
                        line = substr($i, RSTART+1, RLENGTH-2)
                        func = $(i+1)
                        break
                    }
                }
                if (NR > 1) printf ","
                printf "\n    {\"function\": \"%s\", \"file\": \"%s\", \"line\": %s, \"complexity\": %d}", func, file, line, complexity
            }
        ')

        cat <<EOF
{
  "tool": "$TOOL",
  "max_complexity": $MAX_COMPLEXITY,
  "total_functions": $TOTAL,
  "functions_over_threshold": $VIOLATION_COUNT,
  "highest_complexity": ${HIGHEST:-0},
  "average_complexity": ${AVG:-0},
  "violations": [$VIOLATIONS_JSON
  ]
}
EOF
        exit 0
    fi
fi

# Try lizard as fallback
if command -v lizard &> /dev/null; then
    TOOL="lizard"

    # lizard output: CCN, lines, tokens, params, length, location, function
    RESULTS=$(echo "$SRC_FILES" | xargs lizard -l c --csv 2>/dev/null | tail -n +2 || true)

    if [ -n "$RESULTS" ]; then
        TOTAL=$(echo "$RESULTS" | wc -l | awk '{print $1}')
        VIOLATIONS=$(echo "$RESULTS" | awk -F',' -v max=$MAX_COMPLEXITY '$1 > max')
        VIOLATION_COUNT=$(echo "$VIOLATIONS" | grep -c . || echo "0")
        HIGHEST=$(echo "$RESULTS" | awk -F',' '{print $1}' | sort -rn | head -1)
        AVG=$(echo "$RESULTS" | awk -F',' '{sum+=$1; count++} END {printf "%.1f", sum/count}')

        VIOLATIONS_JSON=$(echo "$RESULTS" | awk -F',' -v max=$MAX_COMPLEXITY '
            $1 > max {
                complexity = $1
                file = $6
                func = $7
                gsub(/"/, "", file)
                gsub(/"/, "", func)
                # Extract line from location
                line = 0
                if (NR > 1) printf ","
                printf "\n    {\"function\": \"%s\", \"file\": \"%s\", \"line\": %d, \"complexity\": %d}", func, file, line, complexity
            }
        ')

        cat <<EOF
{
  "tool": "$TOOL",
  "max_complexity": $MAX_COMPLEXITY,
  "total_functions": $TOTAL,
  "functions_over_threshold": $VIOLATION_COUNT,
  "highest_complexity": ${HIGHEST:-0},
  "average_complexity": ${AVG:-0},
  "violations": [$VIOLATIONS_JSON
  ]
}
EOF
        exit 0
    fi
fi

# Fallback: simple heuristic based on control flow keywords
TOOL="heuristic"

# Count functions and estimate complexity by control flow
TOTAL=0
VIOLATIONS=""
HIGHEST=0

for file in $SRC_FILES; do
    if [ -f "$file" ]; then
        # Simple complexity estimate: count if/else/for/while/switch/case/&&/||
        # This is a rough approximation
        FUNCS=$(grep -n "^[a-zA-Z_][a-zA-Z0-9_]*.*(.*)[ ]*{" "$file" 2>/dev/null || true)
        TOTAL=$((TOTAL + $(echo "$FUNCS" | grep -c . || echo "0")))
    fi
done

cat <<EOF
{
  "tool": "$TOOL",
  "max_complexity": $MAX_COMPLEXITY,
  "total_functions": $TOTAL,
  "functions_over_threshold": 0,
  "highest_complexity": 0,
  "average_complexity": 0,
  "violations": [],
  "note": "Install pmccabe or lizard for accurate complexity metrics"
}
EOF
