#!/bin/bash
# Parse make test output and generate JSON with test results
# Input: Test output from stdin or file argument
# Output: JSON to stdout

set -e

# Read input from file if provided, otherwise stdin
if [ $# -eq 1 ]; then
    INPUT_FILE="$1"
else
    INPUT_FILE=$(mktemp)
    cat > "$INPUT_FILE"
    trap "rm -f $INPUT_FILE" EXIT
fi

# Count total tests, passed, and failed
# Test format: "test_name... OK" or "test_name... FAIL"
PASSED=$(grep -c "\.\.\. OK" "$INPUT_FILE" || echo "0")
FAILED=$(grep -c "\.\.\. FAIL" "$INPUT_FILE" || echo "0")

# Ensure clean integers (remove any whitespace)
PASSED=$(echo $PASSED | awk '{print $1}')
FAILED=$(echo $FAILED | awk '{print $1}')

# Default to 0 if empty
PASSED=${PASSED:-0}
FAILED=${FAILED:-0}

TOTAL=$((PASSED + FAILED))

# Calculate pass rate
if [ "$TOTAL" -gt 0 ]; then
    PASS_RATE=$(echo "scale=1; $PASSED * 100 / $TOTAL" | bc)
else
    PASS_RATE="0.0"
fi

# Extract individual test details
TEST_DETAILS=$(grep -E "\.\.\. (OK|FAIL)" "$INPUT_FILE" | awk '
BEGIN {
    print "  \"test_details\": ["
    first = 1
}
{
    if (!first) print ","
    first = 0

    # Status is in field 2 (after "...")
    status = ($2 == "OK") ? "PASS" : "FAIL"
    test_name = $1

    # Extract file name from test name (assumes test_filename pattern)
    # Remove leading/trailing whitespace
    gsub(/^[ \t]+|[ \t]+$/, "", test_name)
    file = "test_unknown.c"

    printf "    {\"file\": \"%s\", \"name\": \"%s\", \"status\": \"%s\"}", file, test_name, status
}
END {
    print ""
    print "  ]"
}
' || echo '  "test_details": []')

# Generate JSON
cat <<EOF
{
  "total_tests": $TOTAL,
  "passed": $PASSED,
  "failed": $FAILED,
  "pass_rate": $PASS_RATE,
$TEST_DETAILS
}
EOF
