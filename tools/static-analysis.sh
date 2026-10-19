#!/usr/bin/env bash
#
# Static-analysis gate for the PeerTalk SDK (POSIX build of core + backend).
# Extends tools/cppcheck.sh with clang-tidy, scan-build and an ASan/UBSan run
# of tests/test_seam.c.  Standard tools only (Constitution XI); every stage
# exits non-zero on a finding so it can gate a release.
#
# Usage:  CLOG_DIR=~/clog tools/static-analysis.sh [stage...]
#         stages: cppcheck tidy scan sanitize   (default: all)
# Env:    CLOG_DIR  clog checkout (default ~/clog)
#         LLVM_BIN  dir with clang-tidy/scan-build (default: PATH, then
#                   /opt/homebrew/opt/llvm/bin)
#         CC        compiler for the sanitizer stage (default clang)
#         OUT       output dir (default: <repo>/build-static, git-ignored)
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="${PT_REPO:-$(dirname "$here")}"
clog="${CLOG_DIR:-$HOME/clog}"
out="${OUT:-$repo/build-static}"
llvm="${LLVM_BIN:-}"
[ -z "$llvm" ] && ! command -v clang-tidy >/dev/null 2>&1 && llvm=/opt/homebrew/opt/llvm/bin
tool() { if [ -n "$llvm" ] && [ -x "$llvm/$1" ]; then echo "$llvm/$1"; else command -v "$1"; fi; }

[ -d "$clog/include" ] || { echo "clog headers not found at $clog (set CLOG_DIR)" >&2; exit 1; }
mkdir -p "$out"
stages=("$@"); [ ${#stages[@]} -eq 0 ] && stages=(cppcheck tidy scan sanitize)
rc=0
fail() { echo "FAIL: $1" >&2; rc=1; }

SDK=(src/core/pt_core.c src/core/pt_memory.c src/core/pt_discovery.c
     src/core/pt_messaging.c src/platform/posix/pt_posix.c)
INC=(-DPT_PLATFORM_POSIX -I include -I src/core -I "$clog/include")
cd "$repo"

for stage in "${stages[@]}"; do
case "$stage" in
cppcheck)
    echo "== cppcheck (existing project script) =="
    CLOG_DIR="$clog" "$repo/tools/cppcheck.sh" > "$out/cppcheck.txt" 2>&1 \
        && echo "ok" || { cat "$out/cppcheck.txt"; fail cppcheck; } ;;

tidy)
    echo "== clang-tidy =="
    ct="$(tool clang-tidy)" || { echo "clang-tidy missing" >&2; fail tidy; continue; }
    # bugprone-signed-bitwise / narrowing-conversions fire on every uchar|short
    # expression (C integer promotion), mt-unsafe on single-threaded logging
    # calls, branch-clone on deliberate parallel branches: noise, see report.
    checks='-*,bugprone-*,clang-analyzer-*,cert-*,concurrency-*,performance-*,portability-*'
    checks+=',-bugprone-easily-swappable-parameters,-bugprone-signed-bitwise'
    checks+=',-bugprone-narrowing-conversions,-bugprone-branch-clone,-concurrency-mt-unsafe'
    sysroot=(); [ "$(uname)" = Darwin ] && sysroot=(-isysroot "$(xcrun --show-sdk-path)")
    "$ct" -checks="$checks" -warnings-as-errors='*' \
        -header-filter='.*/src/(core|platform)/.*' "${SDK[@]}" \
        -- -std=c89 "${INC[@]}" "${sysroot[@]}" > "$out/clang-tidy.txt" 2>&1 \
        && echo "ok" || { grep -E 'warning:|error:' "$out/clang-tidy.txt" | head -50; fail clang-tidy; } ;;

scan)
    echo "== scan-build =="
    sb="$(tool scan-build)" || { echo "scan-build missing" >&2; fail scan; continue; }
    cat > "$out/sb_inner.sh" <<INNER
#!/usr/bin/env bash
set -e
cd "$repo"
for f in ${SDK[*]}; do \$CC -std=c89 ${INC[*]} -c \$f -o "$out/sb_\$(basename \$f).o"; done
INNER
    rm -rf "$out/scan-build"
    en=(); for c in alpha.unix.cstring.OutOfBounds alpha.unix.cstring.BufferOverlap \
        alpha.core.TestAfterDivZero optin.taint.GenericTaint optin.taint.TaintedAlloc \
        optin.taint.TaintedDiv security.insecureAPI.strcpy \
        security.insecureAPI.DeprecatedOrUnsafeBufferHandling \
        security.insecureAPI.UncheckedReturn; do en+=(-enable-checker "$c"); done
    # NOT enabled on purpose: alpha.core.PointerArithm / CastToStruct fire on the
    # intentional single-arena carve in pt_memory.c (false positive).
    "$sb" ${llvm:+--use-analyzer="$llvm/clang"} "${en[@]}" --status-bugs \
        -o "$out/scan-build" bash "$out/sb_inner.sh" > "$out/scan-build.log" 2>&1 \
        && echo "ok" || { grep -E 'warning:' "$out/scan-build.log" | head -30; fail scan-build; } ;;

sanitize)
    echo "== test_seam under ASan+UBSan =="
    cc="${CC:-clang}"
    clog_src="$(ls "$clog"/src/clog_posix.c "$clog"/src/clog.c 2>/dev/null | head -1)"
    [ -n "$clog_src" ] || { echo "no clog source under $clog/src" >&2; fail sanitize; continue; }
    "$cc" -std=c11 -g -O1 -fno-omit-frame-pointer \
        -fsanitize=address,undefined -fno-sanitize-recover=undefined \
        "${INC[@]}" -I tests "${SDK[@]}" "$clog_src" tests/test_seam.c \
        -o "$out/test_seam_san" > "$out/sanitize-build.txt" 2>&1 \
        || { cat "$out/sanitize-build.txt"; fail "sanitize build"; continue; }
    "$out/test_seam_san" > "$out/test_seam_san.txt" 2>&1 \
        && tail -3 "$out/test_seam_san.txt" || { tail -40 "$out/test_seam_san.txt"; fail sanitize; } ;;

*) echo "unknown stage $stage" >&2; rc=2 ;;
esac
done
[ $rc -eq 0 ] && echo "static-analysis: clean" || echo "static-analysis: FAILED" >&2
exit $rc
