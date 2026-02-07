#!/bin/bash
# extract_test_count.sh - Extract test counts from test files and output
#
# Analyzes test source files and test output to count:
# - Total test functions defined
# - Tests per file
# - Pass/fail counts from test output

set -e

# Find test directory
if [ -d "tests" ]; then
    TEST_DIR="tests"
elif [ -d "../tests" ]; then
    TEST_DIR="../tests"
else
    echo '{"error": "tests directory not found"}'
    exit 1
fi

# Count test functions in source files
count_test_functions() {
    local file="$1"
    local count=0

    if [ -f "$file" ]; then
        # Count TEST("...") macro invocations (custom framework)
        local custom_tests
        custom_tests=$(grep -c 'TEST("' "$file" 2>/dev/null) || custom_tests=0

        # Count RUN_TEST(...) for Unity-style tests
        local unity_tests
        unity_tests=$(grep -c 'RUN_TEST(' "$file" 2>/dev/null) || unity_tests=0

        # Use the maximum of these (they overlap for different styles)
        count=$custom_tests
        if [ "$unity_tests" -gt "$count" ]; then
            count=$unity_tests
        fi
    fi

    echo "$count"
}

# Parse test output for pass/fail
parse_test_output() {
    local output_file="$1"
    local passed=0
    local failed=0

    if [ -f "$output_file" ]; then
        # Look for "Passed: N" pattern
        local passed_match
        passed_match=$(grep -oP 'Passed:\s*\K\d+' "$output_file" 2>/dev/null | tail -1) || true
        if [ -n "$passed_match" ]; then
            passed=$passed_match
        fi

        local failed_match
        failed_match=$(grep -oP 'Failed:\s*\K\d+' "$output_file" 2>/dev/null | tail -1) || true
        if [ -n "$failed_match" ]; then
            failed=$failed_match
        fi

        # Also try Unity-style output: "N Tests M Failures"
        if [ "$passed" = "0" ] && [ "$failed" = "0" ]; then
            local unity_line
            unity_line=$(grep -oP '\d+ Tests \d+ Failures' "$output_file" 2>/dev/null | tail -1) || true
            if [ -n "$unity_line" ]; then
                local total
                total=$(echo "$unity_line" | grep -oP '^\d+') || true
                local fail_count
                fail_count=$(echo "$unity_line" | grep -oP '\d+(?= Failures)') || true
                if [ -n "$total" ] && [ -n "$fail_count" ]; then
                    failed=$fail_count
                    passed=$((total - failed))
                fi
            fi
        fi
    fi

    echo "$passed $failed"
}

# Get list of test files
test_files=$(find "$TEST_DIR" -maxdepth 1 -name "test_*.c" -type f 2>/dev/null | sort)

# Build JSON output
total_tests=0
total_passed=0
total_failed=0
file_counts=""
first=1

for file in $test_files; do
    filename=$(basename "$file")
    count=$(count_test_functions "$file")
    total_tests=$((total_tests + count))

    if [ "$first" = "1" ]; then
        first=0
    else
        file_counts="$file_counts,"
    fi

    file_counts="$file_counts
    \"$filename\": $count"
done

# Try to get pass/fail from test output if provided
if [ -n "$1" ] && [ -f "$1" ]; then
    read passed failed <<< $(parse_test_output "$1")
    total_passed=$passed
    total_failed=$failed
fi

# Calculate test file count
test_file_count=$(echo "$test_files" | wc -w)

# Count categories
unit_count=0
integration_count=0
perf_count=0
fuzz_count=0

for file in $test_files; do
    fname=$(basename "$file")
    if echo "$fname" | grep -q "integration"; then
        integration_count=$((integration_count + 1))
    elif echo "$fname" | grep -q "perf"; then
        perf_count=$((perf_count + 1))
    elif echo "$fname" | grep -q "fuzz"; then
        fuzz_count=$((fuzz_count + 1))
    else
        unit_count=$((unit_count + 1))
    fi
done

# Output JSON
cat << EOF
{
  "timestamp": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
  "summary": {
    "total_tests": $total_tests,
    "test_files": $test_file_count,
    "passed": $total_passed,
    "failed": $total_failed
  },
  "by_file": {$file_counts
  },
  "categories": {
    "unit_tests": $unit_count,
    "integration_tests": $integration_count,
    "performance_tests": $perf_count,
    "fuzz_tests": $fuzz_count
  }
}
EOF
