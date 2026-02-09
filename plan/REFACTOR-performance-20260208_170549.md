# Performance Refactor Plan

**Date**: 2026-02-08
**Status**: Investigation Complete - Items 1-3 verified as already optimized
**Goal**: Maximize throughput and minimize latency across different application types

## Current Performance Baseline

### Performa 6200 (75MHz 603e PPC, 8MB RAM)

| Size | SEND | RECV | Notes |
|------|------|------|-------|
| 256 B | 4 KB/s | 4 KB/s | Perfect symmetry |
| 512 B | 9 KB/s | 9 KB/s | Perfect symmetry |
| 1024 B | 16 KB/s | 16 KB/s | Perfect symmetry |
| 2048 B | 30 KB/s | 19 KB/s | RECV 63% of SEND |
| 4096 B | 45 KB/s | 14 KB/s | RECV 31% of SEND |

### Mac SE (8MHz 68000, 4MB RAM)

| Size | SEND | RECV | Notes |
|------|------|------|-------|
| 256 B | 1 KB/s | 1 KB/s | Perfect symmetry |
| 512 B | 3 KB/s | 3 KB/s | Perfect symmetry |
| 1024 B | 4 KB/s | 4 KB/s | Perfect symmetry |
| 2048 B | 7 KB/s | 4 KB/s | RECV 57% of SEND |
| 4096 B | 14 KB/s | 4 KB/s | RECV 29% of SEND |

### Key Findings

- Bidirectional throughput works at all message sizes
- RECV bottleneck appears at 2048+ bytes due to fragmentation overhead
- Mac SE RECV plateaus at ~4 KB/s (CPU-bound reassembly)
- Small messages (≤1024 bytes) show perfect send/receive symmetry

---

## Improvements - Priority Order

### 1. Fix Capability Exchange Bug ✅ VERIFIED - NOT A BUG
**Impact: N/A | Was a test app logging issue, not a library bug**

**Investigation Result**: The test app was logging capabilities in `on_peer_connected` callback BEFORE capability exchange completes. The library's internal `effective_max_msg` is set correctly AFTER the exchange.

**What happened**:
- Test app logged "Peer capabilities: max_msg=512" (pre-exchange defaults)
- Library correctly updates `peer->hot.effective_max_msg` when capability message arrives
- Fragmentation uses correct values internally

**Added Diagnostics** (src/core/protocol.c, src/core/send.c):
- Warning when capability payload is unexpectedly short
- First-send logging to show actual effective_max being used
- Helps debug future capability issues

---

### 2. Reduce Reassembly Memory Copies ✅ ALREADY OPTIMIZED
**Impact: N/A | Code was already efficient - assessment was overstated**

**Actual data paths** (verified 2026-02-08):

**Non-fragmented messages** (1 copy):
```
MacTCP RDS → ibuf → callback (pointer to ibuf data)
   (copy 1)          (zero-copy)
```

**Fragmented messages** (2 copies):
```
MacTCP RDS → ibuf → recv_direct → callback (pointer to recv_direct)
   (copy 1)    (copy 2)              (zero-copy)
```

**Why these copies are necessary**:
- Copy 1 (RDS → ibuf): MacTCP REQUIRES immediate buffer return. Cannot hold RDS buffers across poll calls.
- Copy 2 (ibuf → recv_direct): Fragments arrive across multiple poll calls, need stable storage.

**What's already zero-copy**:
- Callback receives pointer to buffer data, not a copy
- `pt_reassembly_process()` returns pointer to `recv_direct.data`
- Non-fragmented callbacks point directly into `ibuf`

---

### 3. Application-Tunable Message Limits ✅ ALREADY IMPLEMENTED
**Impact: N/A | Feature already exists in public API**

**Existing API** (include/peertalk.h):
```c
typedef struct {
    // ...
    uint16_t max_message_size;       /* Max message we can handle, 0 = 8192 */
    uint16_t preferred_chunk;        /* Optimal chunk for streaming, 0 = 1024 */
    uint8_t  enable_fragmentation;   /* Auto-fragment large messages, default = 1 */
    // ...
} PeerTalk_Config;
```

**Implementation flow** (verified):
1. `PeerTalk_Init()` copies config → `ctx->local_max_message`
2. Capability exchange sends our max in TLV payload
3. On receive: `effective_max = min(ours, theirs)`
4. Send path uses `effective_max_msg` for fragmentation decisions

**Usage for apps**:
- Chat app: `config.max_message_size = 256` → no fragmentation needed
- File transfer: `config.max_message_size = 8192` → maximize throughput
- Low-memory: `config.enable_fragmentation = 0` → reject oversized messages

---

### 4. Streaming Mode for Bulk Transfers ✅ ALREADY IMPLEMENTED
**Impact: N/A | Feature already exists and is wired into poll loops**

**Existing API** (include/peertalk.h, src/core/stream.c):
```c
PeerTalk_Error PeerTalk_StreamSend(ctx, peer_id, data, length, on_complete, user_data);
PeerTalk_Error PeerTalk_StreamCancel(ctx, peer_id);
int PeerTalk_StreamActive(ctx, peer_id);
```

**Implementation** (verified 2026-02-08):
- `pt_stream_poll()` called in both MacTCP and POSIX poll loops
- Uses `peer->hot.effective_chunk` for adaptive chunk sizing
- Supports up to `PT_MAX_STREAM_SIZE` (64KB)
- Used by test apps for log streaming to partner

---

### 5. Reduce Protocol Overhead for Small Messages ✅ IMPLEMENTED
**Impact: MEDIUM | Effort: MEDIUM | Expected Gain: 10-15% for chat apps**

Current message frame: 10-byte header + 2-byte CRC = 12 bytes overhead

For 256-byte messages: 4.7% overhead
For 32-byte chat messages: 37.5% overhead

**Implementation** (2026-02-08):
- POSIX send path (`net_posix.c`) uses compact headers when `peer->cold.caps.compact_mode` is set
- POSIX receive path handles two-phase header detection (4 bytes initially, extend to 10 if full header)
- MacTCP send path (`tcp_io.c`) uses compact headers when negotiated
- `is_compact` flag in `pt_recv_hot` tracks current message format
- CRC validation skipped for compact headers (no CRC in 4-byte format)

**Critical edge case**: Fragment messages (PT_MSG_FLAG_FRAGMENT = 0x10) do NOT use compact headers because the flag doesn't fit in the 4-bit flags field. Only unfragmented messages use compact format.

**Tests**: `tests/test_compact_header.c` - 16 tests covering encoding, decoding, format detection, roundtrip, and edge cases

---

### 6. UDP Fast Path for Games ✅ ALREADY IMPLEMENTED
**Impact: N/A | Feature already exists with explicit zero-queue semantics**

**Existing API** (include/peertalk.h, src/posix/net_posix.c):
```c
PeerTalk_Error PeerTalk_SendUDP(ctx, peer_id, data, length);      /* Standard UDP */
PeerTalk_Error PeerTalk_SendUDPFast(ctx, peer_id, data, length);  /* Explicit fast path */
#define PT_SEND_UDP_NO_QUEUE 0x08  /* Flag for zero-queue semantics */
#define PT_MAX_UDP_MESSAGE_SIZE 1400  /* Larger payload limit */
```

**Implementation** (verified 2026-02-08):
- UDP already has zero queuing - `SendUDPFast` makes this explicit
- 1400 byte limit (vs old 576) for LAN usage
- Tests in `tests/test_udp_posix.c` and `tests/test_streaming.c`

---

### 7. Adaptive Performance Tuning ✅ ALREADY IMPLEMENTED
**Impact: N/A | Feature already exists and updates based on RTT**

**Existing Implementation** (src/core/peer.c, src/posix/net_posix.c):
```c
void pt_peer_update_adaptive_params(struct pt_context *ctx, struct pt_peer *peer);
```

**RTT-based tuning** (verified 2026-02-08):
- Called after latency measurement in `net_posix.c:1544`
- Updates `peer->hot.effective_chunk` and `peer->hot.pipeline_depth`
- Stream uses `effective_chunk` for chunk sizing
- Tuning logic: fast LAN → 4096 chunks, slow link → 512 chunks

---

### 8. Poll Loop Optimization ✅ ALREADY IMPLEMENTED
**Impact: LOW-MEDIUM | Effort: LOW | Expected Gain: 5-10%**

Current poll does discovery + TCP + UDP every call. For high-throughput:
```c
// Fast poll: TCP only (call frequently)
PeerTalk_PollFast(ctx);

// Full poll: Everything (call less often)
PeerTalk_Poll(ctx);
```

---

---

### B. Async Receive Implementation ✅ COMPLETE (NEUTRAL)
**Impact: NEUTRAL | Effort: HIGH | Result: No regression, matches baseline**

**Goal**: Convert synchronous TCPNoCopyRcv to async with permanent receive outstanding.

**Implementation** (2026-02-09):
1. Added `recv_pending` flag and dedicated `recv_pb` to hot/cold structs
2. Changed `pt_mactcp_tcp_recv()` from PBControlSync to PBControlAsync
3. Issue initial async receive on connection (tcp_listen.c, tcp_connect.c, poll_mactcp.c)
4. Poll ioResult in main loop, re-issue immediately on completion
5. **CRITICAL FIX**: Process receives BEFORE sends in poll loop (poll_mactcp.c)

**Results** (Performa 6200, with poll order fix):

| Size | Baseline RECV | Async RECV | Notes |
|------|---------------|------------|-------|
| 256 B | 11 KB/s | 11 KB/s | No change |
| 512 B | 14 KB/s | 10-14 KB/s | Matches baseline |
| 1024 B | 14 KB/s | 12-14 KB/s | Matches baseline |
| 2048 B | 13 KB/s | 13 KB/s | Matches baseline |
| 4096 B | 11 KB/s | 11 KB/s | **Matches baseline** |

**Key Fixes Applied**:
1. `commandTimeoutValue = 2` (minimum, prevents infinite stall)
2. **Poll order**: Receive processed BEFORE sends to prevent backpressure buildup
   - Previous order: sends first → starved receive under heavy load
   - New order: receive first → data drained before more sends queued

**Root Cause of Initial 4096-byte Regression**:
Under heavy bidirectional load, processing sends before receives caused:
1. Send queue drained (8 messages) before checking receive
2. TCP backpressure built up as receive fell behind
3. 2-second timeout fired repeatedly, causing stalls

**Decision**: Async receive implementation is NEUTRAL - no improvement, but no regression.
Foundation is in place for future optimizations (multiple outstanding receives, etc.).

---

## Summary Table

| Priority | Improvement | Impact | Effort | Best For | Status |
|----------|-------------|--------|--------|----------|--------|
| 1 | Fix capability exchange | N/A | N/A | - | ✅ Not a bug (test logging issue) |
| 2 | Reduce reassembly copies | N/A | N/A | - | ✅ Already optimized (2 copies) |
| 3 | Tunable message limits | N/A | N/A | - | ✅ Already implemented |
| **A** | **Async send pipelining** | **HIGH** | **HIGH** | **Throughput** | **✅ DONE - 112 KB/s (2.5x)** |
| **B** | **Async receive** | **NEUTRAL** | **HIGH** | **Throughput** | **✅ DONE - matches baseline** |
| **C** | **Buffer pre-allocation** | **N/A** | **MEDIUM** | **Throughput** | **❌ NOT VIABLE - memory constraints** |
| 4 | Streaming mode | N/A | N/A | File transfer | ✅ Already implemented |
| 5 | Reduce small msg overhead | MEDIUM | MEDIUM | Chat apps | ✅ Compact headers wired up |
| 6 | UDP fast path | N/A | N/A | Games | ✅ Already implemented |
| 7 | Adaptive tuning | N/A | N/A | Mixed traffic | ✅ Already implemented |
| 8 | Poll loop optimization | N/A | N/A | High throughput | ✅ Already implemented |

**Key Finding**: The existing implementation is more optimized than initially assessed. The perceived capability exchange "bug" was a test app timing issue - the library handles capabilities correctly. Async send pipelining provided significant gains (2.5x). Async receive is now implemented and stable (neutral performance) - poll order critical for preventing backpressure under heavy load.

---

### C. Buffer Pre-allocation ❌ NOT VIABLE
**Impact: N/A | Effort: MEDIUM | Result: Memory constraints prevent implementation**

**Goal**: Pre-allocate TCP receive buffers before MacTCP driver opens to get larger buffers (16-32KB instead of 4KB), improving the 25% threshold rule.

**Implementation Attempted** (2026-02-09):
1. Added `prealloced_bufs[]` array to `pt_mactcp_data` structure
2. Created `pt_mactcp_preallocate_early()` in platform_mactcp.c
3. Called BEFORE `PBOpenSync()` for MacTCP driver
4. Modified `pt_mactcp_tcp_create()` to use pre-allocated buffers

**Results**: Pre-allocation fails due to memory constraints.

**Logs show**:
```
[00000016][INF] Early pre-allocation: MaxBlock=9664 (before MacTCP driver)
[00000016][WRN] Insufficient memory for pre-allocation (MaxBlock=9664, need=147456 for 16 peers)
```

**Root Cause**: Even before MacTCP driver opens, `MaxBlock` is only ~10KB. This is because:
1. MacTCP is a shared driver - already loaded before app starts
2. System and app resources allocate before `main()` runs
3. App CODE segments load into application heap

**Memory required**:
- For 4 peers × 8KB buffers + headroom = ~48KB minimum
- Available: ~10KB contiguous

**Conclusion**: The theoretical optimization is sound - larger buffers would improve receive throughput via the 25% threshold rule. However, Classic Mac memory management constraints make pre-allocation impractical:
- Heap is fragmented before app gets control
- No opportunity to allocate early enough
- Would require application-level changes (allocating in main() before PeerTalk_Init)

**Code preserved** in platform_mactcp.c and mactcp_driver.c for future reference if memory constraints change or if apps can be modified to allocate buffers before calling PeerTalk_Init.

---

## Test Plan

After implementing items 1-3, re-run echo mode throughput tests on both machines:

1. Build updated Mac test apps
2. Start POSIX partner in echo mode
3. Run throughput test on Performa 6200
4. Run throughput test on Mac SE
5. Compare results to baseline above

**Success criteria**:
- Large message (2048/4096) RECV should improve by 50%+
- No regression in small message performance
- Memory usage should not increase significantly
