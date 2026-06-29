#!/bin/bash
# two-peer-test.sh -- run a PeerTalk test binary across two POSIX peers locally.
#
# Two real Linux peers can't share one host (TCP listener port clash, no
# SO_REUSEPORT), so this spins up two containers on a private Docker bridge.
# Limited broadcast (255.255.255.255) floods the bridge's L2 segment, so
# discovery works. This is the local integration test-track for the SDK.
#
# Usage:  tools/two-peer-test.sh [test_name] [seconds]
#   test_name  one of: test_lifecycle test_chat test_reliable test_fast test_multi
#              (default: test_lifecycle)
#   seconds    per-peer run cap (default: 25)
#
# Requires: docker, the pt-spike image (build-essential + cmake on ubuntu:24.04),
#           and a one-time in-container build into build-docker/.
# Exit code: 0 if BOTH peers print "*** PASS ***", else 1.
set -u

REPO="$(cd "$(dirname "$0")/.." && pwd)"
CLOG="${CLOG_DIR:-$HOME/clog}"
TEST="${1:-test_lifecycle}"
SECS="${2:-25}"
IMG="pt-spike"
NET="ptnet"

mounts=(-v "$REPO":/workspace -v "$CLOG":/clog)

# Ensure image exists
if ! docker image inspect "$IMG" >/dev/null 2>&1; then
  echo "ERROR: image '$IMG' missing. Build it from an ubuntu:24.04 + build-essential + cmake Dockerfile." >&2
  exit 2
fi

# Ensure network exists
docker network inspect "$NET" >/dev/null 2>&1 || docker network create "$NET" >/dev/null

# Always (re)build inside the container so source edits are picked up.
# make is incremental; cmake only re-runs when the cache is missing.
echo "[build] compiling inside container into build-docker/ ..."
docker run --rm "${mounts[@]}" "$IMG" bash -c \
  'mkdir -p build-docker && cd build-docker && \
   { [ -f CMakeCache.txt ] || cmake .. -DCLOG_DIR=/clog >/tmp/c.log 2>&1; } && \
   make >/tmp/m.log 2>&1' \
  || { echo "BUILD FAILED"; docker run --rm "${mounts[@]}" "$IMG" \
       bash -c 'tail -25 /tmp/m.log 2>/dev/null; tail -25 /tmp/c.log 2>/dev/null'; exit 2; }

docker rm -f ptA ptB >/dev/null 2>&1
# logs are root-owned from a prior container run; the '>' redirect below
# (run as root in-container) truncates them, so no host-side rm needed.

docker run -d --name ptA --network "$NET" "${mounts[@]}" "$IMG" \
  bash -c "timeout $SECS ./build-docker/$TEST --name AAA > build-docker/A.log 2>&1" >/dev/null
docker run -d --name ptB --network "$NET" "${mounts[@]}" "$IMG" \
  bash -c "timeout $SECS ./build-docker/$TEST --name BBB > build-docker/B.log 2>&1" >/dev/null

echo "[run] $TEST across ptA/ptB for ${SECS}s ..."
sleep "$((SECS + 3))"
docker rm -f ptA ptB >/dev/null 2>&1

a=$(grep -c "PASS" "$REPO/build-docker/A.log" 2>/dev/null || echo 0)
b=$(grep -c "PASS" "$REPO/build-docker/B.log" 2>/dev/null || echo 0)
echo "----- A tail -----"; tail -4 "$REPO/build-docker/A.log" 2>/dev/null
echo "----- B tail -----"; tail -4 "$REPO/build-docker/B.log" 2>/dev/null

if [ "$a" -ge 1 ] && [ "$b" -ge 1 ]; then
  echo "RESULT: PASS ($TEST)"; exit 0
else
  echo "RESULT: FAIL ($TEST)  [A pass=$a B pass=$b]"; exit 1
fi
