# PeerTalk Architecture Review & Improvement Plan

**Date:** 2026-02-13
**Reviewer:** Claude Code
**Codebase Version:** develop branch (commit 1feb85b)

---

## Executive Summary

The PeerTalk SDK has **significantly matured** with recent flow control improvements. The SDK now includes:

- ✅ Token bucket rate limiting (automatic, pressure-adaptive)
- ✅ Pressure-triggered fragmentation
- ✅ CRITICAL priority bypass for control messages
- ✅ Adaptive window/chunk/pipeline based on RTT
- ✅ Two-tier send queue with O(1) priority scheduling

**The main remaining gaps are API completeness** (query functions, helpers) and **configuration exposure**, not fundamental architecture issues. The SDK handles most flow control internally—test apps still duplicate some patterns that should be SDK features.

---

## Part 1: Current SDK Capabilities

### 1.1 Flow Control Architecture

The SDK implements a sophisticated multi-layer flow control system:

```
┌─────────────────────────────────────────────────────────────┐
│                    PeerTalk_SendEx()                        │
├─────────────────────────────────────────────────────────────┤
│  Layer 1: Peer Pressure Throttling                          │
│    - 0-50%: No throttle                                     │
│    - 50-85%: Skip LOW priority                              │
│    - 85-95%: Skip NORMAL + LOW                              │
│    - 95%+: Only CRITICAL passes                             │
├─────────────────────────────────────────────────────────────┤
│  Layer 2: Token Bucket Rate Limiting                        │
│    - Pressure < 50%: Unlimited                              │
│    - Pressure 50-85%: 100 KB/s                              │
│    - Pressure ≥ 85%: 50 KB/s                                │
├─────────────────────────────────────────────────────────────┤
│  Layer 3: Send Window Flow Control                          │
│    - Window = peer_recv_buffer / max_message                │
│    - Clamped to 2-8 messages in flight                      │
│    - CRITICAL priority bypasses window check                │
├─────────────────────────────────────────────────────────────┤
│  Layer 4: Queue Backpressure                                │
│    - Queue >75%: Reject NORMAL priority                     │
│    - Queue >90%: Reject LOW priority                        │
│    - CRITICAL always accepted                               │
└─────────────────────────────────────────────────────────────┘
```

**Key Files:**
- `src/core/send.c:403-469` - Flow control decision points
- `src/core/peer.c:638-735` - Throttle and rate limit checks
- `src/core/peer.c:886-1028` - Adaptive parameter tuning

### 1.2 Token Bucket Rate Limiting

**Location:** `src/core/peer.c:685-735`

```c
int pt_peer_check_rate_limit(struct pt_context *ctx, struct pt_peer *peer, uint16_t bytes) {
    if (peer->cold.caps.rate_limit_bytes_per_sec == 0) {
        return 0;  // No limit
    }

    // Refill tokens based on elapsed time
    elapsed = now - peer->cold.caps.rate_last_update;
    tokens_to_add = (elapsed * rate_limit_bytes_per_sec) / 1000;
    peer->cold.caps.rate_bucket_tokens += tokens_to_add;

    // Consume tokens
    if (peer->cold.caps.rate_bucket_tokens < bytes) {
        return 1;  // Rate limited - returns PT_ERR_RATE_LIMITED
    }
    peer->cold.caps.rate_bucket_tokens -= bytes;
    return 0;
}
```

**Automatic adjustment** based on peer pressure (`peer.c:960-996`):

| Peer Pressure | Rate Limit | Rationale |
|---------------|------------|-----------|
| < 50% | Unlimited | Peer has capacity |
| 50-85% | 100 KB/s | Light throttle |
| ≥ 85% | 50 KB/s | Heavy throttle |

### 1.3 Pressure-Triggered Fragmentation

**Location:** `src/core/send.c:495-614`

When peer reports high buffer pressure, SDK automatically fragments:

```c
if (peer->cold.caps.buffer_pressure >= ctx->pressure_frag &&
    length > PT_PRESSURE_REDUCED_MAX) {
    needs_fragmentation = 1;
    frag_max = PT_PRESSURE_REDUCED_MAX;  // 2048 bytes
}
```

This prevents large messages from overwhelming constrained peers.

### 1.4 Adaptive Parameters Based on RTT

**Location:** `src/core/peer.c:886-1028`

| RTT | Window | Chunk Size | Pipeline Depth |
|-----|--------|------------|----------------|
| < 50ms | 6 | 4096 | 4 |
| < 100ms | 4 | 2048 | 3 |
| < 200ms | 3 | 1024 | 2 |
| ≥ 200ms | 2 | 512 | 1 |

### 1.5 Two-Tier Send Queue

| Tier | Message Size | Mechanism |
|------|--------------|-----------|
| Tier 1 | ≤ 256 bytes | Priority queue with O(1) pop |
| Tier 2 | > 256 bytes | Direct buffer (one at a time) |
| Async | MacTCP only | 4-slot pipeline for concurrent sends |

### 1.6 CRITICAL Priority Bypass

**Location:** `src/core/send.c:455`

```c
if (priority < PT_PRIORITY_CRITICAL && peer->cold.caps.caps_exchanged ...) {
    // Window check - CRITICAL skips this entirely
}
```

Control messages using `PT_PRIORITY_CRITICAL` bypass:
- Pressure throttling
- Rate limiting
- Send window checks

---

## Part 2: Gaps Identified

### 2.1 API Completeness Gaps

#### Gap A: No Window Query API

**Current:** Apps manually track in-flight messages

```c
// test_throughput.c:107 - Manual tracking
static int g_in_flight = 0;

// test_throughput.c:139-174 - Manual window check
while (g_in_flight < FLOW_CONTROL_WINDOW) {
    err = PeerTalk_Send(...);
    if (err == PT_OK) g_in_flight++;
}
```

**Impact:** Every app reimplements window tracking, error-prone.

#### Gap B: No Control Message Helper

**Current:** Apps use `PeerTalk_SendEx()` with manual retry

```c
// test_stream.c:176-207
while (retries < max_retries) {
    err = PeerTalk_SendEx(ctx, peer_id, &ctrl, sizeof(ctrl),
                          PT_PRIORITY_CRITICAL, PT_SEND_DEFAULT, 0);
    if (err == PT_OK) return 0;
    PeerTalk_Poll(ctx);  // Manual drain and retry
}
```

**Impact:** Complex retry logic duplicated in each app.

#### Gap C: No Tick Conversion Utilities

**Current:** Every Mac test app implements:

```c
static unsigned long ticks_to_ms(unsigned long ticks) {
    return (ticks * 1000UL) / 60UL;
}
```

**Impact:** Code duplication, platform knowledge required.

### 2.2 Configuration Gaps

#### Gap D: Rate Limit Thresholds Not Configurable

**Current:** Hardcoded in `peer.c:966-972`

```c
if (peer_pressure >= thresh_high) {
    new_rate_limit = 50 * 1024;   // Hardcoded 50 KB/s
} else if (peer_pressure >= thresh_med) {
    new_rate_limit = 100 * 1024;  // Hardcoded 100 KB/s
}
```

**Impact:** Cannot tune for different hardware (Mac SE vs Performa).

#### Gap E: Drain Periods Not Configurable

**Current:** Hardcoded in test apps

```c
// test_stream.c:66-76
#define DRAIN_WAIT_TICKS       (10 * 60)   // 10 seconds
#define DRAIN_WAIT_LONG_TICKS  (20 * 60)   // 20 seconds
```

**Impact:** Phase transitions require app-specific tuning.

### 2.3 Protocol Gaps

#### Gap F: O(n) Peer Lookup

**Location:** `src/core/peer.c:95-133`

```c
/* DOD PERFORMANCE NOTE: This function is called on EVERY incoming packet.
   Currently it accesses peer->info.address and peer->info.port
   which are in cold storage (~1.4KB per peer). On 68030 with 256-byte cache,
   scanning 16 peers touches 22KB+ causing severe cache thrashing. */
```

**Impact:** Performance killer on Classic Mac with many peers.

#### Gap G: Fragment Reassembly Timeout

**Current:** No timeout if last fragment never arrives

```c
// protocol.c:801-808 - Validates order but no timeout
if (frag_hdr->fragment_offset != rs->received_length) {
    // Abort reassembly - but what if we just wait forever?
}
```

**Impact:** Stuck reassembly state on packet loss.

#### Gap H: Log Streaming in Test Helper

**Current:** Complete implementation in `tests/mac/log_stream.h`

**Impact:** Should be SDK feature, not test infrastructure.

### 2.4 Memory Optimization Gaps

#### Gap I: FRAME_BUF_SIZE on PT_LOWMEM

**Current:** `pt_types.h:98` uses 16384 bytes always

**Impact:** With PT_MAX_PEERS=8, that's 256KB on 4MB Mac SE.

---

## Part 3: Test App Analysis

### 3.1 perf_partner.c Workarounds

| Workaround | Lines | Should Be SDK? | Assessment |
|------------|-------|----------------|------------|
| Echo retry queue | 141-799 | No | Test-specific echoing |
| Stream rate limiting | 689-730 | Partial | SDK has it, thresholds hardcoded |
| CRITICAL priority | 469-488 | Yes | ✅ Already in SDK |
| PollFast selection | 1641-1694 | No | Proper API usage |
| Burst sending | 887-930 | No | SDK batching is better |

**Conclusion:** perf_partner correctly uses SDK features. Only gap is rate limit configuration.

### 3.2 Mac Test App Workarounds

| Workaround | Files | Should Be SDK? | Assessment |
|------------|-------|----------------|------------|
| Window tracking (g_in_flight) | test_throughput.c:107 | Yes | Need query API |
| Drain periods | test_stream.c:66-76 | Partial | Need config exposure |
| Early bootstrap | All | Yes | ✅ PeerTalk_Bootstrap() exists |
| ticks_to_ms() | All | Yes | Need SDK utility |
| Control message retry | test_stream.c:176-207 | Yes | Need helper function |
| log_stream.h | tests/mac/ | Yes | Need SDK API |
| table_ui.h | tests/mac/ | No | Test-specific UI |

### 3.3 Patterns That Work Well

These SDK features are used correctly by test apps:

1. **PeerTalk_Bootstrap()** - Early buffer allocation
2. **PeerTalk_SendEx() with PT_PRIORITY_CRITICAL** - Control messages
3. **PeerTalk_PollFast()** - High-frequency polling in echo mode
4. **PeerTalk_GetPeerCapabilities()** - Query peer constraints
5. **Backpressure handling** - Stop sending on WOULD_BLOCK/BUFFER_FULL

---

## Part 4: Protocol Assessment

### 4.1 Wire Format Quality

| Aspect | Rating | Notes |
|--------|--------|-------|
| Standard header (10 bytes) | Excellent | Magic, version, type, flags, seq, length |
| Compact header (4 bytes) | Excellent | 60% reduction for small messages |
| Discovery packet | Good | Minimal (14-45 bytes) with CRC |
| Fragment header | Good | 8 bytes, clear semantics |

### 4.2 Capability Exchange Quality

| Aspect | Rating | Notes |
|--------|--------|-------|
| TLV encoding | Excellent | Forward-compatible, extensible |
| Pressure reporting | Good | Bidirectional, threshold-based |
| Optimal chunk negotiation | Excellent | 25% MacTCP threshold |
| Compact mode negotiation | Good | Symmetric, both must support |

### 4.3 Protocol Issues

| Issue | Severity | Description |
|-------|----------|-------------|
| Sequence validation missing | Low | Can't detect duplicate capabilities |
| Compact fragments | Medium | 4-bit flags can't carry fragment flag |
| Fragment timeout | High | No timeout on incomplete reassembly |
| Peer lookup O(n) | High | Linear scan per packet |
| MacTCP fields in generic struct | Low | Platform leak in pt_peer_caps |

---

## Part 5: Alignment with PROJECT_GOALS.md

### Fully Achieved

| Goal | Evidence |
|------|----------|
| "Two headers, unified APIs" | `peertalk.h`, `pt_log.h` |
| "Same logic on POSIX and Classic Mac" | Identical API, automatic adaptation |
| "Event-driven callbacks" | Complete callback system |
| "Non-blocking by default" | All async, returns immediately |
| "Resource-aware on constrained hardware" | Buffer pool, PT_LOWMEM, adaptive sizing |
| "Priority-based message handling" | O(1) priority queues, CRITICAL bypass |

### Partially Achieved

| Goal | Status | Gap |
|------|--------|-----|
| "Add networking in minutes" | 80% | Manual window tracking, no control helper |
| "Trust the library to manage resources" | 85% | Flow control automatic, but no retry queue |
| "Zero platform-specific code" | 90% | ticks_to_ms() still needed in apps |

### Not Yet Achieved

| Goal | Status | Gap |
|------|--------|-----|
| Query APIs for flow state | Missing | No CanSend, GetCapacity |
| Built-in log streaming | Missing | In test helper, not SDK |

---

## Part 6: Improvement Plan

### Phase 1: API Completeness (High Priority)

**Estimated Effort:** 2-3 days

#### Task 1.1: Add Window Query API

**File:** `include/peertalk.h`, `src/core/send.c`

```c
/**
 * Check if a message can be sent without blocking.
 * Returns 1 if send window has room, 0 if would block.
 */
int PeerTalk_CanSend(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id);

/**
 * Get send capacity for a peer.
 * Returns available slots in send window.
 */
uint16_t PeerTalk_GetSendCapacity(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    uint16_t *out_pending,      /* Messages in flight */
    uint16_t *out_available);   /* Slots available */
```

**Implementation:**
```c
int PeerTalk_CanSend(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id) {
    pt_peer *peer = pt_peer_get(ctx, peer_id);
    if (!peer || !peer->hot.connected) return 0;

    uint16_t in_flight = peer->pipeline.pending_count;
    if (peer->send_queue) {
        in_flight += peer->send_queue->count;
    }
    return in_flight < peer->cold.caps.send_window;
}
```

#### Task 1.2: Add Control Message Helper

**File:** `include/peertalk.h`, `src/core/send.c`

```c
/**
 * Send a control message with automatic retry.
 * Uses CRITICAL priority, retries up to 5 times with backoff.
 */
PeerTalk_Error PeerTalk_SendControl(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    const void *data,
    uint16_t length);
```

**Implementation:**
```c
PeerTalk_Error PeerTalk_SendControl(PeerTalk_Context *ctx,
                                     PeerTalk_PeerID peer_id,
                                     const void *data, uint16_t length) {
    int retries = 0;
    const int max_retries = 5;

    while (retries < max_retries) {
        PeerTalk_Error err = PeerTalk_SendEx(ctx, peer_id, data, length,
                                             PT_PRIORITY_CRITICAL,
                                             PT_SEND_NO_DELAY, 0);
        if (err == PT_OK) return PT_OK;
        if (err != PT_ERR_WOULD_BLOCK && err != PT_ERR_BUFFER_FULL) {
            return err;  // Fatal error
        }
        retries++;
        PeerTalk_Poll(ctx);  // Drain and retry
    }
    return PT_ERR_TIMEOUT;
}
```

#### Task 1.3: Add Tick Conversion Utilities

**File:** `include/peertalk.h`, `src/core/pt_compat.c`

```c
/**
 * Convert Mac ticks (1/60th second) to milliseconds.
 * On POSIX, assumes input is already milliseconds.
 */
uint32_t PeerTalk_TicksToMs(uint32_t ticks);

/**
 * Convert milliseconds to Mac ticks.
 * On POSIX, returns input unchanged.
 */
uint32_t PeerTalk_MsToTicks(uint32_t ms);
```

### Phase 2: Configuration Exposure (Medium Priority)

**Estimated Effort:** 1-2 days

#### Task 2.1: Expose Rate Limit Configuration

**File:** `include/peertalk.h` (PeerTalk_Config)

```c
typedef struct {
    // ... existing fields ...

    /* Rate limiting configuration (0 = use defaults) */
    uint32_t rate_limit_high_kbps;    /* Rate at HIGH pressure (default: 50) */
    uint32_t rate_limit_medium_kbps;  /* Rate at MEDIUM pressure (default: 100) */
    uint8_t  rate_limit_threshold_high;   /* Pressure % for high limit (default: 85) */
    uint8_t  rate_limit_threshold_medium; /* Pressure % for medium limit (default: 50) */
} PeerTalk_Config;
```

#### Task 2.2: Expose Drain Configuration

**File:** `include/peertalk.h` (PeerTalk_Config)

```c
typedef struct {
    // ... existing fields ...

    /* Phase transition drain periods (0 = use defaults) */
    uint16_t drain_short_ms;      /* Short drain period (default: 10000) */
    uint16_t drain_long_ms;       /* Long drain period (default: 20000) */
    uint16_t drain_threshold;     /* Message size for long drain (default: 512) */
} PeerTalk_Config;
```

### Phase 3: Protocol Improvements (Medium Priority)

**Estimated Effort:** 2-3 days

#### Task 3.1: Implement Peer Address Hash Table

**File:** `src/core/peer.c`

Replace O(n) linear scan with O(1) hash lookup:

```c
#define PT_PEER_HASH_BUCKETS 16

typedef struct {
    uint16_t peer_idx;   /* Index into peer array */
    uint16_t next;       /* Next in chain (collision handling) */
} pt_peer_hash_entry;

/* Add to pt_context */
pt_peer_hash_entry peer_hash[PT_PEER_HASH_BUCKETS];

/* Hash function: XOR high/low bits of address */
static uint16_t pt_peer_hash(uint32_t addr, uint16_t port) {
    uint32_t h = addr ^ (port << 16) ^ (port >> 16);
    return (h ^ (h >> 8)) & (PT_PEER_HASH_BUCKETS - 1);
}
```

#### Task 3.2: Add Fragment Reassembly Timeout

**File:** `src/core/protocol.c`

```c
/* In pt_reassembly_state */
uint32_t start_time_ms;  /* When first fragment received */

/* In reassembly check */
#define PT_FRAGMENT_TIMEOUT_MS 30000

if (rs->active && (now_ms - rs->start_time_ms > PT_FRAGMENT_TIMEOUT_MS)) {
    PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
        "Fragment reassembly timeout: msg_id=%u, received=%u/%u",
        rs->message_id, rs->received_length, rs->total_length);
    rs->active = 0;  /* Abort */
}
```

### Phase 4: SDK Features (Lower Priority)

**Estimated Effort:** 2-3 days

#### Task 4.1: Add Log Streaming API

**File:** `include/peertalk.h`, `src/core/stream.c`

```c
/**
 * Stream log data to a connected peer.
 * Captures PT_Log output and sends via PeerTalk_StreamSend().
 */
PeerTalk_Error PeerTalk_StreamLogs(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    PeerTalk_StreamCompleteCB on_complete,
    void *user_data);
```

#### Task 4.2: Reduce FRAME_BUF_SIZE for PT_LOWMEM

**File:** `src/core/pt_types.h`

```c
#ifdef PT_LOWMEM
    #define PT_FRAME_BUF_SIZE   8192   /* 8KB for 4MB Macs */
#else
    #define PT_FRAME_BUF_SIZE   16384  /* 16KB standard */
#endif
```

### Phase 5: Test App Cleanup (After SDK Changes)

**Estimated Effort:** 1 day

After SDK improvements, update test apps to use new APIs:

1. Replace manual `g_in_flight` tracking with `PeerTalk_CanSend()`
2. Replace control message retry loops with `PeerTalk_SendControl()`
3. Replace `ticks_to_ms()` with `PeerTalk_TicksToMs()`
4. Remove `log_stream.h`, use `PeerTalk_StreamLogs()`

---

## Part 7: Implementation Priority Matrix

| Task | Priority | Effort | Impact | Dependencies |
|------|----------|--------|--------|--------------|
| 1.1 Window Query API | High | 4 hours | High | None |
| 1.2 Control Message Helper | High | 4 hours | High | None |
| 1.3 Tick Conversion | High | 2 hours | Medium | None |
| 2.1 Rate Limit Config | Medium | 4 hours | Medium | None |
| 2.2 Drain Config | Medium | 4 hours | Medium | None |
| 3.1 Peer Hash Table | Medium | 8 hours | High | None |
| 3.2 Fragment Timeout | Medium | 4 hours | Medium | None |
| 4.1 Log Streaming API | Low | 8 hours | Medium | None |
| 4.2 PT_LOWMEM Buffer | Low | 2 hours | Low | None |
| 5.0 Test App Cleanup | Low | 4 hours | Low | 1.1-1.3, 4.1 |

**Recommended Order:**
1. Tasks 1.1, 1.2, 1.3 (API completeness) - Immediate value
2. Task 3.1 (Hash table) - Performance critical for Mac
3. Tasks 2.1, 2.2 (Configuration) - Tuning capability
4. Tasks 3.2, 4.1, 4.2, 5.0 - Polish

---

## Part 8: Success Criteria

### API Completeness
- [ ] `PeerTalk_CanSend()` returns accurate window state
- [ ] `PeerTalk_GetSendCapacity()` matches actual queue depth
- [ ] `PeerTalk_SendControl()` reliably delivers control messages
- [ ] Mac test apps no longer need manual window tracking

### Configuration
- [ ] Rate limits configurable without code changes
- [ ] Drain periods configurable for different hardware
- [ ] PT_LOWMEM builds use reduced buffer sizes

### Performance
- [ ] Peer lookup O(1) via hash table
- [ ] No regression in throughput tests
- [ ] Mac SE tests pass with reduced memory

### Protocol
- [ ] Fragment reassembly times out after 30 seconds
- [ ] No stuck reassembly states in stress tests

---

## Appendix A: File Reference

| Component | Primary File | Lines |
|-----------|--------------|-------|
| Flow control decisions | src/core/send.c | 403-469 |
| Pressure throttling | src/core/peer.c | 638-683 |
| Rate limiting | src/core/peer.c | 685-735 |
| Adaptive tuning | src/core/peer.c | 886-1028 |
| Priority queue | src/core/queue.c | 574-627 |
| Fragment reassembly | src/core/protocol.c | 750-850 |
| Capability exchange | src/core/protocol.c | 558-700 |
| Peer lookup | src/core/peer.c | 95-133 |

## Appendix B: Recent Commits Reference

| Commit | Description | Relevance |
|--------|-------------|-----------|
| 813ed8d | Auto rate limiting and pressure-triggered fragmentation | Core flow control |
| 67193fe | Bidirectional pressure updates and CRITICAL bypass | Priority system |
| c428cff | Rate limiting in perf_partner | Test partner fix |
| 4859c7d | Raise pressure thresholds | Tuning |
| 508cc6c | Longer drain period for large messages | Phase transitions |

## Appendix C: Test Commands

```bash
# Run throughput test
/run-test throughput performa6200

# Run stream test
/run-test stream performa6200

# Run all tests
/run-test all performa6200

# Build Mac tests
./scripts/build-mac-tests.sh mactcp perf
```
