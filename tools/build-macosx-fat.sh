#!/usr/bin/env bash
#
# build-macosx-fat.sh -- Build PeerTalk as a fat (universal) Mac OS X binary.
#
# Produces a single Mach-O with a PPC slice and an i386 slice, so one
# binary runs on both PowerPC and Intel Macs from 10.4 through 10.7. The
# POSIX backend (src/platform/posix/pt_posix.c) is pure BSD sockets, and
# Darwin is BSD, so the SDK compiles as-is -- only the build system is new
# (Constitution IX: the SDK stays one small reused backend, no OS X code).
#
# WHY a shell script and not CMake: the vintage build host (Xcode 3/4 on
# Lion) has gcc + the 10.4u universal SDK but no modern CMake. A single-pass
# `gcc -arch ppc -arch i386 -isysroot <10.4u>` is all that's needed for
# plain C (no AltiVec, no per-arch macros), so lipo-compositing is unneeded.
#
# RUN THIS ON THE OS X BUILD HOST (e.g. an Intel Lion Mac mini with the
# Xcode 3.2.6 / 10.4u SDK installed). From a Linux dev box, rsync the repo
# + clog over first, then run this over ssh. See tools/build-macosx-fat.md.
#
# usage:   tools/build-macosx-fat.sh [APP ...]
#   APP    test app(s) to build (default: all five POSIX gate apps).
# env:
#   CLOG_DIR   clog checkout (default: ../clog relative to repo root)
#   SDK        sysroot   (default: first of 10.4u / 10.5 / 10.3.9 present)
#   MIN        deployment target (default: 10.4 -- first universal OS X)
#   CC         compiler  (default: gcc-4.0, the 10.4u-compatible compiler)
#   ARCHS      space-separated -arch list (default: "ppc i386")
#   OUT        output dir (default: build-macosx-fat relative to repo root)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

CLOG_DIR="${CLOG_DIR:-$REPO_ROOT/../clog}"
CC="${CC:-/usr/bin/gcc-4.0}"
MIN="${MIN:-10.4}"
ARCHS="${ARCHS:-ppc i386}"
OUT="${OUT:-$REPO_ROOT/build-macosx-fat}"

# Pick a sysroot if the caller didn't. 10.4u is the canonical universal SDK.
if [ -z "${SDK:-}" ]; then
  for cand in \
    /Developer/SDKs/MacOSX10.4u.sdk \
    /Developer/SDKs/MacOSX10.5.sdk \
    /Developer/SDKs/MacOSX10.3.9.sdk; do
    if [ -d "$cand" ]; then SDK="$cand"; break; fi
  done
fi
: "${SDK:?no Mac OS X SDK found -- set SDK=/Developer/SDKs/MacOSX10.4u.sdk}"

if [ ! -d "$CLOG_DIR/include" ]; then
  echo "[fat] clog not found at $CLOG_DIR (set CLOG_DIR)" >&2
  exit 1
fi
if [ ! -x "$CC" ]; then
  echo "[fat] compiler $CC not found (set CC)" >&2
  exit 1
fi

ARCHFLAGS=""
for a in $ARCHS; do ARCHFLAGS="$ARCHFLAGS -arch $a"; done

# The SDK is written in C89, but we compile this test/demo build under
# gnu99 so the C11 test apps (mixed decls, // comments) build with the same
# flags. C89 enforcement stays the job of the CMake POSIX build (C_STANDARD
# 90); this target only proves the POSIX backend runs on OS X.
COMMON="$ARCHFLAGS -isysroot $SDK -mmacosx-version-min=$MIN -O2 -std=gnu99"
WARN="-Wall -Wextra -Wno-unused-parameter"
# clog is an external dep (Principle VII). Its header uses `#pragma GCC
# diagnostic push/pop`, which the vintage gcc-4.0 predates and warns on at
# every include. Include it via -isystem so GCC treats it as a system
# header and stays silent about it -- suppresses only third-party noise.
# Two residual third-party warnings this build cannot fix from here:
#   * clog_posix.c implicit-decl of fsync (clog .c missing <unistd.h>)
#   * SDK crt1.o "-mlong-branch no longer needed" (Apple 10.4u SDK object)
# All peertalk sources (src/) compile warning-clean.
INCS="-I$REPO_ROOT/include -I$REPO_ROOT/src/core -isystem $CLOG_DIR/include -I$REPO_ROOT/tests"
DEFS="-DPT_PLATFORM_POSIX"

SDK_SOURCES="
  src/core/pt_core.c
  src/core/pt_memory.c
  src/core/pt_discovery.c
  src/core/pt_messaging.c
  src/platform/posix/pt_posix.c
  $CLOG_DIR/src/clog_posix.c
"

APPS="$*"
if [ -z "$APPS" ]; then
  APPS="test_lifecycle test_reliable test_chat test_fast test_multi"
fi

mkdir -p "$OUT/obj"

echo "[fat] host: $(sw_vers -productVersion 2>/dev/null || echo '?')  CC: $($CC -dumpversion 2>/dev/null)"
echo "[fat] SDK: $SDK  archs:$ARCHFLAGS  min: $MIN"

# Compile the shared SDK + clog objects once.
SDK_OBJS=""
for src in $SDK_SOURCES; do
  obj="$OUT/obj/$(basename "${src%.c}").o"
  echo "[fat] cc $src"
  # shellcheck disable=SC2086  # word-split flag groups intentionally
  "$CC" $COMMON $WARN $INCS $DEFS -c "$src" -o "$obj"
  SDK_OBJS="$SDK_OBJS $obj"
done

# Link each app against the shared objects.
for app in $APPS; do
  echo "[fat] link $app"
  # shellcheck disable=SC2086  # word-split flag groups intentionally
  "$CC" $COMMON $WARN $INCS $DEFS \
    "tests/$app.c" $SDK_OBJS -o "$OUT/$app"
  lipo -info "$OUT/$app"
done

echo "[fat] done -- binaries in $OUT"
