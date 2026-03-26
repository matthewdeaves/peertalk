# Quickstart: Code Review Fixes

## Overview

Five internal fixes to the PeerTalk SDK. No public API changes. No wire protocol changes. All existing tests must continue to pass.

## Fix Order (by priority)

### Fix 1: Atomic Flag Exchange (P1)

**MacTCP** (`src/platform/mactcp/pt_mactcp.c`):
- Add `pt_disable_interrupts()` / `pt_restore_interrupts()` helper functions using 68k inline assembly
- Wrap the flag snapshot-and-clear in `mactcp_poll_tcp()` with interrupt disable/restore
- Also wrap any other snapshot-and-clear sites (UDP flags if any)

**OT** (`src/platform/opentransport/pt_ot.c`):
- Change `volatile unsigned long flags` to `volatile UInt8 flags` in endpoint slot struct
- Define bit-index constants (EVT_BIT_DATA=0, EVT_BIT_DISCONNECT=1, etc.)
- Replace `slot->flags |= EVT_xxx` in notifiers with `OTAtomicSetBit(&slot->flags, EVT_BIT_xxx)`
- Replace snapshot-and-clear in poll with individual `OTAtomicClearBit()` calls (returns previous state)
- Same treatment for listener flags

### Fix 2: Init Failure Cleanup (P2)

**MacTCP** (`src/platform/mactcp/pt_mactcp.c`):
- Add goto labels in `mactcp_init()`: `fail_msg_udp`, `fail_disc_udp`, `fail_tcp`, `fail_upp`
- Each failure path jumps to the appropriate label
- Cleanup releases resources in reverse creation order (model on `mactcp_shutdown`)

**OT** (`src/platform/opentransport/pt_ot.c`):
- Add goto labels in `ot_init()`: `fail_msg_udp`, `fail_disc_udp`, `fail_tcp`, `fail_listener`, `fail_upp`
- Each failure path jumps to the appropriate label
- Cleanup closes endpoints and disposes UPPs before CloseOpenTransport

### Fix 3: Reassembly Admission Check (P3)

**Core** (`src/core/pt_messaging.c`):
- Replace the `total_size = total * chunk_payload` aggregate check
- Instead, on each chunk arrival, check `offset + chunk_payload <= reassembly_buf_size`
- Keep the `reassembly_total` tracking for completion detection

### Fix 4: PT_Broadcast Semantics (P4)

**Core** (`src/core/pt_messaging.c`):
- Change: return PT_OK when `sent_any == 0` and no sends were attempted (no connected peers)
- Keep: return PT_ERR_SEND_FAILED when at least one send was attempted and failed

### Fix 5: POSIX UDP Drain Loop (P5)

**POSIX** (`src/platform/posix/pt_posix.c`):
- Wrap discovery UDP recvfrom in a `for(;;)` loop, break on recvfrom returning -1
- Wrap message UDP recvfrom in the same loop pattern
- Ensure sockets are non-blocking (already set via SOCK_DGRAM + sendto pattern, but verify)

## Build & Test

```bash
# POSIX build and verify
cd build && cmake .. -DCLOG_DIR=~/clog && make && cd ..

# Run POSIX tests (fixes 3, 4, 5 testable locally)
./build/test_init_only
./build/test_fast      # Verify UDP drain doesn't regress

# Cross-compile for hardware testing
# 68k MacTCP (fixes 1, 2)
cd build-68k && make && cd ..

# PPC OT (fixes 1, 2)
cd build-ppc-ot && make && cd ..

# Hardware tests: test_lifecycle, test_reliable on all 3 targets
```

## Key References

- R1: 68k interrupt disable pattern (research.md)
- R2: OTAtomic* function signatures and usage (research.md)
- R3: MacTCP cleanup sequence (research.md)
- R4: OT cleanup sequence (research.md)
- R5: Reassembly bounds check approach (research.md)
