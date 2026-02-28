# OT TCP SEND Throughput Fix: 62-Byte Segment Investigation

## Context

Open Transport on Performa 6400 (OT 1.1.1, System 7.6.1, PPC 603e) limits TCP SEND throughput to ~6-7 KB/s. Each `OTSnd()` call accepts only ~62 bytes regardless of buffer size. The `ss` data confirms 62-byte TCP segments on the wire. RECV works fine at 36-131 KB/s, proving the network/hardware isn't the bottleneck.

**What's been tried (no effect):**
- TCP_NODELAY on/off, XTI_SNDLOWAT (1 and 536), XTI_SNDBUF=65536
- OTSetNonBlocking + OTSnd loop (256 iterations)
- Send coalescing (8KB threshold in obuf before flush)

**Root cause theory:** OT's STREAMS architecture uses 64-byte minimum buffer units (mblks). The OT book confirms: *"64 bytes is the smallest STREAMS buffer size"* (NetworkingOpenTransport.txt:41250). Each OTSnd copies 62 bytes of app data into a 64-byte mblk. TCP's STREAMS service procedure processes ~1.6 mblks per tick (60 Hz), yielding: 62 * 96 = 5,952 bytes/sec - matching observed throughput exactly.

## Changes (All Applied Together)

Three changes in one build: OTAckSends, async blocking mode, and diagnostic logging.

### 1. OTAckSends (Zero-Copy Sends)

**Why:** Changes the fundamental data path. Default mode copies data into 64-byte mblks. With OTAckSends, OT references your buffer directly - mblk becomes a descriptor, not a data container. TCP module could then build MSS-sized segments (536 bytes) from the full referenced buffer.

**Files:** `tcp_ot.c`, `poll_ot.c`, `ot_defs.h`

**Step 1a: Enable OTAckSends** (`tcp_ot.c:pt_ot_tcp_create`, after line 302)
```c
err = OTAckSends(hot->ref);
/* Non-fatal if fails - continue with copy mode */
```

**Step 1b: Double-buffer obuf** (`ot_defs.h` / peer cold struct)

With OTAckSends, the buffer passed to OTSnd must stay valid until T_MEMORYRELEASED. Need two buffers:
- Buffer A: locked by OT (being sent)
- Buffer B: accumulating new frames

```c
uint8_t obuf[2][PT_FRAME_BUF_SIZE];  /* Double buffer */
uint16_t obuflen[2];
uint8_t obuf_active;      /* Which buffer is accumulating (0 or 1) */
uint8_t obuf_sending;     /* Which buffer OT holds (0xFF = none) */
```

**Step 1c: Modified flush** (`poll_ot.c:pt_ot_flush_send`)
- If OT holds a buffer (`obuf_sending != 0xFF`), return WOULD_BLOCK
- Send the active buffer, mark it as sending, swap to the other
- On T_MEMORYRELEASED (already handled in notifier at `tcp_ot.c:109-112`), release buffer in poll loop

**Step 1d: Timeout safety** - If T_MEMORYRELEASED doesn't fire within 5 seconds, force-release the buffer to prevent deadlock.

### 2. Async Blocking Mode

**Why:** OT docs explicitly state *"For best performance, use asynchronous blocking mode"* and *"Never use asynchronous nonblocking mode."* Current code uses the "never use" mode (`tcp_ot.c:295+302`).

**Change:** `tcp_ot.c:302` - Remove `OTSetNonBlocking()` call (keep `OTSetAsynchronous`). The endpoint defaults to blocking mode when not explicitly set non-blocking.

**kOTSyncIdleEvent handler:** Add to notifier (`tcp_ot.c:pt_ot_tcp_notifier`):
```c
case kOTSyncIdleEvent:
    /* OT is blocked in a sync call - yield CPU so STREAMS can drain */
    break;  /* Just returning yields on cooperative multitasking */
```

**OTSnd loop adjustment:** With blocking mode, OTSnd may accept more data per call (waits for STREAMS resources instead of returning kEAgainErr). May need to reduce `PT_OT_SND_MAX_LOOPS` or adjust loop exit conditions. The loop still exits on kOTFlowErr (flow control).

**Risk:** The code comment at `tcp_ot.c:289-294` warns about deadlock with blocking OTSnd. Mitigated by: (a) still being async so completions go to notifier, (b) kOTSyncIdleEvent allows yielding, (c) OTSnd in async mode returns kOTFlowErr regardless of blocking setting for true flow control.

### 3. Diagnostic Logging

**Why:** We need data to understand what changed. Add temporary logging that can be compiled out later.

**In `tcp_ot.c:pt_ot_tcp_send`:**
- Log first OTSnd return value per flush (before/after comparison)
- Count total bytes accepted and loop iterations per flush call
- Log at end: `"DIAG: flush %u bytes in %d loops (first_ret=%ld)"`

**In `tcp_ot.c:pt_ot_tcp_set_options`:**
- After each T_NEGOTIATE, read back with T_CURRENT and log actual negotiated value
- Specifically check `opt->status` for T_PARTSUCCESS (OT negotiated lower value)
- Log: `"DIAG: XTI_SNDBUF requested=65536 negotiated=%lu status=%ld"`

**In `poll_ot.c` (T_MEMORYRELEASED handling):**
- Log time delta between OTSnd and T_MEMORYRELEASED
- Log: `"DIAG: T_MEMORYRELEASED after %lu ticks"`

## Files to Modify

| File | Changes |
|------|---------|
| `src/opentransport/tcp_ot.c` | OTAckSends enable, remove OTSetNonBlocking, kOTSyncIdleEvent, send diagnostics |
| `src/opentransport/poll_ot.c` | Double-buffer obuf, modified flush, T_MEMORYRELEASED release, timing diag |
| `src/opentransport/ot_defs.h` | Double-buffer fields in cold struct (if obuf is there) |

## Verification

1. Build: `./scripts/build-mac-tests.sh opentransport`
2. Start partner: `docker run -d --name perf-partner --network host -v "$(pwd)":/workspace -w /workspace peertalk-posix:latest ./build/bin/perf_partner --verbose`
3. Execute: `mcp execute_binary(machine="performa6400", platform="opentransport", binary_path="build/mac/test_stream.bin")`
4. Collect logs (FTP download from performa6400 since log streaming is broken for OT)
5. Compare SEND throughput vs baseline 6-7 KB/s
6. Check diagnostic output: OTSnd return values, negotiated buffer sizes
7. Check `ss -ti` on POSIX side for TCP segment sizes
8. Success: SEND > 20 KB/s (3x improvement)

## Risks

| Risk | Mitigation |
|------|-----------|
| OTAckSends unsupported on OT 1.1.1 | Non-fatal fallback, logged |
| Blocking mode deadlock | Async mode + kOTSyncIdleEvent + yield |
| Buffer lifetime bugs | Double-buffer + flag tracking + timeout |
| Both changes interact badly | Diagnostics tell us which helped/hurt |
| No improvement at all | Diagnostics reveal actual bottleneck for next attempt |

## If This Doesn't Work

Next approaches in priority order:
1. **Multiple TCP connections** (4 parallel = guaranteed ~24 KB/s)
2. **OT version upgrade** (OT 1.1.2 standalone, or Mac OS 8.x for OT 2.x)
3. **UDP data channel** (bypass TCP STREAMS entirely, highest effort)
