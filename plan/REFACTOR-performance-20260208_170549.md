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

### 4. Streaming Mode for Bulk Transfers
**Impact: MEDIUM-HIGH | Effort: MEDIUM | Expected Gain: 50%+ for file transfers**

Bypass the queue system for large sequential transfers:
```c
// Current: Fragment → Queue → Poll → Send (per fragment)
// Streaming: Direct buffer → Send → Callback when done

PeerTalk_StreamSend(ctx, peer, file_data, 64KB, on_complete);
```

**Benefits**:
- No queue slot limitations
- Fewer context switches
- Better TCP window utilization

---

### 5. Reduce Protocol Overhead for Small Messages
**Impact: MEDIUM | Effort: LOW | Expected Gain: 10-15% for chat apps**

Current message frame: 10-byte header + 2-byte CRC = 12 bytes overhead

For 256-byte messages: 4.7% overhead
For 32-byte chat messages: 37.5% overhead

**Options**:
- Compact header mode for small messages (4 bytes)
- Batch multiple small messages in one frame
- Skip CRC for localhost/reliable networks (configurable)

---

### 6. UDP Fast Path for Games
**Impact: HIGH for games | Effort: HIGH | Expected Gain: 50% latency reduction**

Current UDP implementation is minimal. For games, need:
```c
// Fire-and-forget game state updates
PeerTalk_SendUnreliable(ctx, peer, player_state, 64, PT_UDP_NO_QUEUE);
```

**Benefits**:
- No TCP head-of-line blocking
- No retransmission delays
- Predictable latency

---

### 7. Adaptive Performance Tuning
**Impact: MEDIUM | Effort: MEDIUM | Expected Gain: Optimal for all link types**

Auto-tune based on measured characteristics:
```c
// Measure RTT and bandwidth during capability exchange
if (measured_rtt < 20ms && measured_bandwidth > 50KB/s) {
    // Fast LAN: large chunks, aggressive pipelining
    effective_chunk = 4096;
    queue_depth = 32;
} else if (measured_rtt > 100ms) {
    // Slow link: smaller chunks, conservative
    effective_chunk = 512;
    queue_depth = 8;
}
```

---

### 8. Poll Loop Optimization
**Impact: LOW-MEDIUM | Effort: LOW | Expected Gain: 5-10%**

Current poll does discovery + TCP + UDP every call. For high-throughput:
```c
// Fast poll: TCP only (call frequently)
PeerTalk_PollFast(ctx);

// Full poll: Everything (call less often)
PeerTalk_Poll(ctx);
```

---

## Summary Table

| Priority | Improvement | Impact | Effort | Best For | Status |
|----------|-------------|--------|--------|----------|--------|
| 1 | Fix capability exchange | N/A | N/A | - | ✅ Not a bug (test logging issue) |
| 2 | Reduce reassembly copies | N/A | N/A | - | ✅ Already optimized (2 copies) |
| 3 | Tunable message limits | N/A | N/A | - | ✅ Already implemented |
| 4 | Streaming mode | MEDIUM-HIGH | MEDIUM | File transfer | ⏳ Planned |
| 5 | Reduce small msg overhead | MEDIUM | LOW | Chat apps | ⏳ Planned |
| 6 | UDP fast path | HIGH | HIGH | Games | ⏳ Planned |
| 7 | Adaptive tuning | MEDIUM | MEDIUM | Mixed traffic | ⏳ Planned |
| 8 | Poll loop optimization | LOW-MEDIUM | LOW | High throughput | ⏳ Planned |

**Key Finding**: The existing implementation is more optimized than initially assessed. The perceived capability exchange "bug" was a test app timing issue - the library handles capabilities correctly. Further performance gains require new features (streaming, UDP fast path) rather than fixes.

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
