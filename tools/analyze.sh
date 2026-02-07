#!/bin/bash
# analyze.sh - Run all static analysis tools
#
# Usage: ./tools/analyze.sh [--all|--complexity|--cppcheck|--duplicates|--quick]
#
# Options:
#   --all         Run all analysis (default)
#   --complexity  Run cyclomatic complexity analysis only
#   --cppcheck    Run cppcheck static analysis only
#   --duplicates  Run copy-paste detection only
#   --quick       Quick summary (skip detailed reports)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-build/analysis}"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Parse arguments
RUN_COMPLEXITY=0
RUN_CPPCHECK=0
RUN_DUPLICATES=0
QUICK=0

if [ $# -eq 0 ] || [ "$1" = "--all" ]; then
    RUN_COMPLEXITY=1
    RUN_CPPCHECK=1
    RUN_DUPLICATES=1
else
    for arg in "$@"; do
        case $arg in
            --complexity) RUN_COMPLEXITY=1 ;;
            --cppcheck) RUN_CPPCHECK=1 ;;
            --duplicates) RUN_DUPLICATES=1 ;;
            --quick) QUICK=1; RUN_COMPLEXITY=1; RUN_CPPCHECK=1; RUN_DUPLICATES=1 ;;
            *) echo "Unknown option: $arg"; exit 1 ;;
        esac
    done
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

echo -e "${BLUE}=== PeerTalk Static Analysis ===${NC}"
echo ""

# Track issues for summary
TOTAL_ISSUES=0
COMPLEXITY_VIOLATIONS=0
CPPCHECK_ERRORS=0
DUPLICATE_PERCENT=0

# 1. Cyclomatic Complexity
if [ $RUN_COMPLEXITY -eq 1 ]; then
    echo -e "${YELLOW}[1/3] Cyclomatic Complexity...${NC}"

    if "$SCRIPT_DIR/metrics/extract_complexity.sh" > "$OUTPUT_DIR/complexity.json" 2>/dev/null; then
        COMPLEXITY_VIOLATIONS=$(python3 -c "import json; d=json.load(open('$OUTPUT_DIR/complexity.json')); print(d.get('functions_over_threshold', 0))")
        HIGHEST=$(python3 -c "import json; d=json.load(open('$OUTPUT_DIR/complexity.json')); print(d.get('highest_complexity', 0))")
        AVG=$(python3 -c "import json; d=json.load(open('$OUTPUT_DIR/complexity.json')); print(d.get('average_complexity', 0))")

        if [ "$COMPLEXITY_VIOLATIONS" -gt 0 ]; then
            echo -e "  ${RED}✗ $COMPLEXITY_VIOLATIONS functions exceed threshold${NC}"
            TOTAL_ISSUES=$((TOTAL_ISSUES + COMPLEXITY_VIOLATIONS))
        else
            echo -e "  ${GREEN}✓ All functions within complexity limits${NC}"
        fi
        echo "    Highest: $HIGHEST, Average: $AVG"

        if [ $QUICK -eq 0 ] && [ "$COMPLEXITY_VIOLATIONS" -gt 0 ]; then
            echo ""
            echo "  Violations:"
            python3 -c "
import json
d = json.load(open('$OUTPUT_DIR/complexity.json'))
for v in d.get('violations', [])[:5]:
    print(f\"    - {v.get('function', '?')}: {v.get('complexity', 0)} ({v.get('file', '?')}:{v.get('line', 0)})\")
"
        fi
    else
        echo -e "  ${YELLOW}⚠ Complexity check skipped (tool not available)${NC}"
    fi
    echo ""
fi

# 2. Cppcheck Static Analysis
if [ $RUN_CPPCHECK -eq 1 ]; then
    echo -e "${YELLOW}[2/3] Static Analysis (cppcheck)...${NC}"

    if "$SCRIPT_DIR/metrics/extract_cppcheck.sh" > "$OUTPUT_DIR/cppcheck.json" 2>/dev/null; then
        CPPCHECK_ERRORS=$(python3 -c "import json; d=json.load(open('$OUTPUT_DIR/cppcheck.json')); print(d.get('by_severity', {}).get('error', 0))")
        CPPCHECK_WARNINGS=$(python3 -c "import json; d=json.load(open('$OUTPUT_DIR/cppcheck.json')); print(d.get('by_severity', {}).get('warning', 0))")
        CPPCHECK_TOTAL=$(python3 -c "import json; d=json.load(open('$OUTPUT_DIR/cppcheck.json')); print(d.get('total_issues', 0))")

        if [ "$CPPCHECK_ERRORS" -gt 0 ]; then
            echo -e "  ${RED}✗ $CPPCHECK_ERRORS errors found${NC}"
            TOTAL_ISSUES=$((TOTAL_ISSUES + CPPCHECK_ERRORS))
        elif [ "$CPPCHECK_WARNINGS" -gt 0 ]; then
            echo -e "  ${YELLOW}⚠ $CPPCHECK_WARNINGS warnings found${NC}"
        else
            echo -e "  ${GREEN}✓ No significant issues${NC}"
        fi
        echo "    Total issues: $CPPCHECK_TOTAL"

        if [ $QUICK -eq 0 ] && [ "$CPPCHECK_ERRORS" -gt 0 ]; then
            echo ""
            echo "  Errors:"
            python3 -c "
import json
d = json.load(open('$OUTPUT_DIR/cppcheck.json'))
for v in [i for i in d.get('issues', []) if i.get('severity') == 'error'][:5]:
    print(f\"    - [{v.get('id', '?')}] {v.get('message', '?')[:60]}\")
    print(f\"      {v.get('file', '?')}:{v.get('line', 0)}\")
"
        fi
    else
        echo -e "  ${YELLOW}⚠ cppcheck not available${NC}"
    fi
    echo ""
fi

# 3. Copy-Paste Detection
if [ $RUN_DUPLICATES -eq 1 ]; then
    echo -e "${YELLOW}[3/3] Copy-Paste Detection (jscpd)...${NC}"

    if "$SCRIPT_DIR/metrics/extract_duplicates.sh" > "$OUTPUT_DIR/duplicates.json" 2>/dev/null; then
        DUPLICATE_PERCENT=$(python3 -c "import json; d=json.load(open('$OUTPUT_DIR/duplicates.json')); print(d.get('percentage', 0))")
        DUPLICATE_COUNT=$(python3 -c "import json; d=json.load(open('$OUTPUT_DIR/duplicates.json')); print(d.get('total_duplicates', 0))")

        if (( $(echo "$DUPLICATE_PERCENT > 10" | bc -l 2>/dev/null || echo "0") )); then
            echo -e "  ${YELLOW}⚠ ${DUPLICATE_PERCENT}% duplicated code${NC}"
        elif [ "$DUPLICATE_COUNT" -gt 0 ]; then
            echo -e "  ${GREEN}✓ ${DUPLICATE_PERCENT}% duplicated (acceptable)${NC}"
        else
            echo -e "  ${GREEN}✓ No significant duplicates${NC}"
        fi
        echo "    $DUPLICATE_COUNT duplicate blocks found"

        if [ $QUICK -eq 0 ] && [ "$DUPLICATE_COUNT" -gt 0 ]; then
            echo ""
            echo "  Largest duplicates:"
            python3 -c "
import json
d = json.load(open('$OUTPUT_DIR/duplicates.json'))
for v in sorted(d.get('clones', []), key=lambda x: -x.get('lines', 0))[:3]:
    print(f\"    - {v.get('lines', 0)} lines: {v.get('first_file', '?')}:{v.get('first_start', 0)} ↔ {v.get('second_file', '?')}:{v.get('second_start', 0)}\")
"
        fi
    else
        echo -e "  ${YELLOW}⚠ jscpd not available${NC}"
    fi
    echo ""
fi

# Summary
echo -e "${BLUE}=== Summary ===${NC}"
if [ $TOTAL_ISSUES -eq 0 ]; then
    echo -e "${GREEN}✓ No critical issues found${NC}"
else
    echo -e "${RED}✗ $TOTAL_ISSUES issue(s) require attention${NC}"
fi
echo ""
echo "Reports saved to: $OUTPUT_DIR/"
echo "  - complexity.json"
echo "  - cppcheck.json"
echo "  - duplicates.json"

# Exit with error if critical issues found
if [ "$CPPCHECK_ERRORS" -gt 0 ]; then
    exit 1
fi

exit 0
