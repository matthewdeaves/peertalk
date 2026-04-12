# Quickstart: 011-disconnect-poll-hardening

**Date**: 2026-04-12

## What This Changes

1. **New API function**: `PT_DisconnectAll(ctx)` — disconnects all connected peers with goodbye frames
2. **OT poll hardening**: Error checking on OTRcv returns in disconnect/ordrel/data paths
3. **MacTCP poll hardening**: Stream state validation and error logging in terminated drain path

## Files Modified

| File | Change |
|------|--------|
| `include/peertalk.h` | Add `PT_DisconnectAll` declaration, update section comment to "Connections (3)" |
| `src/core/pt_core.c` | Add `PT_DisconnectAll` implementation (~15 lines) |
| `src/platform/opentransport/pt_ot.c` | Add OTRcv error checking + slot state validation in poll (~10 lines) |
| `src/platform/mactcp/pt_mactcp.c` | Add stream state validation + error logging in poll (~8 lines) |

## Build & Test

```bash
# POSIX — build and run test_lifecycle (exercises connect/disconnect)
cd build && cmake .. -DCLOG_DIR=~/clog && make
./test_lifecycle

# 68k MacTCP — build and deploy to Mac SE
cd build-68k && make

# PPC OT — build and deploy to Performa 6400
cd build-ppc-ot && make
```

## Usage Example

```c
/* Game over — return to lobby */
PT_DisconnectAll(ctx);    /* goodbye to all peers */
PT_StopDiscovery(ctx);    /* stop broadcasting */
PT_StartDiscovery(ctx);   /* re-enter lobby */
```

## Estimated Impact

- ~25 lines of new code across 4 files
- No new allocations, no new state, no API breaking changes
- Function #23 added to the public API
