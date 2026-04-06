# Quickstart: Poll Back-Pointer Optimisation

**Feature**: 010-poll-backpointer  
**Date**: 2026-04-06

## What Changed

Internal optimisation only — no public API changes, no new files, no build system changes.

Two platform backend files are modified:
- `src/platform/mactcp/pt_mactcp.c` — `TCPStreamSlot` gains `owner` field
- `src/platform/opentransport/pt_ot.c` — `OTEndpointSlot` gains `owner` field

## How to Verify

Build and run existing tests. No new test code needed.

```bash
# POSIX
cd build && cmake .. -DCLOG_DIR=~/clog && make
./test_init_only
./test_lifecycle   # needs a second peer
./test_fast        # needs a second peer
./test_reliable    # needs a second peer

# 68k MacTCP (cross-compile, run on real hardware)
cd build-68k && cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake \
  -DPT_PLATFORM=MACTCP -DCLOG_DIR=~/clog -DCLOG_LIB_DIR=~/clog/build-m68k && make

# PPC OT (cross-compile, run on real hardware)
cd build-ppc-ot && cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=$RETRO68_TOOLCHAIN/powerpc-apple-macos/cmake/retroppc.toolchain.cmake \
  -DPT_PLATFORM=OT -DCLOG_DIR=~/clog -DCLOG_LIB_DIR=~/clog/build-ppc && make
```

## What to Look For

- All tests pass with identical behaviour
- No crashes or stale pointer issues during connect/disconnect cycles
- `find_peer_for_stream` and `find_peer_for_ep` no longer exist in the codebase
