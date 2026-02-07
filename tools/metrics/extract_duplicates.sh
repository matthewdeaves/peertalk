#!/bin/bash
# extract_duplicates.sh - Detect copy-paste code using jscpd
#
# Finds duplicated code blocks that should be refactored into
# shared functions or macros.

set -e

# Find source directories
SRC_DIRS=""
[ -d "src" ] && SRC_DIRS="$SRC_DIRS src"
[ -d "include" ] && SRC_DIRS="$SRC_DIRS include"
[ -d "tests" ] && SRC_DIRS="$SRC_DIRS tests"

if [ -z "$SRC_DIRS" ]; then
    cat <<EOF
{
  "tool": "jscpd",
  "version": "unknown",
  "total_duplicates": 0,
  "total_lines": 0,
  "duplicate_lines": 0,
  "percentage": 0,
  "clones": [],
  "note": "No source directories found"
}
EOF
    exit 0
fi

# Check if jscpd is available
if ! command -v jscpd &> /dev/null; then
    cat <<EOF
{
  "tool": "jscpd",
  "version": "not installed",
  "total_duplicates": 0,
  "total_lines": 0,
  "duplicate_lines": 0,
  "percentage": 0,
  "clones": [],
  "note": "Install jscpd: npm install -g jscpd"
}
EOF
    exit 0
fi

VERSION=$(jscpd --version 2>&1 | head -1)

# Create temp directory for output
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Run jscpd with JSON reporter
# Settings: minimum 5 lines or 50 tokens to count as duplicate
# Redirect stdout to /dev/null as jscpd prints clone info to stdout
jscpd \
    --min-lines 5 \
    --min-tokens 50 \
    --reporters json \
    --output "$TMPDIR" \
    --format "c,cpp" \
    --ignore "**/tests/unity/**" \
    --ignore "**/build/**" \
    --silent \
    $SRC_DIRS >/dev/null 2>&1 || true

# Check if output was generated
if [ ! -f "$TMPDIR/jscpd-report.json" ]; then
    cat <<EOF
{
  "tool": "jscpd",
  "version": "$VERSION",
  "total_duplicates": 0,
  "total_lines": 0,
  "duplicate_lines": 0,
  "percentage": 0,
  "clones": [],
  "note": "No duplicates detected or jscpd failed"
}
EOF
    exit 0
fi

# Parse and transform jscpd output
python3 << PYTHON
import json
import sys

try:
    with open("$TMPDIR/jscpd-report.json") as f:
        data = json.load(f)
except:
    print(json.dumps({
        "tool": "jscpd",
        "version": "$VERSION",
        "total_duplicates": 0,
        "total_lines": 0,
        "duplicate_lines": 0,
        "percentage": 0,
        "clones": []
    }))
    sys.exit(0)

stats = data.get("statistics", {})
total_stats = stats.get("total", {})

clones = []
for dup in data.get("duplicates", [])[:20]:  # Limit to 20
    first = dup.get("firstFile", {})
    second = dup.get("secondFile", {})
    clones.append({
        "lines": dup.get("lines", 0),
        "tokens": dup.get("tokens", 0),
        "first_file": first.get("name", ""),
        "first_start": first.get("startLoc", {}).get("line", 0),
        "first_end": first.get("endLoc", {}).get("line", 0),
        "second_file": second.get("name", ""),
        "second_start": second.get("startLoc", {}).get("line", 0),
        "second_end": second.get("endLoc", {}).get("line", 0),
        "fragment": dup.get("fragment", "")[:200]  # Truncate fragment
    })

result = {
    "tool": "jscpd",
    "version": "$VERSION",
    "total_duplicates": len(data.get("duplicates", [])),
    "total_lines": total_stats.get("lines", 0),
    "duplicate_lines": total_stats.get("duplicatedLines", 0),
    "percentage": round(total_stats.get("percentage", 0), 2),
    "clones": clones
}

if len(data.get("duplicates", [])) > 20:
    result["note"] = f"Showing 20 of {len(data.get('duplicates', []))} duplicates"

print(json.dumps(result, indent=2))
PYTHON
