#!/bin/bash
# Code Quality Gates - Check compliance with CLAUDE.md standards
#
# Usage:
#   ./tools/build/quality_gates.sh          # Check all gates
#   ./tools/build/quality_gates.sh quick    # Quick checks only (no coverage)
#
# Exit codes:
#   0 - All gates pass
#   1 - One or more gates failed

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

MODE="${1:-full}"

cd "$PROJECT_DIR"

ERRORS=0
WARNINGS=0

echo "PeerTalk Quality Gates"
echo "======================"
echo ""

# Gate 1: File size (max 500 lines)
check_file_size() {
    echo "Checking file sizes (max 500 lines)..."
    local failed=0

    for f in $(find src include \( -name "*.c" -o -name "*.h" \) 2>/dev/null); do
        lines=$(wc -l < "$f")
        if [[ "$lines" -gt 500 ]]; then
            echo "  ✗ $f: $lines lines (max 500)"
            failed=1
        fi
    done

    if [[ "$failed" -eq 0 ]]; then
        echo "  ✓ All files within size limit"
    else
        ERRORS=$((ERRORS + 1))
    fi
    echo ""
}

# Gate 2: Function length (max 100 lines, prefer 50)
check_function_length() {
    echo "Checking function lengths (max 100 lines)..."

    if ! command -v ctags >/dev/null 2>&1; then
        echo "  ⚠ ctags not available - skipping"
        echo "    (Install with: sudo apt install universal-ctags)"
        echo ""
        return
    fi

    local failed=0
    local long_funcs=""

    for f in $(find src -name "*.c" 2>/dev/null); do
        # Get function names and line numbers
        local prev_line=0
        local prev_name=""

        while IFS= read -r entry; do
            name=$(echo "$entry" | awk '{print $1}')
            line=$(echo "$entry" | awk '{print $3}')

            # Check previous function's length (line - prev_line)
            if [[ "$prev_line" -gt 0 ]] && [[ "$line" -gt "$prev_line" ]]; then
                length=$((line - prev_line))
                if [[ "$length" -gt 100 ]]; then
                    long_funcs="$long_funcs\n  ✗ $f:$prev_name - $length lines"
                    failed=1
                fi
            fi

            prev_line="$line"
            prev_name="$name"
        done < <(ctags -x --c-kinds=f "$f" 2>/dev/null | sort -t' ' -k3 -n)
    done

    if [[ "$failed" -eq 0 ]]; then
        echo "  ✓ No excessively long functions found"
    else
        echo -e "$long_funcs"
        WARNINGS=$((WARNINGS + 1))
    fi
    echo ""
}

# Gate 3: Compiler warnings
check_compiler_warnings() {
    echo "Checking for compiler warnings..."

    if [[ ! -f "Makefile" ]]; then
        echo "  ⚠ No Makefile - skipping"
        echo ""
        return
    fi

    # Clean and rebuild with warnings as errors
    make clean >/dev/null 2>&1 || true

    if make CFLAGS="-Wall -Wextra -Werror" 2>&1 | grep -q "error:"; then
        echo "  ✗ Compiler warnings/errors found"
        ERRORS=$((ERRORS + 1))
    else
        echo "  ✓ No compiler warnings"
    fi
    echo ""
}

# Gate 4: Coverage threshold (10% minimum)
check_coverage() {
    echo "Checking test coverage (10% minimum)..."

    if [[ ! -f "coverage.info" ]] && [[ ! -d "coverage" ]]; then
        echo "  ⚠ No coverage data found"
        echo "    Run: make test && make coverage"
        echo ""
        return
    fi

    if command -v lcov >/dev/null 2>&1 && [[ -f "coverage.info" ]]; then
        COVERAGE=$(lcov --summary coverage.info 2>&1 | grep "lines" | awk '{print $2}' | tr -d '%')

        if [[ -n "$COVERAGE" ]]; then
            if (( $(echo "$COVERAGE < 10.0" | bc -l) )); then
                echo "  ✗ Coverage: ${COVERAGE}% (below 10% threshold)"
                ERRORS=$((ERRORS + 1))
            else
                echo "  ✓ Coverage: ${COVERAGE}%"
            fi
        else
            echo "  ⚠ Could not parse coverage data"
        fi
    else
        echo "  ⚠ lcov not available or no coverage.info"
    fi
    echo ""
}

# Gate 5: ISR Safety (for Mac code)
check_isr_safety() {
    echo "Checking ISR safety (Mac networking code)..."

    local has_mac_code=0
    for dir in src/mactcp src/opentransport src/appletalk; do
        if [[ -d "$dir" ]] && [[ -n "$(ls -A $dir 2>/dev/null)" ]]; then
            has_mac_code=1
            break
        fi
    done

    if [[ "$has_mac_code" -eq 0 ]]; then
        echo "  ⚠ No Mac networking code found - skipping"
        echo ""
        return
    fi

    if [[ -f "tools/validators/isr_safety.py" ]]; then
        if python3 tools/validators/isr_safety.py --quiet 2>/dev/null; then
            echo "  ✓ No ISR safety violations"
        else
            echo "  ✗ ISR safety violations found"
            ERRORS=$((ERRORS + 1))
        fi
    else
        echo "  ⚠ ISR validator not found"
    fi
    echo ""
}

# Gate 6: Format check
check_formatting() {
    echo "Checking code formatting..."

    if [[ ! -f ".clang-format" ]]; then
        echo "  ⚠ No .clang-format file - skipping"
        echo ""
        return
    fi

    if ! command -v clang-format >/dev/null 2>&1; then
        echo "  ⚠ clang-format not available - skipping"
        echo ""
        return
    fi

    local unformatted=0
    for f in $(find src include \( -name "*.c" -o -name "*.h" \) 2>/dev/null | head -20); do
        if ! clang-format --dry-run --Werror "$f" >/dev/null 2>&1; then
            if [[ "$unformatted" -eq 0 ]]; then
                echo "  Unformatted files:"
            fi
            echo "    - $f"
            unformatted=$((unformatted + 1))
        fi
    done

    if [[ "$unformatted" -eq 0 ]]; then
        echo "  ✓ All files properly formatted"
    else
        echo "  ⚠ $unformatted files need formatting"
        WARNINGS=$((WARNINGS + 1))
    fi
    echo ""
}

# Run checks
check_file_size

if [[ "$MODE" == "quick" ]]; then
    check_formatting
else
    check_function_length
    check_compiler_warnings
    check_coverage
    check_isr_safety
    check_formatting
fi

# Summary
echo "=============================="
if [[ "$ERRORS" -eq 0 ]] && [[ "$WARNINGS" -eq 0 ]]; then
    echo "✓ All quality gates passed"
    exit 0
elif [[ "$ERRORS" -eq 0 ]]; then
    echo "⚠ Passed with $WARNINGS warning(s)"
    exit 0
else
    echo "✗ $ERRORS gate(s) failed, $WARNINGS warning(s)"
    exit 1
fi
