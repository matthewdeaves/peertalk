# PHASE 3: Advanced Queue Management

> **Status:** OPEN
> **Depends on:** Phase 2 (Protocol layer - base queue implementation in `src/core/queue.c`)
> **Used by:** Phases 4, 5, 6 (Networking layers use enhanced queues)
> **Produces:** Robust message handling with backpressure and burst tolerance
> **Risk Level:** LOW
> **Estimated Sessions:** 2
> **Review Applied:** 2026-01-27 (DOD review - fixed struct layout for cache efficiency, added O(1) priority free-lists and coalesce hash table, pre-allocated batch buffer, documented build order)
> **Review Applied:** 2026-01-28 (Plan review - fixed peek/consume bypass of priority lists, moved slot_next into pt_queue_slot, added PT_QUEUE_MAX_SLOTS, fixed logging categories, added ISR-safety warnings, enhanced logging guidance)
> **Review Applied:** 2026-01-28 (Dependency review - moved Session 3.3 to Phase 3.5 to resolve Phase 4 dependency)
> **Review Applied:** 2026-01-28 (Comprehensive review - added PT_Log context requirement, ISR logging example, pt_batch field reorder, direct pop variant, reduced hash table size, size assertions, logging table additions)
> **Review Applied:** 2026-01-29 (Plan review - fixed PT_Log macro names (PT_WARN→PT_LOG_WARN etc), fixed coalesce hash function for per-peer keys, added pt_check_queue_isr_flags() helper, documented ctx->log initialization requirement, added capacity validation)

**Build Order:** Phase 2 → Phase 3 → Phase 4 → Phase 3.5. This phase has no blocking dependencies.

## Fact-Check Summary

| Claim | Source | Status |
|-------|--------|--------|
| TickCount() not in Table B-3 | Inside Macintosh Vol VI, lines 224396-224607 | ✓ Verified |
| BlockMove not in Table B-3 | Inside Macintosh Vol VI Table B-3 | ✓ Verified |
| BlockMove documented as interrupt-safe in Sound Manager | Inside Macintosh Vol VI, line 162410 | ✓ Verified (but conservative approach: use pt_memcpy_isr) |
| ASR cannot allocate/free memory | MacTCP Programmer's Guide, p.1506, p.4230 | ✓ Verified |
| ASR can issue async MacTCP calls | MacTCP Programmer's Guide, p.4232 | ✓ Verified |
| OT notifiers run at deferred task time | NetworkingOpenTransport.txt, page 5793 | ✓ Verified |
| OTGetTimeStamp/OTElapsedMilliseconds interrupt-safe | NetworkingOpenTransport.txt Table C-1, page 793 | ✓ Verified |
| OTAtomicSetBit/Add interrupt-safe | NetworkingOpenTransport.txt Table C-1, pages 794-795 | ✓ Verified |
| OTAllocMem may fail at deferred task time | NetworkingOpenTransport.txt Ch.5, p.9143-9148 | ✓ Verified |
| ADSP completion routines at interrupt level | Programming With AppleTalk Ch.3, line 1554 | ✓ Verified |
| 68030 data cache 256 bytes | Hardware specification | ✓ Verified |
| pt_queue_slot size 268 bytes | Calculated: 4+2+2+2+1+1+256 | ✓ Verified (compile-time assertion) |
| PT_Log macros: PT_LOG_WARN, PT_LOG_DEBUG, PT_LOG_ERR | PHASE-0-LOGGING.md lines 663-687 | ✓ Verified |

**BREAKING CHANGE:** Phase 3 modifies `pt_queue_slot` structure (adds fields, reorders for cache efficiency). All code using `pt_queue` or `pt_queue_slot` must be rebuilt after Phase 3 implementation.

**Note:** This phase enhances the core queue system from Phase 2. It does NOT depend on the networking layers. Phases 4, 5, and 6 can use these enhanced features if Phase 3 is implemented first, or can use the basic queue features from Phase 2 if Phase 3 is deferred. The public API integration (`PeerTalk_SendEx`) is in Phase 3.5, which depends on Phase 4's `send_udp` callback.

**Phase 2 Dependency:** This phase uses `pt_queue_pressure()` from Phase 2, which returns a 0-100 percentage of queue fill level. Verify Phase 2 exports this function before implementing Session 3.2.

**Compile Guards:** After Phase 3 is implemented, define `PT_PHASE3_QUEUES` in the build to enable Phase 3 features. Phases 4-6 can check `#ifdef PT_PHASE3_QUEUES` to use enhanced functions (`pt_queue_push_coalesce`, `pt_queue_pop_priority`) or fall back to Phase 2's basic functions.

## Phase 1 Structure Modifications Required

**IMPORTANT:** Phase 3 requires additions to Phase 1/2 structures. These should be added when implementing the relevant sessions:

### Queue Pressure Design Decision

**NOTE:** The plan originally suggested adding a `queue_pressure` field to `struct pt_peer`, but the implementation uses on-demand calculation via `pt_queue_pressure()` instead. This design decision:
- Avoids field synchronization bugs (no stale cached values)
- Reduces memory footprint per peer
- Is more efficient (pressure calculated only when needed)

Queue pressure is queried via `pt_queue_backpressure(peer->send_queue)` which internally calls `pt_queue_pressure()` from Phase 2. No additional field in `pt_peer` is required.

### Add to `struct pt_context` (in `src/core/pt_internal.h`):
```c
/* Add to struct pt_context - logging context and batch buffer.
 * PT_Log *log is REQUIRED for PT_CTX_DEBUG/PT_CTX_WARN/PT_CTX_ERR macros.
 * MUST be initialized with PT_LogCreate() before calling any Phase 3 queue
 * functions that log (pt_drain_send_queue, pt_check_queue_isr_flags, etc.).
 * send_batch avoids 1.4KB stack allocation per pt_drain_send_queue() call. */
PT_Log             *log;             /* Logging context (from Phase 0) - REQUIRED */
pt_batch            send_batch;      /* Pre-allocated batch buffer */
```

### Initialization Requirements

**CRITICAL:** Before using Phase 3 queue functions, initialize the logging context:

```c
/* In your application initialization code (after pt_context_create): */
ctx->log = PT_LogCreate("peertalk.log", PT_LOG_LEVEL_DEBUG);
if (!ctx->log) {
    /* Handle error - Phase 3 functions will crash without valid ctx->log */
    return PT_ERR_MEMORY;
}

/* Later, before shutdown: */
PT_LogDestroy(ctx->log);
ctx->log = NULL;
```

Functions that require `ctx->log` to be initialized:
- `pt_drain_send_queue()` - logs batch operations at DEBUG, WARN, ERR levels
- `pt_check_queue_isr_flags()` - logs deferred ISR events at DEBUG, WARN levels
- `pt_queue_push()` - logs queue full warnings (if ctx provided)
- Backpressure logging in `pt_queue_try_push()`

### Add to `pt_queue` (in `src/core/queue.h`):
```c
/* Add to pt_queue struct - extended data for O(1) priority and coalesce operations.
 * See pt_queue_ext definition in Phase 3 for details. */
pt_queue_ext        ext;             /* Priority free-lists and coalesce hash */

/* For direct pop (zero-copy) operations - tracks pending pop state */
uint8_t             pending_pop_prio;  /* Priority level of pending pop */
uint16_t            pending_pop_slot;  /* Slot index of pending pop */

/* ISR deferred logging flags - set in ISR, checked/logged in main loop.
 * These flags are in pt_queue (not pt_context) because pt_queue_push_coalesce_isr
 * only has access to the queue pointer. Main loop checks these via pt_poll().
 *
 * LIFECYCLE: Flags are volatile booleans, not counters. Multiple events of the
 * same type within one ISR call only set the flag once (not accumulated). Main
 * loop should call pt_check_queue_isr_flags() after each network operation and
 * the function clears each flag immediately after logging. If not checked
 * frequently enough, subsequent events of the same type may be lost (but the
 * first occurrence is always captured).
 */
struct {
    volatile uint8_t queue_full    : 1;  /* Queue push failed in ISR */
    volatile uint8_t coalesce_hit  : 1;  /* Message coalesced in ISR */
    volatile uint8_t hash_collision: 1;  /* Hash collision in ISR */
} isr_flags;
```

### Add to `pt_platform_ops` (in `src/core/pt_internal.h`):
```c
/* Add to pt_platform_ops struct - UDP send function for unreliable messages.
 * NOTE: This callback is implemented by Phases 4, 5, 6. Used by Phase 3.5
 * (PeerTalk_SendEx) for unreliable message delivery. */
PeerTalk_Error  (*send_udp)(struct pt_context *ctx, struct pt_peer *peer,
                            const void *data, uint16_t length);
```

These additions enable:
- `PeerTalk_SendEx()`: Queue messages per-peer with priority and coalescing (implemented in Phase 3.5)
- `pt_drain_send_queue()`: Batch send without stack allocation
- O(1) priority dequeue and coalesce lookup

## Overview

Phase 3 enhances the queue system from Phase 2 with advanced features for production use: priority queues, coalescing for high-frequency updates, backpressure signaling, and batch send operations.

**Key Insight:** Classic Mac games often send position updates 10+ times per second. Without coalescing, queues overflow. With coalescing, only the latest position matters.

## Public API Mapping

The internal queue system is exposed through the public `PeerTalk_SendEx()` function:

```c
/* Public API (from peertalk.h) */
PeerTalk_Error PeerTalk_SendEx(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    const void *data,
    uint16_t length,
    uint16_t flags,           /* PT_SEND_* flags */
    uint8_t priority,         /* PT_PRIORITY_* */
    uint16_t coalesce_id      /* PT_COALESCE_* or custom ID */
);

/* Maps to internal queue functions */
/* - PT_SEND_UNRELIABLE: Uses UDP path instead of TCP queue */
/* - PT_PRIORITY_*: Maps to pt_queue priority levels */
/* - coalesce_id: Maps to pt_coalesce_key for queue push */
```

**Priority Mapping:**
| Public API | Internal |
|------------|----------|
| PT_PRIORITY_LOW | PT_PRIO_LOW |
| PT_PRIORITY_NORMAL | PT_PRIO_NORMAL |
| PT_PRIORITY_HIGH | PT_PRIO_HIGH |
| PT_PRIORITY_REALTIME | PT_PRIO_CRITICAL |

**Coalesce Mapping:**
| Public API | Internal |
|------------|----------|
| PT_COALESCE_NONE | PT_COALESCE_NONE |
| PT_COALESCE_NEWEST | Uses pt_queue_push_coalesce() |
| PT_COALESCE_OLDEST | Uses pt_queue_try_push() with key check |
| User-provided coalesce_id | Passed as pt_coalesce_key |

## Goals

1. Implement priority-based dequeue
2. Add automatic message coalescing by type
3. Implement backpressure signaling to callers
4. Add batch send for efficiency
5. Validate with stress tests

## Session Scope Table

| Session | Focus | Status | Files Created/Modified | Tests | Verify |
|---------|-------|--------|------------------------|-------|--------|
| 3.1 | Priority & Coalesce | [DONE] | `src/core/queue.c` (enhance) | `tests/test_queue_advanced.c` | Priority order, coalescing |
| 3.2 | Backpressure & Batch | [DONE] | `src/core/queue.c`, `src/core/send.c` | `tests/test_backpressure.c` | Pressure thresholds, batch send |

> **Note:** Public API integration (`PeerTalk_SendEx`) moved to Phase 3.5 to resolve Phase 4 dependency.

### Status Key
- **[OPEN]** - Not started
- **[IN PROGRESS]** - Currently being worked on
- **[READY TO TEST]** - Implementation complete, needs verification
- **[DONE]** - Verified and complete

---

## PT_Log Integration

Phase 3 queue operations should log via PT_Log using the following categories and levels:

| Operation | Category | Level | Example Message |
|-----------|----------|-------|-----------------|
| Queue ext init | `PT_LOG_CAT_PROTOCOL` | DEBUG | "Queue ext initialized: capacity=%u" |
| Queue push failed (full) | `PT_LOG_CAT_PROTOCOL` | WARN | "Queue push failed: queue full" |
| Queue push failed (oversized) | `PT_LOG_CAT_PROTOCOL` | WARN | "Queue push failed: message too large (%u bytes)" |
| Coalesce hit | `PT_LOG_CAT_PROTOCOL` | DEBUG | "Coalesce hit: key=0x%04X" |
| Hash collision | `PT_LOG_CAT_PROTOCOL` | DEBUG | "Hash collision at bucket %u" |
| Backpressure transition | `PT_LOG_CAT_PERF` | WARN | "Queue pressure CRITICAL: %u%% (%u/%u slots)" |
| Message dropped (backpressure) | `PT_LOG_CAT_PROTOCOL` | WARN | "Message dropped: backpressure %s" |
| Batch sent | `PT_LOG_CAT_PROTOCOL` | DEBUG | "Batch sent: %u messages, %u bytes" |
| Batch send failed | `PT_LOG_CAT_PROTOCOL` | ERR | "Batch send failed" |
| Message oversized | `PT_LOG_CAT_PROTOCOL` | WARN | "Message too large for batch" |
| ISR queue full (deferred) | `PT_LOG_CAT_PROTOCOL` | WARN | "Queue full during ISR" |

**Logging Category Rationale:**
- **Backpressure transitions** use `PT_LOG_CAT_PERF` (performance category) to allow filtering in production. High-frequency queue operations can spam logs; performance category allows applications to suppress these warnings while keeping protocol error logging enabled.
- **Protocol operations** (coalesce, batch, ISR flags) use `PT_LOG_CAT_PROTOCOL` for correctness tracking.

**CRITICAL ISR-Safety Rule:** Do NOT call PT_Log from `pt_queue_push_coalesce_isr()` or any other interrupt-level code. PT_Log is not interrupt-safe (uses File Manager, vsprintf, memory allocation). Instead:
1. Set a volatile flag in the ISR (e.g., `q->isr_flags.queue_full = 1`)
2. Check and log from the main event loop (e.g., `if (q->isr_flags.queue_full) { PT_LOG_WARN(ctx->log, ...); q->isr_flags.queue_full = 0; }`)

**Concrete ISR Logging Pattern Example:**
```c
/* In queue.h - add to struct pt_queue (NOT pt_context, since ISR only has queue ptr) */
struct {
    volatile uint8_t queue_full    : 1;  /* Set when ISR push fails */
    volatile uint8_t coalesce_hit  : 1;  /* Set when ISR coalesces */
    volatile uint8_t hash_collision: 1;  /* Set when hash collision occurs */
} isr_flags;

/* In ISR (pt_queue_push_coalesce_isr): */
if (q->count >= q->capacity) {
    q->isr_flags.queue_full = 1;  /* Signal main loop */
    return -1;
}

/* In main event loop (pt_poll or similar) - MUST be called periodically: */
void pt_check_queue_isr_flags(struct pt_context *ctx, pt_queue *q) {
    if (!ctx || !ctx->log || !q)
        return;
    if (q->isr_flags.queue_full) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_PROTOCOL, "Queue full during ISR");
        q->isr_flags.queue_full = 0;
    }
    if (q->isr_flags.coalesce_hit) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_PROTOCOL, "Coalesce hit during ISR");
        q->isr_flags.coalesce_hit = 0;
    }
    if (q->isr_flags.hash_collision) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_PROTOCOL, "Hash collision during ISR");
        q->isr_flags.hash_collision = 0;
    }
}

/* Add declaration to queue.h:
 *   void pt_check_queue_isr_flags(struct pt_context *ctx, pt_queue *q);
 * Caller (e.g., pt_poll) must call this after processing network events.
 */
```

---

## Session 3.1: Priority & Coalescing

### Objective
Add priority-based dequeue and automatic coalescing for high-frequency message types.

### Tasks

#### Task 3.1.1: Enhance queue with priority levels and O(1) data structures

**IMPORTANT:** This task MODIFIES the `pt_queue_slot` structure from Phase 2.
The enhanced slot adds `coalesce_key` and `timestamp` fields, and the queue
gains priority free-lists and a coalesce hash table. All code using the queue
must be rebuilt after this change.

**O(1) Data Structures:** The priority free-lists and coalesce hash table
ensure that both `pt_queue_pop_priority()` and `pt_queue_push_coalesce()` are
O(1) operations. This is critical for 68k Macs with no/tiny cache where O(n)
scans cause unacceptable latency.

```c
/* Add to src/core/queue.h */

typedef enum {
    PT_PRIO_LOW = 0,
    PT_PRIO_NORMAL = 1,
    PT_PRIO_HIGH = 2,
    PT_PRIO_CRITICAL = 3
} pt_priority;

/* Coalesce key - messages with same key are coalesced
 *
 * NOTE: For per-peer coalescing, combine key with peer ID:
 *   key = PT_COALESCE_POSITION | (peer_id << 8);
 * This ensures peer A's position updates don't coalesce with peer B's.
 */
typedef uint16_t pt_coalesce_key;

#define PT_COALESCE_NONE     0x0000  /* Never coalesce */
#define PT_COALESCE_POSITION 0x0001  /* Position updates */
#define PT_COALESCE_STATE    0x0002  /* State updates */
#define PT_COALESCE_CHAT     0x0003  /* Chat typing indicator */

/* Helper macro for per-peer coalesce keys
 *
 * COUPLING: peer_id must be < 256 (fits in upper 8 bits of 16-bit key).
 * This matches PT_MAX_PEER_ID defined in Phase 1. If peer IDs exceed 255,
 * use a different coalescing strategy or extend to 32-bit keys.
 */
#define PT_COALESCE_KEY(type, peer_id) ((type) | (((peer_id) & 0xFF) << 8))

/* Enhanced slot - extends Phase 2's pt_queue_slot with coalesce_key and timestamp
 *
 * CACHE EFFICIENCY: Metadata fields are placed BEFORE data[] so that checking
 * flags/priority doesn't load the 256-byte payload into cache. On 68030 with
 * 256-byte data cache, this is critical - wrong ordering guarantees cache misses.
 *
 * TRAVERSAL LOCALITY: next_slot is stored IN the slot (not in pt_queue_ext) so
 * that traversing a priority list accesses contiguous memory. This avoids
 * ping-ponging between slots[] and ext.slot_next[] which would thrash the cache.
 *
 * Field ordering: largest alignment first within metadata, then data[] last.
 */
typedef struct {
    uint32_t timestamp;      /* 4 bytes - age-based prioritization (ticks) */
    uint16_t length;         /* 2 bytes - payload length */
    uint16_t coalesce_key;   /* 2 bytes - for coalescing lookup */
    uint16_t next_slot;      /* 2 bytes - next slot in priority list (for traversal locality) */
    uint8_t  priority;       /* 1 byte - PT_PRIO_* */
    volatile uint8_t flags;  /* 1 byte - PT_SLOT_* flags */
    uint8_t  data[PT_QUEUE_SLOT_SIZE];  /* 256 bytes - COLD, placed last */
} pt_queue_slot;  /* Total: 12 bytes metadata + 256 bytes data = 268 bytes */

/* SIZE ASSERTION: Verify struct layout has no unexpected padding.
 * On Classic Mac, use #pragma pack if needed to ensure tight packing.
 * The actual metadata is 12 bytes (4+2+2+2+1+1), not 14 as originally stated.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    _Static_assert(sizeof(pt_queue_slot) == 268, "pt_queue_slot size mismatch");
#else
    /* For C89/C99: runtime check in pt_queue_ext_init() */
    #define PT_QUEUE_SLOT_EXPECTED_SIZE 268
#endif

/* Priority free-list constants */
#define PT_PRIO_COUNT        4   /* Number of priority levels */
#define PT_SLOT_NONE         0xFFFF  /* Invalid slot index (end of list) */
#define PT_QUEUE_MAX_SLOTS   32  /* Maximum slots per queue - HARD LIMIT
                                   * This is not just a memory optimization but a data
                                   * structure correctness constraint. pt_queue_ext_init()
                                   * only initializes next_slot pointers for slots 0..31.
                                   * Queues with capacity > 32 will have uninitialized
                                   * next_slot fields causing corruption. */

/* Coalesce hash table size - power of 2 for fast modulo
 *
 * MEMORY OPTIMIZATION: Size matches PT_QUEUE_MAX_SLOTS (32) to avoid
 * wasting memory. With 32-entry hash and 32 slots, load factor is ~50%
 * which is acceptable for simple direct-mapped hash.
 *
 * LIMITATION: Simple direct-mapped hash table (one slot per bucket).
 * If two different keys hash to the same bucket, only the most recent
 * is tracked. The older key won't benefit from coalescing until its
 * message is popped. For small queues (8-32 slots) this is acceptable.
 * A more sophisticated approach (chaining/probing) could be added later.
 */
#define PT_COALESCE_HASH_SIZE  32  /* Match PT_QUEUE_MAX_SLOTS */
#define PT_COALESCE_HASH_MASK  (PT_COALESCE_HASH_SIZE - 1)

/* Hash function for coalesce keys - mixes both type and peer_id bits
 *
 * Per-peer keys use PT_COALESCE_KEY(type, peer_id) = type | (peer_id << 8).
 * A simple (key & MASK) would only use the bottom 5 bits (type), causing
 * all peers with the same type to collide. This XOR-fold mixes the peer_id
 * (upper 8 bits) into the hash for better distribution.
 */
#define PT_COALESCE_HASH(key)  (((key) ^ ((key) >> 8)) & PT_COALESCE_HASH_MASK)

/* Enhanced queue structure - O(1) priority dequeue and coalesce lookup
 *
 * PERFORMANCE: Classic Mac 68k has no/tiny cache. O(n) scans are unacceptable.
 * - Priority free-lists: O(1) dequeue from highest non-empty priority
 * - Coalesce hash table: O(1) lookup for existing key
 *
 * NOTE: slot_next is stored IN pt_queue_slot (not here) for traversal locality.
 * This avoids ping-ponging between slots[] and ext during list traversal.
 *
 * Add these fields to pt_queue (defined in Phase 2):
 */
typedef struct pt_queue_ext {
    /* Priority-indexed free lists - O(1) dequeue */
    uint16_t prio_head[PT_PRIO_COUNT];  /* Head index per priority level */
    uint16_t prio_tail[PT_PRIO_COUNT];  /* Tail index per priority level */
    uint16_t prio_count[PT_PRIO_COUNT]; /* Count per priority level */

    /* Coalesce key hash table - O(1) lookup */
    uint16_t coalesce_hash[PT_COALESCE_HASH_SIZE];  /* key hash -> slot index */
} pt_queue_ext;

/*
 * Initialize extended queue data structures
 *
 * Call this after pt_queue_init() from Phase 2 to set up
 * priority free-lists and coalesce hash table.
 *
 * LOGGING: Logs DEBUG message on success with queue capacity.
 */
void pt_queue_ext_init(pt_queue *q) {
    pt_queue_ext *ext;
    uint16_t i;
    uint16_t max_slots;  /* Loop invariant hoisted for optimization */

    if (!q) {
        /* Cannot log without context, but defend against NULL */
        return;
    }

#ifdef PT_QUEUE_SLOT_EXPECTED_SIZE
    /* Runtime size check for C89/C99 (C11 uses _Static_assert) */
    if (sizeof(pt_queue_slot) != PT_QUEUE_SLOT_EXPECTED_SIZE) {
        /* Size mismatch - likely compiler padding issue.
         * Use #pragma pack on Classic Mac compilers if this fails. */
        return;  /* Fail-safe: don't corrupt memory */
    }
#endif

    /* Capacity validation - queue capacity must not exceed PT_QUEUE_MAX_SLOTS
     * because next_slot pointers are only initialized for slots 0..MAX_SLOTS-1 */
    if (q->capacity > PT_QUEUE_MAX_SLOTS) {
        /* Configuration error - slots beyond MAX_SLOTS have uninitialized next_slot.
         * Caller should use smaller capacity or increase PT_QUEUE_MAX_SLOTS.
         * NOTE: Cannot log here since we don't have context - caller should check. */
        return;  /* Fail-safe: don't use queue with uninitialized slots */
    }

    ext = &q->ext;

    /* Initialize priority free-lists as empty */
    for (i = 0; i < PT_PRIO_COUNT; i++) {
        ext->prio_head[i] = PT_SLOT_NONE;
        ext->prio_tail[i] = PT_SLOT_NONE;
        ext->prio_count[i] = 0;
    }

    /* Initialize slot next_slot pointers (stored in slots themselves)
     * OPTIMIZATION: Hoist loop invariant min(capacity, MAX_SLOTS) */
    max_slots = (q->capacity < PT_QUEUE_MAX_SLOTS) ? q->capacity : PT_QUEUE_MAX_SLOTS;
    for (i = 0; i < max_slots; i++) {
        q->slots[i].next_slot = PT_SLOT_NONE;
    }

    /* Initialize coalesce hash table as empty */
    for (i = 0; i < PT_COALESCE_HASH_SIZE; i++) {
        ext->coalesce_hash[i] = PT_SLOT_NONE;
    }

    /* Initialize pending pop state */
    q->pending_pop_prio = 0;
    q->pending_pop_slot = PT_SLOT_NONE;

    /* Initialize ISR flags (for deferred logging from interrupt context) */
    q->isr_flags.queue_full = 0;
    q->isr_flags.coalesce_hit = 0;
    q->isr_flags.hash_collision = 0;

    /* NOTE: Logging should be done by caller who has context:
     * PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_PROTOCOL, "Queue ext initialized: capacity=%u", q->capacity);
     */
}

/* Platform-portable tick getter
 * On Classic Mac: wraps TickCount()
 * On POSIX: wraps clock_gettime() converted to ms
 */
uint32_t pt_get_ticks(void);
```

#### Task 3.1.2: Implement pt_get_ticks()

**Implementation Note:** `pt_get_ticks()` should be added to `src/core/pt_compat.c` (with declaration in `pt_compat.h`), NOT in `queue.c`. This keeps platform-specific code centralized in the compatibility layer established in Phase 1.

```c
/* Add to src/core/pt_compat.h */
uint32_t pt_get_ticks(void);
```

```c
/* Add to src/core/pt_compat.c */

/*
 * Platform-portable tick getter
 *
 * Returns monotonically increasing tick count.
 * Resolution varies by platform but sufficient for coalescing/priority.
 */
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
    /* Classic Mac - use TickCount() directly (60 ticks/sec)
     * TickCount() is declared in OSUtils.h, NOT Timer.h
     * NOTE: TickCount() is NOT documented as interrupt-safe in Inside Macintosh
     * Table B-3. Do NOT call from ASR/notifier - use timestamp=0 instead.
     */
    #include <OSUtils.h>
    uint32_t pt_get_ticks(void) {
        return (uint32_t)TickCount();
    }
#else
    /* POSIX - use milliseconds */
    #include <time.h>
    uint32_t pt_get_ticks(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    }
#endif
```

#### Task 3.1.3: Implement priority dequeue and coalescing

```c
/* Add to src/core/queue.c */

/*
 * Pop highest priority message - O(1) using priority free-lists
 *
 * Strategy: Check priority lists from CRITICAL down to LOW.
 * Return head of first non-empty list (FIFO within priority).
 *
 * PERFORMANCE: On 68k with no cache, O(n) scans are unacceptable.
 * With 32 slots at 270 bytes each, scanning = 8.6KB memory reads per pop.
 * At ~2MB/s memory bandwidth, that's ~4ms per pop - unacceptable for games.
 * Priority free-lists make this O(1) regardless of queue size.
 *
 * NOTE: This function properly maintains priority lists and coalesce hash.
 * Always use this instead of peek/consume patterns.
 */
int pt_queue_pop_priority(pt_queue *q, void *data, uint16_t *len) {
    pt_queue_ext *ext;
    pt_queue_slot *slot;
    uint16_t slot_idx;
    int prio;

    if (!q || q->count == 0)
        return -1;

    ext = &q->ext;  /* Extended queue data */

    /* Find highest non-empty priority level */
    for (prio = PT_PRIO_CRITICAL; prio >= PT_PRIO_LOW; prio--) {
        if (ext->prio_head[prio] != PT_SLOT_NONE)
            break;
    }

    if (prio < PT_PRIO_LOW)
        return -1;  /* All lists empty (shouldn't happen if count > 0) */

    /* Pop from head of this priority's list */
    slot_idx = ext->prio_head[prio];
    slot = &q->slots[slot_idx];

    /* Update list head (next_slot is IN the slot for traversal locality) */
    ext->prio_head[prio] = slot->next_slot;
    if (ext->prio_head[prio] == PT_SLOT_NONE)
        ext->prio_tail[prio] = PT_SLOT_NONE;  /* List now empty */
    ext->prio_count[prio]--;

    /* Remove from coalesce hash if present */
    if (slot->coalesce_key != PT_COALESCE_NONE) {
        uint16_t hash_idx = PT_COALESCE_HASH(slot->coalesce_key);
        if (ext->coalesce_hash[hash_idx] == slot_idx)
            ext->coalesce_hash[hash_idx] = PT_SLOT_NONE;
    }

    /* Copy data out */
    if (data && len) {
        pt_memcpy(data, slot->data, slot->length);
        *len = slot->length;
    }

    /* Clear slot */
    slot->flags = 0;
    slot->length = 0;
    slot->next_slot = PT_SLOT_NONE;
    q->count--;

    if (q->count == 0)
        q->has_data = 0;

    return 0;
}

/*
 * Direct pop - returns pointer to slot data without copying (ZERO-COPY)
 *
 * PERFORMANCE: Avoids double-copy in batch send path. Instead of:
 *   1. pop_priority copies slot->data to temp buffer
 *   2. batch_add copies temp buffer to batch->buffer
 * We now have:
 *   1. pop_priority_direct returns pointer to slot->data
 *   2. batch_add copies directly from slot->data to batch->buffer
 *
 * USAGE:
 *   const void *data;
 *   uint16_t len;
 *   if (pt_queue_pop_priority_direct(q, &data, &len) == 0) {
 *       // Use data[0..len-1] - MUST call commit before next pop
 *       pt_batch_add(batch, data, len);
 *       pt_queue_pop_priority_commit(q);
 *   }
 *
 * WARNING: The returned pointer is only valid until pt_queue_pop_priority_commit()
 * is called. Do NOT store the pointer for later use.
 */
int pt_queue_pop_priority_direct(pt_queue *q, const void **data_out,
                                  uint16_t *len_out) {
    pt_queue_ext *ext;
    pt_queue_slot *slot;
    uint16_t slot_idx;
    int prio;

    if (!q || q->count == 0 || !data_out || !len_out)
        return -1;

    ext = &q->ext;

    /* Find highest non-empty priority level */
    for (prio = PT_PRIO_CRITICAL; prio >= PT_PRIO_LOW; prio--) {
        if (ext->prio_head[prio] != PT_SLOT_NONE)
            break;
    }

    if (prio < PT_PRIO_LOW)
        return -1;

    /* Return pointer to head slot's data */
    slot_idx = ext->prio_head[prio];
    slot = &q->slots[slot_idx];

    *data_out = slot->data;
    *len_out = slot->length;

    /* Store pending pop info for commit (use reserved field or add to ext) */
    q->pending_pop_prio = (uint8_t)prio;
    q->pending_pop_slot = slot_idx;

    return 0;
}

/*
 * Commit a direct pop - actually removes the slot from queue
 *
 * Call this AFTER processing the data returned by pt_queue_pop_priority_direct().
 * This completes the pop operation by updating the priority list and coalesce hash.
 */
void pt_queue_pop_priority_commit(pt_queue *q) {
    pt_queue_ext *ext;
    pt_queue_slot *slot;
    uint16_t slot_idx;
    int prio;

    if (!q)
        return;

    ext = &q->ext;
    prio = q->pending_pop_prio;
    slot_idx = q->pending_pop_slot;
    slot = &q->slots[slot_idx];

    /* Update list head */
    ext->prio_head[prio] = slot->next_slot;
    if (ext->prio_head[prio] == PT_SLOT_NONE)
        ext->prio_tail[prio] = PT_SLOT_NONE;
    ext->prio_count[prio]--;

    /* Remove from coalesce hash if present */
    if (slot->coalesce_key != PT_COALESCE_NONE) {
        uint16_t hash_idx = PT_COALESCE_HASH(slot->coalesce_key);
        if (ext->coalesce_hash[hash_idx] == slot_idx)
            ext->coalesce_hash[hash_idx] = PT_SLOT_NONE;
    }

    /* Clear slot */
    slot->flags = 0;
    slot->length = 0;
    slot->next_slot = PT_SLOT_NONE;
    q->count--;

    if (q->count == 0)
        q->has_data = 0;
}

/*
 * Push with coalescing - O(1) using hash table lookup
 *
 * If a message with the same coalesce_key exists, replace it
 * rather than adding a new slot.
 *
 * PERFORMANCE: On 68k, O(n) key scan is unacceptable.
 * Position updates at 10Hz = 80KB/sec just for scanning.
 * Hash table makes coalesce lookup O(1) average case.
 *
 * LOGGING: Caller should log coalesce events and hash collisions:
 *   PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_PROTOCOL, "Coalesce hit: key=0x%04X", key);
 *   PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_PROTOCOL, "Hash collision at bucket %u", hash_idx);
 */
int pt_queue_push_coalesce(pt_queue *q, const void *data, uint16_t len,
                            uint8_t priority, pt_coalesce_key key) {
    pt_queue_ext *ext;
    pt_queue_slot *slot;
    uint16_t slot_idx;
    uint16_t hash_idx;

    if (!q || !data || len == 0 || len > PT_QUEUE_SLOT_SIZE)
        return -1;

    ext = &q->ext;

    /* Check hash table for existing message with same key - O(1) */
    if (key != PT_COALESCE_NONE) {
        hash_idx = PT_COALESCE_HASH(key);
        slot_idx = ext->coalesce_hash[hash_idx];

        if (slot_idx != PT_SLOT_NONE) {
            slot = &q->slots[slot_idx];
            /* Verify it's actually our key (hash collision check) */
            if ((slot->flags & PT_SLOT_USED) && slot->coalesce_key == key) {
                /* Found - replace data in place */
                /* LOGGING: Caller can log "Coalesce hit: key=0x%04X" at DEBUG level */
                pt_memcpy(slot->data, data, len);
                slot->length = len;
                /* Note: priority and list position don't change on coalesce */
                slot->timestamp = pt_get_ticks();  /* Update timestamp */
                return 0;  /* Coalesced */
            }
            /* Hash collision - different key at same bucket */
            /* LOGGING: Caller can log "Hash collision at bucket %u" at DEBUG level */
        }
    }

    /* No existing message - allocate new slot */
    if (q->count >= q->capacity)
        return -1;  /* Full */

    /* Find free slot (use write_idx as starting point) */
    slot_idx = q->write_idx;
    slot = &q->slots[slot_idx];

    /* Fill slot */
    pt_memcpy(slot->data, data, len);
    slot->length = len;
    slot->priority = priority;
    slot->flags = PT_SLOT_USED;
    slot->coalesce_key = key;
    slot->timestamp = pt_get_ticks();

    /* Add to priority list (append to tail for FIFO within priority)
     * NOTE: next_slot is IN the slot (not ext) for traversal locality */
    slot->next_slot = PT_SLOT_NONE;
    if (ext->prio_tail[priority] == PT_SLOT_NONE) {
        /* List was empty */
        ext->prio_head[priority] = slot_idx;
    } else {
        /* Append to tail - update previous tail's next_slot */
        q->slots[ext->prio_tail[priority]].next_slot = slot_idx;
    }
    ext->prio_tail[priority] = slot_idx;
    ext->prio_count[priority]++;

    /* Add to coalesce hash table */
    if (key != PT_COALESCE_NONE) {
        hash_idx = PT_COALESCE_HASH(key);
        ext->coalesce_hash[hash_idx] = slot_idx;
    }

    q->write_idx = (q->write_idx + 1) & q->capacity_mask;
    q->count++;
    q->has_data = 1;

    return 0;
}

/*
 * ISR-safe push with coalescing (for MacTCP ASR) - O(1) hash lookup
 *
 * CRITICAL ISR-SAFETY RULES:
 * - Uses pt_memcpy_isr(), NOT pt_memcpy() (which calls BlockMoveData)
 * - Does NOT call pt_get_ticks() or TickCount() (not in Table B-3)
 * - Does NOT call PT_Log (not interrupt-safe, uses File Manager)
 * - Does NOT allocate memory
 * See CLAUDE.md ASR rules.
 *
 * LOGGING: Do NOT call PT_Log from this function! Instead, set isr_flags
 * and log from the main event loop. The flags are in pt_queue (not pt_context)
 * because this function only has access to the queue pointer.
 *   ISR: q->isr_flags.queue_full = 1;
 *   Main: if (q->isr_flags.queue_full) { PT_LOG_WARN(ctx->log, ...); q->isr_flags.queue_full = 0; }
 *
 * Note: Uses same O(1) hash table as regular push - no scanning.
 */
int pt_queue_push_coalesce_isr(pt_queue *q, const void *data, uint16_t len,
                                uint8_t priority, pt_coalesce_key key) {
    pt_queue_ext *ext;
    pt_queue_slot *slot;
    uint16_t slot_idx;
    uint16_t hash_idx;

    if (!q || !data || len == 0 || len > PT_QUEUE_SLOT_SIZE)
        return -1;

    ext = &q->ext;

    /* Check hash table for existing message with same key - O(1) */
    if (key != PT_COALESCE_NONE) {
        hash_idx = PT_COALESCE_HASH(key);
        slot_idx = ext->coalesce_hash[hash_idx];

        if (slot_idx != PT_SLOT_NONE) {
            slot = &q->slots[slot_idx];
            if ((slot->flags & PT_SLOT_USED) && slot->coalesce_key == key) {
                /* Found - replace using ISR-safe copy */
                pt_memcpy_isr(slot->data, data, len);
                slot->length = len;
                /* Don't update timestamp in ISR - no TickCount() call */
                q->isr_flags.coalesce_hit = 1;  /* Signal main loop for logging */
                return 0;  /* Coalesced */
            }
            /* Hash collision - set flag for logging */
            q->isr_flags.hash_collision = 1;
        }
    }

    /* No existing message - allocate new slot */
    if (q->count >= q->capacity) {
        q->isr_flags.queue_full = 1;  /* Signal main loop for logging */
        return -1;
    }

    slot_idx = q->write_idx;
    slot = &q->slots[slot_idx];

    /* Fill slot using ISR-safe copy */
    pt_memcpy_isr(slot->data, data, len);
    slot->length = len;
    slot->priority = priority;
    slot->flags = PT_SLOT_USED;
    slot->coalesce_key = key;
    slot->timestamp = 0;  /* No TickCount() in ISR */

    /* Add to priority list (next_slot is IN the slot for traversal locality) */
    slot->next_slot = PT_SLOT_NONE;
    if (ext->prio_tail[priority] == PT_SLOT_NONE) {
        ext->prio_head[priority] = slot_idx;
    } else {
        q->slots[ext->prio_tail[priority]].next_slot = slot_idx;
    }
    ext->prio_tail[priority] = slot_idx;
    ext->prio_count[priority]++;

    /* Add to coalesce hash table */
    if (key != PT_COALESCE_NONE) {
        hash_idx = PT_COALESCE_HASH(key);
        ext->coalesce_hash[hash_idx] = slot_idx;
    }

    q->write_idx = (q->write_idx + 1) & q->capacity_mask;
    q->count++;
    q->has_data = 1;

    return 0;
}
```

#### Task 3.1.4: Create `tests/test_queue_advanced.c`

**API Compatibility Note:** These tests use Phase 3's new functions:
- `pt_queue_ext_init()` - initializes priority free-lists and coalesce hash
- `pt_queue_push_coalesce()` - pushes with priority and optional coalescing (O(1))
- `pt_queue_pop_priority()` - pops highest priority first (O(1))

Phase 2's basic `pt_queue_push()` (3 args) remains available for simple use cases without priority/coalescing. Phase 3 adds `coalesce_key`, `timestamp`, and the `ext` field to the queue, requiring a rebuild of all code using this structure.

```c
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "queue.h"

void test_priority_order(void) {
    pt_queue q;
    char buf[256];
    uint16_t len;

    pt_queue_init(&q, 16);
    pt_queue_ext_init(&q);  /* Initialize O(1) data structures */

    /* Push in random priority order - use pt_queue_push_coalesce with PT_COALESCE_NONE */
    pt_queue_push_coalesce(&q, "low", 4, PT_PRIO_LOW, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "high", 5, PT_PRIO_HIGH, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "normal", 7, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "critical", 9, PT_PRIO_CRITICAL, PT_COALESCE_NONE);

    /* Pop should return highest priority first (O(1) operation) */
    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "critical") == 0);

    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "high") == 0);

    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "normal") == 0);

    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "low") == 0);

    pt_queue_free(&q);
    printf("test_priority_order: PASSED\n");
}

void test_priority_fifo_within_level(void) {
    pt_queue q;
    char buf[256];
    uint16_t len;

    pt_queue_init(&q, 16);
    pt_queue_ext_init(&q);

    /* Push multiple messages at same priority - should be FIFO within level */
    pt_queue_push_coalesce(&q, "first", 6, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "second", 7, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "third", 6, PT_PRIO_NORMAL, PT_COALESCE_NONE);

    /* Pop should return FIFO order within same priority */
    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "first") == 0);

    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "second") == 0);

    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "third") == 0);

    pt_queue_free(&q);
    printf("test_priority_fifo_within_level: PASSED\n");
}

void test_coalescing(void) {
    pt_queue q;
    char buf[256];
    uint16_t len;

    pt_queue_init(&q, 16);
    pt_queue_ext_init(&q);

    /* Push multiple position updates with same key - O(1) hash lookup */
    pt_queue_push_coalesce(&q, "pos:1,1", 8, PT_PRIO_NORMAL, PT_COALESCE_POSITION);
    pt_queue_push_coalesce(&q, "pos:2,2", 8, PT_PRIO_NORMAL, PT_COALESCE_POSITION);
    pt_queue_push_coalesce(&q, "pos:3,3", 8, PT_PRIO_NORMAL, PT_COALESCE_POSITION);

    /* Should only have 1 message (coalesced in O(1) time) */
    assert(pt_queue_count(&q) == 1);

    /* Should get latest */
    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "pos:3,3") == 0);

    pt_queue_free(&q);
    printf("test_coalescing: PASSED\n");
}

void test_mixed_coalesce_and_normal(void) {
    pt_queue q;

    pt_queue_init(&q, 16);
    pt_queue_ext_init(&q);

    /* Mix of coalesced and normal messages */
    pt_queue_push_coalesce(&q, "pos:1", 6, PT_PRIO_NORMAL, PT_COALESCE_POSITION);
    pt_queue_push_coalesce(&q, "chat:hi", 8, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "pos:2", 6, PT_PRIO_NORMAL, PT_COALESCE_POSITION);
    pt_queue_push_coalesce(&q, "chat:bye", 9, PT_PRIO_NORMAL, PT_COALESCE_NONE);

    /* Should have 3 messages (pos coalesced to 1, plus 2 chats) */
    assert(pt_queue_count(&q) == 3);

    pt_queue_free(&q);
    printf("test_mixed_coalesce_and_normal: PASSED\n");
}

void test_coalesce_hash_collision(void) {
    pt_queue q;
    char buf[256];
    uint16_t len;
    uint16_t key_a, key_b;

    pt_queue_init(&q, 16);
    pt_queue_ext_init(&q);

    /* Create two keys that hash to same bucket using PT_COALESCE_HASH
     * PT_COALESCE_HASH(key) = ((key) ^ ((key) >> 8)) & 0x1F
     * key_a = 0x0101: hash = (0x0101 ^ 0x01) & 0x1F = 0x0100 & 0x1F = 0
     * key_b = 0x0001: hash = (0x0001 ^ 0x00) & 0x1F = 0x0001 & 0x1F = 1
     * These DON'T collide now. Let's find keys that actually collide:
     * key_a = 0x0001: hash = (0x0001 ^ 0x00) & 0x1F = 1
     * key_b = 0x0100: hash = (0x0100 ^ 0x01) & 0x1F = 0x0101 & 0x1F = 1 (collision!)
     */
    key_a = 0x0001;  /* Hash: (0x0001 ^ 0x00) & 0x1F = 1 */
    key_b = 0x0100;  /* Hash: (0x0100 ^ 0x01) & 0x1F = 1 (collision) */

    pt_queue_push_coalesce(&q, "key_a_v1", 9, PT_PRIO_NORMAL, key_a);
    pt_queue_push_coalesce(&q, "key_b_v1", 9, PT_PRIO_NORMAL, key_b);

    /* Should have 2 messages - collision handled correctly */
    assert(pt_queue_count(&q) == 2);

    /* Update key_a - should coalesce despite collision */
    pt_queue_push_coalesce(&q, "key_a_v2", 9, PT_PRIO_NORMAL, key_a);
    assert(pt_queue_count(&q) == 2);  /* Still 2 */

    pt_queue_free(&q);
    printf("test_coalesce_hash_collision: PASSED\n");
}

void test_direct_pop(void) {
    pt_queue q;
    const void *data;
    uint16_t len;

    pt_queue_init(&q, 16);
    pt_queue_ext_init(&q);

    pt_queue_push_coalesce(&q, "hello", 5, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "world", 5, PT_PRIO_HIGH, PT_COALESCE_NONE);

    /* Direct pop returns pointer to HIGH priority first */
    assert(pt_queue_pop_priority_direct(&q, &data, &len) == 0);
    assert(len == 5);
    assert(memcmp(data, "world", 5) == 0);

    /* Not committed yet - count unchanged */
    assert(pt_queue_count(&q) == 2);

    /* Commit removes from queue */
    pt_queue_pop_priority_commit(&q);
    assert(pt_queue_count(&q) == 1);

    pt_queue_free(&q);
    printf("test_direct_pop: PASSED\n");
}

int main(void) {
    test_priority_order();
    test_priority_fifo_within_level();
    test_coalescing();
    test_mixed_coalesce_and_normal();
    test_coalesce_hash_collision();
    test_direct_pop();
    printf("\nAll advanced queue tests PASSED!\n");
    return 0;
}
```

### Acceptance Criteria
1. Priority pop returns highest priority first (O(1) via free-lists)
2. Same priority uses FIFO order (linked list within priority level)
3. Coalescing replaces existing message with same key (O(1) via hash table)
4. Queue count reflects coalescing
5. Different coalesce keys don't interfere
6. Hash collisions are handled correctly (verify key match after hash lookup)
7. Per-peer coalesce keys work (PT_COALESCE_KEY macro, peer_id < 256)
8. pt_get_ticks() returns monotonic value on all platforms
9. ISR-safe coalesce variant (pt_queue_push_coalesce_isr) works
10. pt_queue_ext_init() properly initializes free-lists and hash table
11. **ISR-safety:** pt_queue_push_coalesce_isr does NOT call PT_Log, TickCount, or pt_memcpy
12. **Logging:** Coalesce operations are loggable at DEBUG level (caller logs, not queue code)
13. **next_slot is in pt_queue_slot:** Traversal locality is maintained
14. **Direct pop (zero-copy):** pt_queue_pop_priority_direct() returns pointer to slot data
15. **Size assertion:** pt_queue_slot size verified at init (268 bytes expected)
16. **Hash table sized correctly:** PT_COALESCE_HASH_SIZE matches PT_QUEUE_MAX_SLOTS (32)
17. **Hash function mixes peer_id:** PT_COALESCE_HASH uses XOR-fold to distribute per-peer keys
18. **Capacity validation:** pt_queue_ext_init() fails if capacity > PT_QUEUE_MAX_SLOTS
19. **ISR flag checker:** pt_check_queue_isr_flags() defined and logs ISR events from main loop

---

## Session 3.2: Backpressure & Batch

### Objective
Implement backpressure signaling and batch send operations.

### Tasks

#### Task 3.2.1: Add backpressure signaling

```c
/* Add to src/core/queue.h */

/* Pressure thresholds */
#define PT_PRESSURE_LOW      25  /* Below this: safe to send freely */
#define PT_PRESSURE_MEDIUM   50  /* Above this: reduce send rate */
#define PT_PRESSURE_HIGH     75  /* Above this: only critical messages */
#define PT_PRESSURE_CRITICAL 90  /* Above this: drop non-critical */

typedef enum {
    PT_BACKPRESSURE_NONE = 0,
    PT_BACKPRESSURE_LIGHT,      /* Recommend reducing rate */
    PT_BACKPRESSURE_HEAVY,      /* Only high priority */
    PT_BACKPRESSURE_BLOCKING    /* Queue full */
} pt_backpressure;

/* Get backpressure status */
pt_backpressure pt_queue_backpressure(pt_queue *q);

/* Try push with backpressure check */
int pt_queue_try_push(pt_queue *q, const void *data, uint16_t len,
                      uint8_t priority, pt_coalesce_key key,
                      pt_backpressure *pressure_out);

/*
 * NOTE: pt_queue_peek() and pt_queue_consume() were REMOVED in the 2026-01-28
 * review because they bypass the priority free-lists and coalesce hash,
 * causing data structure corruption. Use pt_queue_pop_priority() instead.
 */
```

#### Task 3.2.2: Implement backpressure functions

```c
/* Add to src/core/queue.c */

/*
 * Get current backpressure level.
 *
 * LOGGING: Caller should log transitions between pressure levels:
 *   static pt_backpressure last_bp = PT_BACKPRESSURE_NONE;
 *   pt_backpressure bp = pt_queue_backpressure(q);
 *   if (bp != last_bp) {
 *       PT_CTX_WARN(ctx, PT_LOG_CAT_PERF, "Queue pressure: %s (%u%% full)",
 *               pressure_names[bp], pt_queue_pressure(q));
 *       last_bp = bp;
 *   }
 *
 * Note: Uses PT_LOG_CAT_PERF (not PT_LOG_CAT_PROTOCOL) to allow filtering
 * performance warnings without disabling protocol error logging.
 */
pt_backpressure pt_queue_backpressure(pt_queue *q) {
    uint8_t pressure;

    if (!q)
        return PT_BACKPRESSURE_BLOCKING;

    pressure = pt_queue_pressure(q);

    if (pressure >= PT_PRESSURE_CRITICAL)
        return PT_BACKPRESSURE_BLOCKING;
    if (pressure >= PT_PRESSURE_HIGH)
        return PT_BACKPRESSURE_HEAVY;
    if (pressure >= PT_PRESSURE_MEDIUM)
        return PT_BACKPRESSURE_LIGHT;

    return PT_BACKPRESSURE_NONE;
}

/*
 * Try to push with backpressure awareness.
 *
 * LOGGING: Caller should log when messages are dropped due to backpressure:
 *   if (result < 0 && bp >= PT_BACKPRESSURE_HEAVY) {
 *       PT_LOG_WARN(ctx->log, PT_LOG_CAT_PROTOCOL, "Message dropped: backpressure %s, prio=%u",
 *               pressure_names[bp], priority);
 *   }
 */
int pt_queue_try_push(pt_queue *q, const void *data, uint16_t len,
                      uint8_t priority, pt_coalesce_key key,
                      pt_backpressure *pressure_out) {
    pt_backpressure bp;
    int result;

    bp = pt_queue_backpressure(q);

    if (pressure_out)
        *pressure_out = bp;

    /* Apply backpressure policy */
    switch (bp) {
    case PT_BACKPRESSURE_BLOCKING:
        if (priority < PT_PRIO_CRITICAL)
            return -1;  /* Drop - caller should log at WARN level */
        break;

    case PT_BACKPRESSURE_HEAVY:
        if (priority < PT_PRIO_HIGH)
            return -1;  /* Drop - caller should log at WARN level */
        break;

    case PT_BACKPRESSURE_LIGHT:
        /* Allow but signal caller to slow down */
        break;

    case PT_BACKPRESSURE_NONE:
        /* All clear */
        break;
    }

    result = pt_queue_push_coalesce(q, data, len, priority, key);

    /* Update pressure after push */
    if (pressure_out)
        *pressure_out = pt_queue_backpressure(q);

    return result;
}
```

#### Task 3.2.3: Implement batch send

**Prerequisite:** Add `PT_MSG_FLAG_BATCH` to `src/core/protocol.h`:
```c
/* Add to protocol.h message flags */
#define PT_MSG_FLAG_BATCH       0x08  /* Contains multiple sub-messages */
```

```c
/* Add to src/core/send.c */

/*
 * Batch send - combine multiple small messages into one TCP packet
 *
 * This improves efficiency by reducing TCP/IP overhead for
 * many small messages common in games.
 *
 * NOTE: The actual pt_batch_send() function requires a platform-specific
 * send function. This is provided by Phases 3/4/5:
 *   - POSIX: pt_posix_send()
 *   - MacTCP: pt_mactcp_send()
 *   - Open Transport: pt_ot_send()
 *
 * The batch building (pt_batch_init, pt_batch_add) is platform-independent.
 */

/* Batch size: 1400 = 1500 (Ethernet MTU) - 100 (TCP/IP headers margin)
 * This is conservative but ensures batches fit in a single frame. */
#define PT_BATCH_MAX_SIZE   1400
#define PT_BATCH_HEADER     4     /* Length prefix for each message */

/* Batch buffer - pre-allocated in pt_context, NOT on stack
 *
 * MEMORY: Classic Mac apps often have 32KB stacks. Allocating 1.4KB
 * per call is significant and risks stack overflow with nested calls.
 * Pre-allocating in pt_context avoids this.
 *
 * CACHE EFFICIENCY: Metadata (used, count) placed BEFORE buffer so that
 * pt_batch_add() accesses metadata at offset 0, not at offset 1400.
 * On 68030 with 256-byte cache, this avoids guaranteed cache misses
 * when alternating between metadata updates and buffer writes.
 */
typedef struct {
    uint16_t used;                       /* HOT: accessed every pt_batch_add() */
    uint16_t count;                      /* HOT: accessed every pt_batch_add() */
    uint8_t  buffer[PT_BATCH_MAX_SIZE];  /* COLD: sequential writes only */
} pt_batch;

/* Add to struct pt_context (in pt_internal.h):
 *     pt_batch send_batch;  // Pre-allocated batch buffer for pt_drain_send_queue
 */

void pt_batch_init(pt_batch *batch) {
    batch->used = 0;
    batch->count = 0;
}

int pt_batch_add(pt_batch *batch, const void *data, uint16_t len) {
    if (batch->used + PT_BATCH_HEADER + len > PT_BATCH_MAX_SIZE)
        return -1;  /* Batch full */

    /* Add length prefix (big-endian) */
    batch->buffer[batch->used++] = (len >> 8) & 0xFF;
    batch->buffer[batch->used++] = len & 0xFF;

    /* Reserved bytes for flags/type */
    batch->buffer[batch->used++] = 0;
    batch->buffer[batch->used++] = 0;

    /* Add data */
    pt_memcpy(batch->buffer + batch->used, data, len);
    batch->used += len;
    batch->count++;

    return 0;
}

/*
 * Send a batch as a single framed message
 *
 * This function builds the complete framed message but delegates
 * the actual send to the platform layer. The caller (poll loop)
 * should use the appropriate platform send function.
 *
 * Returns: bytes to send (stored in batch->buffer with header prepended),
 *          or 0 if batch is empty
 */
int pt_batch_prepare(struct pt_peer *peer, pt_batch *batch) {
    pt_message_header hdr;
    uint8_t header_buf[PT_MESSAGE_HEADER_SIZE];

    if (batch->count == 0)
        return 0;

    /* Build header */
    hdr.version = PT_PROTOCOL_VERSION;
    hdr.type = PT_MSG_DATA;
    hdr.flags = PT_MSG_FLAG_BATCH;
    hdr.sequence = peer->send_seq++;
    hdr.payload_len = batch->used;

    pt_message_encode_header(&hdr, header_buf);

    /*
     * The caller should send:
     *   1. header_buf (PT_MESSAGE_HEADER_SIZE bytes)
     *   2. batch->buffer (batch->used bytes)
     *   3. CRC-16 of (header + payload)
     *
     * Or use the platform's send_framed() function if available.
     */
    return batch->used;
}

/*
 * NOTE: pt_queue_peek() and pt_queue_consume() were REMOVED.
 *
 * These functions bypassed the priority free-lists and coalesce hash,
 * causing data structure corruption. The batch send code now uses
 * pt_queue_pop_priority() which properly maintains all data structures.
 *
 * If you need peek-like functionality, pop into a local buffer instead:
 *   uint8_t buf[PT_QUEUE_SLOT_SIZE];
 *   uint16_t len;
 *   if (pt_queue_pop_priority(q, buf, &len) == 0) {
 *       // Process buf[0..len-1]
 *   }
 */

/*
 * Platform-specific batch send callback type.
 *
 * This callback is provided by the platform layer (Phases 4/5/6):
 *   - POSIX: pt_posix_batch_send()
 *   - MacTCP: pt_mactcp_batch_send()
 *   - Open Transport: pt_ot_batch_send()
 *
 * The callback receives:
 *   - ctx: PeerTalk context
 *   - peer: Target peer
 *   - batch: Prepared batch (use pt_batch_prepare to get header)
 *
 * Returns: 0 on success, -1 on error
 */
typedef int (*pt_batch_send_fn)(struct pt_context *ctx,
                                 struct pt_peer *peer,
                                 pt_batch *batch);

/*
 * Drain send queue in batches
 *
 * Called from poll loop - combines queued messages into batches.
 * The send_fn callback is platform-specific and provided by the
 * networking layer (Phase 4 POSIX, Phase 5 MacTCP, Phase 6 OT).
 *
 * Uses pre-allocated batch buffer from ctx->send_batch to avoid
 * 1.4KB stack allocation per call.
 *
 * IMPORTANT: Uses pt_queue_pop_priority() which properly maintains
 * priority free-lists and coalesce hash. Messages are batched in
 * priority order (CRITICAL first, then HIGH, NORMAL, LOW).
 *
 * LOGGING: Logs INFO on successful batch send, ERR on failure, WARN
 * for oversized messages.
 */
int pt_drain_send_queue(struct pt_context *ctx, struct pt_peer *peer,
                        pt_batch_send_fn send_fn) {
    pt_batch *batch;
    pt_queue *q = peer->send_queue;
    const void *msg_data;  /* Zero-copy pointer to slot data */
    uint16_t len;
    int sent = 0;

    if (!ctx || !q || pt_queue_is_empty(q) || !send_fn)
        return 0;

    /* Use pre-allocated batch buffer from context */
    batch = &ctx->send_batch;
    pt_batch_init(batch);

    /* Pop messages in priority order using ZERO-COPY direct pop.
     * This avoids double-copy: instead of slot->temp->batch, we do slot->batch.
     * On 68k with 2-10 MB/s memory bandwidth, this saves significant time.
     *
     * PROTOCOL: Only commit after successful batch_add to avoid message loss. */
    while (pt_queue_pop_priority_direct(q, &msg_data, &len) == 0) {
        if (pt_batch_add(batch, msg_data, len) < 0) {
            /* Batch full - DON'T commit yet, send current batch first */
            if (send_fn(ctx, peer, batch) == 0) {
                sent++;
                PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_PROTOCOL,
                    "Batch sent: %u messages, %u bytes", batch->count, batch->used);
            } else {
                PT_LOG_ERR(ctx->log, PT_LOG_CAT_PROTOCOL, "Batch send failed");
            }
            pt_batch_init(batch);

            /* Now try adding the message to the fresh batch */
            if (pt_batch_add(batch, msg_data, len) < 0) {
                /* Message too large even for empty batch - should not happen
                 * if PT_BATCH_MAX_SIZE > PT_QUEUE_SLOT_SIZE */
                PT_LOG_WARN(ctx->log, PT_LOG_CAT_PROTOCOL,
                    "Message too large for batch (%u bytes), dropped", len);
                /* Commit the pop anyway - message is lost (config error) */
                pt_queue_pop_priority_commit(q);
                continue;
            }
        }
        /* Message added to batch - now commit the pop */
        pt_queue_pop_priority_commit(q);
    }

    /* Send remaining batch */
    if (batch->count > 0) {
        if (send_fn(ctx, peer, batch) == 0) {
            sent++;
            PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_PROTOCOL,
                "Batch sent: %u messages, %u bytes", batch->count, batch->used);
        } else {
            PT_LOG_ERR(ctx->log, PT_LOG_CAT_PROTOCOL, "Batch send failed");
        }
    }

    return sent;
}
```

#### Task 3.2.4: Create `tests/test_backpressure.c`

```c
#include <stdio.h>
#include <assert.h>
#include "queue.h"

void test_backpressure_levels(void) {
    pt_queue q;
    int i;

    /* Use power-of-two capacity (Phase 2 requirement) */
    pt_queue_init(&q, 128);
    pt_queue_ext_init(&q);  /* Initialize O(1) data structures */

    /* Empty - no pressure */
    assert(pt_queue_backpressure(&q) == PT_BACKPRESSURE_NONE);

    /* Fill to 25% (32/128) */
    for (i = 0; i < 32; i++) {
        pt_queue_push_coalesce(&q, "x", 1, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    }
    assert(pt_queue_backpressure(&q) == PT_BACKPRESSURE_NONE);

    /* Fill to 50% (64/128) */
    for (i = 0; i < 32; i++) {
        pt_queue_push_coalesce(&q, "x", 1, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    }
    assert(pt_queue_backpressure(&q) == PT_BACKPRESSURE_LIGHT);

    /* Fill to 75% (96/128) */
    for (i = 0; i < 32; i++) {
        pt_queue_push_coalesce(&q, "x", 1, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    }
    assert(pt_queue_backpressure(&q) == PT_BACKPRESSURE_HEAVY);

    /* Fill to >90% (116/128 = 90.6%) to trigger BLOCKING */
    for (i = 0; i < 20; i++) {
        pt_queue_push_coalesce(&q, "x", 1, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    }
    assert(pt_queue_backpressure(&q) == PT_BACKPRESSURE_BLOCKING);

    pt_queue_free(&q);
    printf("test_backpressure_levels: PASSED\n");
}

void test_try_push_policy(void) {
    pt_queue q;
    int i;
    pt_backpressure bp;

    /* Use power-of-two capacity (Phase 2 requirement) */
    pt_queue_init(&q, 128);
    pt_queue_ext_init(&q);  /* Initialize O(1) data structures */

    /* Fill to blocking (>90% = 116/128) */
    for (i = 0; i < 116; i++) {
        pt_queue_push_coalesce(&q, "x", 1, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    }

    /* Low priority should fail */
    assert(pt_queue_try_push(&q, "y", 1, PT_PRIO_LOW, 0, &bp) != 0);
    assert(bp == PT_BACKPRESSURE_BLOCKING);

    /* Critical priority should succeed */
    assert(pt_queue_try_push(&q, "z", 1, PT_PRIO_CRITICAL, 0, &bp) == 0);

    pt_queue_free(&q);
    printf("test_try_push_policy: PASSED\n");
}

void test_batch_send(void) {
    pt_batch batch;

    pt_batch_init(&batch);

    assert(pt_batch_add(&batch, "hello", 5) == 0);
    assert(pt_batch_add(&batch, "world", 5) == 0);

    assert(batch.count == 2);
    assert(batch.used == 2 * (4 + 5));  /* 2 * (header + data) */

    printf("test_batch_send: PASSED\n");
}

int main(void) {
    test_backpressure_levels();
    test_try_push_policy();
    test_batch_send();
    printf("\nAll backpressure tests PASSED!\n");
    return 0;
}
```

### Acceptance Criteria
1. Backpressure correctly signals queue fill level
2. try_push respects backpressure policy
3. High priority messages bypass light pressure
4. Critical messages bypass heavy pressure
5. Batch adds multiple messages correctly
6. Batch stays under MTU size
7. **pt_drain_send_queue uses pt_queue_pop_priority:** Priority order is maintained during batch send
8. **No peek/consume:** pt_queue_peek and pt_queue_consume are NOT implemented (removed)
9. **Logging uses correct macros:** PT_LOG_WARN, PT_LOG_DEBUG, PT_LOG_ERR (not PT_WARN, etc.)
10. **ctx->log required:** All functions that log require ctx->log to be initialized

---

> **Note:** Session 3.3 (Public API Integration) has been moved to **Phase 3.5** (`PHASE-3.5-SENDEX-API.md`) to resolve the Phase 4 dependency. The `PeerTalk_SendEx()` API requires the `send_udp` callback which is implemented in Phase 4.

---

## Phase 3 Complete Checklist

### Session 3.1: Priority & Coalescing
- [ ] pt_queue_slot struct has correct field ordering (metadata first, data last)
- [ ] pt_queue_slot includes next_slot field (for traversal locality)
- [ ] pt_queue_slot size verified (268 bytes expected - _Static_assert or runtime check)
- [ ] pt_queue_ext struct defined with priority free-lists and coalesce hash (no slot_next)
- [ ] PT_QUEUE_MAX_SLOTS defined (32)
- [ ] PT_COALESCE_HASH_SIZE matches PT_QUEUE_MAX_SLOTS (32, not 64)
- [ ] PT_COALESCE_HASH macro uses XOR-fold for per-peer key distribution
- [ ] pt_queue_ext_init() initializes free-lists, slot next_slot pointers, hash table, and pending_pop state
- [ ] pt_queue_ext_init() includes runtime size check for C89/C99
- [ ] pt_queue_ext_init() validates capacity ≤ PT_QUEUE_MAX_SLOTS
- [ ] pt_get_ticks() implemented in pt_compat.c (declaration in pt_compat.h)
- [ ] Priority dequeue is O(1) via free-lists (pt_queue_pop_priority)
- [ ] Direct pop implemented for zero-copy (pt_queue_pop_priority_direct, _commit)
- [ ] Same priority uses FIFO via linked list
- [ ] Coalescing is O(1) via hash table (pt_queue_push_coalesce)
- [ ] Hash collision handling works (verify key after hash lookup)
- [ ] Per-peer coalesce keys work (PT_COALESCE_KEY macro, peer_id < 256)
- [ ] ISR-safe coalesce variant implemented (pt_queue_push_coalesce_isr)
- [ ] ISR-safe variant does NOT call PT_Log, TickCount(), or pt_memcpy()
- [ ] pt_check_queue_isr_flags() defined to log ISR events from main loop

### Session 3.2: Backpressure & Batch
- [ ] Backpressure levels correct (NONE/LIGHT/HEAVY/BLOCKING)
- [ ] try_push policy implemented
- [ ] PT_MSG_FLAG_BATCH added to protocol.h
- [ ] pt_batch field ordering correct (used/count BEFORE buffer for cache efficiency)
- [ ] pt_batch added to struct pt_context (pre-allocated, not stack)
- [ ] PT_Log *log added to struct pt_context (required for PT_LOG_DEBUG/WARN/ERR macros)
- [ ] Batch building works (pt_batch_init, pt_batch_add)
- [ ] Batch stays under MTU (1400 bytes = 1500 - 100 header margin)
- [ ] pt_drain_send_queue uses ctx->send_batch (no stack allocation)
- [ ] pt_drain_send_queue uses pt_queue_pop_priority_direct (zero-copy)
- [ ] pt_drain_send_queue only commits pop after successful batch_add
- [ ] pt_queue_peek and pt_queue_consume are NOT implemented (removed)
- [ ] Logging uses PT_LOG_CAT_PROTOCOL for queue/batch operations (PT_LOG_CAT_SEND is for SendEx API in Phase 3.5)
- [ ] Logging uses correct macro names: PT_LOG_WARN, PT_LOG_DEBUG, PT_LOG_ERR (not PT_WARN etc.)
- [ ] ISR flags struct added to pt_queue (not pt_context) for deferred logging
- [ ] ISR flags lifecycle documented (flags are booleans, cleared after logging)

### General
- [ ] All tests pass on POSIX (including test_direct_pop)
- [ ] Slot structure change documented as BREAKING CHANGE
- [ ] All code rebuilt after pt_queue_slot modification
- [ ] pt_queue includes pending_pop_prio and pending_pop_slot fields
- [ ] pt_queue includes isr_flags struct for deferred ISR logging
- [ ] pt_context includes PT_Log *log pointer for PT_DEBUG/WARN/ERR macros

> **Note:** Session 3.3 (Public API Integration) checklist is now in Phase 3.5.

## Common Pitfalls

1. **Don't call pt_get_ticks() from ISR/ASR** - TickCount() is **NOT documented as interrupt-safe** in Inside Macintosh Volume VI Table B-3 ("Routines That May Be Called at Interrupt Time"). The ISR variants use `timestamp = 0` instead. On Open Transport, `OTGetTimeStamp()` IS interrupt-safe per Table C-1, but MacTCP has no equivalent.

2. **Don't call PT_Log from ISR variants** - PT_Log is NOT interrupt-safe (uses File Manager, vsprintf, memory allocation). The `pt_queue_push_coalesce_isr()` function must NOT call any logging functions. Instead, set a volatile flag and log from the main event loop.

3. **Always call pt_queue_ext_init() after pt_queue_init()** - The priority free-lists and coalesce hash table must be initialized before using `pt_queue_push_coalesce()` or `pt_queue_pop_priority()`. Failure to initialize will cause crashes or undefined behavior.

4. **Coalesce keys are global by default** - If you use `PT_COALESCE_POSITION` without a peer ID, ALL position updates coalesce together. Use `PT_COALESCE_KEY(PT_COALESCE_POSITION, peer_id)` for per-peer coalescing.

5. **Coalesce hash collisions** - The hash table is 32 entries with simple masking. When checking for an existing key, ALWAYS verify the actual key matches after hash lookup. Two different keys can hash to the same bucket.

6. **Backpressure policy drops messages** - When backpressure is BLOCKING, only CRITICAL priority messages are accepted. Design your message priorities accordingly.

7. **Batch send requires platform integration** - The batch building (`pt_batch_init`, `pt_batch_add`, `pt_batch_prepare`) is platform-independent. The `pt_drain_send_queue()` function takes a `pt_batch_send_fn` callback that must be provided by the platform layer (Phase 4 POSIX, Phase 5 MacTCP, Phase 6 OT).

8. **Batch buffer is pre-allocated** - `pt_drain_send_queue()` uses `ctx->send_batch` instead of a stack variable. Make sure `struct pt_context` includes the `send_batch` field before using this function.

9. **Slot structure changed** - After implementing Phase 3, all code using `pt_queue_slot` must be rebuilt due to added fields and reordered layout (metadata first, data last for cache efficiency). This is a **BREAKING CHANGE**.

10. **Use pt_memcpy_isr in ISR variants** - The ISR-safe push functions MUST use `pt_memcpy_isr()`, not `pt_memcpy()`, because `pt_memcpy()` calls BlockMoveData which is forbidden at interrupt time.

11. **Platform macro consistency** - This phase uses `PT_PLATFORM_MACTCP` and `PT_PLATFORM_OT` (matching Phase 1's Makefile). Phase 2 also uses `PT_OPEN_TRANSPORT` in some places. Ensure Phase 1's `Makefile.retro68` defines both for OT builds: `-DPT_PLATFORM_OT -DPT_OPEN_TRANSPORT`.

12. **Do NOT use pt_queue_peek/pt_queue_consume** - These functions were REMOVED because they bypass the priority free-lists and coalesce hash, causing data structure corruption. Use `pt_queue_pop_priority()` instead.

13. **Logging categories differ by operation type** - Queue and batch operations use `PT_LOG_CAT_PROTOCOL`. **Exception:** Backpressure transitions use `PT_LOG_CAT_PERF` to allow applications to filter performance warnings without disabling protocol error logging. The `PT_LOG_CAT_SEND` category is reserved for Phase 3.5's SendEx API.

14. **Direct pop requires commit** - When using `pt_queue_pop_priority_direct()`, you MUST call `pt_queue_pop_priority_commit()` after processing the data. Failure to commit leaves the slot in an inconsistent state (data accessible but not removed from queue).

15. **Direct pop pointer validity** - The pointer returned by `pt_queue_pop_priority_direct()` is only valid until `pt_queue_pop_priority_commit()` is called. Do NOT store the pointer for later use - copy the data to your buffer first.

16. **PT_COALESCE_KEY peer_id limit** - The `PT_COALESCE_KEY(type, peer_id)` macro requires `peer_id < 256` because it shifts peer_id into the upper 8 bits of a 16-bit key. This matches PT_MAX_PEER_ID from Phase 1.

17. **Struct pt_context must include PT_Log *log** - The `pt_drain_send_queue()` function uses `PT_DEBUG`, `PT_WARN`, and `PT_ERR` macros which require `ctx->log` to be valid. Initialize this pointer from Phase 0's `PT_LogCreate()`.

18. **ISR flags are in pt_queue, not pt_context** - The `q->isr_flags` struct collects events from ISR callbacks. This is in pt_queue because `pt_queue_push_coalesce_isr()` only has access to the queue pointer. Call `pt_check_queue_isr_flags(ctx, q)` from your main event loop to log these events safely.

19. **Use correct PT_Log macro names** - The logging macros are `PT_LOG_WARN(ctx->log, cat, ...)`, `PT_LOG_DEBUG(ctx->log, cat, ...)`, and `PT_LOG_ERR(ctx->log, cat, ...)`. Note the first parameter is `ctx->log` (the log context pointer), NOT `ctx`. Do NOT use `PT_WARN`, `PT_DEBUG`, or `PT_ERR` - these don't exist.

20. **ctx->log must be initialized** - All Phase 3 functions that log (pt_drain_send_queue, pt_check_queue_isr_flags, etc.) require `ctx->log` to be a valid PT_Log pointer. Initialize with `PT_LogCreate()` in your application's init function.

21. **Coalesce hash uses XOR-fold** - The hash function is `PT_COALESCE_HASH(key) = ((key ^ (key >> 8)) & PT_COALESCE_HASH_MASK)`. This mixes both the type (lower 8 bits) and peer_id (upper 8 bits) of per-peer coalesce keys. A simple mask would cause all peers with the same type to collide.

## References

- Phase 2 (PHASE-2-PROTOCOL.md): Base queue implementation, pt_queue_pressure()
- CLAUDE.md: ASR/notifier restrictions, ISR-safe patterns
- Inside Macintosh Volume VI, Appendix B, Table B-3: "Routines That May Be Called at Interrupt Time" (TickCount NOT listed)
- Networking With Open Transport, Appendix C, Table C-1: Interrupt-safe OT functions (OTGetTimeStamp, OTAtomicAdd16, etc.)
- OSUtils.h: TickCount() declaration (NOT Timer.h)
- Game networking best practices for coalescing
- TCP Nagle algorithm considerations

### Open Transport Notes

**OTAllocMem Caveat:** While OTAllocMem is listed in Table C-1 as callable at deferred task time, allocation may fail under memory pressure. For reliability, pre-allocate buffers instead of calling OTAllocMem from notifiers. The plan uses pre-allocated `pt_batch` and `pt_queue_slot` structures to avoid this issue.

### Data-Oriented Design References
- 68030 data cache: 256 bytes only - struct field ordering critical
- Memory bandwidth on 68k: ~2-10 MB/s - O(n) scans cause measurable latency
- Classic Mac stack size: often 32KB - avoid large stack allocations
- Priority free-lists: O(1) dequeue using per-priority linked lists
- Coalesce hash table: O(1) lookup using direct-mapped 32-entry table (matches PT_QUEUE_MAX_SLOTS)
- Traversal locality: next_slot stored IN pt_queue_slot to avoid cache thrashing
- PT_QUEUE_MAX_SLOTS: 32 slots maximum for Classic Mac memory constraints

### Logging References
- PT_Log API: See PHASE-0-LOGGING.md for API details
- PT_LOG_CAT_PROTOCOL: Use for queue operations, message handling, batch building
- PT_LOG_CAT_NETWORK: Use for actual network transmission (in Phases 4-6)
- PT_Log is NOT interrupt-safe: Do not call from ASR/notifiers
- ISR logging pattern: Set volatile flag in ISR, log from main loop

### Timestamp Field Usage (Latency Analysis)
The `pt_queue_slot.timestamp` field captures when a message was enqueued. This enables:
- **Queue latency measurement:** `latency = pt_get_ticks() - slot->timestamp` in pop
- **Age-based prioritization:** Older messages could be promoted (not implemented yet)
- **Debugging:** Log message age when dropped due to backpressure
- **Performance analysis:** Track queue residence time for optimization

Note: ISR-enqueued messages have `timestamp = 0` (TickCount not interrupt-safe). Filter these when computing latency statistics.
