#!/bin/bash
# extract_cppcheck.sh - Run cppcheck static analysis and output JSON
#
# Detects:
# - Buffer overflows, null pointer dereferences
# - Memory leaks, resource leaks
# - Undefined behavior
# - Style issues, performance hints

set -e

# Find source directories
SRC_DIRS=""
[ -d "src" ] && SRC_DIRS="$SRC_DIRS src"
[ -d "include" ] && SRC_DIRS="$SRC_DIRS include"

if [ -z "$SRC_DIRS" ]; then
    cat <<EOF
{
  "tool": "cppcheck",
  "version": "unknown",
  "total_issues": 0,
  "by_severity": {},
  "issues": [],
  "note": "No source directories found"
}
EOF
    exit 0
fi

# Check if cppcheck is available
if ! command -v cppcheck &> /dev/null; then
    cat <<EOF
{
  "tool": "cppcheck",
  "version": "not installed",
  "total_issues": 0,
  "by_severity": {},
  "issues": [],
  "note": "Install cppcheck: apt-get install cppcheck"
}
EOF
    exit 0
fi

VERSION=$(cppcheck --version 2>&1 | head -1)

# Create temp file for XML output
TMPFILE=$(mktemp)
trap "rm -f $TMPFILE" EXIT

# Run cppcheck with XML output
# Suppress some noisy checks that don't apply to embedded/Mac code
cppcheck \
    --enable=all \
    --xml \
    --xml-version=2 \
    --suppress=missingIncludeSystem \
    --suppress=unusedFunction \
    --suppress=unmatchedSuppression \
    --inline-suppr \
    -I include \
    -I src/core \
    --platform=unix64 \
    $SRC_DIRS 2> "$TMPFILE" || true

# Parse XML and convert to JSON
python3 << PYTHON
import xml.etree.ElementTree as ET
import json
import sys

try:
    tree = ET.parse("$TMPFILE")
    root = tree.getroot()
except:
    print(json.dumps({
        "tool": "cppcheck",
        "version": "$VERSION",
        "total_issues": 0,
        "by_severity": {},
        "issues": []
    }))
    sys.exit(0)

issues = []
severity_counts = {}

errors = root.find('errors')
if errors is not None:
    for error in errors.findall('error'):
        severity = error.get('severity', 'unknown')
        severity_counts[severity] = severity_counts.get(severity, 0) + 1

        location = error.find('location')
        file_path = location.get('file', '') if location is not None else ''
        line = int(location.get('line', 0)) if location is not None else 0

        issues.append({
            "id": error.get('id', ''),
            "severity": severity,
            "message": error.get('msg', ''),
            "file": file_path,
            "line": line
        })

# Sort by severity (error > warning > style > performance > information)
severity_order = {'error': 0, 'warning': 1, 'style': 2, 'performance': 3, 'portability': 4, 'information': 5}
issues.sort(key=lambda x: (severity_order.get(x['severity'], 99), x['file'], x['line']))

result = {
    "tool": "cppcheck",
    "version": "$VERSION",
    "total_issues": len(issues),
    "by_severity": severity_counts,
    "issues": issues[:50]  # Limit to 50 issues in output
}

if len(issues) > 50:
    result["note"] = f"Showing 50 of {len(issues)} issues"

print(json.dumps(result, indent=2))
PYTHON
