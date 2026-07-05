#!/usr/bin/env bash
#
# Static-analysis sweep for the PeerTalk SDK (Constitution XI: standard tools).
#
# Runs cppcheck over core + all three platform backends, each with its own
# PT_PLATFORM_* define, using the documented suppressions in
# cppcheck-suppressions.txt for the library-in-isolation false positives.
# Exits non-zero if any un-suppressed finding surfaces, so CI / the release
# gate can rely on it.
#
# Usage:  CLOG_DIR=~/clog tools/cppcheck.sh
# Env:    CLOG_DIR  clog checkout (default ~/clog)
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(dirname "$here")"
clog_inc="${CLOG_DIR:-$HOME/clog}/include"
supp="$here/cppcheck-suppressions.txt"

if ! command -v cppcheck >/dev/null 2>&1; then
    echo "cppcheck not found on PATH" >&2
    exit 127
fi
if [ ! -d "$clog_inc" ]; then
    echo "clog headers not found at $clog_inc (set CLOG_DIR)" >&2
    exit 1
fi

common=(
    --enable=all --inconclusive --std=c89 --language=c --quiet
    --check-level=exhaustive
    --suppressions-list="$supp"
    --error-exitcode=2
    -I "$repo/include" -I "$repo/src/core" -I "$clog_inc"
)

echo "== core + POSIX (BSD sockets) =="
cppcheck "${common[@]}" --platform=unix64 -DPT_PLATFORM_POSIX \
    "$repo/src/core" "$repo/src/platform/posix"

echo "== MacTCP (68k/PPC) =="
cppcheck "${common[@]}" --platform=unix32 -DPT_PLATFORM_MACTCP \
    "$repo/src/platform/mactcp/pt_mactcp.c"

echo "== Open Transport (PPC/68k) =="
cppcheck "${common[@]}" --platform=unix32 -DPT_PLATFORM_OT \
    "$repo/src/platform/opentransport/pt_ot.c"

echo "cppcheck: clean (no un-suppressed findings)"
