#!/bin/bash
# Parse lcov coverage.info and generate JSON
# Reads from: build/coverage/coverage.info
# Output: JSON to stdout

set -e

COVERAGE_FILE="build/coverage/coverage.info"

if [ ! -f "$COVERAGE_FILE" ]; then
    echo '{"error": "Coverage file not found", "line_coverage": 0, "function_coverage": 0, "lines_covered": 0, "lines_total": 0, "functions_covered": 0, "functions_total": 0}' >&2
    exit 1
fi

lcov --summary "$COVERAGE_FILE" 2>&1 | awk '
  /lines......:/ {
    line_pct=$2;
    gsub(/%/, "", line_pct);
    split($4, a, "/");
    line_covered=a[1];
    line_total=a[2];
  }
  /functions..:/ {
    func_pct=$2;
    gsub(/%/, "", func_pct);
    split($4, a, "/");
    func_covered=a[1];
    func_total=a[2];
  }
  END {
    printf "{\n";
    printf "  \"line_coverage\": %.1f,\n", line_pct;
    printf "  \"function_coverage\": %.1f,\n", func_pct;
    printf "  \"lines_covered\": %d,\n", line_covered;
    printf "  \"lines_total\": %d,\n", line_total;
    printf "  \"functions_covered\": %d,\n", func_covered;
    printf "  \"functions_total\": %d\n", func_total;
    printf "}\n";
  }
'
