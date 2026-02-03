# PHASE 6: Open Transport Networking

> **Status:** OPEN
> **Depends on:** Phase 2 (Protocol layer for shared types and encoding)
>                Phase 7 (AppleTalk - **REQUIRED** for Sessions 6.8-6.10 multi-transport)
>                Phase 3 (Advanced Queues - optional)
> **Validates against:** Phase 4 (POSIX) and Phase 5 (MacTCP) for cross-platform interop testing
> **Produces:** Fully functional PeerTalk on Mac OS 7.6.1+ with Open Transport
> **Risk Level:** MEDIUM (more modern API but still event-driven)
> **Estimated Sessions:** 10 (6 TCP/IP + 4 Multi-Transport)
> **Note:** Gateway/bridging functionality moved to [FUTURE-GATEWAY-BRIDGING.md](FUTURE-GATEWAY-BRIDGING.md)
> **CSend Lessons:** See [CSEND-LESSONS.md](CSEND-LESSONS.md) Part B for critical Open Transport gotchas
> **Build Order:** Sessions 6.1-6.6 (TCP/IP) can proceed after Phase 2. Sessions 6.8-6.10 require Phase 7.
> **Phase 1/2 Prerequisite:** `PT_TRANSPORT_ADSP` (0x04) and `PT_TRANSPORT_NBP` (0x08) constants - ✓ Added to Phase 1
> **Phase 8 Dependency:** Session 8.3 (Multi-Transport Gateway Chat) is **BLOCKED** until Sessions 6.8-6.10 complete.
>
> **Multi-Transport API Contract (defined in Session 6.8):**
> ```c
> /* Returns bitmask of transports available for peer (PT_TRANSPORT_TCP, PT_TRANSPORT_ADSP, etc.)
>  * Returns 0 if peer not found */
> uint16_t PeerTalk_GetAvailableTransports(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id);
>
> /* Connect to peer via specific transport. Returns PT_OK on success,
>  * PT_ERR_NOT_SUPPORTED if transport unavailable for this peer */
> PeerTalk_Error PeerTalk_ConnectVia(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id, uint16_t transport);
>
> /* Enable/disable gateway mode for cross-transport message relay (DEFERRED - see FUTURE-GATEWAY-BRIDGING.md) */
> PeerTalk_Error PeerTalk_SetGatewayMode(PeerTalk_Context *ctx, Boolean enabled);
>
> /* Get gateway statistics (DEFERRED - see FUTURE-GATEWAY-BRIDGING.md) */
> PeerTalk_Error PeerTalk_GetGatewayStats(PeerTalk_Context *ctx, PeerTalk_GatewayStats *stats_out);
>
> /* Gateway stats structure */
> typedef struct {
>     uint32_t messages_relayed;   /* Total messages relayed between transports */
>     uint32_t tcp_to_adsp;        /* Messages relayed from TCP to ADSP */
>     uint32_t adsp_to_tcp;        /* Messages relayed from ADSP to TCP */
>     uint32_t relay_failures;     /* Failed relay attempts */
> } PeerTalk_GatewayStats;
> ```

## Plan Review Fixes Applied (2026-01-29)

This plan was reviewed by the `/review` skill and the following fixes were applied:

### Review 3 - 2026-01-29

#### Critical Fixes
1. **Pool exhaustion warning implementation added** - `pt_endpoint_pool_alloc()` now includes explicit comment pattern for caller to warn at 75% capacity.

2. **TCP send function added** - New `pt_ot_tcp_send()` implementation with logging: flow control at DEBUG, errors at ERR level.

3. **ADSP close timeout tracking documented** - `close_start` field now explicitly used with timeout check pattern in close documentation.

4. **NBP lookup failure logging** - Added PT_LOG_WARN for OTLookupName errors and PT_LOG_DEBUG for 0 results.

#### Documentation Fixes
5. **TCPClose blocking clarification** - Changed misleading "30+ seconds" claim to "ULP timeout period (user-configurable)".

6. **CSend architecture comparison clarified** - Added note that CSend uses polling (OTLook), not notifiers; pattern principles apply to both.

7. **Phase dependency statement clarified** - Explicitly states Sessions 6.1-6.7 depend on Phase 2; Sessions 6.8-6.10 require Phase 7 completion.

8. **68030 cache guidance added** - Explicit cache size table and PT_MAX_PEERS recommendation for 68030 targets.

#### Performance Fixes
9. **Main poll loop uses bitmap iteration** - Changed from O(PT_MAX_PEERS) to O(active) using bitmap pattern.

---

### Review 2 - 2026-01-29

#### Critical Fixes
1. **PT_LOG_CAT_GENERAL replaced with specific categories** - All 13 instances of `PT_LOG_CAT_GENERAL` replaced with appropriate categories (`PT_LOG_CAT_INIT` for startup/shutdown, `PT_LOG_CAT_PLATFORM` for OT operations, `PT_LOG_CAT_NETWORK` for network interface info).

2. **pt_adsp_endpoint_hot missing close_start** - Added `unsigned long close_start;` field for timeout tracking consistency with TCP endpoints. Struct now 32 bytes (matches TCP).

3. **Bitmap capacity>=32 guard** - Added `pt_endpoint_pool_active_mask()` helper with proper UB protection for capacity values of 32 or higher.

#### Architectural Fixes
4. **pt_ot_multi_data reordered for cache efficiency** - Hot data grouped first (pools, hot arrays, timing), then warm data (configs, UPPs), then cold pointers, then coldest (NBP mapper with 2KB buffer).

5. **Pool exhaustion warning** - Added warning at 75% capacity in `pt_endpoint_pool_alloc()`.

#### Documentation Fixes
6. **OTRcvUDErr Table C-3 clarification** - Documented that OTRcvUDErr IS in Table C-3 (deferred tasks) but main-loop approach chosen for simplicity.

7. **UDP receive loop exit condition** - Noted that UDP only checks `kOTNoDataErr` (no `kOTLookErr` for connectionless).

8. **80% cache efficiency caveat** - Added note that claim applies to 68040/PPC; 68030 with large PT_MAX_PEERS will experience cache thrashing.

---

### Review 1 - 2026-01-28

#### Critical Fixes
1. **OTRcvUDErr moved to main loop** - Was incorrectly called from UDP notifier. While OTRcvUDErr IS in Table C-3 (deferred task functions), we use the main-loop pattern for simplicity and consistency with CSend. Now sets `PT_FLAG_UDERR_PENDING` flag in notifier; `pt_ot_udp_clear_error()` calls OTRcvUDErr from main poll loop.

2. **Missing log categories added** - `PT_LOG_CAT_DISCOVERY`, `PT_LOG_CAT_SEND`, `PT_LOG_CAT_RECV`, `PT_LOG_CAT_INIT` were used but not defined in PHASE-0-LOGGING.md. Now added.

### Architectural Fixes
3. **ADSP hot/cold separation** - `pt_adsp_endpoint` split into `pt_adsp_endpoint_hot` (~24 bytes) and `pt_adsp_endpoint_cold` (~1.1KB) following TCP endpoint pattern for cache efficiency.

4. **pt_ot_multi_data uses SoA pattern** - Separate `adsp_hot[]`, `adsp_cold*`, and `adsp_pool` arrays instead of embedded full structs.

5. **ADSP notifier uses atomic flags** - Changed from `ep->flags.data_available = 1` to `PT_FLAG_SET(hot->flags, PT_FLAG_DATA_AVAILABLE)` for reentrancy safety.

6. **NBP lookup buffer pre-allocated** - Moved 2KB `reply_buf` from stack to `pt_nbp_mapper.lookup_reply_buf` to prevent stack overflow on 68k.

### Documentation Fixes
7. **Execution level clarification** - Documented three OT execution levels (hardware interrupt, deferred task, system task) instead of calling deferred task "slightly safer".

8. **OTSnd/OTRcv return type documented** - These return `OTResult` (byte count on success), not `OSStatus`.

9. **State transition logging helper** - Added `pt_endpoint_set_state()` helper and documentation.

10. **Timeout/race condition logging** - Added logging patterns for connect timeout, close timeout, and T_DATA-before-T_PASSCON race.

### Performance Fixes
11. **Bitmap-optimized poll loop** - Connect poll now iterates only active slots via `~tcp_pool.free_bitmap` instead of all `PT_MAX_PEERS`.

12. **Performance timing integration** - Documented OT-safe timing pattern using `OTGetTimeStamp()` / `OTElapsedMilliseconds()`.

## Overview

Phase 6 implements the Open Transport (OT) networking layer for PowerPC Macintosh and late 68k systems running System 7.6.1 or later. Open Transport provides a more modern, XTI-based API compared to MacTCP, with proper support for multiple simultaneous connections.

**Key Advantages over MacTCP (from NetworkingOpenTransport.txt):**
- Multiple listeners on same port (tilisten pattern)
- Endpoint/provider model more similar to modern APIs
- Better support for async operations via notifiers
- Memory management more flexible

**Architecture Choice: Notifiers vs Polling**

This implementation uses OT notifiers for event detection. An alternative approach (used successfully by the `csend` reference implementation) is synchronous polling with `OTLook()` from the main event loop.

| Approach | Pros | Cons |
|----------|------|------|
| Notifiers | Lower latency, event-driven | Reentrancy concerns, need atomic ops |
| Polling | Simpler, no reentrancy | Higher latency, more CPU in tight loops |

Both are valid. This phase uses notifiers with atomic flag operations for safety.

**Key Constraints (from NetworkingOpenTransport.txt Ch.3):**

Open Transport defines **three execution levels** (from most restricted to least):
1. **Hardware interrupt time** - Most restricted; only Table C-1 functions allowed
2. **Deferred task time** - Notifiers run here; similar restrictions to interrupt level
3. **System task time** - Normal application code; full API available

Notifiers run at **deferred task time**, which has similar (but not identical) restrictions to hardware interrupt time:
- Must not allocate memory from Memory Manager (except OTAllocMem - see warning below)
- Must not call synchronous OT/Device Manager/File Manager functions
- **Notifier reentrancy warning:** Per documentation, "Open Transport might call a notification routine reentrantly." Write notifiers defensively.
- **Exception:** When handling `kOTProviderWillClose`, you CAN make synchronous calls (this event is only issued at system task time)

**IMPORTANT:** While deferred task time is "safer" than hardware interrupt time (OT queues calls to prevent reentrancy *most* of the time), you should still treat it as restricted and use atomic flag patterns.

**OTAllocMem Warning (from Networking With Open Transport p.9143-9148):**
> "You can safely call the functions OTAllocMem and OTFreeMem from your notifier. However, keep in mind that the memory allocated by OTAllocMem comes from the application's memory pool, which, due to Memory Manager constraints, can only be replenished at system task time. Therefore, **if you allocate memory at hardware interrupt level or deferred task level, be prepared to handle a failure as a result of a temporarily depleted memory pool.**"

**Best Practice:** Pre-allocate all buffers at initialization time. Only use OTAllocMem in notifiers as a last resort, and always check for NULL return.

**Critical: OTConfiguration Disposal**
Per Networking With Open Transport: "The functions used to open providers take a pointer to the configuration structure as input, but as part of their processing, they dispose of the original configuration structure."

**Solution:** Use `OTCloneConfiguration()` before each `OTOpenEndpoint()` call if reusing configurations.

### ISR-Safe Logging Pattern (CRITICAL)

**PT_Log CANNOT be called from notifier callbacks.** Notifiers run at deferred task time with similar restrictions to interrupt level - no memory allocation (except OTAllocMem with caveats), no synchronous calls.

**The Flag-Based Logging Pattern for OT:**

```c
/*============================================================================
 * ISR-Safe Logging - Atomic Flag + Main Loop Pattern
 *
 * Notifier callbacks set atomic flags; main loop does the actual logging.
 * Uses OTAtomic* functions for true atomicity on PPC.
 *============================================================================*/

/* Log event bit positions (within flags uint32_t)
 *
 * These are separate from operational flags (bits 0-15) and are
 * set by notifiers when events should be logged. The main loop
 * clears these bits after logging.
 *
 * IMPORTANT: Store any additional info (error codes, etc.) in
 * pre-allocated fields BEFORE setting the log event flag.
 */
#define PT_LOG_EVT_CONNECT_DONE     16
#define PT_LOG_EVT_DATA_ARRIVED     17
#define PT_LOG_EVT_DISCONNECT       18
#define PT_LOG_EVT_ORDERLY_REL      19
#define PT_LOG_EVT_ERROR            20
#define PT_LOG_EVT_ACCEPT_DONE      21
#define PT_LOG_EVT_LISTEN           22
#define PT_LOG_EVT_FLOW_CONTROL     23  /* T_GODATA received */

/* Store error info in pre-allocated field */
typedef struct pt_tcp_endpoint_hot {
    /* ... existing fields ... */
    volatile int32_t  log_error_code;    /* Error from notifier */
} pt_tcp_endpoint_hot;

/*
 * In notifier callback - set atomic flags only, NO PT_Log calls!
 */
static pascal void tcp_notifier(void *context, OTEventCode code,
                                OTResult result, void *cookie) {
    pt_tcp_endpoint_hot *hot = (pt_tcp_endpoint_hot *)context;

    switch (code) {
    case T_DATA:
        PT_FLAG_SET(hot->flags, PT_FLAG_DATA_AVAILABLE);
        PT_FLAG_SET(hot->flags, PT_LOG_EVT_DATA_ARRIVED);  /* Mark for logging */
        break;

    case T_DISCONNECT:
        PT_FLAG_SET(hot->flags, PT_FLAG_DISCONNECT);
        PT_FLAG_SET(hot->flags, PT_LOG_EVT_DISCONNECT);
        hot->log_error_code = result;  /* Save for logging */
        break;

    case T_CONNECTCOMPLETE:
        hot->async_result = result;
        PT_FLAG_SET(hot->flags, PT_FLAG_CONNECT_COMPLETE);
        PT_FLAG_SET(hot->flags, PT_LOG_EVT_CONNECT_DONE);
        if (result != kOTNoError) {
            PT_FLAG_SET(hot->flags, PT_LOG_EVT_ERROR);
            hot->log_error_code = result;
        }
        break;
    }
    /* NO PT_LOG_* calls here! */
}

/*
 * In main poll loop - process log events safely
 *
 * This function processes all pending log events set by notifiers.
 * Call this from pt_ot_poll() BEFORE processing operational flags.
 *
 * Uses bitmap iteration to only check active endpoints (O(active)
 * not O(PT_MAX_PEERS)).
 */
static void pt_ot_process_log_events(pt_ot_data *od, struct pt_context *ctx) {
    /* Iterate only active slots using bitmap */
    uint32_t active = ~od->tcp_pool.free_bitmap & ((1u << od->tcp_pool.capacity) - 1);

    while (active) {
        #if defined(__GNUC__) && (defined(__powerpc__) || defined(__ppc__))
            int i = __builtin_ffs(active) - 1;
        #else
            int i = 0;
            uint32_t tmp = active;
            while ((tmp & 1) == 0) { tmp >>= 1; i++; }
        #endif
        active &= ~(1u << i);

        pt_tcp_endpoint_hot *hot = &od->tcp_hot[i];

        /* Check and clear log event flags atomically */
        if (PT_FLAG_TEST(hot->flags, PT_LOG_EVT_DATA_ARRIVED)) {
            PT_FLAG_CLEAR(hot->flags, PT_LOG_EVT_DATA_ARRIVED);
            PT_LOG_DEBUG(ctx, PT_LOG_CAT_RECV, "TCP endpoint %d: data arrived", i);
        }
        if (PT_FLAG_TEST(hot->flags, PT_LOG_EVT_DISCONNECT)) {
            PT_FLAG_CLEAR(hot->flags, PT_LOG_EVT_DISCONNECT);
            PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
                    "TCP endpoint %d: disconnected (result=%ld)",
                    i, hot->log_error_code);
        }
        if (PT_FLAG_TEST(hot->flags, PT_LOG_EVT_CONNECT_DONE)) {
            PT_FLAG_CLEAR(hot->flags, PT_LOG_EVT_CONNECT_DONE);
            if (hot->async_result == kOTNoError) {
                PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT, "TCP endpoint %d: connected", i);
            } else {
                PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                    "TCP endpoint %d: connect failed (result=%ld)",
                    i, hot->async_result);
            }
        }
        if (PT_FLAG_TEST(hot->flags, PT_LOG_EVT_ORDERLY_REL)) {
            PT_FLAG_CLEAR(hot->flags, PT_LOG_EVT_ORDERLY_REL);
            PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
                "TCP endpoint %d: orderly release received", i);
        }
        if (PT_FLAG_TEST(hot->flags, PT_LOG_EVT_ACCEPT_DONE)) {
            PT_FLAG_CLEAR(hot->flags, PT_LOG_EVT_ACCEPT_DONE);
            PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
                "TCP endpoint %d: accept completed", i);
        }
        if (PT_FLAG_TEST(hot->flags, PT_LOG_EVT_FLOW_CONTROL)) {
            PT_FLAG_CLEAR(hot->flags, PT_LOG_EVT_FLOW_CONTROL);
            PT_LOG_DEBUG(ctx, PT_LOG_CAT_SEND,
                "TCP endpoint %d: flow control lifted (T_GODATA)", i);
        }
        if (PT_FLAG_TEST(hot->flags, PT_LOG_EVT_ERROR)) {
            PT_FLAG_CLEAR(hot->flags, PT_LOG_EVT_ERROR);
            PT_LOG_ERR(ctx, PT_LOG_CAT_PLATFORM,
                   "TCP endpoint %d: error %ld", i, hot->log_error_code);
        }
    }
}

/*
 * Call from pt_ot_poll() at appropriate point
 */
int pt_ot_poll(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);

    /* Process any pending log events from notifiers first */
    pt_ot_process_log_events(od, ctx->log);

    /* ... rest of poll processing ... */
}
```

**Key Rules:**
1. Notifier sets atomic flags (via `PT_FLAG_SET`) and stores data in pre-allocated fields
2. Notifier NEVER calls PT_Log, PT_ERR, PT_DEBUG, etc.
3. Main loop tests and clears flags atomically, performs actual logging
4. Use `OTAtomicSetBit`/`OTAtomicClearBit` for flag operations
5. Error codes stored in `volatile` fields for main loop access

**OT-Safe Timing:**
Unlike MacTCP, Open Transport provides interrupt-safe timing functions:
- `OTGetTimeStamp()` - Get current timestamp (safe in notifier)
- `OTElapsedMilliseconds()` - Calculate elapsed time (safe in notifier)

Use these instead of `TickCount()` if you need timing in notifiers:
```c
/* In notifier - safe to use OT timing functions */
hot->event_timestamp = OTGetTimeStamp();

/* In main loop - calculate elapsed time */
OTTimeStamp now;
OTGetTimeStamp(&now);
uint32_t elapsed_ms = OTElapsedMilliseconds(&hot->event_timestamp, &now);
PT_DEBUG(log, PT_LOG_CAT_PERF, "Event latency: %lu ms", elapsed_ms);
```

**Log Categories for Open Transport:**
- `PT_LOG_CAT_PLATFORM` - OT init, endpoint creation, configuration errors, state transitions
- `PT_LOG_CAT_NETWORK` - Connections, data transfer, peer events
- `PT_LOG_CAT_PERF` - Timing measurements using OT timing functions
- `PT_LOG_CAT_DISCOVERY` - UDP broadcast, NBP lookup operations
- `PT_LOG_CAT_SEND` - TCP/UDP/ADSP send operations
- `PT_LOG_CAT_RECV` - TCP/UDP/ADSP receive operations
- `PT_LOG_CAT_INIT` - OT initialization, shutdown
- `PT_LOG_CAT_MEMORY` - Memory allocation, pool exhaustion, kENOMEMErr retries
- `PT_LOG_CAT_CONNECT` - Connection lifecycle, state machine transitions

**State Transition Logging Helper:**

For debugging connection issues, log all endpoint state transitions:

```c
/*
 * Helper to log state transitions - call from main loop only.
 * Centralizes state change logging for easier debugging.
 */
static inline void pt_endpoint_set_state(
    pt_tcp_endpoint_hot *hot,
    pt_endpoint_state new_state,
    struct pt_context *ctx,
    int idx,
    const char *endpoint_type)  /* "TCP" or "ADSP" */
{
    pt_endpoint_state old_state = hot->state;
    if (old_state != new_state) {
        hot->state = new_state;
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_PLATFORM,
            "%s endpoint %d: state %d -> %d",
            endpoint_type, idx, old_state, new_state);
    }
}
```

**Race Condition Logging:**

Log when race conditions are detected and resolved:

```c
/* In poll loop after T_PASSCON handling */
if (PT_FLAG_TEST(hot->flags, PT_FLAG_DEFER_DATA)) {
    PT_FLAG_CLEAR(hot->flags, PT_FLAG_DEFER_DATA);
    PT_FLAG_SET(hot->flags, PT_FLAG_DATA_AVAILABLE);
    PT_LOG_DEBUG(ctx, PT_LOG_CAT_PLATFORM,
        "TCP endpoint %d: T_DATA arrived before T_PASSCON (race resolved)", idx);
}
```

**Timeout Event Logging:**

Log timeout detection and forced aborts:

```c
#define PT_CONNECT_TIMEOUT_TICKS (30 * 60)  /* 30 seconds */
#define PT_CLOSE_TIMEOUT_TICKS   (30 * 60)  /* 30 seconds */

/* In poll loop */
if (hot->state == PT_EP_OUTGOING) {
    unsigned long elapsed = now - cold->connect_start;
    if (elapsed > PT_CONNECT_TIMEOUT_TICKS) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "TCP endpoint %d: connect timeout after %lu ticks",
            idx, elapsed);
        /* ... cleanup ... */
    }
}

if (hot->state == PT_EP_CLOSING) {
    unsigned long elapsed = now - hot->close_start;
    if (elapsed > PT_CLOSE_TIMEOUT_TICKS) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "TCP endpoint %d: close timeout after %lu ticks, forcing abort",
            idx, elapsed);
        OTSndDisconnect(hot->ref, NULL);  /* Abortive disconnect */
        /* ... cleanup ... */
    }
}
```

**Performance Timing Integration:**

Use OT-safe timing functions for latency measurement:

```c
/* In notifier - store timestamp using OT-safe function */
OTTimeStamp ts;
OTGetTimeStamp(&ts);
hot->event_timestamp = ts;  /* Pre-allocated field in hot struct */

/* In main poll loop - calculate and log latency */
OTTimeStamp now;
OTGetTimeStamp(&now);
uint32_t latency_ms = OTElapsedMilliseconds(&hot->event_timestamp, &now);
if (latency_ms > 100) {  /* Only log slow events */
    PT_LOG_DEBUG(ctx, PT_LOG_CAT_PERF,
        "TCP endpoint %d: event latency %lu ms", idx, latency_ms);
}
```

## Goals

1. Implement Open Transport initialization and endpoint management
2. Create UDP endpoint for discovery with notifier
3. Create TCP endpoint for connections
4. Implement "tilisten" pattern for multiple connections
5. Test alongside MacTCP version on same protocol

## Session Scope Table

| Session | Focus | Status | Files Created/Modified | Tests | Verify |
|---------|-------|--------|------------------------|-------|--------|
| 6.1 | OT Init & Types | [OPEN] | `src/opentransport/ot_defs.h`, `src/opentransport/ot_driver.c` | Real hardware | OT gestalt, init |
| 6.2 | UDP Endpoint | [OPEN] | `src/opentransport/udp_ot.c` | Real hardware | Send/recv UDP |
| 6.3 | TCP Endpoint | [OPEN] | `src/opentransport/tcp_ot.c` | Real hardware | Endpoint lifecycle |
| 6.4 | TCP Connect | [OPEN] | `src/opentransport/tcp_connect_ot.c` | Real hardware | Outgoing connections |
| 6.5 | TCP Server | [OPEN] | `src/opentransport/tcp_server_ot.c` | Real hardware | tilisten pattern |
| 6.6 | TCP/IP Integration | [OPEN] | All OT TCP/IP files | Real hardware | End-to-end TCP/IP |
| **6.7** | **Multi-Transport Types** | [OPEN] | `src/opentransport/ot_multi.h` | Real hardware | Types compile |
| **6.8** | **AppleTalk via OT** | [OPEN] | `src/opentransport/ot_adsp.c`, `src/opentransport/ot_nbp.c` | Real hardware | NBP + ADSP work |
| **6.9** | **Unified Library Build** | [OPEN] | `Makefile.retro68` update | Real hardware | `libpeertalk_ot_at.a` builds |
| **6.10** | **Multi-Transport Poll** | [OPEN] | `src/opentransport/ot_multi.c` | Real hardware | Unified poll, peer dedup |

### Status Key
- **[OPEN]** - Not started
- **[IN PROGRESS]** - Currently being worked on
- **[READY TO TEST]** - Implementation complete, needs verification
- **[DONE]** - Verified and complete

---

## Session 6.1: Open Transport Init & Types

### Objective
Establish Open Transport initialization and define endpoint wrapper types.

### Key Differences from MacTCP
1. Use `InitOpenTransport()` instead of PBOpen
2. Use `OTOpenEndpoint()` instead of TCPCreate
3. Use notifiers instead of ASRs (similar but different API)
4. Endpoints use `TEndpointRef` instead of `StreamPtr`

### Tasks

#### Task 6.1.1: Create `src/opentransport/ot_defs.h`

```c
#ifndef PT_OT_DEFS_H
#define PT_OT_DEFS_H

/* Open Transport includes */
#include <OpenTransport.h>
#include <OpenTptInternet.h>

/*============================================================================
 * Endpoint States (from NetworkingOpenTransport.txt Ch.4)
 *
 * Using uint8_t instead of enum to save memory (enum defaults to 4 bytes).
 * Only 8 states needed (3 bits), but uint8_t is more efficient than bitfield.
 *============================================================================*/
typedef uint8_t pt_endpoint_state;
#define PT_EP_UNUSED    0
#define PT_EP_OPENING   1
#define PT_EP_UNBOUND   2
#define PT_EP_IDLE      3
#define PT_EP_OUTGOING  4
#define PT_EP_INCOMING  5
#define PT_EP_DATAXFER  6
#define PT_EP_CLOSING   7

/*============================================================================
 * Notifier Event Flags - OPTIMIZED for cache efficiency
 *
 * Uses single uint32_t with OTAtomicSetBit/OTAtomicClearBit/OTAtomicTestBit
 * for true atomicity on PPC. This reduces 8 memory accesses to 1.
 *
 * From NetworkingOpenTransport.txt Table C-1: OTAtomic* operations are
 * safe to call at hardware interrupt time and are atomic.
 *
 * Usage in notifier:
 *   OTAtomicSetBit((UInt8*)&ep->flags, PT_FLAG_DATA_AVAILABLE);
 *
 * Usage in poll (main thread):
 *   if (OTAtomicTestBit((UInt8*)&ep->flags, PT_FLAG_DATA_AVAILABLE)) {
 *       OTAtomicClearBit((UInt8*)&ep->flags, PT_FLAG_DATA_AVAILABLE);
 *       // process...
 *   }
 *
 * IMPORTANT: OTAtomic* requires pointer to UInt8, but operates on 4-byte
 * aligned word. Bit numbers 0-31 address all 32 bits of the word.
 *============================================================================*/

/* Flag bit positions within uint32_t */
#define PT_FLAG_CONNECT_COMPLETE    0
#define PT_FLAG_DATA_AVAILABLE      1
#define PT_FLAG_DISCONNECT          2
#define PT_FLAG_ORDERLY_DISCONNECT  3
#define PT_FLAG_ACCEPT_COMPLETE     4
#define PT_FLAG_SEND_COMPLETE       5
#define PT_FLAG_LISTEN_COMPLETE     6
#define PT_FLAG_PASSCON             7
#define PT_FLAG_RECV_PENDING        8
#define PT_FLAG_SEND_PENDING        9
#define PT_FLAG_DEFER_DATA          10
#define PT_FLAG_UDERR_PENDING       11  /* UDP error needs clearing from main loop */
#define PT_FLAG_LISTEN_PENDING      12  /* For listener: T_LISTEN received */

/* Helper macros for flag operations */
#define PT_FLAG_SET(flags, bit)    OTAtomicSetBit((UInt8*)&(flags), (bit))
#define PT_FLAG_CLEAR(flags, bit)  OTAtomicClearBit((UInt8*)&(flags), (bit))
#define PT_FLAG_TEST(flags, bit)   OTAtomicTestBit((UInt8*)&(flags), (bit))

/* Clear all flags (use only from main thread, not notifier) */
#define PT_FLAGS_CLEAR_ALL(flags)  ((flags) = 0)

/*============================================================================
 * HOT/COLD Data Separation for Cache Efficiency
 *
 * Classic Mac cache sizes:
 *   - 68030: 256 bytes data cache (SE/30, IIci)
 *   - 68040: 4KB data cache (Quadra series)
 *   - PPC 601: 32KB L1 (Power Mac 6100/7100/8100)
 *
 * By separating hot data (checked every poll) from cold data (only during I/O),
 * we achieve 80%+ cache efficiency instead of <5% with mixed structs.
 *
 * IMPORTANT: The 80%+ efficiency applies to 68040 and PPC. On 68030 with its
 * 256-byte cache, hot arrays (32 bytes * PT_MAX_PEERS) may exceed cache size
 * when PT_MAX_PEERS > 6-8. Consider reducing PT_MAX_PEERS on 68030 targets
 * or accept some cache thrashing as unavoidable on these early machines.
 *
 * RECOMMENDED PT_MAX_PEERS BY CPU:
 *   - 68030: 8 (hot arrays fit in 256-byte cache: 8 * 32 = 256 bytes)
 *   - 68040: 16-24 (4KB cache has headroom)
 *   - PPC: 32 (32KB cache, no constraints)
 *
 * HOT data: ref, state, flags, peer pointer (~32 bytes)
 * COLD data: recv_buf, TCall structs, addresses (~1-2KB)
 *============================================================================*/

/*============================================================================
 * UDP Endpoint - Hot Data (polled frequently)
 *============================================================================*/
typedef struct pt_udp_endpoint_hot {
    EndpointRef         ref;            /* OT endpoint reference - 4 bytes */
    pt_endpoint_state   state;          /* 1 byte */
    uint8_t             _pad1[3];       /* Alignment padding */
    volatile uint32_t   flags;          /* Atomic flag word - 4 bytes */
    void               *user_data;      /* 4 bytes */
} pt_udp_endpoint_hot;  /* Total: 16 bytes - fits in cache line */

/*============================================================================
 * UDP Endpoint - Cold Data (accessed only during I/O)
 *============================================================================*/
typedef struct pt_udp_endpoint_cold {
    TUnitData           udata;          /* ~24 bytes */
    InetAddress         addr;           /* Remote address - 8 bytes */
    InetAddress         local_addr;     /* Local binding - 8 bytes */
    uint8_t             recv_buf[2048]; /* Receive buffer */
} pt_udp_endpoint_cold;

/*============================================================================
 * TCP Endpoint - Hot Data (polled frequently)
 *============================================================================*/
typedef struct pt_tcp_endpoint_hot {
    EndpointRef         ref;            /* OT endpoint reference - 4 bytes */
    pt_endpoint_state   state;          /* 1 byte */
    uint8_t             endpoint_idx;   /* Index in pool (for notifier context) */
    uint8_t             _pad1[2];       /* Alignment padding */
    volatile uint32_t   flags;          /* Atomic flag word - 4 bytes */
    struct pt_peer     *peer;           /* Associated peer - 4 bytes */
    OTResult            async_result;   /* From notifier completion - 4 bytes */
    volatile int32_t    log_error_code; /* Error code for deferred logging - 4 bytes */
    unsigned long       close_start;    /* Timeout tracking - 4 bytes */
    void               *user_data;      /* 4 bytes */
} pt_tcp_endpoint_hot;  /* Total: 32 bytes - fits in cache line */

/*============================================================================
 * TCP Endpoint - Cold Data (accessed only during I/O)
 *
 * NOTE: pending_call/pending_addr only used for listener endpoint.
 * For worker endpoints, these fields are wasted space but the simplicity
 * of uniform struct layout outweighs the ~44 byte overhead per endpoint.
 *============================================================================*/
typedef struct pt_tcp_endpoint_cold {
    TCall               call;           /* Connection call info - ~36 bytes */
    InetAddress         remote_addr;    /* Remote address - 8 bytes */
    InetAddress         local_addr;     /* Local address - 8 bytes */
    TCall               pending_call;   /* For listener: pending - ~36 bytes */
    InetAddress         pending_addr;   /* For listener: pending addr - 8 bytes */
    OTFlags             recv_flags;     /* 4 bytes */
    uint8_t             recv_buf[1024]; /* Receive buffer */
} pt_tcp_endpoint_cold;

/*============================================================================
 * Endpoint Pool with Free Bitmap
 *
 * O(1) allocation using bitmap instead of O(n) linear scan.
 * On PPC, __builtin_ffs() compiles to single cntlzw instruction.
 * On 68k, we provide a fallback implementation.
 *
 * Usage:
 *   int idx = pt_endpoint_pool_alloc(&od->tcp_pool);
 *   if (idx < 0) { // no free slots }
 *   pt_endpoint_pool_free(&od->tcp_pool, idx);
 *============================================================================*/
typedef struct pt_endpoint_pool {
    uint32_t            free_bitmap;    /* Bit set = slot is free */
    uint8_t             count;          /* Number of active endpoints */
    uint8_t             capacity;       /* Max endpoints (PT_MAX_PEERS) */
    uint8_t             _pad[2];
} pt_endpoint_pool;

/* Initialize pool with all slots free */
static inline void pt_endpoint_pool_init(pt_endpoint_pool *pool, uint8_t capacity) {
    pool->capacity = capacity;
    pool->count = 0;
    /* Set bits 0 to capacity-1 as free (1 = free, 0 = in use) */
    pool->free_bitmap = (capacity >= 32) ? 0xFFFFFFFF : ((1u << capacity) - 1);
}

/* Get bitmask of active (in-use) slots with proper UB protection for capacity>=32 */
static inline uint32_t pt_endpoint_pool_active_mask(pt_endpoint_pool *pool) {
    uint32_t mask = (pool->capacity >= 32) ? 0xFFFFFFFF : ((1u << pool->capacity) - 1);
    return ~pool->free_bitmap & mask;
}

/* Allocate a slot, returns index or -1 if full */
static inline int pt_endpoint_pool_alloc(pt_endpoint_pool *pool) {
    if (pool->free_bitmap == 0) return -1;

    /* Find first set bit (first free slot) */
    #if defined(__GNUC__) && (defined(__powerpc__) || defined(__ppc__))
        /* PPC: use builtin for single-instruction lookup */
        int idx = __builtin_ffs(pool->free_bitmap) - 1;
    #else
        /* 68k fallback: simple loop (still faster than scanning 1KB structs) */
        int idx = 0;
        uint32_t mask = pool->free_bitmap;
        while ((mask & 1) == 0) { mask >>= 1; idx++; }
    #endif

    /* Mark slot as in use (clear bit) */
    pool->free_bitmap &= ~(1u << idx);
    pool->count++;

    /*
     * Pool exhaustion warning at 75% capacity.
     * Caller MUST check and log with context after calling this function:
     *
     *   int idx = pt_endpoint_pool_alloc(&od->tcp_pool);
     *   if (idx >= 0 && od->tcp_pool.count > (od->tcp_pool.capacity * 3 / 4)) {
     *       PT_LOG_WARN(ctx, PT_LOG_CAT_MEMORY,
     *           "TCP endpoint pool at %d/%d (75%% threshold)",
     *           od->tcp_pool.count, od->tcp_pool.capacity);
     *   }
     */

    return idx;
}

/* Free a slot */
static inline void pt_endpoint_pool_free(pt_endpoint_pool *pool, int idx) {
    if (idx < 0 || idx >= pool->capacity) return;
    pool->free_bitmap |= (1u << idx);
    pool->count--;
}

/* Check if slot is in use */
static inline int pt_endpoint_pool_in_use(pt_endpoint_pool *pool, int idx) {
    return (pool->free_bitmap & (1u << idx)) == 0;
}

/*============================================================================
 * Platform-specific Context Extension
 *
 * Uses Structure of Arrays (SoA) pattern for endpoint storage.
 * Hot data arrays are compact and cache-friendly for polling.
 * Cold data is accessed via pointer only when needed for I/O.
 *============================================================================*/
typedef struct {
    /* OT client context - from InitOpenTransportInContext for CFM */
    OTClientContextPtr  client_context;

    /* Local IP */
    InetHost            local_ip;

    /* Universal Procedure Pointers for notifiers
     *
     * From OpenTransport.h: "Even though a OTNotifyUPP is a OTNotifyProcPtr
     * on pre-Carbon system, use NewOTNotifyUPP() and friends to make your
     * source code portable to OS X and Carbon."
     *
     * Create once at init, dispose at shutdown.
     */
    OTNotifyUPP         udp_notifier_upp;
    OTNotifyUPP         tcp_notifier_upp;

    /* UDP discovery endpoint (only one, so not pooled) */
    pt_udp_endpoint_hot  udp_hot;
    pt_udp_endpoint_cold *udp_cold;     /* Allocated separately */

    /* TCP listener endpoint (only one) */
    pt_tcp_endpoint_hot  listener_hot;
    pt_tcp_endpoint_cold *listener_cold; /* Allocated separately */

    /* TCP peer endpoints - Structure of Arrays for cache efficiency
     *
     * Hot arrays: scanned every poll (~28 bytes * PT_MAX_PEERS)
     * Cold array: accessed only during actual I/O
     */
    pt_endpoint_pool     tcp_pool;
    pt_tcp_endpoint_hot  tcp_hot[PT_MAX_PEERS];
    pt_tcp_endpoint_cold *tcp_cold;     /* Allocated: PT_MAX_PEERS * sizeof */

    /* Timing */
    unsigned long       last_announce_tick;

    /* Configuration string cache
     * CRITICAL: OTOpenEndpoint disposes configs. Use OTCloneConfiguration()
     * before each OTOpenEndpoint() call.
     */
    OTConfigurationRef  tcp_config;
    OTConfigurationRef  udp_config;

} pt_ot_data;

/* Get platform data from context */
static inline pt_ot_data *pt_ot_get(struct pt_context *ctx) {
    return (pt_ot_data *)((char *)ctx + sizeof(struct pt_context));
}

/* Helper to get cold data for a TCP endpoint index */
static inline pt_tcp_endpoint_cold *pt_tcp_cold(pt_ot_data *od, int idx) {
    return &od->tcp_cold[idx];
}

/* Configuration strings (from Ch.11) */
#define PT_OT_TCP_CONFIG    "tcp"
#define PT_OT_UDP_CONFIG    "udp"

/* IP address string buffer size for OTInetHostToString()
 * Max: "255.255.255.255" + NUL = 16 bytes */
#define PT_IP_STR_LEN       16

/* Open Transport modes */
#define PT_OT_FLAGS         (O_NONBLOCK)

#endif /* PT_OT_DEFS_H */
```

#### Task 6.1.2: Create `src/opentransport/ot_driver.c`

```c
#include "ot_defs.h"
#include "pt_internal.h"
#include "log.h"

size_t pt_ot_extra_size(void) {
    return sizeof(pt_ot_data);
}

/* Forward declarations for notifier functions (defined in later sessions) */
static pascal void pt_udp_notifier(void *, OTEventCode, OTResult, void *);
static pascal void pt_tcp_notifier(void *, OTEventCode, OTResult, void *);

/* Forward declarations for functions called before definition */
int pt_ot_get_local_ip(struct pt_context *ctx);
void pt_ot_close_all_endpoints(struct pt_context *ctx);
static void pt_ot_free_cold_data(struct pt_context *ctx);

/*
 * Allocate cold data structures (called once at init)
 *
 * Cold data contains large buffers (recv_buf) that are only accessed
 * during actual I/O operations. By allocating separately from hot data,
 * we keep the hot structures compact for cache efficiency during polling.
 *
 * Memory layout:
 *   - UDP cold: 1 x pt_udp_endpoint_cold (~2KB)
 *   - TCP listener cold: 1 x pt_tcp_endpoint_cold (~1.1KB)
 *   - TCP peer cold: PT_MAX_PEERS x pt_tcp_endpoint_cold (~1.1KB each)
 *
 * Total: ~2KB + ~1.1KB + (PT_MAX_PEERS * ~1.1KB)
 * With PT_MAX_PEERS=8: ~12KB cold data
 * With PT_MAX_PEERS=16: ~20KB cold data
 */
static int pt_ot_alloc_cold_data(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);

    /* Allocate UDP cold data */
    od->udp_cold = (pt_udp_endpoint_cold *)NewPtrClear(sizeof(pt_udp_endpoint_cold));
    if (!od->udp_cold) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to allocate UDP cold data (%lu bytes)",
            (unsigned long)sizeof(pt_udp_endpoint_cold));
        return -1;
    }

    /* Allocate TCP listener cold data */
    od->listener_cold = (pt_tcp_endpoint_cold *)NewPtrClear(sizeof(pt_tcp_endpoint_cold));
    if (!od->listener_cold) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to allocate listener cold data");
        pt_ot_free_cold_data(ctx);
        return -1;
    }

    /* Allocate TCP peer cold data array */
    od->tcp_cold = (pt_tcp_endpoint_cold *)NewPtrClear(
        PT_MAX_PEERS * sizeof(pt_tcp_endpoint_cold));
    if (!od->tcp_cold) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to allocate TCP cold data (%lu bytes)",
            (unsigned long)(PT_MAX_PEERS * sizeof(pt_tcp_endpoint_cold)));
        pt_ot_free_cold_data(ctx);
        return -1;
    }

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_MEMORY,
        "Allocated cold data: UDP=%lu, listener=%lu, TCP=%lu bytes",
        (unsigned long)sizeof(pt_udp_endpoint_cold),
        (unsigned long)sizeof(pt_tcp_endpoint_cold),
        (unsigned long)(PT_MAX_PEERS * sizeof(pt_tcp_endpoint_cold)));

    return 0;
}

/*
 * Free cold data structures
 */
static void pt_ot_free_cold_data(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);

    if (od->udp_cold) {
        DisposePtr((Ptr)od->udp_cold);
        od->udp_cold = NULL;
    }
    if (od->listener_cold) {
        DisposePtr((Ptr)od->listener_cold);
        od->listener_cold = NULL;
    }
    if (od->tcp_cold) {
        DisposePtr((Ptr)od->tcp_cold);
        od->tcp_cold = NULL;
    }
}

/*
 * Initialize Open Transport
 * From NetworkingOpenTransport.txt Ch.2: InitOpenTransport
 */
int pt_ot_init(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);
    OSStatus err;
    int i;

    /* Clear hot data (cold data allocated separately) */
    memset(od, 0, sizeof(pt_ot_data));

    /* Check if OT is available via Gestalt */
    long response;
    err = Gestalt(gestaltOpenTpt, &response);
    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_INIT,
            "Open Transport not available (Gestalt failed)");
        return -1;
    }

    if (!(response & gestaltOpenTptPresentMask)) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_INIT,
            "Open Transport not installed");
        return -1;
    }

    /* Initialize Open Transport */
    #if TARGET_API_MAC_CARBON
        /* Carbon uses InitOpenTransportInContext */
        err = InitOpenTransportInContext(kInitOTForApplicationMask,
                                         &od->client_context);
    #else
        err = InitOpenTransport();
        od->client_context = NULL;
    #endif

    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_INIT,
            "InitOpenTransport failed: %d", err);
        return -1;
    }

    /* Allocate cold data structures BEFORE anything that might need them */
    if (pt_ot_alloc_cold_data(ctx) < 0) {
        #if TARGET_API_MAC_CARBON
            CloseOpenTransportInContext(od->client_context);
        #else
            CloseOpenTransport();
        #endif
        return -1;
    }

    /* Create Universal Procedure Pointers for notifiers
     *
     * From OpenTransport.h: "use NewOTNotifyUPP() and friends to make
     * your source code portable to OS X and Carbon."
     *
     * On Classic Mac OS these are essentially no-ops (UPP == ProcPtr),
     * but using them is the correct portable pattern.
     */
    od->udp_notifier_upp = NewOTNotifyUPP(pt_udp_notifier);
    od->tcp_notifier_upp = NewOTNotifyUPP(pt_tcp_notifier);

    if (!od->udp_notifier_upp || !od->tcp_notifier_upp) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to create OT notifier UPPs");
        pt_ot_free_cold_data(ctx);
        #if TARGET_API_MAC_CARBON
            CloseOpenTransportInContext(od->client_context);
        #else
            CloseOpenTransport();
        #endif
        return -1;
    }

    /*
     * Create master configuration templates for TCP and UDP endpoints.
     *
     * IMPORTANT (from Networking With Open Transport Ch.2):
     * "The functions used to open providers take a pointer to the
     * configuration structure as input, but as part of their processing,
     * they dispose of the original configuration structure."
     *
     * Therefore, we must use OTCloneConfiguration() before each
     * OTOpenEndpoint() call. These master configs are never passed
     * directly to OTOpenEndpoint - only clones are.
     */
    od->tcp_config = OTCreateConfiguration(PT_OT_TCP_CONFIG);
    if (!od->tcp_config) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_PLATFORM,
            "Failed to create TCP configuration");
        pt_ot_free_cold_data(ctx);
        CloseOpenTransport();
        return -1;
    }

    od->udp_config = OTCreateConfiguration(PT_OT_UDP_CONFIG);
    if (!od->udp_config) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_PLATFORM,
            "Failed to create UDP configuration");
        OTDestroyConfiguration(od->tcp_config);
        pt_ot_free_cold_data(ctx);
        CloseOpenTransport();
        return -1;
    }

    /* Initialize endpoint pool for O(1) allocation */
    pt_endpoint_pool_init(&od->tcp_pool, PT_MAX_PEERS);

    /* Initialize endpoint states */
    od->udp_hot.state = PT_EP_UNUSED;
    od->listener_hot.state = PT_EP_UNUSED;

    for (i = 0; i < PT_MAX_PEERS; i++) {
        od->tcp_hot[i].state = PT_EP_UNUSED;
        od->tcp_hot[i].endpoint_idx = i;  /* Store index for notifier context */
    }

    PT_LOG_INFO(ctx, PT_LOG_CAT_INIT,
        "Open Transport initialized (hot/cold separation enabled)");

    /* Get local IP (requires opening a temporary endpoint) */
    pt_ot_get_local_ip(ctx);

    return 0;
}

/*
 * Get local IP address
 * From Ch.11: Use InetInterfaceInfo
 */
int pt_ot_get_local_ip(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);
    InetInterfaceInfo info;
    OSStatus err;

    err = OTInetGetInterfaceInfo(&info, kDefaultInetInterface);

    if (err != noErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
            "OTInetGetInterfaceInfo failed: %d", err);
        od->local_ip = 0;
        return -1;
    }

    od->local_ip = info.fAddress;

    char ip_str[32];
    OTInetHostToString(od->local_ip, ip_str);
    PT_LOG_INFO(ctx, PT_LOG_CAT_NETWORK,
        "Local IP: %s (0x%08lX)", ip_str, od->local_ip);

    return 0;
}

/*
 * Shutdown Open Transport
 */
void pt_ot_shutdown(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);

    /* Close all endpoints first (they reference the UPPs) */
    pt_ot_close_all_endpoints(ctx);

    /* Dispose Universal Procedure Pointers
     * CRITICAL: Must dispose after all endpoints are closed,
     * as endpoints may still reference these UPPs until closed.
     */
    if (od->udp_notifier_upp) {
        DisposeOTNotifyUPP(od->udp_notifier_upp);
        od->udp_notifier_upp = NULL;
    }
    if (od->tcp_notifier_upp) {
        DisposeOTNotifyUPP(od->tcp_notifier_upp);
        od->tcp_notifier_upp = NULL;
    }

    /* Destroy configurations */
    if (od->tcp_config) {
        OTDestroyConfiguration(od->tcp_config);
        od->tcp_config = NULL;
    }
    if (od->udp_config) {
        OTDestroyConfiguration(od->udp_config);
        od->udp_config = NULL;
    }

    /* Free cold data structures */
    pt_ot_free_cold_data(ctx);

    /* Close Open Transport */
    #if TARGET_API_MAC_CARBON
        CloseOpenTransportInContext(od->client_context);
    #else
        CloseOpenTransport();
    #endif

    PT_LOG_INFO(ctx, PT_LOG_CAT_INIT,
        "Open Transport shutdown complete");
}

/*
 * Generic endpoint close helper for hot data
 * Takes hot struct fields directly for cache efficiency.
 */
static void pt_ot_close_endpoint_hot(EndpointRef *ref_ptr, pt_endpoint_state *state_ptr) {
    EndpointRef ref = *ref_ptr;
    if (!ref)
        return;

    /* Try orderly unbind first */
    if (OTGetEndpointState(ref) >= T_IDLE) {
        OTUnbind(ref);
    }

    OTCloseProvider(ref);
    *ref_ptr = NULL;
    *state_ptr = PT_EP_UNUSED;
}

/*
 * Close all endpoints
 *
 * Uses hot data arrays for cache efficiency. The endpoint_pool tracks
 * which slots are in use, so we only touch active endpoints.
 */
void pt_ot_close_all_endpoints(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);
    int i;

    /* Close UDP endpoint */
    if (od->udp_hot.ref) {
        pt_ot_close_endpoint_hot(&od->udp_hot.ref, &od->udp_hot.state);
    }

    /* Close TCP listener */
    if (od->listener_hot.ref) {
        pt_ot_close_endpoint_hot(&od->listener_hot.ref, &od->listener_hot.state);
    }

    /* Close TCP peer endpoints - only check in-use slots via bitmap */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        if (pt_endpoint_pool_in_use(&od->tcp_pool, i) && od->tcp_hot[i].ref) {
            pt_ot_close_endpoint_hot(&od->tcp_hot[i].ref, &od->tcp_hot[i].state);
            pt_endpoint_pool_free(&od->tcp_pool, i);
        }
    }
}
```

### Acceptance Criteria
1. Gestalt check for OT presence works
2. InitOpenTransport succeeds
3. Configuration strings create successfully
4. Local IP retrieved via OTInetGetInterfaceInfo
5. Shutdown cleans up properly
6. **Hot/cold data separation working** - cold data allocated at init, freed at shutdown
7. **Endpoint pool tracks free slots** - O(1) allocation via bitmap
8. **Logging uses correct categories** - `PT_LOG_CAT_INIT` for startup/shutdown, `PT_LOG_CAT_MEMORY` for allocation, `PT_LOG_CAT_PLATFORM` for OT-specific operations
9. **Memory allocation failures logged** - `PT_LOG_ERR(ctx, PT_LOG_CAT_MEMORY, ...)` on NewPtrClear failure

#### Task 6.1.4: Add ISR Safety Compile-Time Test

Create `tests/test_isr_safety_ot.c` to provide static verification that PT_Log cannot be called from notifier context:

```c
/*
 * ISR Safety Compile-Time Test for Open Transport
 *
 * This file MUST NOT compile. It verifies that the PT_ISR_CONTEXT guard
 * macro correctly blocks PT_Log calls from notifier (deferred task) code.
 *
 * DO NOT add this to the Makefile test target - it's intentionally designed
 * to fail compilation as a safety check.
 */

#define PT_ISR_CONTEXT  /* Mark as deferred task context */
#include "pt_log.h"
#include <OpenTransport.h>
#include <OpenTptInternet.h>

/*
 * Example notifier callback that violates ISR safety
 *
 * If PT_ISR_CONTEXT guard is working, this should produce:
 * "error: PT_Log functions cannot be called at interrupt time"
 *
 * Note: Open Transport notifiers run at "deferred task time" not hardware
 * interrupt time, but have similar restrictions (no File Manager, no sync I/O,
 * etc). The PT_ISR_CONTEXT guard applies to both.
 */
static pascal void test_notifier_violates_isr_safety(
    void *contextPtr,
    OTEventCode code,
    OTResult result,
    void *cookie)
{
    PT_Log *log = (PT_Log *)contextPtr;

    /* These should cause compile errors */
    PT_LOG_ERR(log, PT_LOG_CAT_PLATFORM, "Notifier event: %ld", code);
    PT_LOG_INFO(log, PT_LOG_CAT_NETWORK, "Result: %ld", result);
    PT_LOG_DEBUG(log, PT_LOG_CAT_PLATFORM, "Cookie: %p", cookie);
}

int main(void) {
    /* This file should never reach the linker */
    return 0;
}
```

**Verification Steps:**

1. **Manual compile test:**
   ```bash
   # This should FAIL with error about PT_Log at interrupt time
   powerpc-apple-macos-gcc -c tests/test_isr_safety_ot.c -I include -I src/core
   ```

2. **Add to CI workflow** (`.github/workflows/ci.yml`):
   ```yaml
   - name: ISR safety check (Open Transport)
     run: |
       # Verify PT_ISR_CONTEXT blocks PT_Log calls
       if powerpc-apple-macos-gcc -c tests/test_isr_safety_ot.c -I include -I src/core 2>&1 | \
          grep -q "cannot be called at interrupt time"; then
         echo "✓ ISR safety guard working correctly"
       else
         echo "✗ ISR safety guard failed - PT_Log may be callable from notifiers!"
         exit 1
       fi
   ```

3. **Expected result:** Compilation fails with explicit error message about interrupt-time restrictions.

**Why This Matters for Open Transport:**

Open Transport notifiers run at "deferred task time" (Table C-3 functions), not hardware interrupt time (Table C-1). However, they still have severe restrictions:
- Cannot call File Manager (PT_Log writes to files)
- Cannot allocate memory reliably (PT_Log may allocate)
- Cannot call most Toolbox routines

The PT_ISR_CONTEXT guard treats deferred task time and hardware interrupt time the same—both are forbidden from calling PT_Log.

**Integration with Notifier Code:**

When implementing notifiers in Sessions 6.2-6.7, add `#define PT_ISR_CONTEXT` at the top of each notifier function to enable compile-time checking:

```c
static pascal void pt_tcp_notifier(...) {
    #define PT_ISR_CONTEXT  /* Enable ISR safety checks */

    /* Notifier implementation - PT_Log calls will fail to compile */
    PT_FLAG_SET(hot->flags, PT_FLAG_DATA_AVAILABLE);

    #undef PT_ISR_CONTEXT
}
```

This provides per-function ISR safety verification during development.

**Deferred Task vs. Hardware Interrupt:**

From Networking With Open Transport (lines 5793-5826):
- **Hardware interrupt time** (Table C-1): Most restricted
- **Deferred task time** (Table C-3): Notifiers run here, still restricted
- **System task time**: Normal app code, full API available

PT_ISR_CONTEXT blocks PT_Log for both hardware interrupt and deferred task contexts, since both share similar restrictions on File Manager and memory allocation.

---

## Session 6.2: UDP Endpoint

### Objective
Implement UDP endpoint for discovery with notifier for data arrival.

### Tasks

#### Task 6.2.1: Create `src/opentransport/udp_ot.c`

```c
#include "ot_defs.h"
#include "protocol.h"
#include "pt_internal.h"
#include "log.h"

/*
 * UDP Notifier function
 *
 * From NetworkingOpenTransport.txt Ch.3:
 * - Called at deferred task time (safer than MacTCP ASR)
 * - Still cannot allocate memory (except OTAllocMem - but must handle failure!)
 * - Cannot call synchronous OT/Device Manager/File Manager functions
 *
 * WARNING (from Ch.3 p.75): "Open Transport might call a notification
 * routine reentrantly." Write defensively - use atomic operations
 * (OTAtomicSetBit) rather than simple assignment for true safety.
 *
 * Context is pointer to pt_udp_endpoint_hot (compact for cache efficiency).
 */
static pascal void pt_udp_notifier(
    void *context,
    OTEventCode code,
    OTResult result,
    void *cookie)
{
    pt_udp_endpoint_hot *hot = (pt_udp_endpoint_hot *)context;

    switch (code) {
    case T_DATA:
        /* Data available to read - use atomic set for reentrancy safety */
        PT_FLAG_SET(hot->flags, PT_FLAG_DATA_AVAILABLE);
        break;

    case T_UDERR:
        /*
         * Unit data error - set flag for main loop to handle.
         * Per OT docs: "Failing to clear this leaves the endpoint in a state
         * where it cannot do other sends."
         *
         * NOTE: OTRcvUDErr IS in Table C-3 (deferred task functions) with
         * "asynchronous only" restriction, meaning it CAN technically be called
         * from notifiers when handling T_UDERR asynchronously. However, we
         * defer to the main loop for simplicity and consistency with CSend
         * reference implementation, which calls OTRcvUDErr from PollOTEvents().
         *
         * ARCHITECTURAL NOTE: CSend uses polling (OTLook from main loop) rather
         * than notifiers. This conservative "set flags, do nothing else" pattern
         * applies to BOTH architectures - keep event handlers simple regardless
         * of whether you're using notifiers or polling.
         */
        PT_FLAG_SET(hot->flags, PT_FLAG_UDERR_PENDING);
        break;

    case T_GODATA:
        /* Can send again after flow control */
        PT_FLAG_SET(hot->flags, PT_FLAG_SEND_COMPLETE);
        break;
    }
}

/*
 * Create and bind UDP endpoint
 *
 * Uses hot/cold separation:
 * - hot: od->udp_hot (polled frequently, ~16 bytes)
 * - cold: od->udp_cold (I/O buffers, ~2KB, allocated separately)
 */
int pt_ot_udp_create(struct pt_context *ctx, InetPort port) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_udp_endpoint_hot *hot = &od->udp_hot;
    pt_udp_endpoint_cold *cold = od->udp_cold;
    TBind req, ret;
    OSStatus err;

    if (hot->state != PT_EP_UNUSED) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_DISCOVERY,
            "UDP endpoint already exists");
        return -1;
    }

    /* Open endpoint
     *
     * NOTE: kENOMEMErr (-3211) may occur under memory pressure.
     * UDP endpoints are typically created once at init, so retry logic
     * is less critical than for TCP (which creates endpoints dynamically).
     * If robustness is needed, use the retry pattern from pt_ot_tcp_create().
     */
    hot->ref = OTOpenEndpoint(
        OTCloneConfiguration(od->udp_config),
        0,              /* oflag */
        NULL,           /* info */
        &err);

    if (err != noErr || !hot->ref) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_DISCOVERY,
            "OTOpenEndpoint for UDP failed: %d", err);
        return -1;
    }

    /* Install notifier using UPP for portability
     * Context is hot struct for cache efficiency in notifier.
     */
    err = OTInstallNotifier(hot->ref, od->udp_notifier_upp, hot);
    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_DISCOVERY,
            "OTInstallNotifier failed: %d", err);
        OTCloseProvider(hot->ref);
        hot->ref = NULL;
        return -1;
    }

    /* Set non-blocking mode */
    err = OTSetNonBlocking(hot->ref);
    if (err != noErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_DISCOVERY,
            "OTSetNonBlocking failed: %d", err);
    }

    /* Enable broadcasts */
    TOptMgmt optReq;
    TOption opt;
    opt.len = kOTFourByteOptionSize;
    opt.level = INET_IP;
    opt.name = IP_BROADCAST;
    opt.value[0] = 1;

    optReq.opt.buf = (UInt8 *)&opt;
    optReq.opt.len = sizeof(opt);
    optReq.flags = T_NEGOTIATE;

    OTOptionManagement(hot->ref, &optReq, NULL);

    /* Bind to port - use cold data for address storage */
    OTInitInetAddress(&cold->local_addr, port, kOTAnyInetAddress);

    req.addr.buf = (UInt8 *)&cold->local_addr;
    req.addr.len = sizeof(InetAddress);
    req.qlen = 0;  /* No queue for connectionless */

    ret.addr.buf = (UInt8 *)&cold->local_addr;
    ret.addr.maxlen = sizeof(InetAddress);

    err = OTBind(hot->ref, &req, &ret);

    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_DISCOVERY,
            "OTBind failed: %d", err);
        OTCloseProvider(hot->ref);
        hot->ref = NULL;
        return -1;
    }

    /* Clear flags using atomic-compatible clear */
    PT_FLAGS_CLEAR_ALL(hot->flags);
    hot->state = PT_EP_IDLE;

    PT_LOG_INFO(ctx, PT_LOG_CAT_DISCOVERY,
        "UDP endpoint created on port %u", port);

    return 0;
}

/*
 * Send UDP datagram
 */
int pt_ot_udp_send(struct pt_context *ctx,
                   InetHost dest_ip, InetPort dest_port,
                   const void *data, size_t len) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_udp_endpoint_hot *hot = &od->udp_hot;
    TUnitData udata;
    InetAddress dest_addr;
    OSStatus err;

    if (hot->state != PT_EP_IDLE)
        return -1;

    OTInitInetAddress(&dest_addr, dest_port, dest_ip);

    udata.addr.buf = (UInt8 *)&dest_addr;
    udata.addr.len = sizeof(InetAddress);
    udata.opt.buf = NULL;
    udata.opt.len = 0;
    udata.udata.buf = (UInt8 *)data;
    udata.udata.len = len;

    err = OTSndUData(hot->ref, &udata);

    if (err == kOTFlowErr) {
        /* Flow control - would block */
        return 0;
    }

    if (err != noErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_SEND,
            "OTSndUData failed: %d", err);
        return -1;
    }

    return 0;
}

/*
 * Receive UDP datagram (non-blocking)
 *
 * IMPORTANT: Must drain all available data to fully clear T_DATA event.
 * Per OT docs, continue calling OTRcvUData until kOTNoDataErr.
 * This function returns one datagram; caller should loop until it returns 0.
 *
 * Uses cold data for receive buffer (cache efficiency).
 */
int pt_ot_udp_recv(struct pt_context *ctx,
                   InetHost *from_ip, InetPort *from_port,
                   void *data, size_t *len) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_udp_endpoint_hot *hot = &od->udp_hot;
    pt_udp_endpoint_cold *cold = od->udp_cold;
    TUnitData udata;
    OTFlags flags = 0;
    OSStatus err;

    if (hot->state != PT_EP_IDLE)
        return 0;

    /* Setup receive - use cold data for address storage */
    udata.addr.buf = (UInt8 *)&cold->addr;
    udata.addr.maxlen = sizeof(InetAddress);
    udata.opt.buf = NULL;
    udata.opt.maxlen = 0;
    udata.udata.buf = data;
    udata.udata.maxlen = *len;

    err = OTRcvUData(hot->ref, &udata, &flags);

    if (err == kOTNoDataErr) {
        /* No more data - clear the flag now using atomic operation */
        PT_FLAG_CLEAR(hot->flags, PT_FLAG_DATA_AVAILABLE);
        return 0;
    }

    if (err == kOTLookErr) {
        /* Async event pending (e.g., T_UDERR) - notifier will handle it.
         *
         * NOTE: CSEND-LESSONS.md B.4 says TCP loops must check BOTH kOTNoDataErr
         * AND kOTLookErr as exit conditions. For UDP (connectionless), kOTLookErr
         * typically indicates T_UDERR only, not connection events like T_DISCONNECT.
         * We still handle it for completeness.
         */
        PT_FLAG_CLEAR(hot->flags, PT_FLAG_DATA_AVAILABLE);
        return 0;
    }

    if (err != noErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_RECV,
            "OTRcvUData failed: %d", err);
        return -1;
    }

    *from_ip = cold->addr.fHost;
    *from_port = cold->addr.fPort;
    *len = udata.udata.len;

    /* More data may be available - don't clear flag yet */
    return 1;
}

/*
 * Example caller pattern for draining:
 *
 * while (pt_ot_udp_recv(ctx, &ip, &port, buf, &len) > 0) {
 *     process_datagram(buf, len);
 *     len = sizeof(buf);  // reset for next call
 * }
 */

/*
 * Clear pending UDP error (MUST be called from main loop, NOT notifier)
 *
 * This handles T_UDERR events set by the notifier. OTRcvUDErr is NOT
 * interrupt-safe (not in Table C-1), so we defer it to the main loop.
 *
 * Call this from pt_ot_poll() before attempting new sends.
 */
static void pt_ot_udp_clear_error(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_udp_endpoint_hot *hot = &od->udp_hot;

    if (PT_FLAG_TEST(hot->flags, PT_FLAG_UDERR_PENDING)) {
        PT_FLAG_CLEAR(hot->flags, PT_FLAG_UDERR_PENDING);

        /* Clear the error condition - SAFE here in main loop */
        TUDErr udErr = {0};
        OSStatus err = OTRcvUDErr(hot->ref, &udErr);

        if (err == noErr) {
            PT_LOG_DEBUG(ctx, PT_LOG_CAT_NETWORK,
                "UDP error cleared: remote error %ld", udErr.error);
        }
        /* If err == kOTNoDataErr, no error was pending (race condition - OK) */
    }
}

/*
 * Close UDP endpoint
 */
void pt_ot_udp_close(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_udp_endpoint_hot *hot = &od->udp_hot;

    if (hot->ref) {
        OTUnbind(hot->ref);
        OTCloseProvider(hot->ref);
        hot->ref = NULL;
    }

    hot->state = PT_EP_UNUSED;
    PT_LOG_INFO(ctx, PT_LOG_CAT_DISCOVERY, "UDP endpoint closed");
}
```

### Acceptance Criteria
1. UDP endpoint opens and binds
2. Notifier installed and fires on data (using atomic flag operations)
3. Broadcast option set
4. Send works to broadcast address
5. Receive works with notifier flag check
6. Clean close with unbind
7. **Hot/cold separation** - hot data (~16 bytes) for polling, cold data (~2KB) for I/O
8. **T_UDERR handling** - Notifier sets `PT_FLAG_UDERR_PENDING`; `pt_ot_udp_clear_error()` calls `OTRcvUDErr()` from main loop (NOT from notifier)
9. **Logging** - Uses `PT_LOG_CAT_DISCOVERY` for UDP operations

---

## Session 6.3: TCP Endpoint

### Objective
Implement TCP endpoint creation and basic operations.

### Tasks

#### Task 6.3.1: Create `src/opentransport/tcp_ot.c`

```c
#include "ot_defs.h"
#include "pt_internal.h"
#include "log.h"

/*
 * TCP Notifier function
 *
 * From Ch.4 & Ch.5: Events for connection-oriented endpoints
 *
 * IMPORTANT: Uses atomic flag operations (OTAtomicSetBit) for reentrancy safety.
 * Per Ch.3 p.75: "Open Transport might call a notification routine reentrantly."
 *
 * Context is pt_tcp_endpoint_hot* for cache efficiency. The hot struct
 * contains endpoint_idx which can be used to access cold data if needed.
 *
 * Exception: When handling kOTProviderWillClose, you CAN call
 * synchronous OT functions because this event is only issued at
 * system task time.
 */
static pascal void pt_tcp_notifier(
    void *context,
    OTEventCode code,
    OTResult result,
    void *cookie)
{
    pt_tcp_endpoint_hot *hot = (pt_tcp_endpoint_hot *)context;

    switch (code) {
    case T_LISTEN:
        /* Incoming connection request */
        PT_FLAG_SET(hot->flags, PT_FLAG_LISTEN_COMPLETE);
        PT_FLAG_SET(hot->flags, PT_LOG_EVT_LISTEN);
        break;

    case T_CONNECT:
        /* Outgoing connection complete */
        hot->async_result = result;  /* Store for logging */
        PT_FLAG_SET(hot->flags, PT_FLAG_CONNECT_COMPLETE);
        PT_FLAG_SET(hot->flags, PT_LOG_EVT_CONNECT_DONE);
        if (result != kOTNoError) {
            hot->log_error_code = result;
            PT_FLAG_SET(hot->flags, PT_LOG_EVT_ERROR);
        }
        break;

    case T_DATA:
        /* Data available
         *
         * RACE CONDITION: Per NetworkingOpenTransport.txt p.493,
         * T_DATA may arrive before T_PASSCON for accepted endpoints.
         * If we're still in PT_EP_INCOMING state, defer processing.
         */
        if (hot->state == PT_EP_INCOMING) {
            PT_FLAG_SET(hot->flags, PT_FLAG_DEFER_DATA);
        } else {
            PT_FLAG_SET(hot->flags, PT_FLAG_DATA_AVAILABLE);
            PT_FLAG_SET(hot->flags, PT_LOG_EVT_DATA_ARRIVED);
        }
        break;

    case T_GODATA:
        /* Can send after flow control */
        PT_FLAG_SET(hot->flags, PT_FLAG_SEND_COMPLETE);
        PT_FLAG_SET(hot->flags, PT_LOG_EVT_FLOW_CONTROL);
        break;

    case T_DISCONNECT:
        /* Abortive disconnect */
        hot->log_error_code = result;  /* Store reason for logging */
        PT_FLAG_SET(hot->flags, PT_FLAG_DISCONNECT);
        PT_FLAG_SET(hot->flags, PT_LOG_EVT_DISCONNECT);
        break;

    case T_ORDREL:
        /* Orderly release (remote closed send side) */
        PT_FLAG_SET(hot->flags, PT_FLAG_ORDERLY_DISCONNECT);
        PT_FLAG_SET(hot->flags, PT_LOG_EVT_ORDERLY_REL);
        break;

    case T_PASSCON:
        /* Pass connection (for tilisten) */
        PT_FLAG_SET(hot->flags, PT_FLAG_PASSCON);
        break;

    case T_ACCEPTCOMPLETE:
        /* Accept completed */
        hot->async_result = result;
        PT_FLAG_SET(hot->flags, PT_FLAG_ACCEPT_COMPLETE);
        PT_FLAG_SET(hot->flags, PT_LOG_EVT_ACCEPT_DONE);
        break;

    case T_BINDCOMPLETE:
        /* Bind completed (async) */
        break;

    case T_OPENCOMPLETE:
        /* Open completed (async) */
        break;
    }
    /* NO PT_LOG_* calls here - notifier runs at deferred task time! */
}

/*
 * Create TCP endpoint (allocate from pool)
 *
 * Uses hot/cold separation:
 * - hot: od->tcp_hot[idx] (~28 bytes, polled frequently)
 * - cold: od->tcp_cold[idx] (~1.1KB, accessed during I/O)
 *
 * Returns endpoint index on success, -1 on failure.
 * Use pt_tcp_hot(od, idx) and pt_tcp_cold(od, idx) to access data.
 */
int pt_ot_tcp_create(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);
    OSStatus err;

    /* Allocate slot from pool - O(1) via bitmap */
    int idx = pt_endpoint_pool_alloc(&od->tcp_pool);
    if (idx < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No free TCP endpoint slots");
        return -1;
    }

    pt_tcp_endpoint_hot *hot = &od->tcp_hot[idx];
    pt_tcp_endpoint_cold *cold = &od->tcp_cold[idx];

    /* Clear structures */
    PT_FLAGS_CLEAR_ALL(hot->flags);
    hot->state = PT_EP_UNUSED;
    hot->peer = NULL;
    hot->endpoint_idx = idx;

    /* Open endpoint
     *
     * NOTE: kENOMEMErr (-3211) may occur under memory pressure.
     * Consider retry pattern for robustness:
     *
     * int retries = 3;
     * do {
     *     hot->ref = OTOpenEndpoint(...);
     *     if (err == kENOMEMErr && --retries > 0) {
     *         PT_LOG_WARN(ctx, PT_LOG_CAT_MEMORY, "OT out of memory, retrying...");
     *         // Optional: yield time for memory compaction
     *         unsigned long start = TickCount();
     *         while (TickCount() - start < 30) ; // ~0.5 second
     *     }
     * } while (err == kENOMEMErr && retries > 0);
     */
    hot->ref = OTOpenEndpoint(
        OTCloneConfiguration(od->tcp_config),
        0,
        NULL,
        &err);

    if (err != noErr || !hot->ref) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTOpenEndpoint for TCP failed: %d", err);
        pt_endpoint_pool_free(&od->tcp_pool, idx);
        return -1;
    }

    /* Install notifier using UPP for portability
     * Context is hot struct for cache efficiency in notifier.
     */
    err = OTInstallNotifier(hot->ref, od->tcp_notifier_upp, hot);
    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTInstallNotifier failed: %d", err);
        OTCloseProvider(hot->ref);
        hot->ref = NULL;
        pt_endpoint_pool_free(&od->tcp_pool, idx);
        return -1;
    }

    /* Set async mode with notifier */
    err = OTSetAsynchronous(hot->ref);
    if (err != noErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "OTSetAsynchronous failed: %d", err);
    }

    hot->state = PT_EP_UNBOUND;

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "TCP endpoint created: idx=%d ref=0x%08lX", idx, (unsigned long)hot->ref);

    return idx;
}

/*
 * Bind TCP endpoint to port
 *
 * Takes endpoint index (returned by pt_ot_tcp_create).
 */
int pt_ot_tcp_bind(struct pt_context *ctx, int idx, InetPort port, int qlen) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = &od->tcp_hot[idx];
    pt_tcp_endpoint_cold *cold = &od->tcp_cold[idx];
    TBind req, ret;
    OSStatus err;

    if (hot->state != PT_EP_UNBOUND) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "TCP endpoint not in unbound state");
        return -1;
    }

    /* Use cold data for address storage */
    OTInitInetAddress(&cold->local_addr, port, kOTAnyInetAddress);

    req.addr.buf = (UInt8 *)&cold->local_addr;
    req.addr.len = sizeof(InetAddress);
    req.qlen = qlen;  /* Listen queue length */

    ret.addr.buf = (UInt8 *)&cold->local_addr;
    ret.addr.maxlen = sizeof(InetAddress);

    err = OTBind(hot->ref, &req, &ret);

    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTBind failed: %d", err);
        return -1;
    }

    hot->state = PT_EP_IDLE;

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "TCP endpoint %d bound to port %u (qlen=%d)",
        idx, cold->local_addr.fPort, qlen);

    return 0;
}

/* Forward declaration */
static void pt_ot_tcp_cleanup(struct pt_context *ctx, int idx);

/*
 * Close TCP endpoint
 *
 * Takes endpoint index. Releases slot back to pool after cleanup.
 *
 * NOTE: OTSndOrderlyDisconnect can potentially delay if the remote
 * is unresponsive. Unlike MacTCP's TCPClose (which blocks until the
 * ULP timeout period expires - a user-configurable value), OT's orderly
 * disconnect is more graceful, but still monitor for slow completion
 * via T_ORDREL event in notifier.
 */
void pt_ot_tcp_close(struct pt_context *ctx, int idx) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = &od->tcp_hot[idx];

    if (!hot->ref)
        return;

    /* Send orderly disconnect if connected */
    if (hot->state == PT_EP_DATAXFER) {
        /*
         * Optional: Set XTI_LINGER to bound close time (prevents indefinite hang)
         *
         * TOptMgmt optReq;
         * struct {
         *     OTXTILevel level;
         *     OTXTIName  name;
         *     UInt32     status;
         *     struct t_linger linger;
         * } opt;
         * opt.level = XTI_GENERIC;
         * opt.name = XTI_LINGER;
         * opt.status = 0;
         * opt.linger.l_onoff = 1;
         * opt.linger.l_linger = 10;  // 10 seconds max linger
         * optReq.opt.buf = (UInt8*)&opt;
         * optReq.opt.len = sizeof(opt);
         * optReq.flags = T_NEGOTIATE;
         * OTOptionManagement(hot->ref, &optReq, NULL);
         */

        hot->state = PT_EP_CLOSING;
        hot->close_start = TickCount();  /* Track for timeout monitoring */

        OTResult err = OTSndOrderlyDisconnect(hot->ref);
        if (err == kOTFlowErr) {
            /* Flow controlled - queue is full, will complete later */
            PT_LOG_DEBUG(ctx, PT_LOG_CAT_CONNECT,
                "OTSndOrderlyDisconnect flow controlled");
        }
        /*
         * Wait for T_ORDREL in notifier, or timeout after 30 seconds
         * Main poll loop should check close_start and force abort if too slow:
         *
         * #define PT_CLOSE_TIMEOUT_TICKS (30 * 60)
         * if (hot->state == PT_EP_CLOSING &&
         *     (TickCount() - hot->close_start) > PT_CLOSE_TIMEOUT_TICKS) {
         *     PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT, "Close timeout, forcing abort");
         *     OTSndDisconnect(hot->ref, NULL);  // Abortive disconnect
         *     pt_ot_tcp_cleanup(ctx, idx);
         * }
         */
        return;
    }

    /* Already disconnected or never connected - proceed with cleanup */
    pt_ot_tcp_cleanup(ctx, idx);
}

/*
 * Final cleanup after orderly disconnect completes (or timeout)
 *
 * Releases endpoint slot back to pool for reuse.
 */
static void pt_ot_tcp_cleanup(struct pt_context *ctx, int idx) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = &od->tcp_hot[idx];

    /* Unbind */
    if (hot->ref && OTGetEndpointState(hot->ref) >= T_IDLE) {
        OTUnbind(hot->ref);
    }

    if (hot->ref) {
        OTCloseProvider(hot->ref);
        hot->ref = NULL;
    }
    hot->peer = NULL;
    hot->state = PT_EP_UNUSED;
    PT_FLAGS_CLEAR_ALL(hot->flags);

    /* Return slot to pool for reuse */
    pt_endpoint_pool_free(&od->tcp_pool, idx);

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT, "TCP endpoint %d closed and returned to pool", idx);
}

/*
 * Send data on TCP endpoint
 *
 * Takes endpoint index for hot/cold access.
 * Returns bytes sent, 0 if flow controlled, -1 on error.
 *
 * IMPORTANT: OTSnd returns OTResult (byte count on success, negative on error),
 * NOT OSStatus. Check for kOTFlowErr (negative) as flow control condition.
 *
 * Logs flow control at DEBUG level, errors at ERR level per CSEND-LESSONS.md
 * requirement for comprehensive send path logging.
 */
int pt_ot_tcp_send(struct pt_context *ctx, int idx,
                   const void *data, uint16_t len) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = &od->tcp_hot[idx];
    OTResult result;

    if (hot->state != PT_EP_DATAXFER) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_SEND,
            "TCP send on endpoint %d in wrong state: %d", idx, hot->state);
        return -1;
    }

    /* OTSnd returns byte count on success (OTResult), negative on error */
    result = OTSnd(hot->ref, (void *)data, len, 0);

    if (result == kOTFlowErr) {
        /* Flow controlled - send buffer full, wait for T_GODATA */
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_SEND,
            "TCP endpoint %d: flow control (kOTFlowErr), %u bytes queued", idx, len);
        return 0;
    }

    if (result < 0) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_SEND,
            "TCP OTSnd failed on endpoint %d: %ld", idx, result);
        return -1;
    }

    /* Success - log at DEBUG for high-volume tracing */
    PT_LOG_DEBUG(ctx, PT_LOG_CAT_SEND,
        "TCP endpoint %d: sent %ld bytes", idx, result);

    return result;  /* Returns byte count sent */
}
```

### Acceptance Criteria
1. TCP endpoint opens successfully (O(1) allocation from pool)
2. Notifier installed correctly (via UPP, uses atomic flags)
3. Async mode set
4. Bind to port works
5. Clean close with orderly disconnect
6. **Endpoint slot returned to pool** after close
7. **Hot/cold separation** - hot data for polling, cold for I/O

---

## Session 6.4: TCP Connect (Outgoing Connections)

### Objective
Implement outgoing TCP connections to discovered peers.

### Key Difference from MacTCP
- MacTCP used TCPActiveOpen with parameter blocks
- OT uses OTConnect with TCall structure
- Connection is async - wait for T_CONNECT event in notifier

### Tasks

#### Task 6.4.1: Create `src/opentransport/tcp_connect_ot.c`

```c
#include "ot_defs.h"
#include "peer.h"
#include "pt_internal.h"
#include "log.h"

#define PT_CONNECT_TIMEOUT_TICKS (30 * 60)  /* 30 seconds at 60 ticks/sec */

/*
 * Initiate outgoing TCP connection
 *
 * Uses O(1) bitmap allocation for endpoint slot, hot/cold separation
 * for cache efficiency.
 */
int pt_ot_tcp_connect(struct pt_context *ctx, struct pt_peer *peer) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot;
    pt_tcp_endpoint_cold *cold;
    TCall call;
    OSStatus err;
    int ep_idx;

    if (!peer || peer->connection_idx >= 0)
        return -1;

    /* Allocate endpoint from pool using O(1) bitmap lookup */
    ep_idx = pt_endpoint_pool_alloc(&od->tcp_pool);
    if (ep_idx < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No free endpoint for connection (pool exhausted)");
        return -1;
    }

    hot = &od->tcp_hot[ep_idx];
    cold = &od->tcp_cold[ep_idx];

    /* Create endpoint */
    if (pt_ot_tcp_create(ctx, ep_idx) < 0) {
        pt_endpoint_pool_free(&od->tcp_pool, ep_idx);
        return -1;
    }

    /*
     * Note: Do NOT bind before connect for outgoing connections.
     * Per Networking With Open Transport p.109:
     * "If you do not need to connect from a specific port, you can
     * skip the bind step and let the endpoint provider bind the
     * endpoint automatically when you call OTConnect."
     */

    /* Setup call structure for connect */
    OTInitInetAddress(&cold->remote_addr, peer->port, peer->addr);

    call.addr.buf = (UInt8 *)&cold->remote_addr;
    call.addr.len = sizeof(InetAddress);
    call.opt.buf = NULL;
    call.opt.len = 0;
    call.udata.buf = NULL;
    call.udata.len = 0;
    call.sequence = 0;

    /* Track connection start for timeout (stored in cold data) */
    cold->close_start = TickCount();
    cold->pool_index = ep_idx;
    cold->peer = peer;

    hot->state = PT_EP_OUTGOING;
    peer->connection_idx = ep_idx;

    /* Initiate async connect */
    err = OTConnect(hot->ref, &call, NULL);

    if (err != noErr && err != kOTNoDataErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTConnect failed: %d", err);
        cold->peer = NULL;
        peer->connection_idx = -1;
        pt_ot_tcp_close(ctx, ep_idx);
        return -1;
    }

    /* kOTNoDataErr is expected for async connect - wait for T_CONNECT */
    char ip_str[32];
    OTInetHostToString(peer->addr, ip_str);
    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Connecting to %s:%u...", ip_str, peer->port);

    return 0;
}

/*
 * Poll for connection completion
 *
 * Returns: 1 if a connection completed, 0 if nothing happened, -1 on error
 *
 * Cache-efficient optimizations:
 * 1. Uses bitmap to iterate ONLY active slots (O(active) not O(PT_MAX_PEERS))
 * 2. First pass scans hot data only for state check
 * 3. Cold data accessed only when processing is needed
 *
 * The bitmap iteration is especially important on Classic Mac where
 * PT_MAX_PEERS might be 16 but only 2-3 slots are typically active.
 */
int pt_ot_connect_poll(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);
    unsigned long now = TickCount();
    int result = 0;

    /* Iterate only active slots using bitmap
     * ~tcp_pool.free_bitmap gives us bits set for IN-USE slots
     */
    uint32_t active = ~od->tcp_pool.free_bitmap & ((1u << od->tcp_pool.capacity) - 1);
    while (active) {
        /* Find first set bit (first active slot) */
        #if defined(__GNUC__) && (defined(__powerpc__) || defined(__ppc__))
            int i = __builtin_ffs(active) - 1;
        #else
            int i = 0;
            uint32_t tmp = active;
            while ((tmp & 1) == 0) { tmp >>= 1; i++; }
        #endif

        /* Clear this bit so we don't process it again */
        active &= ~(1u << i);

        pt_tcp_endpoint_hot *hot = &od->tcp_hot[i];

        /* Quick state check on hot data - no cold access needed */
        if (hot->state != PT_EP_OUTGOING)
            continue;

        /* Now access cold data for timeout check and peer info */
        pt_tcp_endpoint_cold *cold = &od->tcp_cold[i];
        struct pt_peer *peer = cold->peer;

        /* Check for connection timeout */
        if ((now - cold->close_start) > PT_CONNECT_TIMEOUT_TICKS) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "Connection timeout");

            if (peer) {
                peer->connection_idx = -1;
                pt_peer_set_state(peer, PT_PEER_DISCOVERED);
            }

            cold->peer = NULL;
            pt_ot_tcp_close(ctx, i);
            continue;
        }

        /* Check for T_CONNECT event using atomic test */
        if (PT_FLAG_TEST(hot->flags, PT_FLAG_CONNECT_COMPLETE)) {
            PT_FLAG_CLEAR(hot->flags, PT_FLAG_CONNECT_COMPLETE);

            /* Complete the connection */
            TCall ret;
            InetAddress ret_addr;

            ret.addr.buf = (UInt8 *)&ret_addr;
            ret.addr.maxlen = sizeof(InetAddress);
            ret.opt.buf = NULL;
            ret.opt.maxlen = 0;
            ret.udata.buf = NULL;
            ret.udata.maxlen = 0;

            OSStatus err = OTRcvConnect(hot->ref, &ret);

            if (err != noErr) {
                PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
                    "OTRcvConnect failed: %d", err);

                if (peer) {
                    peer->connection_idx = -1;
                    pt_peer_set_state(peer, PT_PEER_DISCOVERED);
                }

                cold->peer = NULL;
                pt_ot_tcp_close(ctx, i);
                continue;
            }

            /* Connection established */
            hot->state = PT_EP_DATAXFER;

            if (peer) {
                pt_peer_set_state(peer, PT_PEER_CONNECTED);
                peer->last_seen = now;

                if (ctx->callbacks.on_peer_connected) {
                    ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                                     peer->id,
                                                     ctx->callbacks.user_data);
                }
            }

            char ip_str[32];
            OTInetHostToString(cold->remote_addr.fHost, ip_str);
            PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
                "Connected to %s:%u", ip_str, cold->remote_addr.fPort);

            result = 1;
        }

        /* Check for connection failure (T_DISCONNECT during connect) */
        if (PT_FLAG_TEST(hot->flags, PT_FLAG_DISCONNECT)) {
            PT_FLAG_CLEAR(hot->flags, PT_FLAG_DISCONNECT);

            /* Must retrieve and discard disconnect info */
            OTRcvDisconnect(hot->ref, NULL);

            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "Connection rejected");

            if (peer) {
                peer->connection_idx = -1;
                pt_peer_set_state(peer, PT_PEER_DISCOVERED);
            }

            cold->peer = NULL;
            pt_ot_tcp_close(ctx, i);
        }
    }

    return result;
}
```

### Acceptance Criteria
1. OTConnect initiates async connection
2. T_CONNECT event detected in notifier
3. OTRcvConnect completes handshake
4. Connection timeout works (30s) **with logging**
5. T_DISCONNECT during connect handled
6. Peer state updated correctly
7. connected_cb fires on success
8. **Bitmap-optimized polling** - Iterates only active slots via `~tcp_pool.free_bitmap`
9. **State transition logging** - Uses `PT_LOG_DEBUG(ctx, PT_LOG_CAT_PLATFORM, ...)`

---

## Session 6.5: TCP Server (tilisten Pattern)

### Objective
Implement the "tilisten" pattern for accepting multiple simultaneous connections.

### Key Pattern from NetworkingOpenTransport.txt Ch.5:
> "To handle multiple simultaneous connection requests, you can use a technique called 'tilisten'."

The pattern is:
1. Create listener endpoint with qlen > 0
2. When T_LISTEN fires, call OTListen to get pending call
3. Create new endpoint for the connection
4. Call OTAccept with listener and new endpoint
5. New endpoint transitions to T_DATAXFER

### Tasks

#### Task 6.5.1: Create `src/opentransport/tcp_server_ot.c`

```c
#include "ot_defs.h"
#include "peer.h"
#include "pt_internal.h"
#include "log.h"

#define LISTEN_QLEN 4  /* Queue 4 pending connections */

/*
 * Check OT version for tilisten support (requires 1.1.1+)
 *
 * From Networking With Open Transport documentation: the tilisten
 * pattern requires OT 1.1.1 or later for reliable operation.
 *
 * Returns: true if tilisten is supported, false otherwise
 */
static Boolean pt_ot_supports_tilisten(void) {
    NumVersionVariant vers;
    OSStatus err;

    err = Gestalt(gestaltOpenTptVersions, (long *)&vers);
    if (err != noErr) {
        return false;
    }

    /*
     * Version format: major.minor.bug in BCD
     * 1.1.1 = 0x01118000 (majorRev=1, minorAndBugRev=0x11)
     * 1.1.2 = 0x01128000
     * 1.3.0 = 0x01300000
     *
     * We need majorRev >= 1 AND minorAndBugRev >= 0x11 (1.1)
     */
    if (vers.parts.majorRev > 1) {
        return true;  /* 2.x or later */
    }
    if (vers.parts.majorRev == 1 && vers.parts.minorAndBugRev >= 0x11) {
        return true;  /* 1.1.x or later */
    }

    return false;  /* 1.0.x - tilisten unreliable */
}

/*
 * Start TCP listener using tilisten pattern
 *
 * Note: If OT < 1.1.1, tilisten may be unreliable. In practice,
 * all shipping Macs with OT (System 7.6.1+) have 1.1.1 or later,
 * so this check is mainly for edge cases with manual OT installs.
 */
int pt_ot_listen_start(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = &od->listener_hot;
    pt_tcp_endpoint_cold *cold = od->listener_cold;
    InetPort port;
    int ep_idx;

    /* Check tilisten support and log warning if not available */
    if (!pt_ot_supports_tilisten()) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_INIT,
            "OT < 1.1.1 detected - tilisten may be unreliable");
    }

    /* Create listener endpoint - returns index into hot array */
    ep_idx = pt_ot_tcp_create_listener(ctx);
    if (ep_idx < 0) {
        return -1;
    }

    /* Bind with listen queue */
    port = ctx->config.tcp_port > 0 ? ctx->config.tcp_port : PT_DEFAULT_TCP_PORT;

    if (pt_ot_tcp_bind_listener(ctx, port, LISTEN_QLEN) < 0) {
        pt_ot_tcp_close_listener(ctx);
        return -1;
    }

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "TCP listener started on port %u (qlen=%d)", port, LISTEN_QLEN);

    return 0;
}

/*
 * Poll for incoming connections
 *
 * This implements the tilisten pattern:
 * 1. Check for T_LISTEN event (via atomic flag test)
 * 2. Call OTListen to get connection info
 * 3. Allocate endpoint from pool (O(1) via bitmap)
 * 4. Create new endpoint for connection
 * 5. Call OTAccept to complete handoff
 */
int pt_ot_listen_poll(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *listener = &od->listener_hot;
    pt_tcp_endpoint_hot *client_hot;
    pt_tcp_endpoint_cold *client_cold;
    struct pt_peer *peer;
    TCall call;
    InetAddress from_addr;
    OSStatus err;
    int client_idx;

    if (!listener->ref || listener->state != PT_EP_IDLE)
        return 0;

    /* Check for T_LISTEN event using atomic test */
    if (!PT_FLAG_TEST(listener->flags, PT_FLAG_LISTEN_PENDING))
        return 0;

    PT_FLAG_CLEAR(listener->flags, PT_FLAG_LISTEN_PENDING);

    /* Setup call structure for OTListen */
    call.addr.buf = (UInt8 *)&from_addr;
    call.addr.maxlen = sizeof(InetAddress);
    call.opt.buf = NULL;
    call.opt.maxlen = 0;
    call.udata.buf = NULL;
    call.udata.maxlen = 0;

    /* Get pending connection info */
    err = OTListen(listener->ref, &call);

    if (err == kOTNoDataErr) {
        /* No pending connection (race condition) */
        return 0;
    }

    if (err != noErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "OTListen failed: %d", err);
        return -1;
    }

    char ip_str[32];
    OTInetHostToString(from_addr.fHost, ip_str);
    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Incoming connection from %s:%u",
        ip_str, from_addr.fPort);

    /* Allocate client endpoint from pool using O(1) bitmap lookup */
    client_idx = pt_endpoint_pool_alloc(&od->tcp_pool);
    if (client_idx < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No free endpoint for connection (pool exhausted)");
        /* Reject connection */
        OTSndDisconnect(listener->ref, &call);
        return 0;
    }

    client_hot = &od->tcp_hot[client_idx];
    client_cold = &od->tcp_cold[client_idx];

    /* Create client endpoint */
    if (pt_ot_tcp_create(ctx, client_idx) < 0) {
        pt_endpoint_pool_free(&od->tcp_pool, client_idx);
        OTSndDisconnect(listener->ref, &call);
        return 0;
    }

    /*
     * Note: Do NOT bind the client endpoint here.
     * Per Networking With Open Transport p.112-113:
     * "If the endpoint is not bound, the endpoint provider automatically
     * binds it to the address of the endpoint that listened for the
     * connection request."
     */

    /* Accept the connection, handing off to client endpoint */
    client_cold->remote_addr = from_addr;
    client_hot->state = PT_EP_INCOMING;

    err = OTAccept(listener->ref, client_hot->ref, &call);

    if (err != noErr && err != kOTNoDataErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTAccept failed: %d", err);
        pt_ot_tcp_close(ctx, client_idx);
        return -1;
    }

    /* OTAccept initiates async handoff. T_PASSCON signals completion on
     * the client endpoint, T_ACCEPTCOMPLETE on the listener.
     *
     * We pre-emptively set DATAXFER state here rather than waiting for
     * T_PASSCON because:
     * 1. The handoff is virtually instant on local networks
     * 2. It simplifies the state machine (no PT_EP_ACCEPTING state needed)
     * 3. The defer_data mechanism handles the T_DATA-before-T_PASSCON race
     *
     * Per NetworkingOpenTransport.txt p.493: "It is possible, in the case
     * where the listening and accepting endpoints are different, that the
     * accepting endpoint receives a T_DATA event before receiving the
     * T_PASSCON event." The notifier sets defer_data=true if this happens.
     */
    client_hot->state = PT_EP_DATAXFER;

    /* Check if we received T_DATA before T_PASSCON (race condition) */
    if (PT_FLAG_TEST(client_hot->flags, PT_FLAG_DEFER_DATA)) {
        PT_FLAG_CLEAR(client_hot->flags, PT_FLAG_DEFER_DATA);
        PT_FLAG_SET(client_hot->flags, PT_FLAG_DATA_AVAILABLE);
    }

    /* Store endpoint index for peer lookup */
    client_cold->pool_index = client_idx;

    /* Find or create peer */
    peer = pt_peer_find_by_addr(ctx, from_addr.fHost, 0);
    if (!peer) {
        peer = pt_peer_create(ctx, "", from_addr.fHost, from_addr.fPort);
    }

    if (peer) {
        peer->connection_idx = client_idx;  /* Store index, not pointer */
        client_cold->peer = peer;
        pt_peer_set_state(peer, PT_PEER_CONNECTED);
        peer->last_seen = TickCount();

        if (ctx->callbacks.on_peer_connected) {
            ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                             peer->id,
                                             ctx->callbacks.user_data);
        }
    }

    return 1;
}

/*
 * Stop listener
 */
void pt_ot_listen_stop(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_ot_tcp_close_listener(ctx);
    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT, "Listener stopped");
}
```

### Acceptance Criteria
1. Listener binds with qlen > 0
2. T_LISTEN event detected
3. OTListen retrieves pending call
4. New endpoint created for connection
5. OTAccept completes handoff
6. Peer created and callback fires
7. T_DATA before T_PASSCON race condition handled (defer_data flag)

---

## Session 6.6: Integration

### Objective
Integrate all OT components and implement main poll loop.

### Tasks

#### Task 6.6.1: Create main poll function

```c
/* src/opentransport/poll_ot.c */

/*
 * TCP receive helper - returns bytes received, 0 if no data, -1 on error.
 * Caller must loop until 0 to fully drain T_DATA event.
 *
 * Uses hot/cold separation: hot data for flag checks, cold for buffers.
 */
static int pt_ot_tcp_recv(struct pt_context *ctx, int ep_idx) {
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = &od->tcp_hot[ep_idx];
    pt_tcp_endpoint_cold *cold = &od->tcp_cold[ep_idx];
    OTFlags flags = 0;
    OTResult result;

    if (!hot->ref)
        return -1;

    /* Use cold data buffer for receive */
    result = OTRcv(hot->ref, cold->recv_buf, sizeof(cold->recv_buf), &flags);

    if (result == kOTNoDataErr) {
        return 0;  /* No more data */
    }

    if (result < 0) {
        if (result == kOTLookErr) {
            /* Event pending - check with OTLook */
            return 0;
        }
        PT_LOG_WARN(ctx, PT_LOG_CAT_RECV,
            "OTRcv failed: %d", result);
        return -1;
    }

    /* Process received data */
    if (result > 0 && ctx->callbacks.on_message_received) {
        /* Parse and deliver message via callback */
        pt_protocol_handle_data(ctx, cold->peer, cold->recv_buf, result);
    }

    return result;  /* Bytes received */
}

/*
 * Main poll function - process all OT events
 *
 * Cache-efficient design:
 * - First pass: scan hot data array only (flags, state) - fits in cache line
 * - Only access cold data when processing is needed
 * - Batch operations to minimize cache thrashing
 */
int pt_ot_poll(struct pt_context *ctx) {
    pt_ot_data *od = pt_ot_get(ctx);
    unsigned long now = TickCount();
    int i;

    /* Process UDP discovery */
    pt_ot_discovery_poll(ctx);

    /* Process listener */
    pt_ot_listen_poll(ctx);

    /* Process connecting endpoints */
    pt_ot_connect_poll(ctx);

    /*
     * Process connected endpoints using bitmap iteration + hot/cold separation.
     *
     * O(active) not O(PT_MAX_PEERS): Only iterate slots in use via bitmap.
     * Hot array iteration: only touch 32-byte hot structs for flag checks.
     * Cold data access: only when needed for actual I/O operations.
     */
    uint32_t active = pt_endpoint_pool_active_mask(&od->tcp_pool);
    while (active) {
        /* Find first set bit (first active slot) */
        #if defined(__GNUC__) && (defined(__powerpc__) || defined(__ppc__))
            int i = __builtin_ffs(active) - 1;
        #else
            int i = 0;
            uint32_t tmp = active;
            while ((tmp & 1) == 0) { tmp >>= 1; i++; }
        #endif
        active &= ~(1u << i);

        pt_tcp_endpoint_hot *hot = &od->tcp_hot[i];

        /* Quick state check on hot data - no cold access needed */
        if (hot->state != PT_EP_DATAXFER)
            continue;

        /* Now access cold data for peer and buffer operations */
        pt_tcp_endpoint_cold *cold = &od->tcp_cold[i];
        struct pt_peer *peer = cold->peer;
        if (!peer)
            continue;

        /* Check for abortive disconnect (T_DISCONNECT) using atomic test */
        if (PT_FLAG_TEST(hot->flags, PT_FLAG_DISCONNECT)) {
            PT_FLAG_CLEAR(hot->flags, PT_FLAG_DISCONNECT);

            /* Must retrieve and clear disconnect info */
            OTRcvDisconnect(hot->ref, NULL);

            if (ctx->callbacks.on_peer_disconnected) {
                ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                                    peer->id, PEERTALK_ERR_NONE,
                                                    ctx->callbacks.user_data);
            }

            peer->connection_idx = -1;
            pt_peer_destroy(ctx, peer);
            pt_ot_tcp_close(ctx, i);
            continue;
        }

        /* Check for orderly disconnect (T_ORDREL)
         *
         * Per NetworkingOpenTransport.txt p.516-517:
         * "You call the OTRcvOrderlyDisconnect function to acknowledge the
         * receipt of an orderly disconnect event. After using the
         * OTRcvOrderlyDisconnect function, there will not be any more data
         * to receive."
         *
         * Sequence:
         * 1. Drain any remaining data first
         * 2. Call OTRcvOrderlyDisconnect to acknowledge
         * 3. Send our orderly disconnect
         * 4. Clean up
         */
        if (PT_FLAG_TEST(hot->flags, PT_FLAG_ORDERLY_RELEASE)) {
            PT_FLAG_CLEAR(hot->flags, PT_FLAG_ORDERLY_RELEASE);

            /* Drain any remaining data before acknowledging disconnect */
            while (pt_ot_tcp_recv(ctx, i) > 0) { }

            /* REQUIRED: Acknowledge the orderly disconnect */
            OTRcvOrderlyDisconnect(hot->ref);

            /* Send our orderly disconnect to complete the handshake */
            OTSndOrderlyDisconnect(hot->ref);

            if (ctx->callbacks.on_peer_disconnected) {
                ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                                    peer->id, PEERTALK_ERR_NONE,
                                                    ctx->callbacks.user_data);
            }

            peer->connection_idx = -1;
            pt_peer_destroy(ctx, peer);
            pt_ot_tcp_close(ctx, i);
            continue;
        }

        /* Receive data - drain until kOTNoDataErr to clear T_DATA */
        while (pt_ot_tcp_recv(ctx, i) > 0) {
            /* Process each chunk; loop drains the buffer */
        }
        PT_FLAG_CLEAR(hot->flags, PT_FLAG_DATA_AVAILABLE);
    }

    /* Periodic announce */
    if (ctx->discovery_active &&
        (now - od->last_announce_tick) > 10 * 60) {
        pt_ot_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);
        od->last_announce_tick = now;
    }

    return 0;
}
```

### Acceptance Criteria
1. All OT components work together
2. Discovery finds peers
3. Connections established
4. Messages exchanged
5. Orderly disconnect handled correctly:
   - OTRcvOrderlyDisconnect called to acknowledge T_ORDREL
   - Data drained before acknowledging
   - OTSndOrderlyDisconnect sent to complete handshake
   - Timeout monitoring for unresponsive hosts (30s max)
6. **MUST verify on REAL PPC Mac hardware** (Power Mac, iMac G3/G4, etc.)
7. No memory leaks: **Check MaxBlock before/after 50+ operations** - values must match
8. Close timeout works (30s max wait for unresponsive hosts)
9. **Cross-platform interop with POSIX:** OT peer discovers POSIX peer and vice versa
10. **Cross-platform interop with MacTCP:** OT peer discovers MacTCP peer (if available)
11. **Cross-platform messaging:** Send message from POSIX→OT and OT→POSIX
12. **T_DATA before T_PASSCON race condition:** Server properly defers data until accept completes

**MaxBlock Verification Code:**
```c
/* Run this test on real hardware to verify no leaks */
void pt_ot_leak_test(struct pt_context *ctx) {
    long block_before = MaxBlock();
    long free_before = FreeMem();
    int i;

    PT_LOG_INFO(ctx, PT_LOG_CAT_MEMORY,
        "OT Leak test start: MaxBlock=%ld FreeMem=%ld", block_before, free_before);

    /* Perform 50 connect/disconnect cycles */
    for (i = 0; i < 50; i++) {
        /* ... create endpoint, connect, disconnect, close ... */
    }

    long block_after = MaxBlock();
    long free_after = FreeMem();

    PT_LOG_INFO(ctx, PT_LOG_CAT_MEMORY,
        "OT Leak test end: MaxBlock=%ld (delta=%ld) FreeMem=%ld (delta=%ld)",
        block_after, block_after - block_before,
        free_after, free_after - free_before);

    /* MaxBlock should be same or higher */
    if (block_after < block_before - 1024) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_MEMORY, "WARNING: Possible memory leak!");
    }
}
```

---

## Emulator-Based Smoke Tests (Build Validation)

**Purpose:** Verify Open Transport builds execute without crashes. Does NOT replace real hardware testing.

**Critical Limitation:** Open Transport emulators (SheepShaver, Basilisk II with OT) are useful for build validation but should be followed by verification on real PPC hardware when possible.

**Emulator Capabilities:**

| Emulator | System | OT Version | Use Case | Limitations |
|----------|--------|------------|----------|-------------|
| **SheepShaver** | Mac OS 9 | 2.7+ | PPC emulation, good OT support | Timing differs, network quirks |
| **Basilisk II** | Mac OS 8.6+ | 2.5+ | 68k/PPC hybrid, basic OT | Limited network, timing differs |

**What Emulators CAN Verify:**
- ✅ Application launches without crash
- ✅ PT_Log file created successfully
- ✅ InitOpenTransport succeeds
- ✅ Endpoint creation works (OTOpenEndpoint returns valid ref)
- ✅ Basic API calls don't crash immediately
- ✅ Gestalt checks work correctly
- ✅ Memory allocation patterns work

**What Emulators CANNOT Fully Verify:**
- ⚠️ Discovery (network behavior may differ)
- ⚠️ Cross-platform interop (emulator networking is limited)
- ⚠️ Timing-sensitive operations (notifier timing differs)
- ⚠️ Real-world performance characteristics
- ⚠️ Memory fragmentation under load

**Emulator Smoke Test Workflow:**

```bash
# 1. Build for Open Transport
make opentransport

# 2. Transfer to SheepShaver shared folder
cp build/opentransport/PeerTalk.bin /path/to/sheepshaver/shared/

# 3. Launch in emulator and check for:
#    - No crash on startup
#    - PT_Log file appears
#    - No error dialogs
#    - Application responds
```

**Emulator Smoke Test Checklist:**
- [ ] Application launches (no bus error or unimplemented trap)
- [ ] PT_Log file created in System Folder
- [ ] About box displays correct version
- [ ] InitOpenTransport succeeds (check PT_Log for success message)
- [ ] FreeMem shows reasonable values
- [ ] Quit works cleanly
- [ ] No "Application has unexpectedly quit" dialogs

**When to Use Emulators:**

1. **During development:** Quick iteration on build issues
2. **Pre-hardware validation:** Smoke test before deploying to real PPC Mac
3. **Regression testing:** Quick check that changes didn't break basic functionality
4. **NOT for final acceptance:** Real hardware testing is strongly recommended

**Real Hardware Testing Priority:**

Open Transport runs on a wide range of hardware:
- Power Mac G3/G4 (primary targets - good OT support)
- iMac G3/G4 (excellent OT support)
- PowerBook G3/G4 (portable testing)
- Late 68k Macs with OT (limited, prefer PPC)

Unlike MacTCP (which MUST be tested on real SE/30 due to severe hardware differences), Open Transport on emulators is more reliable. However, real hardware testing is still recommended for:
- Cross-platform interop verification
- Performance tuning
- Final acceptance testing

**Integration with Hardware Testing:**

Emulator smoke tests are a **pre-flight check**. The workflow is:

```
Code Change → Emulator Smoke Test → Real Hardware Test (if available) → DONE
                     ↓ (if fail)              ↓ (if fail)
                Fix Build Issue         Fix Hardware-Specific Issue
```

For Open Transport, emulator testing is more reliable than for MacTCP, so the "Real Hardware Test" step is **recommended** but not **mandatory** if emulator tests pass comprehensively.

---

## Session 6.7: Multi-Transport Types & Peer Management

### Objective
Define types, structures, and peer management logic for multi-transport support.

### Key Design Decisions

**Unified Peer List:**
The SDK maintains ONE peer list, not separate lists per transport. Each peer can be reachable via multiple transports simultaneously.

**Peer Deduplication:**
The same physical Mac might be discovered via:
- UDP broadcast (TCP/IP) → discovers "Alice's Mac" at 192.168.1.42
- NBP lookup (AppleTalk) → discovers "Alice's Mac" at network 65280, node 42

The SDK must recognize these as the **same peer** and merge them, not create duplicates.

**Deduplication Strategy:**
1. **Name matching** - If NBP name matches existing peer name (case-insensitive)
2. **Address correlation** - Same Ethernet MAC address (if available from ARP/AARP)
3. **User hint** - Application can manually merge peers via API

**Transport Preference:**
When a peer is reachable via multiple transports, which one to use?
- Default: TCP/IP preferred (better for POSIX interop)
- Configurable: Application can set preference per-peer or globally
- Fallback: If preferred transport disconnects, try alternate

### Background

Open Transport provides a **unified endpoint API** for both TCP/IP and AppleTalk protocols. From `NetworkingOpenTransport.txt`:

> "You use these same functions whether you are using TCP, ADSP, or any other Open Transport connection-oriented, transactionless protocol."

This means we can support both TCP/IP (for POSIX interop) and ADSP (for AppleTalk-only Macs) using the same OT patterns, just with different configuration strings and address types.

### Tasks

#### Task 6.7.1: Extend `include/peertalk.h` with transport flags and peer management

```c
/*============================================================================
 * Transport Selection Flags
 *============================================================================*/

#define PT_TRANSPORT_TCP    0x0001  /* TCP/IP connections */
#define PT_TRANSPORT_UDP    0x0002  /* UDP discovery/messaging */
#define PT_TRANSPORT_ADSP   0x0004  /* AppleTalk ADSP connections */
#define PT_TRANSPORT_NBP    0x0008  /* AppleTalk NBP discovery */

/* Common combinations */
#define PT_TRANSPORT_TCPIP  (PT_TRANSPORT_TCP | PT_TRANSPORT_UDP)
#define PT_TRANSPORT_ATALK  (PT_TRANSPORT_ADSP | PT_TRANSPORT_NBP)
#define PT_TRANSPORT_ALL    (PT_TRANSPORT_TCPIP | PT_TRANSPORT_ATALK)

/* Transport preference for multi-homed peers */
typedef enum {
    PT_PREFER_TCP,      /* Prefer TCP/IP (better POSIX interop) - DEFAULT */
    PT_PREFER_ADSP,     /* Prefer AppleTalk (better for Mac-only networks) */
    PT_PREFER_FASTEST,  /* Use whichever responds first */
    PT_PREFER_NONE      /* No preference - use first available */
} PeerTalk_TransportPref;

/*============================================================================
 * Extended Configuration
 *
 * BACKWARD COMPATIBILITY: Applications using the simpler config from
 * PROJECT_GOALS.md continue to work without modification:
 * - If transports=0, defaults to PT_TRANSPORT_TCPIP (TCP+UDP)
 * - If pref=0 (PT_PREFER_TCP), TCP/IP is preferred (default behavior)
 * - All new AppleTalk fields default to sensible values when zero-initialized
 * - nbp_type defaults to "PeerTalk", nbp_zone defaults to "*"
 *============================================================================*/

typedef struct {
    const char *local_name;
    uint16_t max_peers;
    int log_level;

    /* Transport selection (0 = default to PT_TRANSPORT_TCPIP) */
    uint32_t transports;            /* PT_TRANSPORT_* flags */
    PeerTalk_TransportPref pref;    /* Default transport preference */

    /* TCP/IP settings */
    uint16_t tcp_port;
    uint16_t udp_port;

    /* AppleTalk settings */
    char nbp_type[33];              /* NBP type (default: "PeerTalk") */
    char nbp_zone[33];              /* NBP zone (default: "*") */

    /* Gateway mode */
    int enable_gateway;             /* Relay messages between transports */

    /* Peer deduplication */
    int auto_merge_peers;           /* Auto-merge same-named peers (default: 1) */

    /* Callbacks */
    PeerTalk_Callbacks callbacks;
    void *user_data;
} PeerTalk_Config;

/*============================================================================
 * Extended Peer Info - Multi-Transport Aware
 *============================================================================*/

typedef struct {
    PeerTalk_PeerID id;
    char name[PT_MAX_PEER_NAME + 1];

    /* Transport availability (bitfield - peer may be on MULTIPLE transports) */
    uint32_t available_transports;  /* Which transports can reach this peer */
    uint32_t connected_transport;   /* Which transport we're currently using (0 if not connected) */

    /* Connection state */
    int connected;
    int connecting;

    /* TCP/IP address (valid if available_transports & PT_TRANSPORT_TCP) */
    uint32_t ip_addr;
    char ip_str[16];
    uint16_t tcp_port;

    /* AppleTalk address (valid if available_transports & PT_TRANSPORT_ADSP) */
    uint16_t at_network;
    uint8_t at_node;
    uint8_t at_socket;
    char at_zone[33];               /* Zone name from NBP */

    /* Discovery source tracking */
    uint32_t discovered_via;        /* How was this peer found (UDP, NBP, or both) */
    unsigned long last_seen_tcp;    /* Last UDP announcement (TickCount) */
    unsigned long last_seen_nbp;    /* Last NBP lookup (TickCount) */

    /* For gateway: is this peer reachable via another peer? */
    int reachable_via_gateway;
    PeerTalk_PeerID gateway_peer;   /* Which peer can relay to this one */
} PeerTalk_PeerInfo;

/*============================================================================
 * Extended Callbacks - Transport Aware
 *============================================================================*/

typedef struct {
    /* Standard callbacks (transport info included in PeerTalk_PeerInfo) */
    void (*on_peer_discovered)(PeerTalk_Context *ctx,
                                const PeerTalk_PeerInfo *peer,
                                void *user_data);

    void (*on_peer_lost)(PeerTalk_Context *ctx,
                         PeerTalk_PeerID peer_id,
                         uint32_t transport,    /* Which transport lost it */
                         void *user_data);

    void (*on_peer_connected)(PeerTalk_Context *ctx,
                              PeerTalk_PeerID peer_id,
                              uint32_t transport,   /* Which transport connected */
                              void *user_data);

    void (*on_peer_disconnected)(PeerTalk_Context *ctx,
                                  PeerTalk_PeerID peer_id,
                                  uint32_t transport,
                                  PeerTalk_Error reason,
                                  void *user_data);

    void (*on_message_received)(PeerTalk_Context *ctx,
                                 PeerTalk_PeerID from_peer,
                                 uint32_t transport,    /* Which transport delivered it */
                                 const void *data,
                                 uint16_t length,
                                 void *user_data);

    /* NEW: Transport state changes */
    void (*on_transport_added)(PeerTalk_Context *ctx,
                                PeerTalk_PeerID peer_id,
                                uint32_t new_transport,
                                void *user_data);

    void (*on_transport_removed)(PeerTalk_Context *ctx,
                                  PeerTalk_PeerID peer_id,
                                  uint32_t removed_transport,
                                  void *user_data);

    /* NEW: Peer merge notification */
    void (*on_peers_merged)(PeerTalk_Context *ctx,
                            PeerTalk_PeerID kept_peer,
                            PeerTalk_PeerID merged_peer,
                            void *user_data);

    void *user_data;
} PeerTalk_Callbacks;

/*============================================================================
 * Peer Query Functions
 *============================================================================*/

/* Get all peers (unified list across all transports) */
PeerTalk_Error PeerTalk_GetPeers(PeerTalk_Context *ctx,
                                  PeerTalk_PeerInfo *peers,
                                  uint16_t max_peers,
                                  uint16_t *count);

/* Get peers filtered by transport */
PeerTalk_Error PeerTalk_GetPeersByTransport(PeerTalk_Context *ctx,
                                             uint32_t transport_mask,
                                             PeerTalk_PeerInfo *peers,
                                             uint16_t max_peers,
                                             uint16_t *count);

/* Get single peer info */
PeerTalk_Error PeerTalk_GetPeerInfo(PeerTalk_Context *ctx,
                                     PeerTalk_PeerID peer_id,
                                     PeerTalk_PeerInfo *info);

/* Check which transports can reach a peer */
uint32_t PeerTalk_GetPeerTransports(PeerTalk_Context *ctx,
                                     PeerTalk_PeerID peer_id);

/* Check if peer is reachable via gateway */
int PeerTalk_IsPeerGatewayReachable(PeerTalk_Context *ctx,
                                     PeerTalk_PeerID peer_id,
                                     PeerTalk_PeerID *gateway_peer);

/*============================================================================
 * Connection Control - Transport Selection
 *============================================================================*/

/* Connect using preferred transport (from config or peer preference) */
PeerTalk_Error PeerTalk_Connect(PeerTalk_Context *ctx,
                                 PeerTalk_PeerID peer_id);

/* Connect via specific transport */
PeerTalk_Error PeerTalk_ConnectVia(PeerTalk_Context *ctx,
                                    PeerTalk_PeerID peer_id,
                                    uint32_t transport);

/* Set transport preference for a specific peer */
PeerTalk_Error PeerTalk_SetPeerTransportPref(PeerTalk_Context *ctx,
                                              PeerTalk_PeerID peer_id,
                                              PeerTalk_TransportPref pref);

/* Disconnect from current transport (keeps peer in list) */
PeerTalk_Error PeerTalk_Disconnect(PeerTalk_Context *ctx,
                                    PeerTalk_PeerID peer_id);

/* Reconnect via alternate transport (if available) */
PeerTalk_Error PeerTalk_ReconnectVia(PeerTalk_Context *ctx,
                                      PeerTalk_PeerID peer_id,
                                      uint32_t transport);

/*============================================================================
 * Peer Management
 *============================================================================*/

/* Manually merge two peers that the SDK didn't auto-detect as same */
PeerTalk_Error PeerTalk_MergePeers(PeerTalk_Context *ctx,
                                    PeerTalk_PeerID keep_peer,
                                    PeerTalk_PeerID merge_peer);

/* Split a merged peer back into separate entries */
PeerTalk_Error PeerTalk_SplitPeer(PeerTalk_Context *ctx,
                                   PeerTalk_PeerID peer_id,
                                   uint32_t transport_to_split,
                                   PeerTalk_PeerID *new_peer_id);

/* Remove peer from list (all transports) */
PeerTalk_Error PeerTalk_RemovePeer(PeerTalk_Context *ctx,
                                    PeerTalk_PeerID peer_id);

/* Remove peer from specific transport only */
PeerTalk_Error PeerTalk_RemovePeerTransport(PeerTalk_Context *ctx,
                                             PeerTalk_PeerID peer_id,
                                             uint32_t transport);

/*============================================================================
 * Gateway Control
 *============================================================================*/

PeerTalk_Error PeerTalk_SetGatewayMode(PeerTalk_Context *ctx, int enable);

/* Get gateway statistics */
typedef struct {
    uint32_t messages_relayed;
    uint32_t tcp_to_adsp;
    uint32_t adsp_to_tcp;
    uint32_t relay_failures;
} PeerTalk_GatewayStats;

PeerTalk_Error PeerTalk_GetGatewayStats(PeerTalk_Context *ctx,
                                         PeerTalk_GatewayStats *stats);

/*============================================================================
 * Discovery Control
 *============================================================================*/

/* Start discovery on all enabled transports */
PeerTalk_Error PeerTalk_StartDiscovery(PeerTalk_Context *ctx);

/* Start discovery on specific transport only */
PeerTalk_Error PeerTalk_StartDiscoveryOn(PeerTalk_Context *ctx,
                                          uint32_t transport);

/* Stop all discovery */
PeerTalk_Error PeerTalk_StopDiscovery(PeerTalk_Context *ctx);

/* Stop discovery on specific transport */
PeerTalk_Error PeerTalk_StopDiscoveryOn(PeerTalk_Context *ctx,
                                         uint32_t transport);

/* Force immediate discovery refresh */
PeerTalk_Error PeerTalk_RefreshDiscovery(PeerTalk_Context *ctx);

/* Get discovery status */
typedef struct {
    int udp_active;
    int nbp_active;
    unsigned long last_udp_broadcast;
    unsigned long last_nbp_lookup;
    int udp_peers_found;
    int nbp_peers_found;
    int merged_peers;           /* Peers found on both transports */
} PeerTalk_DiscoveryStatus;

PeerTalk_Error PeerTalk_GetDiscoveryStatus(PeerTalk_Context *ctx,
                                            PeerTalk_DiscoveryStatus *status);
```

#### Task 6.7.2: Create `src/opentransport/ot_multi.h`

```c
/*
 * Open Transport Multi-Transport Support
 *
 * Provides simultaneous TCP/IP and AppleTalk (ADSP) connectivity
 * through Open Transport's unified endpoint API.
 */

#ifndef PT_OT_MULTI_H
#define PT_OT_MULTI_H

#include <OpenTransport.h>
#include <OpenTptInternet.h>
#include <OpenTransportProviders.h>  /* AppleTalk: DDPAddress, NBPAddress, etc. */

#include "ot_defs.h"  /* Existing TCP/IP types */

/*============================================================================
 * ADSP Endpoint - Hot/Cold Separation (parallel to pt_tcp_endpoint)
 *
 * Following the same cache-efficient pattern as TCP endpoints:
 * - Hot data (~24 bytes): Polled frequently, fits in cache line
 * - Cold data (~1.1KB): Accessed only during I/O
 *
 * This separation is CRITICAL for Classic Mac performance:
 * - 68030 data cache is only 256 bytes
 * - Polling all endpoints scans hot arrays (~24 * PT_MAX_PEERS bytes)
 * - Without separation, polling would thrash the cache with ~1.1KB per endpoint
 *============================================================================*/

typedef struct pt_adsp_endpoint_hot {
    EndpointRef         ref;            /* OT endpoint reference - 4 bytes */
    pt_endpoint_state   state;          /* 1 byte */
    uint8_t             endpoint_idx;   /* Index in pool (for notifier context) */
    uint8_t             _pad1[2];       /* Alignment padding */
    volatile uint32_t   flags;          /* Atomic flag word - 4 bytes */
    struct pt_peer     *peer;           /* Associated peer - 4 bytes */
    OTResult            async_result;   /* From notifier completion - 4 bytes */
    volatile int32_t    log_error_code; /* Error code for deferred logging - 4 bytes */
    unsigned long       close_start;    /* Timeout tracking - 4 bytes */
    void               *user_data;      /* 4 bytes */
} pt_adsp_endpoint_hot;  /* Total: 32 bytes - matches TCP, fits in PPC cache line */

typedef struct pt_adsp_endpoint_cold {
    /* ADSP addressing (from OpenTransportProviders.h) */
    DDPAddress          local_addr;     /* Our DDP address - 8 bytes */
    DDPAddress          remote_addr;    /* Peer's DDP address - 8 bytes */
    NBPEntity           remote_name;    /* Peer's NBP name - ~100 bytes */

    /* Receive buffer */
    uint8_t             recv_buf[1024];
    OTFlags             recv_flags;     /* 4 bytes */
} pt_adsp_endpoint_cold;  /* Total: ~1.1KB */

/*============================================================================
 * NBP Mapper for Discovery
 *
 * NOTE: The lookup_reply_buf is pre-allocated in cold data rather than
 * using stack allocation. This is important because:
 * 1. 68k Macs have limited stack space (~32KB typical)
 * 2. A 2KB stack allocation in a function risks stack overflow
 * 3. Pre-allocation avoids cache pollution during lookup
 *============================================================================*/

typedef struct {
    MapperRef           ref;            /* NBP mapper reference */
    Boolean             registered;     /* Our name registered? */
    NBPEntity           our_entity;     /* Our registered name */
    OTNameID            name_id;        /* For OTDeleteNameByID */

    /* Lookup results */
    DDPAddress          lookup_addrs[PT_MAX_PEERS];
    NBPEntity           lookup_names[PT_MAX_PEERS];
    int                 lookup_count;
    volatile Boolean    lookup_pending;
    volatile Boolean    lookup_complete;

    /* Pre-allocated lookup reply buffer (avoids 2KB stack allocation) */
    UInt8               lookup_reply_buf[2048];
} pt_nbp_mapper;

/*============================================================================
 * Gateway Message Queue (DEFERRED - see FUTURE-GATEWAY-BRIDGING.md)
 *
 * These types are defined here for future use but not currently implemented.
 *============================================================================*/

#if 0 /* Gateway deferred to future phase */
typedef struct pt_gateway_msg {
    PeerTalk_PeerID     from_peer;
    uint32_t            from_transport;
    uint16_t            len;
    uint8_t             data[PT_MAX_MESSAGE_SIZE];
    struct pt_gateway_msg *next;
} pt_gateway_msg;

typedef struct {
    pt_gateway_msg     *head;
    pt_gateway_msg     *tail;
    int                 count;
    pt_gateway_msg      pool[16];       /* Pre-allocated message pool */
    int                 pool_used;
} pt_gateway_queue;
#endif /* Gateway deferred */

/*============================================================================
 * Multi-Transport Context Extension
 *============================================================================*/

typedef struct {
    /*========================================================================
     * HOT DATA - Polled every frame, grouped first for cache efficiency
     * On 68030 (256-byte cache), keeping hot data compact is critical.
     * On PPC 601 (32-byte cache lines), hot arrays should be contiguous.
     *========================================================================*/

    /* Configuration & pools (frequently accessed) */
    uint32_t              transports;     /* Enabled transports - 4 bytes */
    pt_endpoint_pool      tcp_pool;       /* O(1) allocation - 8 bytes */
    pt_endpoint_pool      adsp_pool;      /* O(1) allocation - 8 bytes */

    /* Hot endpoint data - polled every frame */
    pt_udp_endpoint_hot   udp_hot;                      /* 16 bytes */
    pt_tcp_endpoint_hot   tcp_listener_hot;             /* 32 bytes */
    pt_tcp_endpoint_hot   tcp_hot[PT_MAX_PEERS];        /* 32 * PT_MAX_PEERS */
    pt_adsp_endpoint_hot  adsp_listener_hot;            /* 32 bytes */
    pt_adsp_endpoint_hot  adsp_hot[PT_MAX_PEERS];       /* 32 * PT_MAX_PEERS */

    /* Discovery timing (checked every poll) */
    unsigned long         last_udp_announce;  /* 4 bytes */
    unsigned long         last_nbp_lookup;    /* 4 bytes */

    /*========================================================================
     * WARM DATA - Accessed occasionally (endpoint creation, callbacks)
     *========================================================================*/

    /* Cached OT configurations (create once, clone for each endpoint) */
    OTConfigurationRef    adsp_config;    /* "adsp(EnableEOM=1)" - cached */

    /* Notifier UPPs (referenced during endpoint setup) */
    OTNotifyUPP           tcp_notifier_upp;
    OTNotifyUPP           udp_notifier_upp;
    OTNotifyUPP           adsp_notifier_upp;

    /* Gateway (DEFERRED - see FUTURE-GATEWAY-BRIDGING.md) */
    /* Boolean             gateway_enabled; */
    /* pt_gateway_queue    gateway_queue; */

    /*========================================================================
     * COLD POINTERS - Only dereferenced during actual I/O
     *========================================================================*/

    pt_udp_endpoint_cold  *udp_cold;           /* Allocated separately */
    pt_tcp_endpoint_cold  *tcp_listener_cold;
    pt_tcp_endpoint_cold  *tcp_cold;           /* Allocated: PT_MAX_PEERS * sizeof */
    pt_adsp_endpoint_cold *adsp_listener_cold;
    pt_adsp_endpoint_cold *adsp_cold;          /* Allocated: PT_MAX_PEERS * sizeof */

    /*========================================================================
     * COLDEST DATA - Rarely accessed (discovery operations only)
     * NBP mapper contains 2KB lookup_reply_buf - keep at end to avoid
     * cache pollution when polling hot data.
     *========================================================================*/

    pt_nbp_mapper         nbp;

} pt_ot_multi_data;

/* Helper to get cold data for an ADSP endpoint index */
static inline pt_adsp_endpoint_cold *pt_adsp_cold(pt_ot_multi_data *md, int idx) {
    return &md->adsp_cold[idx];
}

/*============================================================================
 * Internal API (called from peertalk.c dispatch)
 *============================================================================*/

/* Initialization */
int pt_ot_multi_init(struct pt_context *ctx);
void pt_ot_multi_shutdown(struct pt_context *ctx);

/* Discovery */
int pt_ot_multi_start_discovery(struct pt_context *ctx);
void pt_ot_multi_stop_discovery(struct pt_context *ctx);

/* Polling (called from PeerTalk_Poll) */
int pt_ot_multi_poll(struct pt_context *ctx);

/* Connection (routes to correct transport) */
int pt_ot_multi_connect(struct pt_context *ctx, struct pt_peer *peer);
int pt_ot_multi_disconnect(struct pt_context *ctx, struct pt_peer *peer);

/* Send (routes to correct transport) */
int pt_ot_multi_send(struct pt_context *ctx, struct pt_peer *peer,
                     const void *data, uint16_t len);

/* Gateway (DEFERRED - see FUTURE-GATEWAY-BRIDGING.md)
 * These are stubs for future implementation: */
/* int pt_ot_gateway_enable(struct pt_context *ctx, Boolean enable); */
/* void pt_ot_gateway_process(struct pt_context *ctx); */

#endif /* PT_OT_MULTI_H */
```

#### Task 6.7.3: Create `src/core/peer_multi.c` - Peer Deduplication Logic

```c
/*
 * Multi-Transport Peer Management
 *
 * Handles peer deduplication, transport merging, and unified peer list.
 */

#include "pt_internal.h"

/*============================================================================
 * Peer Matching - Detect if two discoveries are the same physical Mac
 *============================================================================*/

typedef enum {
    PT_MATCH_NONE = 0,
    PT_MATCH_NAME,          /* Names match (weak match) */
    PT_MATCH_NAME_EXACT,    /* Names match exactly (strong match) */
    PT_MATCH_ETHERNET,      /* Ethernet MAC matches (strongest) */
} pt_match_strength;

/*
 * Compare peer names for potential match.
 * Returns match strength.
 */
static pt_match_strength pt_peer_name_match(const char *name1, const char *name2) {
    if (!name1 || !name2 || !name1[0] || !name2[0]) {
        return PT_MATCH_NONE;
    }

    /* Exact match */
    if (strcmp(name1, name2) == 0) {
        return PT_MATCH_NAME_EXACT;
    }

    /* Case-insensitive match */
    const char *p1 = name1, *p2 = name2;
    while (*p1 && *p2) {
        char c1 = (*p1 >= 'A' && *p1 <= 'Z') ? *p1 + 32 : *p1;
        char c2 = (*p2 >= 'A' && *p2 <= 'Z') ? *p2 + 32 : *p2;
        if (c1 != c2) break;
        p1++; p2++;
    }

    if (!*p1 && !*p2) {
        return PT_MATCH_NAME;  /* Case-insensitive match */
    }

    return PT_MATCH_NONE;
}

/*============================================================================
 * Find Existing Peer That Might Match a New Discovery
 *============================================================================*/

struct pt_peer *pt_peer_find_match(struct pt_context *ctx,
                                    const char *name,
                                    uint32_t new_transport) {
    int i;
    struct pt_peer *best_match = NULL;
    pt_match_strength best_strength = PT_MATCH_NONE;

    for (i = 0; i < ctx->peer_count; i++) {
        struct pt_peer *peer = &ctx->peers[i];

        /* Skip if already has this transport */
        if (peer->available_transports & new_transport) {
            continue;
        }

        pt_match_strength strength = pt_peer_name_match(peer->name, name);

        if (strength > best_strength) {
            best_strength = strength;
            best_match = peer;
        }
    }

    /* Only return if match is strong enough */
    if (best_strength >= PT_MATCH_NAME) {
        return best_match;
    }

    return NULL;
}

/*============================================================================
 * Add Transport to Existing Peer (Merge)
 *============================================================================*/

int pt_peer_add_transport(struct pt_context *ctx,
                          struct pt_peer *peer,
                          uint32_t transport,
                          void *transport_addr) {
    /* Update available transports */
    uint32_t old_transports = peer->available_transports;
    peer->available_transports |= transport;

    /* Store transport-specific address */
    if (transport & PT_TRANSPORT_TCP) {
        pt_tcp_addr *addr = (pt_tcp_addr *)transport_addr;
        peer->ip_addr = addr->ip;
        peer->tcp_port = addr->port;
        peer->last_seen_tcp = TickCount();
    }

    if (transport & PT_TRANSPORT_ADSP) {
        pt_adsp_addr *addr = (pt_adsp_addr *)transport_addr;
        peer->at_network = addr->network;
        peer->at_node = addr->node;
        peer->at_socket = addr->socket;
        if (addr->zone[0]) {
            strncpy(peer->at_zone, addr->zone, sizeof(peer->at_zone) - 1);
        }
        peer->last_seen_nbp = TickCount();
    }

    /* Fire callback if this is a NEW transport for this peer */
    if (!(old_transports & transport)) {
        if (ctx->callbacks.on_transport_added) {
            ctx->callbacks.on_transport_added(
                (PeerTalk_Context *)ctx,
                peer->id,
                transport,
                ctx->callbacks.user_data);
        }

        PT_LOG_INFO(ctx, PT_LOG_CAT_DISCOVERY,
            "Peer '%s' now reachable via %s (was: 0x%x, now: 0x%x)",
            peer->name,
            transport == PT_TRANSPORT_TCP ? "TCP" : "ADSP",
            old_transports, peer->available_transports);
    }

    return 0;
}

/*============================================================================
 * Remove Transport from Peer
 *============================================================================*/

int pt_peer_remove_transport(struct pt_context *ctx,
                             struct pt_peer *peer,
                             uint32_t transport) {
    if (!(peer->available_transports & transport)) {
        return 0;  /* Doesn't have this transport */
    }

    /* If connected via this transport, disconnect first */
    if (peer->connected_transport == transport) {
        pt_peer_disconnect(ctx, peer);
    }

    peer->available_transports &= ~transport;

    /* Fire callback */
    if (ctx->callbacks.on_transport_removed) {
        ctx->callbacks.on_transport_removed(
            (PeerTalk_Context *)ctx,
            peer->id,
            transport,
            ctx->callbacks.user_data);
    }

    /* If no transports left, peer is lost */
    if (peer->available_transports == 0) {
        if (ctx->callbacks.on_peer_lost) {
            ctx->callbacks.on_peer_lost(
                (PeerTalk_Context *)ctx,
                peer->id,
                transport,
                ctx->callbacks.user_data);
        }
        pt_peer_destroy(ctx, peer);
    }

    return 0;
}

/*============================================================================
 * Create Peer from Discovery (with deduplication)
 *============================================================================*/

struct pt_peer *pt_peer_create_from_discovery(struct pt_context *ctx,
                                               const char *name,
                                               uint32_t transport,
                                               void *transport_addr) {
    struct pt_peer *peer;

    /* Check for existing peer we should merge with */
    if (ctx->config.auto_merge_peers) {
        peer = pt_peer_find_match(ctx, name, transport);
        if (peer) {
            pt_peer_add_transport(ctx, peer, transport, transport_addr);
            return peer;
        }
    }

    /* Create new peer */
    peer = pt_peer_create(ctx, name);
    if (!peer) return NULL;

    /* Set transport info */
    peer->available_transports = transport;
    peer->discovered_via = transport;

    pt_peer_add_transport(ctx, peer, transport, transport_addr);

    /* Fire discovery callback */
    if (ctx->callbacks.on_peer_discovered) {
        PeerTalk_PeerInfo info;
        pt_peer_fill_info(peer, &info);

        ctx->callbacks.on_peer_discovered(
            (PeerTalk_Context *)ctx,
            &info,
            ctx->callbacks.user_data);
    }

    return peer;
}

/*============================================================================
 * Select Best Transport for Connection
 *============================================================================*/

uint32_t pt_peer_select_transport(struct pt_context *ctx,
                                   struct pt_peer *peer) {
    uint32_t available = peer->available_transports;
    PeerTalk_TransportPref pref = peer->transport_pref;

    /* Use peer-specific preference if set, otherwise use global */
    if (pref == PT_PREFER_NONE) {
        pref = ctx->config.pref;
    }

    switch (pref) {
    case PT_PREFER_TCP:
        if (available & PT_TRANSPORT_TCP) return PT_TRANSPORT_TCP;
        if (available & PT_TRANSPORT_ADSP) return PT_TRANSPORT_ADSP;
        break;

    case PT_PREFER_ADSP:
        if (available & PT_TRANSPORT_ADSP) return PT_TRANSPORT_ADSP;
        if (available & PT_TRANSPORT_TCP) return PT_TRANSPORT_TCP;
        break;

    case PT_PREFER_FASTEST:
        /* Return whichever was discovered more recently */
        if (peer->last_seen_tcp > peer->last_seen_nbp) {
            if (available & PT_TRANSPORT_TCP) return PT_TRANSPORT_TCP;
        } else {
            if (available & PT_TRANSPORT_ADSP) return PT_TRANSPORT_ADSP;
        }
        /* Fallthrough to any available */
        break;

    case PT_PREFER_NONE:
    default:
        /* Return any available */
        if (available & PT_TRANSPORT_TCP) return PT_TRANSPORT_TCP;
        if (available & PT_TRANSPORT_ADSP) return PT_TRANSPORT_ADSP;
        break;
    }

    return 0;  /* No transport available */
}

/*============================================================================
 * Merge Two Peers (manual API call)
 *============================================================================*/

PeerTalk_Error PeerTalk_MergePeers(PeerTalk_Context *ctx,
                                    PeerTalk_PeerID keep_id,
                                    PeerTalk_PeerID merge_id) {
    struct pt_context *c = (struct pt_context *)ctx;
    struct pt_peer *keep_peer = pt_peer_find_by_id(c, keep_id);
    struct pt_peer *merge_peer = pt_peer_find_by_id(c, merge_id);

    if (!keep_peer || !merge_peer) {
        return PT_ERR_NOT_FOUND;
    }

    if (keep_peer == merge_peer) {
        return PT_ERR_INVALID;
    }

    /* Transfer all transports from merge to keep */
    if (merge_peer->available_transports & PT_TRANSPORT_TCP) {
        pt_tcp_addr addr = {
            .ip = merge_peer->ip_addr,
            .port = merge_peer->tcp_port
        };
        pt_peer_add_transport(c, keep_peer, PT_TRANSPORT_TCP, &addr);
    }

    if (merge_peer->available_transports & PT_TRANSPORT_ADSP) {
        pt_adsp_addr addr = {
            .network = merge_peer->at_network,
            .node = merge_peer->at_node,
            .socket = merge_peer->at_socket
        };
        strncpy(addr.zone, merge_peer->at_zone, sizeof(addr.zone));
        pt_peer_add_transport(c, keep_peer, PT_TRANSPORT_ADSP, &addr);
    }

    /* Fire merge callback */
    if (c->callbacks.on_peers_merged) {
        c->callbacks.on_peers_merged(ctx, keep_id, merge_id, c->callbacks.user_data);
    }

    /* Destroy merged peer */
    pt_peer_destroy(c, merge_peer);

    PT_LOG_INFO(c, PT_LOG_CAT_DISCOVERY,
        "Merged peer %u into peer %u", merge_id, keep_id);

    return PT_OK;
}

/*============================================================================
 * Get Peers by Transport Filter
 *============================================================================*/

PeerTalk_Error PeerTalk_GetPeersByTransport(PeerTalk_Context *ctx,
                                             uint32_t transport_mask,
                                             PeerTalk_PeerInfo *peers,
                                             uint16_t max_peers,
                                             uint16_t *count) {
    struct pt_context *c = (struct pt_context *)ctx;
    uint16_t found = 0;
    int i;

    if (!ctx || !peers || !count) return PT_ERR_INVALID;

    for (i = 0; i < c->peer_count && found < max_peers; i++) {
        struct pt_peer *peer = &c->peers[i];

        /* Check if peer has any of the requested transports */
        if (peer->available_transports & transport_mask) {
            pt_peer_fill_info(peer, &peers[found]);
            found++;
        }
    }

    *count = found;
    return PT_OK;
}

/*============================================================================
 * Check Peer Gateway Reachability
 *============================================================================*/

int PeerTalk_IsPeerGatewayReachable(PeerTalk_Context *ctx,
                                     PeerTalk_PeerID peer_id,
                                     PeerTalk_PeerID *gateway_peer) {
    struct pt_context *c = (struct pt_context *)ctx;
    struct pt_peer *peer = pt_peer_find_by_id(c, peer_id);

    if (!peer) return 0;

    /* Not gateway reachable if directly reachable */
    if (peer->available_transports != 0) {
        return 0;
    }

    /* Check if marked as gateway-reachable */
    if (peer->reachable_via_gateway && peer->gateway_peer) {
        if (gateway_peer) {
            *gateway_peer = peer->gateway_peer;
        }
        return 1;
    }

    return 0;
}
```

### Acceptance Criteria
1. Types compile with Retro68 for PPC
2. Transport flags defined in public header
3. Extended config struct backwards-compatible (defaults work)
4. **ADSP hot/cold separation** - `pt_adsp_endpoint_hot` (~24 bytes) and `pt_adsp_endpoint_cold` (~1.1KB)
5. **SoA pattern for multi-data** - Separate `adsp_hot[]`, `adsp_cold*`, and `adsp_pool` arrays
6. **NBP mapper pre-allocated buffer** - `lookup_reply_buf[2048]` in struct, not on stack
7. ~~Gateway queue pre-allocates messages~~ (DEFERRED - see FUTURE-GATEWAY-BRIDGING.md)
8. **Peer deduplication** - Same Mac found via UDP+NBP = one peer entry
9. **Transport merging** - `on_transport_added` fires when new transport discovered
10. **Transport preference** - Correct transport selected based on config
11. **Manual merge API** - `PeerTalk_MergePeers` works
12. **Filter by transport** - `PeerTalk_GetPeersByTransport` works
13. ~~Gateway reachability~~ (DEFERRED - see FUTURE-GATEWAY-BRIDGING.md)

---

## Session 6.8: ADSP Endpoints

### Objective
Implement ADSP endpoint management using OT's unified API.

### Key Insight

From `NetworkingOpenTransport.txt`, ADSP uses the **same OT functions** as TCP:
- `OTOpenEndpoint(OTCreateConfiguration("adsp"), ...)` - create endpoint
- `OTBind()` - bind to a socket
- `OTConnect()` - connect (can use NBPAddress)
- `OTListen()` / `OTAccept()` - server pattern
- `OTSnd()` / `OTRcv()` - data transfer
- `OTSndOrderlyDisconnect()` - close connection

The only differences are:
1. Configuration string: `"adsp"` instead of `"tcp"`
2. Address type: `DDPAddress` or `NBPAddress` instead of `InetAddress`
3. EOM option: `"adsp(EnableEOM=1)"` for message framing

### Tasks

#### Task 6.8.1: Create `src/opentransport/ot_adsp.c`

```c
#include "ot_multi.h"
#include "pt_internal.h"

/*============================================================================
 * ADSP Notifier
 *
 * Same events as TCP - OT's transport independence!
 *
 * IMPORTANT: Uses atomic flag operations (OTAtomicSetBit) for reentrancy safety.
 * Per NetworkingOpenTransport.txt Ch.3 p.75: "Open Transport might call a
 * notification routine reentrantly."
 *
 * Register Preservation (same rules as TCP notifier):
 * - Registers A0, A1, A2 and D0, D1, D2 may be modified
 * - All other registers must be preserved by the compiler
 *
 * Context is pt_adsp_endpoint_hot* for cache efficiency.
 *============================================================================*/

static pascal void pt_adsp_notifier(
    void *context,
    OTEventCode code,
    OTResult result,
    void *cookie)
{
    pt_adsp_endpoint_hot *hot = (pt_adsp_endpoint_hot *)context;

    switch (code) {
    case T_LISTEN:
        PT_FLAG_SET(hot->flags, PT_FLAG_LISTEN_PENDING);
        break;

    case T_CONNECT:
        PT_FLAG_SET(hot->flags, PT_FLAG_CONNECT_COMPLETE);
        break;

    case T_DATA:
        /* Handle T_DATA before T_PASSCON race condition (same as TCP) */
        if (hot->state == PT_EP_INCOMING) {
            PT_FLAG_SET(hot->flags, PT_FLAG_DEFER_DATA);
        } else {
            PT_FLAG_SET(hot->flags, PT_FLAG_DATA_AVAILABLE);
        }
        break;

    case T_GODATA:
        PT_FLAG_SET(hot->flags, PT_FLAG_SEND_COMPLETE);
        break;

    case T_DISCONNECT:
        PT_FLAG_SET(hot->flags, PT_FLAG_DISCONNECT);
        hot->async_result = result;  /* Store error code for logging */
        break;

    case T_ORDREL:
        PT_FLAG_SET(hot->flags, PT_FLAG_ORDERLY_DISCONNECT);
        break;

    case T_PASSCON:
        PT_FLAG_SET(hot->flags, PT_FLAG_PASSCON);
        break;

    case T_ACCEPTCOMPLETE:
        PT_FLAG_SET(hot->flags, PT_FLAG_ACCEPT_COMPLETE);
        hot->async_result = result;
        break;
    }
    /* NO PT_LOG_* calls here - notifier runs at deferred task time! */
}

/*============================================================================
 * Create ADSP Endpoint
 *
 * Uses hot/cold separation and O(1) pool allocation (same pattern as TCP).
 * Returns endpoint index on success, -1 on failure.
 *============================================================================*/

int pt_ot_adsp_create(struct pt_context *ctx) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    OSStatus err;

    /* Allocate slot from pool - O(1) via bitmap */
    int idx = pt_endpoint_pool_alloc(&md->adsp_pool);
    if (idx < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No free ADSP endpoint slots (pool exhausted)");
        return -1;
    }

    pt_adsp_endpoint_hot *hot = &md->adsp_hot[idx];
    pt_adsp_endpoint_cold *cold = &md->adsp_cold[idx];

    /* Clear structures */
    PT_FLAGS_CLEAR_ALL(hot->flags);
    hot->state = PT_EP_UNUSED;
    hot->peer = NULL;
    hot->endpoint_idx = idx;

    /*
     * Create ADSP endpoint with EOM support
     * From NetworkingOpenTransport.txt: "adsp(EnableEOM=1)"
     *
     * Use cached configuration - OTOpenEndpoint disposes the config
     * it receives, so we must clone our cached copy each time.
     *
     * NOTE: kENOMEMErr (-3211) may occur under memory pressure.
     * Log and retry pattern same as TCP (see pt_ot_tcp_create).
     */
    hot->ref = OTOpenEndpoint(
        OTCloneConfiguration(md->adsp_config),
        0,
        NULL,
        &err);

    if (err == kENOMEMErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_MEMORY,
            "OT out of memory creating ADSP endpoint (kENOMEMErr)");
    }

    if (err != noErr || !hot->ref) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTOpenEndpoint for ADSP failed: %d", err);
        pt_endpoint_pool_free(&md->adsp_pool, idx);
        return -1;
    }

    /* Install notifier - context is hot struct for cache efficiency */
    err = OTInstallNotifier(hot->ref, md->adsp_notifier_upp, hot);
    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "ADSP OTInstallNotifier failed: %d", err);
        OTCloseProvider(hot->ref);
        hot->ref = NULL;
        pt_endpoint_pool_free(&md->adsp_pool, idx);
        return -1;
    }

    OTSetAsynchronous(hot->ref);

    hot->state = PT_EP_UNBOUND;

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "ADSP endpoint created: idx=%d ref=0x%08lX", idx, (unsigned long)hot->ref);

    return idx;
}

/*============================================================================
 * Bind ADSP Endpoint
 *
 * Unlike TCP ports, ADSP uses DDP sockets (0-255).
 * Socket 0 means "let ADSP assign one".
 *
 * Takes endpoint index (returned by pt_ot_adsp_create).
 *============================================================================*/

int pt_ot_adsp_bind(struct pt_context *ctx, int idx,
                    uint8_t socket, int qlen) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    pt_adsp_endpoint_hot *hot = &md->adsp_hot[idx];
    pt_adsp_endpoint_cold *cold = &md->adsp_cold[idx];
    TBind req, ret;
    OSStatus err;
    pt_endpoint_state old_state = hot->state;

    if (hot->state != PT_EP_UNBOUND) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "ADSP endpoint %d not in unbound state (state=%d)", idx, hot->state);
        return -1;
    }

    /* Setup DDP address - use cold data for address storage
     * From OpenTransportProviders.h: DDPAddress structure */
    cold->local_addr.fAddressType = AF_ATALK_DDP;
    cold->local_addr.fNetwork = 0;    /* Filled by AARP */
    cold->local_addr.fNodeID = 0;     /* Filled by AARP */
    cold->local_addr.fSocket = socket; /* 0 = auto-assign */
    cold->local_addr.fDDPType = 7;    /* ADSP = DDP type 7 */
    cold->local_addr.fPad = 0;

    req.addr.buf = (UInt8 *)&cold->local_addr;
    req.addr.len = sizeof(DDPAddress);
    req.qlen = qlen;

    ret.addr.buf = (UInt8 *)&cold->local_addr;
    ret.addr.maxlen = sizeof(DDPAddress);

    err = OTBind(hot->ref, &req, &ret);

    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "ADSP OTBind failed: %d", err);
        return -1;
    }

    /* Log state transition */
    hot->state = PT_EP_IDLE;
    PT_LOG_DEBUG(ctx, PT_LOG_CAT_PLATFORM,
        "ADSP endpoint %d: state %d -> %d", idx, old_state, hot->state);

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "ADSP endpoint %d bound to net %u node %u socket %u (qlen=%d)",
        idx,
        cold->local_addr.fNetwork,
        cold->local_addr.fNodeID,
        cold->local_addr.fSocket,
        qlen);

    return 0;
}

/*============================================================================
 * Connect using NBP name
 *
 * OT supports connecting directly by NBP name - it resolves automatically!
 *
 * Takes endpoint index (from pt_ot_adsp_create or -1 to auto-create).
 * Returns endpoint index on success, -1 on failure.
 *============================================================================*/

int pt_ot_adsp_connect_by_name(struct pt_context *ctx,
                                int idx,  /* -1 to auto-create */
                                const char *name,
                                const char *type,
                                const char *zone) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    TCall call;
    NBPAddress nbp_addr;
    char entity_str[128];
    OTByteCount addr_len;
    OSStatus err;

    /* Auto-create endpoint if idx == -1 */
    if (idx < 0) {
        idx = pt_ot_adsp_create(ctx);
        if (idx < 0) return -1;
    }

    pt_adsp_endpoint_hot *hot = &md->adsp_hot[idx];
    pt_adsp_endpoint_cold *cold = &md->adsp_cold[idx];

    if (hot->state != PT_EP_UNBOUND && hot->state != PT_EP_IDLE) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "ADSP endpoint %d in wrong state for connect: %d", idx, hot->state);
        return -1;
    }

    /* Build NBP entity string: "name:type@zone" */
    snprintf(entity_str, sizeof(entity_str), "%s:%s@%s",
             name, type, zone ? zone : "*");

    /* Store remote name in cold data for later reference */
    /* (NBPEntity copy would go here if needed) */

    /* Setup NBP address */
    nbp_addr.fAddressType = AF_ATALK_NBP;
    addr_len = OTSetAddressFromNBPString(nbp_addr.fNBPNameBuffer,
                                          entity_str, -1);

    /* Setup call structure */
    call.addr.buf = (UInt8 *)&nbp_addr;
    call.addr.len = sizeof(OTAddressType) + addr_len;
    call.opt.buf = NULL;
    call.opt.len = 0;
    call.udata.buf = NULL;
    call.udata.len = 0;
    call.sequence = 0;

    pt_endpoint_state old_state = hot->state;
    hot->state = PT_EP_OUTGOING;
    PT_LOG_DEBUG(ctx, PT_LOG_CAT_PLATFORM,
        "ADSP endpoint %d: state %d -> %d", idx, old_state, hot->state);

    err = OTConnect(hot->ref, &call, NULL);

    if (err != noErr && err != kOTNoDataErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "ADSP OTConnect to %s failed: %d", entity_str, err);
        hot->state = PT_EP_IDLE;
        return -1;
    }

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "ADSP endpoint %d connecting to %s...", idx, entity_str);

    return idx;
}

/*============================================================================
 * Send with EOM
 *
 * ADSP's EOM flag provides message framing - much simpler than TCP!
 *
 * IMPORTANT: OTSnd returns OTResult (byte count on success, negative on error),
 * NOT OSStatus. This is different from functions like OTBind which return OSStatus.
 *
 * Takes endpoint index for hot/cold access.
 *============================================================================*/

int pt_ot_adsp_send(struct pt_context *ctx, int idx,
                    const void *data, uint16_t len, Boolean eom) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    pt_adsp_endpoint_hot *hot = &md->adsp_hot[idx];
    OTFlags flags = eom ? 0 : T_MORE;
    OTResult result;

    if (hot->state != PT_EP_DATAXFER) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_SEND,
            "ADSP send on endpoint %d in wrong state: %d", idx, hot->state);
        return -1;
    }

    /* OTSnd returns byte count on success (OTResult), negative on error */
    result = OTSnd(hot->ref, (void *)data, len, flags);

    if (result == kOTFlowErr) {
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_SEND,
            "ADSP endpoint %d: flow control (kOTFlowErr)", idx);
        return 0;  /* Would block - wait for T_GODATA */
    }

    if (result < 0) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_SEND,
            "ADSP OTSnd failed on endpoint %d: %ld", idx, result);
        return -1;
    }

    return result;  /* Returns byte count sent */
}

/*============================================================================
 * Receive with EOM detection
 *
 * IMPORTANT: OTRcv returns OTResult (byte count on success, negative on error),
 * NOT OSStatus. Check for kOTNoDataErr (negative) and kOTLookErr (negative)
 * as special non-error conditions.
 *
 * Takes endpoint index for hot/cold access.
 *============================================================================*/

int pt_ot_adsp_recv(struct pt_context *ctx, int idx,
                    void *buffer, uint16_t max_len, Boolean *eom) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    pt_adsp_endpoint_hot *hot = &md->adsp_hot[idx];
    OTFlags flags = 0;
    OTResult result;

    if (hot->state != PT_EP_DATAXFER) return 0;

    /* OTRcv returns byte count on success (OTResult), negative on error */
    result = OTRcv(hot->ref, buffer, max_len, &flags);

    if (result == kOTNoDataErr) {
        /* No more data - clear the flag */
        PT_FLAG_CLEAR(hot->flags, PT_FLAG_DATA_AVAILABLE);
        return 0;
    }

    if (result == kOTLookErr) {
        /* Async event pending (T_DISCONNECT, T_ORDREL) - notifier handles it */
        PT_FLAG_CLEAR(hot->flags, PT_FLAG_DATA_AVAILABLE);
        return 0;
    }

    if (result < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_RECV,
            "ADSP OTRcv failed on endpoint %d: %ld", idx, result);
        return -1;
    }

    if (eom) {
        *eom = !(flags & T_MORE);
    }

    /* More data may be available - don't clear flag yet */
    return result;  /* Returns byte count received */
}

/*============================================================================
 * Close ADSP Endpoint
 *
 * Takes endpoint index. Releases slot back to pool after cleanup.
 *============================================================================*/

void pt_ot_adsp_close(struct pt_context *ctx, int idx) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    pt_adsp_endpoint_hot *hot = &md->adsp_hot[idx];
    pt_endpoint_state old_state = hot->state;

    if (!hot->ref) return;

    if (hot->state == PT_EP_DATAXFER) {
        /*
         * Start orderly disconnect with timeout tracking.
         * Same pattern as TCP - record start time and check in poll loop.
         *
         * #define PT_CLOSE_TIMEOUT_TICKS (30 * 60)  // 30 seconds
         * if (hot->state == PT_EP_CLOSING &&
         *     (TickCount() - hot->close_start) > PT_CLOSE_TIMEOUT_TICKS) {
         *     PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT, "ADSP close timeout, forcing abort");
         *     OTSndDisconnect(hot->ref, NULL);  // Abortive disconnect
         *     pt_ot_adsp_cleanup(ctx, idx);
         * }
         */
        hot->state = PT_EP_CLOSING;
        hot->close_start = TickCount();  /* Track for timeout monitoring */
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_CONNECT,
            "ADSP endpoint %d: sending orderly disconnect", idx);
        OTSndOrderlyDisconnect(hot->ref);
        /* Wait for T_ORDREL in notifier, or timeout. For simplicity,
         * we proceed with cleanup here (unlike TCP which defers).
         * Real implementation should mirror TCP's deferred close. */
    }

    if (OTGetEndpointState(hot->ref) >= T_IDLE) {
        OTUnbind(hot->ref);
    }

    OTCloseProvider(hot->ref);
    hot->ref = NULL;
    hot->peer = NULL;
    hot->state = PT_EP_UNUSED;
    PT_FLAGS_CLEAR_ALL(hot->flags);

    /* Return slot to pool for reuse */
    pt_endpoint_pool_free(&md->adsp_pool, idx);

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "ADSP endpoint %d closed (was state %d) and returned to pool",
        idx, old_state);
}
```

### Acceptance Criteria
1. ADSP endpoint creates with `"adsp(EnableEOM=1)"` config
2. **Hot/cold separation** - ADSP uses same pattern as TCP (~24 bytes hot, ~1.1KB cold)
3. **Atomic flag operations** - Notifier uses `PT_FLAG_SET()` macro, not boolean assignment
4. **O(1) pool allocation** - Uses `pt_endpoint_pool_alloc()` same as TCP
5. Notifier receives same events as TCP (T_DATA, T_CONNECT, etc.)
6. Bind works with socket=0 (auto-assign)
7. Connect by NBP name resolves and connects
8. **OTSnd/OTRcv return type documented** - Functions return byte count (OTResult), not OSStatus
9. Send/recv work with EOM flag
10. Orderly disconnect closes cleanly
11. **State transitions logged** - Using `PT_LOG_DEBUG(ctx, PT_LOG_CAT_PLATFORM, ...)`
12. **Endpoint slot returned to pool** after close

---

## Session 6.9: NBP Discovery

### Objective
Implement NBP (Name Binding Protocol) for AppleTalk service discovery.

### Background

From `NetworkingOpenTransport.txt`:
- NBP uses a **mapper provider**, not an endpoint
- `OTOpenMapper()` with `"nbp"` configuration
- `OTRegisterName()` to announce our presence
- `OTLookupName()` to find other services
- `OTDeleteName()` or `OTDeleteNameByID()` to unregister

### Tasks

#### Task 6.9.1: Create `src/opentransport/ot_nbp.c`

```c
#include "ot_multi.h"
#include "pt_internal.h"

/*============================================================================
 * Initialize NBP Mapper
 *============================================================================*/

int pt_ot_nbp_init(struct pt_context *ctx) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    OSStatus err;

    /* Open NBP mapper
     *
     * NOTE: Unlike TCP/UDP endpoints, we pass OTCreateConfiguration()
     * directly here rather than caching and cloning. This is intentional:
     * - Only ONE NBP mapper is created per context (not per-peer)
     * - OTOpenMapper disposes the config, so no leak occurs
     * - Caching would add complexity for no benefit
     *
     * For TCP/UDP we cache configs because endpoints are created dynamically
     * (one per peer), making OTCloneConfiguration() 5x faster than recreating.
     */
    md->nbp.ref = OTOpenMapper(
        OTCreateConfiguration("nbp"),
        0,
        &err);

    if (err != noErr || !md->nbp.ref) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_DISCOVERY,
            "OTOpenMapper for NBP failed: %d", err);
        return -1;
    }

    md->nbp.registered = false;
    md->nbp.lookup_count = 0;
    md->nbp.lookup_pending = false;

    PT_LOG_INFO(ctx, PT_LOG_CAT_DISCOVERY, "NBP mapper initialized");
    return 0;
}

/*============================================================================
 * Register NBP Name
 *
 * Makes our service visible to OTLookupName from other Macs.
 * Format: "name:type@zone" e.g. "Alice's Mac:PeerTalk@*"
 *============================================================================*/

int pt_ot_nbp_register(struct pt_context *ctx,
                       const char *name,
                       const char *type,
                       const char *zone,
                       DDPAddress *bound_addr) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    TRegisterRequest req;
    TRegisterReply reply;
    DDPNBPAddress addr;
    char entity_str[128];
    OTByteCount name_len;
    OSStatus err;

    if (!md->nbp.ref) return -1;
    if (md->nbp.registered) return 0;  /* Already registered */

    /* Build entity string */
    snprintf(entity_str, sizeof(entity_str), "%s:%s@%s",
             name, type, zone ? zone : "*");

    /* Setup combined DDP+NBP address
     * From OpenTransportProviders.h: DDPNBPAddress structure */
    addr.fAddressType = AF_ATALK_DDPNBP;
    addr.fNetwork = bound_addr->fNetwork;
    addr.fNodeID = bound_addr->fNodeID;
    addr.fSocket = bound_addr->fSocket;
    addr.fDDPType = 7;  /* ADSP */
    addr.fPad = 0;
    name_len = OTSetAddressFromNBPString(addr.fNBPNameBuffer, entity_str, -1);

    /* Setup registration request */
    req.name.buf = (UInt8 *)&addr;
    req.name.len = sizeof(DDPNBPAddress);
    req.addr.buf = (UInt8 *)bound_addr;
    req.addr.len = sizeof(DDPAddress);
    req.flags = 0;

    /* Synchronous registration (fast local operation) */
    err = OTRegisterName(md->nbp.ref, &req, &reply);

    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_DISCOVERY,
            "OTRegisterName failed: %d", err);
        return -1;
    }

    /* Save for later deletion */
    md->nbp.name_id = reply.nameid;
    OTSetNBPEntityFromAddress(&md->nbp.our_entity,
                              addr.fNBPNameBuffer, name_len);
    md->nbp.registered = true;

    PT_LOG_INFO(ctx, PT_LOG_CAT_DISCOVERY,
        "NBP registered: %s", entity_str);

    return 0;
}

/*============================================================================
 * Lookup NBP Names
 *
 * Finds all services matching pattern (e.g., "=:PeerTalk@*" for all PeerTalk)
 *============================================================================*/

int pt_ot_nbp_lookup(struct pt_context *ctx,
                     const char *type,
                     const char *zone) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    TLookupRequest req;
    TLookupReply reply;
    NBPAddress lookup_pattern;
    char entity_str[128];
    OTByteCount pattern_len;
    OSStatus err;

    if (!md->nbp.ref) return -1;

    /* Use pre-allocated buffer instead of 2KB stack allocation.
     * This prevents stack overflow on 68k Macs with limited stack space. */

    /* Build wildcard pattern: "=:PeerTalk@*" */
    snprintf(entity_str, sizeof(entity_str), "=:%s@%s",
             type, zone ? zone : "*");

    lookup_pattern.fAddressType = AF_ATALK_NBP;
    pattern_len = OTSetAddressFromNBPString(lookup_pattern.fNBPNameBuffer,
                                             entity_str, -1);

    req.name.buf = (UInt8 *)&lookup_pattern;
    req.name.len = sizeof(OTAddressType) + pattern_len;
    req.addr.buf = NULL;
    req.addr.len = 0;
    req.maxcnt = PT_MAX_PEERS;
    req.timeout = 2000;  /* 2 seconds */
    req.flags = 0;

    reply.names.buf = md->nbp.lookup_reply_buf;
    reply.names.maxlen = sizeof(md->nbp.lookup_reply_buf);

    /* Synchronous lookup */
    err = OTLookupName(md->nbp.ref, &req, &reply);

    if (err != noErr && err != kOTNoDataErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_DISCOVERY,
            "OTLookupName failed: %d", err);
        return -1;
    }

    /* Parse results using TLookupBuffer iteration */
    md->nbp.lookup_count = 0;

    TLookupBuffer *buf = (TLookupBuffer *)md->nbp.lookup_reply_buf;
    for (UInt32 i = 0; i < reply.rspcount && md->nbp.lookup_count < PT_MAX_PEERS; i++) {
        /* Extract address */
        DDPAddress *addr = (DDPAddress *)buf->fAddressBuffer;

        /* Skip if it's our own registration - compare DDP address components */
        if (md->nbp.registered &&
            addr->fNetwork == md->adsp_listener.local_addr.fNetwork &&
            addr->fNodeID == md->adsp_listener.local_addr.fNodeID &&
            addr->fSocket == md->adsp_listener.local_addr.fSocket) {
            buf = OTNextLookupBuffer(buf);
            continue;
        }

        /* Store result */
        md->nbp.lookup_addrs[md->nbp.lookup_count] = *addr;

        /* Extract name (follows address in buffer) */
        UInt8 *name_ptr = buf->fAddressBuffer + buf->fAddressLength;
        OTSetNBPEntityFromAddress(&md->nbp.lookup_names[md->nbp.lookup_count],
                                   name_ptr, buf->fNameLength);

        md->nbp.lookup_count++;

        buf = OTNextLookupBuffer(buf);
    }

    if (md->nbp.lookup_count == 0) {
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_DISCOVERY,
            "NBP lookup found no peers (zone=%s)", zone ? zone : "*");
    } else {
        PT_LOG_INFO(ctx, PT_LOG_CAT_DISCOVERY,
            "NBP lookup found %d peers", md->nbp.lookup_count);
    }

    return md->nbp.lookup_count;
}

/*============================================================================
 * Unregister and Shutdown
 *============================================================================*/

void pt_ot_nbp_unregister(struct pt_context *ctx) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);

    if (md->nbp.ref && md->nbp.registered) {
        OTDeleteNameByID(md->nbp.ref, md->nbp.name_id);
        md->nbp.registered = false;
        PT_LOG_INFO(ctx, PT_LOG_CAT_DISCOVERY, "NBP unregistered");
    }
}

void pt_ot_nbp_shutdown(struct pt_context *ctx) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);

    pt_ot_nbp_unregister(ctx);

    if (md->nbp.ref) {
        OTCloseProvider(md->nbp.ref);
        md->nbp.ref = NULL;
    }
}
```

### Acceptance Criteria
1. NBP mapper opens with `"nbp"` config
2. Registration visible from other Macs (use Chooser or NBP tool to verify)
3. Lookup finds other PeerTalk services
4. Our own registration filtered from results
5. Unregistration works (OTDeleteNameByID)
6. Clean shutdown

---

## Session 6.10: Multi-Transport Poll

### Objective
Implement unified poll loop that handles both TCP/IP and ADSP.

### Tasks

#### Task 6.10.1: Create `src/opentransport/ot_multi.c`

```c
#include "ot_multi.h"
#include "pt_internal.h"

/*============================================================================
 * Initialize Multi-Transport
 *============================================================================*/

int pt_ot_multi_init(struct pt_context *ctx) {
    pt_ot_multi_data *md;
    OSStatus err;

    /* Allocate and clear multi-transport data */
    md = pt_ot_multi_get(ctx);
    memset(md, 0, sizeof(pt_ot_multi_data));

    md->transports = ctx->config.transports;
    /* Gateway is DEFERRED - see FUTURE-GATEWAY-BRIDGING.md */
    /* md->gateway_enabled = ctx->config.enable_gateway; */

    /* Create notifier UPPs */
    md->tcp_notifier_upp = NewOTNotifyUPP(pt_tcp_notifier);
    md->udp_notifier_upp = NewOTNotifyUPP(pt_udp_notifier);
    md->adsp_notifier_upp = NewOTNotifyUPP(pt_adsp_notifier);

    /* Cache ADSP configuration (cloned for each endpoint creation) */
    md->adsp_config = OTCreateConfiguration("adsp(EnableEOM=1)");
    if (!md->adsp_config) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_PLATFORM,
            "Failed to create ADSP configuration");
    }

    /* Initialize TCP/IP if enabled */
    if (md->transports & PT_TRANSPORT_TCPIP) {
        if (pt_ot_tcp_init(ctx) < 0) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_INIT,
                "TCP/IP initialization failed");
            md->transports &= ~PT_TRANSPORT_TCPIP;
        }
    }

    /* Initialize AppleTalk if enabled */
    if (md->transports & PT_TRANSPORT_ATALK) {
        /* NBP mapper */
        if (md->transports & PT_TRANSPORT_NBP) {
            if (pt_ot_nbp_init(ctx) < 0) {
                PT_LOG_WARN(ctx, PT_LOG_CAT_DISCOVERY,
                    "NBP initialization failed");
                md->transports &= ~PT_TRANSPORT_NBP;
            }
        }

        /* ADSP listener */
        if (md->transports & PT_TRANSPORT_ADSP) {
            if (pt_ot_adsp_create(ctx, &md->adsp_listener) < 0 ||
                pt_ot_adsp_bind(ctx, &md->adsp_listener, 0, 4) < 0) {
                PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                    "ADSP listener initialization failed");
                md->transports &= ~PT_TRANSPORT_ADSP;
            } else {
                /* Register with NBP */
                if (md->transports & PT_TRANSPORT_NBP) {
                    pt_ot_nbp_register(ctx,
                        ctx->config.local_name,
                        ctx->config.nbp_type[0] ? ctx->config.nbp_type : "PeerTalk",
                        ctx->config.nbp_zone[0] ? ctx->config.nbp_zone : "*",
                        &md->adsp_listener.local_addr);
                }
            }
        }
    }

    PT_LOG_INFO(ctx, PT_LOG_CAT_INIT,
        "Multi-transport init: TCP=%d UDP=%d ADSP=%d NBP=%d",
        (md->transports & PT_TRANSPORT_TCP) != 0,
        (md->transports & PT_TRANSPORT_UDP) != 0,
        (md->transports & PT_TRANSPORT_ADSP) != 0,
        (md->transports & PT_TRANSPORT_NBP) != 0);

    return 0;
}

/*============================================================================
 * Unified Poll
 *
 * Called from PeerTalk_Poll() - handles all enabled transports.
 *============================================================================*/

int pt_ot_multi_poll(struct pt_context *ctx) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    unsigned long now = TickCount();

    /* Poll TCP/IP */
    if (md->transports & PT_TRANSPORT_UDP) {
        pt_ot_udp_poll(ctx);

        /* Periodic UDP announce */
        if ((now - md->last_udp_announce) > 10 * 60) {
            pt_ot_discovery_send(ctx);
            md->last_udp_announce = now;
        }
    }

    if (md->transports & PT_TRANSPORT_TCP) {
        pt_ot_tcp_listen_poll(ctx);
        pt_ot_tcp_connect_poll(ctx);
        pt_ot_tcp_data_poll(ctx);
    }

    /* Poll AppleTalk */
    if (md->transports & PT_TRANSPORT_NBP) {
        /* Periodic NBP lookup */
        if ((now - md->last_nbp_lookup) > 30 * 60) {  /* Every 30 seconds */
            pt_ot_nbp_lookup(ctx,
                ctx->config.nbp_type[0] ? ctx->config.nbp_type : "PeerTalk",
                ctx->config.nbp_zone[0] ? ctx->config.nbp_zone : "*");
            md->last_nbp_lookup = now;

            /* Create peers from NBP results */
            for (int i = 0; i < md->nbp.lookup_count; i++) {
                pt_peer_create_from_nbp(ctx,
                    &md->nbp.lookup_names[i],
                    &md->nbp.lookup_addrs[i]);
            }
        }
    }

    if (md->transports & PT_TRANSPORT_ADSP) {
        pt_ot_adsp_listen_poll(ctx);
        pt_ot_adsp_connect_poll(ctx);
        pt_ot_adsp_data_poll(ctx);
    }

    /* Gateway relay (DEFERRED - see FUTURE-GATEWAY-BRIDGING.md)
     * if (md->gateway_enabled) {
     *     pt_ot_gateway_process(ctx);
     * }
     */

    return 0;
}

/*============================================================================
 * Send - Routes to Correct Transport
 *============================================================================*/

int pt_ot_multi_send(struct pt_context *ctx, struct pt_peer *peer,
                     const void *data, uint16_t len) {
    if (!peer || !peer->connection) return -1;

    if (peer->transport & PT_TRANSPORT_TCP) {
        return pt_ot_tcp_send(ctx, peer->connection, data, len);
    }

    if (peer->transport & PT_TRANSPORT_ADSP) {
        return pt_ot_adsp_send(ctx, peer->connection, data, len, true);
    }

    return -1;
}

/*============================================================================
 * Shutdown
 *============================================================================*/

void pt_ot_multi_shutdown(struct pt_context *ctx) {
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);

    /* Shutdown AppleTalk */
    pt_ot_nbp_shutdown(ctx);

    for (int i = 0; i < PT_MAX_PEERS; i++) {
        pt_ot_adsp_close(ctx, &md->adsp_endpoints[i]);
    }
    pt_ot_adsp_close(ctx, &md->adsp_listener);

    /* Shutdown TCP/IP */
    pt_ot_tcp_shutdown(ctx);
    pt_ot_udp_shutdown(ctx);

    /* Dispose UPPs (must be after all endpoints using them are closed) */
    if (md->tcp_notifier_upp) DisposeOTNotifyUPP(md->tcp_notifier_upp);
    if (md->udp_notifier_upp) DisposeOTNotifyUPP(md->udp_notifier_upp);
    if (md->adsp_notifier_upp) DisposeOTNotifyUPP(md->adsp_notifier_upp);

    /* Dispose cached configurations */
    if (md->adsp_config) OTDestroyConfiguration(md->adsp_config);

    PT_LOG_INFO(ctx, PT_LOG_CAT_INIT, "Multi-transport shutdown complete");
}
```

### Acceptance Criteria
1. Init creates both TCP and ADSP listeners
2. NBP registration happens automatically
3. Poll processes both transport types
4. Send routes to correct transport based on peer
5. Discovery works on both UDP and NBP
6. Clean shutdown closes all endpoints

---

## Phase 6 Complete Checklist

### Core TCP/IP Functionality (Sessions 6.1-6.6)
- [ ] OT presence check via Gestalt
- [ ] InitOpenTransport succeeds
- [ ] **UPPs created at init** (NewOTNotifyUPP for udp_notifier, tcp_notifier)
- [ ] **UPPs disposed at shutdown** (after all endpoints closed)
- [ ] Configuration strings work (OTCloneConfiguration before each OTOpenEndpoint)
- [ ] Local IP retrieved
- [ ] UDP endpoint create/bind
- [ ] UDP notifier fires on data (via UPP)
- [ ] UDP send (broadcast)
- [ ] UDP receive
- [ ] TCP endpoint create/bind
- [ ] TCP notifier events work (via UPP)
- [ ] OTConnect for outgoing connections
- [ ] Connection timeout works (30s)
- [ ] tilisten pattern implemented
- [ ] OTListen/OTAccept handoff
- [ ] OTSnd/OTRcv work
- [ ] Orderly disconnect with timeout monitoring
- [ ] Main poll integrates all
- [ ] Build validated in emulator
- [ ] **VERIFIED ON REAL PPC MAC HARDWARE**
- [ ] **MaxBlock same before/after 50+ operations**
- [ ] **Cross-platform: POSIX peer discovers OT peer**
- [ ] **Cross-platform: OT peer discovers POSIX peer**
- [ ] **Cross-platform: Messages exchange POSIX↔OT**

### Multi-Transport Functionality (Sessions 6.7-6.10)
- [ ] Transport flags defined in public API (`PT_TRANSPORT_*`)
- [ ] Extended config supports multi-transport selection
- [ ] **ADSP endpoint creates** with `"adsp(EnableEOM=1)"` config
- [ ] ADSP notifier receives T_DATA, T_CONNECT, etc. (same as TCP)
- [ ] ADSP bind works with auto-assign socket (socket=0)
- [ ] ADSP connect by NBP name resolves and connects
- [ ] ADSP send/recv works with EOM framing
- [ ] **NBP mapper opens** with `"nbp"` config
- [ ] NBP registration visible from other Macs
- [ ] NBP lookup finds other PeerTalk services
- [ ] Own registration filtered from lookup results
- [ ] NBP unregistration works (OTDeleteNameByID)
- [ ] **Multi-transport init** creates both TCP and ADSP listeners
- [ ] Unified poll processes both transports
- [ ] Send routes to correct transport based on peer type
- [ ] Discovery works on both UDP broadcast AND NBP
- [ ] Peer deduplication works (same name via TCP and NBP = one peer)
- [ ] **libpeertalk_ot_at.a** builds successfully
- [ ] **VERIFIED:** OT Mac talks to POSIX peer (TCP) AND AppleTalk peer (ADSP)
- [ ] **MaxBlock same before/after 50+ multi-transport operations**

> **Note:** Gateway/bridging (POSIX <-> AppleTalk relay) is deferred. See [FUTURE-GATEWAY-BRIDGING.md](FUTURE-GATEWAY-BRIDGING.md)

### CSend Lessons Applied (see [CSEND-LESSONS.md](CSEND-LESSONS.md) Part B)
- [ ] T_UDERR cleared with OTRcvUDErr in notifier (B.1)
- [ ] Endpoint pool uses T_IDLE or T_UNBND for OTAccept (B.2)
- [ ] Data received BEFORE checking disconnect flags (B.3)
- [ ] Read loop checks both kOTNoDataErr and kOTLookErr (B.4)
- [ ] T_GODATA cleared after timeout via OTLook (B.5)
- [ ] OTSndOrderlyDisconnect used after sends (B.6)
- [ ] **OTRcvOrderlyDisconnect called to acknowledge T_ORDREL** (critical)
- [ ] **T_DATA before T_PASSCON race condition handled** (defer_data flag)
- [ ] UDP sends work after T_UDERR is cleared
- [ ] Endpoint reuse works after disconnect
- [ ] No data loss on orderly disconnect

## tilisten Module Note

For projects that need simpler connection handling, consider using Apple's
sample `tilisten` module from the Open Transport SDK. It provides a pre-built
abstraction for the listen/accept pattern. However, for full control and
debugging, implementing the pattern directly (as done here) is recommended.

## Build Configuration: Makefile.retro68

PeerTalk produces **unified libraries** when built for Classic Mac. The same
library binary contains Open Transport, AppleTalk, and TCP/IP code - the
application chooses which transports to enable via `PT_TRANSPORT_*` flags
at runtime.

### Library Variants

| Library | Target | Contents |
|---------|--------|----------|
| `libpeertalk_posix.a` | Linux/macOS | POSIX sockets only |
| `libpeertalk_mactcp.a` | 68k Mac | MacTCP driver + protocol |
| `libpeertalk_ot.a` | PPC/68040 Mac | OT TCP/IP only |
| `libpeertalk_ot_at.a` | PPC/68040 Mac | OT TCP/IP + AppleTalk (ADSP/NBP) |

### Makefile.retro68 Structure

```makefile
# Retro68 cross-compilation makefile for Classic Mac

RETRO68 = /home/matthew/Retro68
TOOLCHAIN = $(RETRO68)/toolchain/powerpc-apple-macos

CC = $(TOOLCHAIN)/bin/powerpc-apple-macos-gcc
AR = $(TOOLCHAIN)/bin/powerpc-apple-macos-ar

# Unified library sources (all OT + AppleTalk code)
OT_AT_SRCS = \
    src/core/protocol.c \
    src/core/peer.c \
    src/core/queue.c \
    src/log/pt_log_mac.c \
    src/opentransport/ot_driver.c \
    src/opentransport/ot_tcp.c \
    src/opentransport/ot_udp.c \
    src/opentransport/ot_tcp_server.c \
    src/opentransport/ot_discovery.c \
    src/opentransport/ot_multi.c \
    src/opentransport/ot_adsp.c \
    src/opentransport/ot_nbp.c

CFLAGS = -I$(RETRO68)/InterfacesAndLibraries/MPW_Interfaces/Interfaces\&Libraries/Interfaces/CIncludes \
         -Iinclude \
         -DTARGET_API_MAC_OS8=1 \
         -O2 -Wall -Werror

# Build unified OT+AppleTalk library
libpeertalk_ot_at.a: $(OT_AT_SRCS:.c=.o)
	$(AR) rcs $@ $^

# TCP/IP only variant (smaller footprint)
OT_ONLY_SRCS = $(filter-out %adsp.c %nbp.c %multi.c, $(OT_AT_SRCS))

libpeertalk_ot.a: $(OT_ONLY_SRCS:.c=.o)
	$(AR) rcs $@ $^
```

### Runtime Transport Selection

Applications link against one library but enable transports at runtime:

```c
PeerTalk_Config config;
PeerTalk_ConfigInit(&config);

/* Option 1: TCP/IP only (POSIX interop) */
config.transports = PT_TRANSPORT_TCPIP;

/* Option 2: AppleTalk only (Mac-to-Mac, LocalTalk/EtherTalk) */
config.transports = PT_TRANSPORT_ADSP | PT_TRANSPORT_NBP;

/* Option 3: Both (maximum reach) */
config.transports = PT_TRANSPORT_TCPIP | PT_TRANSPORT_ADSP | PT_TRANSPORT_NBP;

PeerTalk_Init(&config);
```

### Build Targets

```bash
# From Ubuntu development machine:
make -f Makefile.retro68 libpeertalk_ot_at.a   # Full OT+AppleTalk library
make -f Makefile.retro68 libpeertalk_ot.a      # OT TCP/IP only
make -f Makefile.retro68 clean                  # Clean build artifacts
```

### Dead Code Elimination

When building with `-O2` and `-ffunction-sections -fdata-sections`, the
linker can eliminate unused transport code. Applications using only TCP/IP
won't include ADSP/NBP code in the final binary.

## Common Pitfalls (from Networking With Open Transport and OpenTransport.h)

1. **Use NewOTNotifyUPP() for portability** - OpenTransport.h states: "Even though a OTNotifyUPP is a OTNotifyProcPtr on pre-Carbon system, use NewOTNotifyUPP() and friends to make your source code portable to OS X and Carbon."

2. **OTOpenEndpoint disposes configurations** - "The functions used to open providers take a pointer to the configuration structure as input, but as part of their processing, they dispose of the original configuration structure." Always use `OTCloneConfiguration()` before each `OTOpenEndpoint()`.

3. **Notifier reentrancy** - Per documentation: "Open Transport might call a notification routine reentrantly." Use `OTAtomicSetBit()`/`OTAtomicClearBit()` for flags rather than simple assignment.

4. **T_UDERR must be cleared** - When `OTSndUData` fails and you receive T_UDERR, you MUST call `OTRcvUDErr()` to clear the error condition, even if you don't need the error details. "Failing to do this leaves the endpoint in a state where it cannot do other sends."

5. **Drain data completely on T_DATA** - Call `OTRcv()`/`OTRcvUData()` in a loop until `kOTNoDataErr` to fully clear the T_DATA event.

6. **kOTLookErr means check OTLook()** - This error indicates an asynchronous event occurred during a synchronous operation. Call `OTLook()` to determine what event needs handling.

7. **OTRcvOrderlyDisconnect is REQUIRED** - Per p.516-517: "You call the OTRcvOrderlyDisconnect function to acknowledge the receipt of an orderly disconnect event." Failure to call this leaves the connection in a bad state.

8. **Don't bind client endpoints before OTAccept** - Per p.112-113: "If the endpoint is not bound, the endpoint provider automatically binds it to the address of the endpoint that listened for the connection request."

9. **tilisten requires OT 1.1.1+** - The tilisten pattern may not work on very early OT versions. Check version via Gestalt if supporting older systems.

10. **Dispose UPPs only after endpoints are closed** - Endpoints may still reference UPPs until `OTCloseProvider()` completes.

11. **kOTProviderWillClose allows sync calls** - Unlike other notifier events, you CAN make synchronous OT calls when handling `kOTProviderWillClose` because it's only issued at system task time.

12. **T_DATA may arrive before T_PASSCON** - Per p.493: "It is possible, in the case where the listening and accepting endpoints are different, that the accepting endpoint receives a T_DATA event before receiving the T_PASSCON event." Set a defer flag and process data after state transition.

## References

- Networking With Open Transport (1997): Complete API reference
  - Ch.2: Initialization
  - Ch.3: Providers and notifiers
  - Ch.4: Endpoints
  - Ch.5: Programming models (tilisten pattern)
  - Ch.9: Mappers (NBP registration and lookup)
  - Ch.11: TCP/IP Services
  - Ch.12: AppleTalk Services (ADSP, NBP via OT)
- OpenTransportProviders.h (Retro68): AppleTalk types
  - `DDPAddress`, `NBPAddress`, `DDPNBPAddress` structures
  - `AF_ATALK_DDP`, `AF_ATALK_NBP`, `AF_ATALK_DDPNBP` constants
  - `MapperRef`, `TRegisterRequest`, `TLookupRequest` types
  - `kADSPName`, `kNBPName` configuration strings
- Inside Macintosh Memory: MaxBlock, FreeMem for leak detection
