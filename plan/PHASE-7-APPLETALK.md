# PHASE 7: AppleTalk Networking

> **Status:** OPEN
> **Review Applied:** 2026-01-29 (2nd review) - Added I/O logging requirements, refined DOD notes, verified all APIs against Retro68 headers
> **Depends on:** Phase 2 (Protocol layer - may need AppleTalk-specific discovery format)
> **Independent of:** Phases 4, 5, 6 (can develop in parallel)
> **Produces:** Fully functional PeerTalk on AppleTalk networks
> **Risk Level:** MEDIUM (simpler than MacTCP/OT, but different paradigm)
> **Estimated Sessions:** 7
> **Library Output:**
>   - `libpeertalk_at.a` - AppleTalk standalone (any Mac with LocalTalk or EtherTalk)
>   - Integrates into `libpeertalk_mactcp_at.a` (with Phase 5)
>   - Integrates into `libpeertalk_ot_at.a` (with Phase 6)

### Fact-Check Summary (2026-01-29 2nd Review)

| Fact | Source | Status |
|------|--------|--------|
| AddrBlock is 4 bytes | AppleTalk.h: `{UInt16 aNet; UInt8 aNode; UInt8 aSocket}` | VERIFIED |
| ADSP callbacks run at interrupt level | Programming With AppleTalk p.5924-5926 | VERIFIED |
| userFlags must be cleared after reading | Programming With AppleTalk p.5780-5781 | VERIFIED - "failure to clear will hang connection" |
| Attention buffer exactly 570 bytes | Programming With AppleTalk p.5934, ADSP.h `attnBufSize` | VERIFIED |
| TickCount() NOT interrupt-safe | Inside Macintosh Vol VI Table B-3 (absent from list) | VERIFIED |
| ffs()/popcount not portable | MPW/Retro68 compatibility | FIXED - added pt_ffs()/pt_popcount() |
| NamesTableEntry is 108 bytes | AppleTalk.h: Ptr(4) + NTElement(104) | VERIFIED - corrected from 109 |
| EntityName is 102 bytes | MacTypes.h: Str32Field[34] × 3 | VERIFIED |
| TRCCB is 242 bytes | ADSP.h struct definition | VERIFIED |
| All 15 ADSP control codes | ADSP.h enum (dspInit=255 to dspNewCID=241) | VERIFIED |
| 4 opening modes (ocRequest/Passive/Accept/Establish) | ADSP.h enum | VERIFIED |
| 6 connection states (sListening to sClosed) | ADSP.h enum | VERIFIED |
| ADSPCompletionUPP receives DSPPBPtr in A0 | ADSP.h uppADSPCompletionProcInfo | VERIFIED |
| ADSPConnectionEventUPP receives TPCCB in A1 | ADSP.h uppADSPConnectionEventProcInfo | VERIFIED |
| mppRefNum = -10 for .MPP driver | AppleTalk.h enum | VERIFIED |
| Phase 2 uses separate discovery format | Design decision | RESOLVED - NBP and UDP broadcast coexist |

### Phase Dependencies Clarification

**Phase 2 Protocol Layer:** This phase requires clarification from Phase 2 regarding:
1. **Discovery packet format:** Is `pt_discovery_packet` transport-agnostic, or does AppleTalk need a separate NBP-specific discovery format? AppleTalk's NBP already provides discovery metadata (name, type, zone, address) which may not map directly to the UDP broadcast format.
2. **Message header compatibility:** Confirm that `pt_message_header` works with ADSP's EOM framing without modification.

**Phase 6.8-6.10 Integration:** This phase produces `libpeertalk_at.a` which is combined with MacTCP (Phase 5) or Open Transport (Phase 6) in the multi-transport unification sessions. Key integration points:
- Peer deduplication: When the same physical peer is visible on both TCP/IP and AppleTalk, how are they merged?
- Transport selection: User-driven or automatic? API contract TBD in Phase 6.8.

### Memory Requirements

| Platform | Max Connections | Memory per Connection | Total Context | Notes |
|----------|-----------------|----------------------|---------------|-------|
| 68k (4MB RAM) | 8 | ~5 KB cold | ~40 KB | Conservative for System 6/7 |
| 68k (8MB RAM) | 16 | ~5 KB cold | ~80 KB | PT_MAX_PEERS=16 default |
| PPC (16MB+ RAM) | 32 | ~5 KB cold | ~160 KB | Can increase PT_MAX_PEERS |

**Per-connection memory breakdown (with 2048-byte queues):**
- CCB (TRCCB): ~236 bytes
- Send queue: 2048 bytes (configurable, min 100)
- Receive queue: 2048 bytes (configurable, min 100)
- Attention buffer: 570 bytes (required, exactly attnBufSize)
- Extended param block: ~40 bytes
- Hot struct: 14 bytes
- **Total: ~4,956 bytes per connection**

**Minimal configuration (1024-byte queues):**
- Total: ~2,908 bytes per connection

**NBP discovery memory:**
- Lookup buffer: 1024 bytes
- Entry hot data: 6 × PT_NBP_MAX_ENTRIES bytes (AddrBlock=4 + slot=1 + pad=1)
- Entry cold names: 34 × PT_NBP_MAX_ENTRIES bytes

**Recommendation:** On memory-constrained systems (4MB), set `PT_MAX_PEERS=8` and reduce queue sizes to 1024 bytes each.

## Why AppleTalk?

AppleTalk provides several advantages for Classic Mac peer-to-peer networking:

| Feature | Benefit |
|---------|---------|
| **Zero configuration** | No IP addresses to configure - just plug in and go |
| **Built-in discovery (NBP)** | Native name registration and lookup - no UDP broadcast needed |
| **Symmetric connections (ADSP)** | Perfect for peer-to-peer - both sides can initiate data transfer |
| **Works on all Macs** | Every Mac has AppleTalk - including those without Ethernet (LocalTalk) |
| **Complements TCP/IP** | Some networks have AppleTalk but not TCP/IP |

**Interoperability Note:** AppleTalk peers can only communicate with other AppleTalk peers. For mixed-network support, applications link multiple PeerTalk libraries.

## Target Hardware

| Machine | System | Network | Notes |
|---------|--------|---------|-------|
| Mac SE | System 6.0.8 | LocalTalk (serial) | Primary test - works without Ethernet |
| Mac IIci | System 7.1 | EtherTalk Phase 2 | Test with Ethernet |
| Power Mac | Mac OS 8/9 | EtherTalk Phase 2 | PPC verification |

**LocalTalk Note:** LocalTalk is slower (230.4 Kbps vs Ethernet's 10 Mbps) but universally available. PeerTalk must work on both.

## Protocol Selection

| Protocol | Purpose | Why |
|----------|---------|-----|
| **ADSP** | Peer connections | Full-duplex, symmetric, reliable streams (like TCP) |
| **NBP** | Peer discovery | Native name registration and lookup (like DNS-SD/Bonjour) |

### Why ADSP over ATP?

| Feature | ADSP | ATP |
|---------|------|-----|
| Connection type | Full-duplex stream | Request-response |
| Reliability | Built-in, ordered | Exactly-once transactions |
| Message framing | EOM flag support | Fixed transaction size |
| Complexity | Simpler for chat/games | Better for RPC |

ADSP is the natural fit for PeerTalk's peer-to-peer model.

## Overview

Phase 7 implements AppleTalk networking using NBP for discovery and ADSP for connections. Like MacTCP, AppleTalk uses completion routines that run at interrupt level - the same ISR safety rules apply.

### Reference Implementations

**AMENDMENT (2026-02-03):** AppleTalk reference implementations are scarcer than MacTCP/OT examples, but these resources are valuable.

| Resource | Location | Pattern | Best For |
|----------|----------|---------|----------|
| **Programming With AppleTalk** | `~/peertalk/books/ProgrammingAppleTalk.txt` | Canonical reference | API documentation, examples |
| **ADSP.h** | `/home/matthew/Retro68/InterfacesAndLibraries/.../CIncludes/` | Header definitions | Callback signatures, constants |
| **Inside Macintosh VI** | `~/peertalk/books/InsideMacVol6.txt` | Interrupt safety | Table B-3 (safe routines) |

**No LaunchAPPL equivalent for AppleTalk** - LaunchAPPL only implements MacTCP and Open Transport. This makes Phase 7 more challenging, but the patterns from Phase 5 (MacTCP) apply directly:
- Callback architecture similar (ioCompletion = ASR)
- ISR restrictions identical (both interrupt-level)
- Flag-based logging pattern reusable

**Programming With AppleTalk Key Sections:**
- Lines 1675-1678, 11544: Register preservation (D3-D7, A2-A6 must be preserved)
- Chapter on ADSP: dspRead, dspWrite, dspOpen async patterns
- NBP registration examples: PRegisterName, PLookupName

**When to Reference:** Read Programming With AppleTalk alongside this plan. Cross-reference ADSP.h for exact callback signatures and constants.

### Architectural Highlights

**Hot/Cold Data Separation:** Following the pattern from PHASE-5-MACTCP and PHASE-6-OPENTRANSPORT, all data structures are split into hot (frequently polled) and cold (I/O operations) structs:
- `pt_adsp_connection_hot` (14 bytes) - state, flags, async_result, remote_addr (AddrBlock=4 bytes)
- `pt_adsp_connection_cold` - CCB, buffers, param blocks, local_addr
- Cold data allocated as a single block at init, accessed via `PT_CONN_COLD(ctx, hot)` macro

**Active Connection Tracking:** When `PT_MAX_PEERS <= 32`, a bitmask (`active_mask`) provides O(active) polling with minimal memory (4 bytes vs 32 bytes for an index array). For larger peer counts, an index array is used. Both approaches avoid O(capacity) iteration, critical for 68k cache efficiency.

**PT_Log Integration:** All functions log errors, state transitions, and key events through PT_Log macros (AT_LOG_ERR, AT_LOG_INFO, etc.). Logging follows ISR-safe patterns - callbacks set flags, main loop logs.

**Packed Flags:** Event flags use a single `volatile uint16_t` with bit constants (PT_FLAG_CONNECTION_CLOSED, etc.) instead of separate bytes, enabling atomic access.

**Key Insights from Programming With AppleTalk:**
- ADSP has TWO callback types with different signatures (see Callback Architecture below)
- Connection Control Block (CCB) must persist for connection lifetime
- Send/receive queues must be locked in memory
- Attention buffer is exactly 570 bytes
- `dspInit` returns a socket - use socket 0 to let ADSP assign one
- Connection listener (`dspCLInit`) is one-shot - re-arm after each accept
- NBP registration is per-socket, not per-application
- NBP entity format: `object:type@zone` (max 32:32:32 chars)

**Callback Architecture (from ADSP.h):**

ADSP uses TWO distinct callback types - do NOT confuse them:

| Callback | Type | Receives | Purpose | When Called |
|----------|------|----------|---------|-------------|
| `ioCompletion` | `ADSPCompletionUPP` | `DSPPBPtr` (A0) | Async operation completion | When dspRead/dspWrite/dspOpen/etc complete |
| `userRoutine` | `ADSPConnectionEventUPP` | `TPCCB` (A1) | Unsolicited connection events | When userFlags change (close, attention, etc) |

**CCB userFlags (from ADSP.h) - set by ADSP, cleared by application:**

| Constant | Value | Meaning |
|----------|-------|---------|
| `eClosed` | 0x80 | Connection closed by remote |
| `eTearDown` | 0x40 | Connection broken (network failure) |
| `eAttention` | 0x20 | Attention message received |
| `eFwdReset` | 0x10 | Forward reset received |

> **Critical:** userFlags do NOT include "data arrived" or "send complete" - those are handled through async completion of dspRead/dspWrite operations.

**Async Pattern Summary:**
| Operation | Mode | Rationale |
|-----------|------|-----------|
| PBOpenSync (.MPP) | Sync | Required for NBP |
| PBOpenSync (.DSP) | Sync | Required for ADSP |
| dspInit | Sync | Fast, one-time CCB setup |
| dspOpen (passive) | **Async** | Can wait indefinitely for connection |
| dspOpen (request) | **Async** | Connection establishment takes time |
| dspWrite | **Async** | High throughput, don't block |
| dspRead | **Async** | Don't block waiting for data |
| dspClose | **Async** | Graceful close may take time |
| dspRemove | Sync | Fast cleanup |
| PRegisterName | Sync | Fast NBP registration |
| PLookupName | Sync/Async | Sync OK for periodic lookup |
| PRemoveName | Sync | Fast NBP unregistration |

### Implementation Complexity Spectrum

**AMENDMENT (2026-02-03):** AppleTalk ADSP implementations follow the same complexity spectrum as MacTCP (Phase 5), since both use interrupt-level callbacks with similar restrictions.

| Approach | Complexity | Diagnostics | Latency | CPU Overhead | Best For |
|----------|-----------|-------------|---------|--------------|----------|
| **Pure Polling** | Low | Minimal | Higher | Low | Simple tools, learning |
| **Flags + Polling** | Medium | Excellent | Medium | Medium | **Production apps (PeerTalk)** |
| **Callback-driven** | High | Complex | Lowest | Higher | Real-time applications |

**Current Plan: Flags + Polling (Middle Ground)**

This plan uses ADSP callbacks (`ioCompletion`, `userRoutine`) to set flags, with all processing in the main poll loop. Benefits:
- **Diagnostics:** Excellent logging via flag-based pattern (callbacks set flags, poll logs)
- **Simplicity:** No reentrancy concerns (callbacks just set flags)
- **Performance:** Good enough for peer discovery (~10ms poll latency acceptable)
- **ISR-safety:** Callbacks obey interrupt-time restrictions (no memory allocation, no logging)

**Alternative: Pure Polling (Simpler)**

Check `ioResult` and `userFlags` directly in poll loop without callback processing:

```c
void poll() {
    if (ccb->pb.ioResult == 0) {          // dspRead/dspWrite completed
        processData();
        issueNextRead();
    }
    if (ccb->userFlags & eClosed) {       // Connection closed
        ccb->userFlags = 0;               // Must clear!
        handleClose();
    }
}
```

**Benefits:** Simpler (no flag management), easier to debug
**Drawbacks:** Higher latency, no diagnostic info from callbacks
**When to use:** Simple applications, learning ADSP

**Alternative: Callback-driven (Complex but Fast)**

Process data directly in callbacks (within ISR restrictions):
- **Benefits:** Lowest latency (~1ms vs ~10ms for polling)
- **Drawbacks:** Very complex (ISR restrictions, reentrancy), hard to debug
- **When to use:** Real-time streaming applications

**PERFORMANCE RECOMMENDATION:** For AppleTalk peer discovery, **Flags + Polling** (current plan) is optimal:
- Callback-driven is only ~9ms faster but significantly more complex
- For peer discovery (not real-time), 10ms poll latency is negligible
- Diagnostics/debuggability more valuable than marginal latency improvement
- Matches MacTCP (Phase 5) and OT (Phase 6) architecture for consistency

**Simplification Path:** To simplify this plan (at cost of diagnostics), remove flag-based logging and just check `ioResult`/`userFlags` directly (pure polling style). Same pattern as MacTCP alternative.

### ISR-Safe Logging Pattern (CRITICAL)

**PT_Log CANNOT be called from ADSP callbacks.** Both `ioCompletion` and `userRoutine` run at interrupt level - the same restrictions as MacTCP ASR apply.

**The Flag-Based Logging Pattern for ADSP:**

```c
/*============================================================================
 * ISR-Safe Logging - Flag + Main Loop Pattern
 *
 * ADSP callbacks (ioCompletion, userRoutine) set flags; main loop logs.
 * NEVER call AT_LOG_*, PT_ERR, PT_DEBUG, etc. from callbacks!
 *============================================================================*/

/* Log event bits (within flags uint16_t) */
#define PT_LOG_EVT_READ_DONE        0x0100
#define PT_LOG_EVT_WRITE_DONE       0x0200
#define PT_LOG_EVT_CONN_CLOSED      0x0400
#define PT_LOG_EVT_ATTENTION        0x0800
#define PT_LOG_EVT_ERROR            0x1000

/*
 * In ioCompletion callback - set flags only, NO logging!
 */
static pascal void pt_adsp_completion(DSPPBPtr pb)
{
    pt_adsp_connection_cold *cold = PT_ADSP_GET_CONTEXT(pb);
    pt_adsp_connection_hot *hot = cold->hot;

    hot->async_result = pb->ioResult;
    hot->flags |= PT_FLAG_ASYNC_COMPLETE;

    /* Mark for logging - main loop will log */
    if (pb->ioResult != noErr) {
        hot->flags |= PT_LOG_EVT_ERROR;
    }
    /* NO AT_LOG_* or PT_LOG_* calls here! */
}

/*
 * In userRoutine callback - set flags only, NO logging!
 */
static pascal void pt_adsp_event(TPCCB ccb)
{
    pt_adsp_connection_cold *cold = (pt_adsp_connection_cold *)ccb;
    pt_adsp_connection_hot *hot = cold->hot;

    if (ccb->userFlags & eClosed) {
        hot->flags |= PT_FLAG_CONNECTION_CLOSED;
        hot->flags |= PT_LOG_EVT_CONN_CLOSED;  /* Mark for logging */
    }
    if (ccb->userFlags & eAttention) {
        hot->flags |= PT_FLAG_ATTENTION;
        hot->flags |= PT_LOG_EVT_ATTENTION;    /* Mark for logging */
    }
    ccb->userFlags = 0;
    /* NO AT_LOG_* or PT_LOG_* calls here! */
}

/*
 * In main poll loop - process log events safely
 *
 * OPTIMIZATION: Uses bitmask iteration when PT_MAX_PEERS <= 32
 */
static void pt_at_process_log_events(pt_at_context *ctx) {
#if PT_MAX_PEERS <= 32
    uint32_t mask = ctx->active_mask;
    int slot;

    while (mask) {
        slot = pt_ffs(mask) - 1;
        mask &= ~(1UL << slot);

        pt_adsp_connection_hot *hot = &ctx->connections[slot];
#else
    int i;
    for (i = 0; i < ctx->active_count; i++) {
        pt_adsp_connection_hot *hot = &ctx->connections[ctx->active_connections[i]];
#endif
        /* Now safe to call PT_Log */
        if (hot->flags & PT_LOG_EVT_CONN_CLOSED) {
            hot->flags &= ~PT_LOG_EVT_CONN_CLOSED;
            AT_LOG_INFO(ctx, "ADSP connection %d: closed by remote", hot->slot_index);
        }
        if (hot->flags & PT_LOG_EVT_ATTENTION) {
            hot->flags &= ~PT_LOG_EVT_ATTENTION;
            AT_LOG_DEBUG(ctx, "ADSP connection %d: attention received", hot->slot_index);
        }
        if (hot->flags & PT_LOG_EVT_ERROR) {
            hot->flags &= ~PT_LOG_EVT_ERROR;
            AT_LOG_ERR(ctx, "ADSP connection %d: error %d",
                       hot->slot_index, hot->async_result);
        }
    }
}

/*
 * Call from pt_at_poll() at appropriate point
 */
int pt_at_poll(pt_at_context *ctx) {
    /* Process any pending log events from callbacks first */
    pt_at_process_log_events(ctx);

    /* ... rest of poll processing ... */
}
```

**Key Rules:**
1. Callbacks set flags (e.g., `PT_LOG_EVT_*`) and store data in pre-allocated fields
2. Callbacks NEVER call `AT_LOG_*`, `PT_ERR`, `PT_DEBUG`, etc.
3. Main loop checks flags, performs logging, then clears flags
4. Error codes stored in `volatile` hot struct fields for main loop access

**Where to use AT_LOG_* macros:**
- ✓ In `pt_at_init()` - initialization is main thread
- ✓ In `pt_at_shutdown()` - shutdown is main thread
- ✓ In poll loop log event processing
- ✓ In synchronous NBP operations (PRegisterName, PLookupName)
- ✗ **NEVER** in `ioCompletion` callback
- ✗ **NEVER** in `userRoutine` callback

**Log Categories for AppleTalk:**
- `PT_LOG_CAT_PLATFORM` - Driver init, CCB creation, low-level errors
- `PT_LOG_CAT_NETWORK` - Connections, NBP registration, data transfer
- `PT_LOG_CAT_PERF` - Performance timing (optional, for profiling)

**Performance Logging (optional, enable with PT_LOG_PERF):**

```c
#ifdef PT_LOG_PERF
#define PERF_LOG_START(ctx, op) \
    unsigned long _perf_start = TickCount()
#define PERF_LOG_END(ctx, op) \
    do { \
        unsigned long _perf_end = TickCount(); \
        PT_DEBUG((ctx)->log, PT_LOG_CAT_PERF, "%s completed in %lu ticks", \
                 (op), _perf_end - _perf_start); \
    } while(0)
#else
#define PERF_LOG_START(ctx, op) ((void)0)
#define PERF_LOG_END(ctx, op) ((void)0)
#endif

/* Usage example in pt_nbp_lookup(): */
PERF_LOG_START(ctx, "NBP lookup");
err = PLookupName(&pb, false);
PERF_LOG_END(ctx, "NBP lookup");
```

Note: TickCount() is safe to call from main thread but NOT from callbacks. Performance logging should only be used in synchronous operations during debugging.

## Goals

1. Implement AppleTalk driver access (.MPP and .DSP)
2. Create NBP-based peer discovery (register and lookup)
3. Implement ADSP connection management with proper ISR-safe patterns
4. Support both passive (listen) and active (connect) modes
5. Test on REAL HARDWARE including LocalTalk

## Session Scope Table

| Session | Focus | Status | Files Created/Modified | Tests | Verify |
|---------|-------|--------|------------------------|-------|--------|
| 7.1 | AppleTalk Init & Types | [OPEN] | `src/appletalk/at_defs.h`, `src/appletalk/at_driver.c` | Real hardware | Drivers open |
| 7.2 | NBP Discovery | [OPEN] | `src/appletalk/nbp_appletalk.c` | Real hardware | Peers appear |
| 7.3 | ADSP Stream Management | [OPEN] | `src/appletalk/adsp_appletalk.c` | Real hardware | CCB lifecycle |
| 7.4 | ADSP Listen | [OPEN] | `src/appletalk/adsp_listen.c` | Real hardware | Accept works |
| 7.5 | ADSP Connect | [OPEN] | `src/appletalk/adsp_connect.c` | Real hardware | Connect works |
| 7.6 | ADSP I/O | [OPEN] | `src/appletalk/adsp_io.c` | Real hardware | Messages work |
| 7.7 | Integration | [OPEN] | All AppleTalk files | Real Mac hardware | End-to-end, MaxBlock leak check |

### Status Key
- **[OPEN]** - Not started
- **[IN PROGRESS]** - Currently being worked on
- **[READY TO TEST]** - Implementation complete, needs verification
- **[DONE]** - Verified and complete

---

## Session 7.1: AppleTalk Init & Types

### Objective
Establish AppleTalk driver access and define all necessary types for ADSP connections.

### Tasks

#### Task 7.1.1: Create `src/appletalk/at_defs.h`

```c
#ifndef PT_APPLETALK_DEFS_H
#define PT_APPLETALK_DEFS_H

/* AppleTalk includes */
#include <AppleTalk.h>
#include <ADSP.h>
#include <string.h>    /* For memset, memcpy, strlen */

/* PeerTalk includes */
#include "pt_log.h"    /* PT_Log for cross-platform logging */

/*============================================================================
 * Constants
 *============================================================================*/

#define PT_ADSP_SEND_QUEUE_SIZE   2048   /* Minimum 100, recommended 2048 */
#define PT_ADSP_RECV_QUEUE_SIZE   2048   /* Minimum 100, recommended 2048 */
#define PT_ADSP_ATTN_BUF_SIZE     570    /* Exactly 570 bytes required */

/* NBP entity limits */
#define PT_NBP_OBJECT_MAX    32
#define PT_NBP_TYPE_MAX      32
#define PT_NBP_ZONE_MAX      32

/* PeerTalk NBP type - all peers register with this type */
#define PT_NBP_TYPE          "\pPeerTalk"

/*============================================================================
 * Portable Bit Operations
 *
 * ffs() and popcount are POSIX/GCC-specific and may not be available on
 * MPW or all Retro68 configurations. Provide fallbacks.
 *============================================================================*/

#ifndef pt_ffs
/* Find first set bit (1-indexed, returns 0 if no bits set) */
static inline int pt_ffs(unsigned int x) {
    int r = 1;
    if (!x) return 0;
    if (!(x & 0xFFFF)) { x >>= 16; r += 16; }
    if (!(x & 0xFF)) { x >>= 8; r += 8; }
    if (!(x & 0xF)) { x >>= 4; r += 4; }
    if (!(x & 0x3)) { x >>= 2; r += 2; }
    if (!(x & 0x1)) { r += 1; }
    return r;
}
#endif

#ifndef pt_popcount
/* Count set bits in a 32-bit word */
static inline int pt_popcount(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555);
    x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
    x = (x + (x >> 4)) & 0x0F0F0F0F;
    return (x * 0x01010101) >> 24;
}
#endif

/*============================================================================
 * ADSP States
 *
 * OPTIMIZATION: Use uint8_t instead of enum to save 3 bytes per connection.
 * On 68k compilers, enums typically become int (4 bytes).
 *============================================================================*/

typedef uint8_t pt_adsp_state;
#define PT_ADSP_UNUSED          0
#define PT_ADSP_INITIALIZING    1
#define PT_ADSP_IDLE            2   /* CCB initialized, ready to listen/connect */
#define PT_ADSP_LISTENING       3   /* Passive open pending */
#define PT_ADSP_CONNECTING      4   /* Active open pending */
#define PT_ADSP_CONNECTED       5   /* Connection established */
#define PT_ADSP_CLOSING         6   /* Close in progress */
#define PT_ADSP_ERROR           7

/*============================================================================
 * Event Flags (set by callbacks, cleared by poll loop)
 *
 * These map to CCB userFlags bits from ADSP.h:
 *   eClosed    = 0x80 (bit 7) - connection closed
 *   eTearDown  = 0x40 (bit 6) - connection broken
 *   eAttention = 0x20 (bit 5) - attention message received
 *   eFwdReset  = 0x10 (bit 4) - forward reset received
 *
 * Note: There is NO "data arrived" userFlag - data arrival is detected
 * through async dspRead completion or by polling recvQPending via dspStatus.
 *
 * OPTIMIZATION: Flags packed into a single uint32_t word for atomic access
 * and better cache behavior on 68k/PPC.
 *============================================================================*/

/* Flag bit positions (packed into volatile uint32_t) */
#define PT_FLAG_CONNECTION_CLOSED   0x01
#define PT_FLAG_ATTENTION           0x02
#define PT_FLAG_FWD_RESET           0x04
#define PT_FLAG_ASYNC_COMPLETE      0x08

/*============================================================================
 * Extended Parameter Block
 *
 * ADSP ioCompletion receives A0 pointing to the parameter block.
 * We prepend a context pointer before the actual DSPParamBlock so we can
 * recover our connection structure.
 *
 * Memory layout:
 *   [context pointer (4 bytes)][DSPParamBlock...]
 *   ^                          ^
 *   |                          A0 points here
 *   Structure start
 *============================================================================*/

typedef struct {
    void           *context;    /* 4-byte context pointer BEFORE param block */
    DSPParamBlock   pb;         /* Actual ADSP parameter block */
} pt_adsp_extended_pb;

/* Access context from parameter block pointer (A0) - for ioCompletion */
#define PT_ADSP_GET_CONTEXT(pb) (((pt_adsp_extended_pb *)((char *)(pb) - sizeof(void *)))->context)

/*============================================================================
 * ADSP Connection Structures - HOT/COLD SEPARATION
 *
 * DATA-ORIENTED DESIGN: Split connection data into hot (frequently accessed
 * during polling) and cold (infrequently accessed I/O data) to maximize
 * cache efficiency on 68k systems with tiny caches (68030 has only 256 bytes).
 *
 * Hot struct target: <32 bytes to fit in a cache line
 * Cold struct: Contains CCB, buffers, param blocks - allocated separately
 *============================================================================*/

/*
 * pt_adsp_connection_hot - Polled every frame (14 bytes)
 *
 * This struct contains ONLY data accessed during the main poll loop.
 * Keep it small so iterating all connections fits in cache.
 *
 * DOD NOTE: The 'peer' pointer is kept in hot struct because it's accessed
 * during message dispatch to route incoming data. IMPORTANT: peer->field
 * accesses cause cache misses on 68k. Only access peer pointer AFTER the
 * poll loop identifies which connection needs processing, not during the
 * iteration itself. If profiling shows peer access is too expensive,
 * consider storing a peer_index instead and using pt_peer_lookup() for
 * cold-path peer field access.
 *
 * FIELD ORDERING: Largest fields first to minimize padding.
 * AddrBlock is 4 bytes: {short aNet; char aNode; char aSocket}
 */
typedef struct pt_adsp_connection_hot {
    struct pt_peer         *peer;           /* 4 bytes: Associated peer (hot: used in dispatch) */
    AddrBlock               remote_addr;    /* 4 bytes: Peer address (net, node, socket) */
    volatile uint16_t       flags;          /* 2 bytes: Event flags (packed) */
    volatile short          async_result;   /* 2 bytes: Result from callback */
    pt_adsp_state           state;          /* 1 byte: Current state */
    uint8_t                 slot_index;     /* 1 byte: Index into cold array */
} pt_adsp_connection_hot;  /* Total: 14 bytes, no padding */

/*
 * pt_adsp_connection_cold - Accessed only during I/O operations
 *
 * IMPORTANT: TRCCB ccb MUST be first member so we can recover connection
 * from CCB pointer in userRoutine callback (which receives TPCCB, not pb).
 */
typedef struct pt_adsp_connection_cold {
    /* Connection Control Block - MUST be first for userRoutine context recovery */
    TRCCB               ccb;            /* Embedded CCB structure (NOT a pointer!) */
    short               ccb_refnum;     /* Reference from dspInit */

    /* Buffers - all must be locked (NewPtrClear) */
    Ptr                 send_queue;     /* Send buffer */
    Ptr                 recv_queue;     /* Receive buffer */
    Ptr                 attn_buffer;    /* Attention buffer (570 bytes) */

    /* Extended parameter block (context + pb) for ioCompletion */
    pt_adsp_extended_pb epb;

    /* Connection info */
    AddrBlock           local_addr;     /* Our address (network, node, socket) */

    /* Back-pointer to hot struct for callback context recovery */
    struct pt_adsp_connection_hot *hot;

    /* User data */
    Ptr                 user_data;
} pt_adsp_connection_cold;

/* Legacy alias for compatibility during transition */
typedef pt_adsp_connection_hot pt_adsp_connection;

/*============================================================================
 * Connection Listener Structures - HOT/COLD SEPARATION
 *
 * Same pattern as connections: separate hot (polling) from cold (I/O) data.
 *============================================================================*/

/*
 * pt_adsp_listener_hot - Polled every frame (10 bytes)
 *
 * OPTIMIZATION: Reordered fields to eliminate internal padding.
 * Fields ordered by size (largest first) within alignment constraints.
 * AddrBlock is 4 bytes: {short aNet; char aNode; char aSocket}
 */
typedef struct pt_adsp_listener_hot {
    AddrBlock               remote_addr;        /* 4 bytes: Pending connection info */
    volatile uint16_t       flags;              /* 2 bytes: Event flags */
    volatile short          async_result;       /* 2 bytes */
    pt_adsp_state           state;              /* 1 byte */
    volatile uint8_t        connection_pending; /* 1 byte (use uint8_t for guaranteed size) */
} pt_adsp_listener_hot;  /* Total: 10 bytes, no padding */

/*
 * pt_adsp_listener_cold - Accessed during accept/deny operations
 *
 * IMPORTANT: TRCCB ccb MUST be first for userRoutine context recovery.
 */
typedef struct pt_adsp_listener_cold {
    /* Connection Listener CCB - MUST be first for userRoutine context recovery */
    TRCCB               ccb;            /* Embedded CCB structure */
    short               ccb_refnum;     /* Reference from dspCLInit */
    pt_adsp_extended_pb epb;            /* Extended param block */

    /* Sync fields needed for accept (copied from dspCLListen completion) */
    unsigned short      remote_cid;
    unsigned long       send_seq;
    unsigned short      send_window;
    unsigned long       attn_send_seq;

    /* Back-pointer to hot struct */
    struct pt_adsp_listener_hot *hot;
} pt_adsp_listener_cold;

/* Legacy alias */
typedef pt_adsp_listener_hot pt_adsp_listener;

/*============================================================================
 * NBP Discovery Structures - HOT/COLD SEPARATION
 *
 * DATA-ORIENTED DESIGN: Split NBP entry data into hot (iteration) and cold
 * (name storage) to maximize cache efficiency during peer iteration.
 *
 * Hot data (6 bytes per entry): slot index + address - touched during iteration
 * Cold data (~33 bytes per entry): name string - touched only for display/matching
 *
 * Large buffers (lookup_buf, entries array) are allocated separately to keep
 * the pt_at_context struct small for cache efficiency.
 *
 * AddrBlock is 4 bytes: {short aNet; char aNode; char aSocket}
 *============================================================================*/

/*
 * pt_nbp_entry_hot - Minimal data for iteration (6 bytes)
 *
 * During peer iteration, we only need the address. Name is looked up
 * from cold storage when needed for display.
 */
typedef struct {
    AddrBlock           addr;           /* 4 bytes: network:node:socket */
    uint8_t             slot_index;     /* 1 byte: Index into cold name array */
    uint8_t             _pad;           /* 1 byte: Alignment to even boundary */
} pt_nbp_entry_hot;  /* Total: 6 bytes */

/*
 * pt_nbp_entry_cold - Name string storage (accessed for display only)
 */
typedef struct {
    char                name[PT_NBP_OBJECT_MAX + 1];  /* 33 bytes: C string */
    uint8_t             _pad;                          /* 1 byte alignment */
} pt_nbp_entry_cold;  /* Total: 34 bytes */

/* Legacy alias for backward compatibility */
typedef pt_nbp_entry_hot pt_nbp_entry;

#define PT_NBP_MAX_ENTRIES 32
#define PT_NBP_LOOKUP_BUF_SIZE 1024

/*
 * pt_nbp_state_hot - Minimal polling data (4 bytes)
 *
 * OPTIMIZATION: Moved local_name (33 bytes) to cold struct since it's
 * only accessed during registration/unregistration, not during polling.
 */
typedef struct {
    uint8_t             entry_count;    /* 1 byte: Max 32 entries, uint8_t suffices */
    uint8_t             registered;     /* 1 byte (use uint8_t for guaranteed size) */
    uint8_t             _pad[2];        /* 2 bytes: Alignment */
} pt_nbp_state_hot;  /* Total: 4 bytes (vs ~38 bytes previously) */

/*
 * pt_nbp_state_cold - Allocated separately, accessed during NBP operations
 */
typedef struct {
    MPPParamBlock       pb;
    NamesTableEntry     nte;            /* 109 bytes for registration */
    unsigned char       local_name[PT_NBP_OBJECT_MAX + 1]; /* 33 bytes: Pascal string (moved from hot) */
    unsigned char      *lookup_buf;     /* Pointer to 1KB buffer (allocated) */
    pt_nbp_entry_hot   *entries;        /* Pointer to hot entry array (allocated) */
    pt_nbp_entry_cold  *entry_names;    /* Pointer to cold name array (allocated) */
    EntityName          temp_entity;    /* Reusable temp for NBP operations (avoid stack alloc) */
} pt_nbp_state_cold;

/* Legacy alias - points to hot state */
typedef pt_nbp_state_hot pt_nbp_state;

/*============================================================================
 * Hot/Cold Access Helpers
 *
 * These macros provide clean access to cold data from hot pointers.
 * The pattern is:
 *   1. Hot structs are in arrays indexed by slot number
 *   2. Cold structs are in parallel arrays at ctx->cold
 *   3. Use slot_index in hot struct to access corresponding cold struct
 *============================================================================*/

/* Get cold connection from hot connection */
#define PT_CONN_COLD(ctx, hot) (&(ctx)->cold->connections[(hot)->slot_index])

/* Get hot connection from cold connection (via back-pointer) */
#define PT_CONN_HOT(cold) ((cold)->hot)

/* Get cold listener from context */
#define PT_LISTENER_COLD(ctx) (&(ctx)->cold->listener)

/* Get NBP cold state from context */
#define PT_NBP_COLD(ctx) (&(ctx)->cold->nbp)

/*============================================================================
 * AppleTalk Platform Context - HOT/COLD SEPARATION
 *
 * The context struct is split to keep frequently-accessed data together
 * and allocate large cold data separately.
 *
 * Memory layout:
 * - pt_at_context (hot): ~200 bytes, kept small for cache efficiency
 * - Cold data: Allocated once at init, pointer stored in context
 *============================================================================*/

/*
 * pt_at_context_cold - Large data structures allocated separately
 *
 * Memory footprint (PT_MAX_PEERS=16, 2048-byte queues):
 *   NBP lookup buffer:    1024 bytes
 *   NBP hot entries:      6*32 = 192 bytes (AddrBlock=4, slot=1, pad=1)
 *   NBP cold names:       34*32 = 1088 bytes
 *   Listener:             ~300 bytes
 *   Connections:          ~4956*16 = 79296 bytes (CCB+queues+buffers)
 *   Total:                ~81.9 KB
 *
 * With minimal 1024-byte queues: ~47 KB total
 *
 * DOD NOTE: nbp_entries[] is "hot" data but lives in cold allocation block.
 * This is acceptable because NBP iteration happens infrequently (during
 * discovery), not during the main poll loop. If profiling shows NBP
 * iteration is hot, consider:
 *   1. Moving nbp_entries to pt_at_context (hot context)
 *   2. Converting to Structure of Arrays: separate addr[] and slot_index[]
 *      arrays (saves 32 bytes padding, better iteration cache behavior)
 */
typedef struct pt_at_context_cold {
    /* NBP cold state */
    pt_nbp_state_cold   nbp;
    unsigned char       nbp_lookup_buf[PT_NBP_LOOKUP_BUF_SIZE];  /* 1KB */
    pt_nbp_entry_hot    nbp_entries[PT_NBP_MAX_ENTRIES];         /* 6*32 = 192 bytes */
    pt_nbp_entry_cold   nbp_entry_names[PT_NBP_MAX_ENTRIES];     /* 34*32 = 1088 bytes */

    /* Listener cold state */
    pt_adsp_listener_cold listener;

    /* Connection cold array */
    pt_adsp_connection_cold connections[PT_MAX_PEERS];
} pt_at_context_cold;

/*
 * pt_at_context - Main context, kept small for cache efficiency
 */
typedef struct pt_at_context {
    /* Logging - MUST be set before calling any pt_at_* functions */
    PT_Log             *log;            /* Cross-platform logging context */

    /* Driver references */
    short               mpp_refnum;     /* .MPP driver refnum */
    short               dsp_refnum;     /* .DSP driver refnum */
    uint8_t             drivers_open;   /* Use uint8_t for guaranteed 1-byte size */
    uint8_t             _pad1;          /* Alignment */

    /* NBP discovery state (hot) */
    pt_nbp_state_hot    nbp;

    /* Connection listener (hot) */
    pt_adsp_listener_hot listener;

    /* Active connections tracking - O(active) polling instead of O(capacity)
     *
     * OPTIMIZATION: Use bitmask instead of index array when PT_MAX_PEERS <= 32.
     * This reduces memory from 32 bytes to 4 bytes and enables efficient
     * iteration using bit operations (ffs, popcount).
     */
#if PT_MAX_PEERS <= 32
    uint32_t            active_mask;    /* Bitmask of active slots (4 bytes) */
#else
    uint8_t             active_count;   /* Number of active connections */
    uint8_t             active_connections[PT_MAX_PEERS];  /* Indices of active slots */
#endif

    /* Connection pool (hot data only) */
    pt_adsp_connection_hot connections[PT_MAX_PEERS];

    /* Pointer to cold data (allocated separately) */
    pt_at_context_cold *cold;

    /* Callback UPPs - THREE required for full operation! */
    ADSPCompletionUPP       completion_upp;          /* For connection ioCompletion (async ops) */
    ADSPCompletionUPP       listener_completion_upp; /* For listener ioCompletion (dspCLListen) */
    ADSPConnectionEventUPP  event_upp;               /* For userRoutine (unsolicited events) */
} pt_at_context;

#endif /* PT_APPLETALK_DEFS_H */
```

#### Task 7.1.2: Create `src/appletalk/at_driver.c`

```c
/*
 * AppleTalk Driver Interface
 *
 * Opens the .MPP and .DSP drivers required for NBP and ADSP.
 */

#include "at_defs.h"
#include <Devices.h>
#include <Memory.h>
#include <string.h>    /* For memset */

/*============================================================================
 * Logging macros - safe wrappers that check for NULL log context
 *
 * WARNING: These macros are NOT ISR-safe! DO NOT call from:
 *   - ioCompletion callback (runs at interrupt level)
 *   - userRoutine callback (runs at interrupt level)
 *
 * Only use from main thread code (init, shutdown, poll loop processing).
 * See "ISR-Safe Logging Pattern" section for callback logging.
 *============================================================================*/
#define AT_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_ERR((ctx)->log, PT_LOG_CAT_PLATFORM, __VA_ARGS__); } while(0)
#define AT_LOG_WARN(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_WARN((ctx)->log, PT_LOG_CAT_PLATFORM, __VA_ARGS__); } while(0)
#define AT_LOG_INFO(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_INFO((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define AT_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/*============================================================================
 * ioCompletion Callback (runs at interrupt level!)
 *
 * Called when async operations (dspRead, dspWrite, dspOpen, etc) complete.
 * Receives DSPPBPtr in A0.
 *
 * CALLING CONVENTION NOTE:
 * ADSP callbacks use register-based calling conventions (parameter in A0/A1).
 * The UPP (Universal Procedure Pointer) system bridges this to our pascal-
 * convention functions. Always use NewADSPCompletionUPP() to create UPPs -
 * never pass raw function pointers to ADSP calls.
 *
 * ISR RULES - same as MacTCP ASR:
 *   - NO memory allocation
 *   - NO synchronous calls
 *   - Only set volatile flags
 *============================================================================*/

static pascal void pt_adsp_completion(DSPPBPtr pb)
{
    pt_adsp_connection_cold *cold;
    pt_adsp_connection_hot *hot;

    /* Recover our context from extended param block
     * Context points to COLD struct (because that's where PB is) */
    cold = (pt_adsp_connection_cold *)PT_ADSP_GET_CONTEXT(pb);
    if (!cold) return;

    /* Get hot struct via back-pointer */
    hot = cold->hot;
    if (!hot) return;

    /* Store result and mark complete in HOT struct */
    hot->async_result = pb->ioResult;
    hot->flags |= PT_FLAG_ASYNC_COMPLETE;

    /* NOTE: Do NOT check userFlags here!
     * userFlags are for unsolicited events delivered through userRoutine,
     * not through ioCompletion. */
}

/*============================================================================
 * userRoutine Callback (runs at interrupt level!)
 *
 * Called for unsolicited connection events (close, attention, etc).
 * Receives TPCCB (CCB pointer) in A1 - NOT the param block!
 *
 * Since TRCCB is the first member of pt_adsp_connection, we can cast
 * directly to recover our context.
 *
 * Per Programming With AppleTalk p.112: userRoutine "is called under
 * the same conditions as a completion routine (at interrupt level)
 * and must follow the same rules as a completion routine."
 *
 * ISR RULES apply - same restrictions as ioCompletion:
 *   - NO memory allocation (NewPtr, NewHandle, etc.)
 *   - NO synchronous calls (PBControlSync, etc.)
 *   - NO Toolbox calls (including BlockMoveData)
 *   - Only set volatile flags and copy to pre-allocated buffers
 *
 * REENTRANCY NOTE:
 * While ADSP typically serializes callbacks, defensive coding assumes
 * this routine could be called while another instance is running.
 * Only atomic flag operations are safe. Our pattern of reading userFlags
 * once, setting our flags, then clearing userFlags is safe because:
 *   1. Each flag is written independently (no read-modify-write)
 *   2. userFlags is cleared atomically at the end
 *   3. Main loop tolerates duplicate flag sets
 *============================================================================*/

static pascal void pt_adsp_event(TPCCB ccb)
{
    pt_adsp_connection_cold *cold;
    pt_adsp_connection_hot *hot;
    UInt8 user_flags;

    if (!ccb) return;

    /* CCB is first member of pt_adsp_connection_cold, so we can cast directly */
    cold = (pt_adsp_connection_cold *)ccb;

    /* Get hot struct via back-pointer */
    hot = cold->hot;
    if (!hot) return;

    /* Read userFlags */
    user_flags = ccb->userFlags;

    /* Map userFlags to our packed flags in HOT struct
     * Using atomic-style OR operations for safety */
    if (user_flags & eClosed) {
        hot->flags |= PT_FLAG_CONNECTION_CLOSED;
    }
    if (user_flags & eTearDown) {
        hot->flags |= PT_FLAG_CONNECTION_CLOSED;  /* Treat same as closed */
    }
    if (user_flags & eAttention) {
        hot->flags |= PT_FLAG_ATTENTION;
    }
    if (user_flags & eFwdReset) {
        hot->flags |= PT_FLAG_FWD_RESET;
    }

    /* CRITICAL: Clear userFlags after reading per Programming With AppleTalk:
     * "Once the attention message has been received, you must set the userFlags
     * to zero. This allows another attention message, or other unsolicited
     * connection event, to occur." */
    ccb->userFlags = 0;
}

/*============================================================================
 * Listener ioCompletion Callback (runs at interrupt level!)
 *
 * Called when dspCLListen completes (incoming connection request arrived).
 * Receives DSPPBPtr in A0.
 *
 * Note: This is separate from pt_adsp_completion because the listener
 * structure is different from connection structures.
 *
 * CALLING CONVENTION NOTE:
 * Same as pt_adsp_completion - uses UPP to bridge register-based calling
 * convention to our pascal function. Use NewADSPCompletionUPP() to create.
 *
 * ISR RULES apply - same restrictions as other completion routines:
 *   - NO memory allocation
 *   - NO synchronous calls
 *   - Only set volatile flags and copy to pre-allocated buffers
 *============================================================================*/

static pascal void pt_listener_completion(DSPPBPtr pb)
{
    pt_adsp_listener_cold *cold;
    pt_adsp_listener_hot *hot;

    /* Recover our context from extended param block
     * Context points to COLD struct (because that's where PB is) */
    cold = (pt_adsp_listener_cold *)PT_ADSP_GET_CONTEXT(pb);
    if (!cold) return;

    /* Get hot struct via back-pointer */
    hot = cold->hot;
    if (!hot) return;

    hot->async_result = pb->ioResult;

    if (pb->ioResult == noErr) {
        /* Connection request arrived - extract synchronization info.
         * Sync fields go to COLD struct (needed for accept),
         * remote_addr goes to HOT struct (needed for logging/decision). */
        cold->remote_cid = pb->u.openParams.remoteCID;
        cold->send_seq = pb->u.openParams.sendSeq;
        cold->send_window = pb->u.openParams.sendWindow;
        cold->attn_send_seq = pb->u.openParams.attnSendSeq;

        /* Remote address needed in hot struct for quick access */
        hot->remote_addr = pb->u.openParams.remoteAddress;
        hot->connection_pending = true;
    }

    hot->flags |= PT_FLAG_ASYNC_COMPLETE;
}

/*============================================================================
 * Driver Management
 *============================================================================*/

int pt_at_init(pt_at_context *ctx, PT_Log *log)
{
    OSErr err;
    ParamBlockRec pb;
    int i;

    if (!ctx) return -1;

    /* Clear context first */
    memset(ctx, 0, sizeof(pt_at_context));

    /* Set log pointer */
    ctx->log = log;

    AT_LOG_DEBUG(ctx, "Initializing AppleTalk drivers");

    /* Allocate cold data block */
    ctx->cold = (pt_at_context_cold *)NewPtrClear(sizeof(pt_at_context_cold));
    if (!ctx->cold) {
        AT_LOG_ERR(ctx, "Failed to allocate cold data block (%ld bytes)",
                   (long)sizeof(pt_at_context_cold));
        return memFullErr;
    }

    /* Initialize hot/cold linkage for connections */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        ctx->connections[i].slot_index = i;
        ctx->connections[i].state = PT_ADSP_UNUSED;
        ctx->cold->connections[i].hot = &ctx->connections[i];
    }

    /* Initialize NBP cold state pointers */
    ctx->cold->nbp.lookup_buf = ctx->cold->nbp_lookup_buf;
    ctx->cold->nbp.entries = ctx->cold->nbp_entries;
    ctx->cold->nbp.entry_names = ctx->cold->nbp_entry_names;

    /* Initialize listener hot/cold linkage */
    ctx->cold->listener.hot = &ctx->listener;

    /* Open .MPP driver (required for NBP) */
    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioNamePtr = "\p.MPP";
    pb.ioParam.ioPermssn = fsCurPerm;

    err = PBOpenSync(&pb);
    if (err != noErr) {
        AT_LOG_ERR(ctx, ".MPP driver open failed: %d", err);
        return err;
    }
    ctx->mpp_refnum = pb.ioParam.ioRefNum;
    AT_LOG_DEBUG(ctx, ".MPP driver opened (refnum=%d)", ctx->mpp_refnum);

    /* Open .DSP driver (required for ADSP) */
    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioNamePtr = "\p.DSP";
    pb.ioParam.ioPermssn = fsCurPerm;

    err = PBOpenSync(&pb);
    if (err != noErr) {
        AT_LOG_ERR(ctx, ".DSP driver open failed: %d", err);
        /* Close .MPP on failure */
        pb.ioParam.ioRefNum = ctx->mpp_refnum;
        PBCloseSync(&pb);
        return err;
    }
    ctx->dsp_refnum = pb.ioParam.ioRefNum;
    AT_LOG_DEBUG(ctx, ".DSP driver opened (refnum=%d)", ctx->dsp_refnum);

    /* Create completion routine UPP (for ioCompletion) */
    ctx->completion_upp = NewADSPCompletionUPP(pt_adsp_completion);
    if (!ctx->completion_upp) {
        AT_LOG_ERR(ctx, "Failed to create completion UPP (memFullErr)");
        pb.ioParam.ioRefNum = ctx->dsp_refnum;
        PBCloseSync(&pb);
        pb.ioParam.ioRefNum = ctx->mpp_refnum;
        PBCloseSync(&pb);
        return memFullErr;
    }

    /* Create event routine UPP (for userRoutine) - DIFFERENT TYPE! */
    ctx->event_upp = NewADSPConnectionEventUPP(pt_adsp_event);
    if (!ctx->event_upp) {
        AT_LOG_ERR(ctx, "Failed to create event UPP (memFullErr)");
        DisposeADSPCompletionUPP(ctx->completion_upp);
        pb.ioParam.ioRefNum = ctx->dsp_refnum;
        PBCloseSync(&pb);
        pb.ioParam.ioRefNum = ctx->mpp_refnum;
        PBCloseSync(&pb);
        return memFullErr;
    }

    /* Create listener completion UPP (separate from connection completion) */
    ctx->listener_completion_upp = NewADSPCompletionUPP(pt_listener_completion);
    if (!ctx->listener_completion_upp) {
        AT_LOG_ERR(ctx, "Failed to create listener UPP (memFullErr)");
        DisposeADSPConnectionEventUPP(ctx->event_upp);
        DisposeADSPCompletionUPP(ctx->completion_upp);
        pb.ioParam.ioRefNum = ctx->dsp_refnum;
        PBCloseSync(&pb);
        pb.ioParam.ioRefNum = ctx->mpp_refnum;
        PBCloseSync(&pb);
        return memFullErr;
    }

    ctx->drivers_open = true;
    AT_LOG_INFO(ctx, "AppleTalk drivers initialized successfully");
    return noErr;
}

void pt_at_shutdown(pt_at_context *ctx)
{
    ParamBlockRec pb;
    long maxblock_before, maxblock_after;

    if (!ctx || !ctx->drivers_open) return;

    AT_LOG_DEBUG(ctx, "Shutting down AppleTalk drivers");

    /* LEAK DETECTION: Always record MaxBlock for debugging on real hardware.
     * Memory leaks on Classic Mac can exhaust the heap quickly. */
    maxblock_before = MaxBlock();
    AT_LOG_DEBUG(ctx, "MaxBlock before shutdown: %ld bytes", maxblock_before);

    /* Dispose all UPPs */
    if (ctx->listener_completion_upp) {
        DisposeADSPCompletionUPP(ctx->listener_completion_upp);
        ctx->listener_completion_upp = NULL;
    }
    if (ctx->event_upp) {
        DisposeADSPConnectionEventUPP(ctx->event_upp);
        ctx->event_upp = NULL;
    }
    if (ctx->completion_upp) {
        DisposeADSPCompletionUPP(ctx->completion_upp);
        ctx->completion_upp = NULL;
    }

    /* Free cold data block */
    if (ctx->cold) {
        DisposePtr((Ptr)ctx->cold);
        ctx->cold = NULL;
    }

    /* Note: Drivers are typically not closed explicitly.
     * They remain open for the application's lifetime.
     * But we track the state for completeness. */

    ctx->drivers_open = false;

    /* LEAK DETECTION: Compare MaxBlock after cleanup.
     * If significantly different from before init, we have a leak.
     * Always log this - essential for debugging on real Mac hardware. */
    maxblock_after = MaxBlock();
    AT_LOG_DEBUG(ctx, "MaxBlock after shutdown: %ld bytes (delta: %ld)",
                 maxblock_after, maxblock_after - maxblock_before);
    if (maxblock_after < maxblock_before - 1024) {
        AT_LOG_WARN(ctx, "Potential memory leak detected: %ld bytes not freed",
                    maxblock_before - maxblock_after);
    }

    AT_LOG_INFO(ctx, "AppleTalk drivers shut down");
}

/*============================================================================
 * Get Local AppleTalk Address
 *
 * Uses GetNodeAddress() to retrieve the local network and node numbers.
 * Note: Socket number is endpoint-specific and set during dspInit/dspCLInit.
 *============================================================================*/

int pt_at_get_local_addr(pt_at_context *ctx, AddrBlock *addr)
{
    OSErr err;
    short node;
    short network;

    if (!ctx || !addr) return -1;

    /* GetNodeAddress returns local network and node numbers.
     * Parameters are pointers to shorts that receive the values.
     * Returns noErr on success. */
    err = GetNodeAddress(&node, &network);
    if (err != noErr) {
        return err;
    }

    addr->aNet = network;
    addr->aNode = node;
    addr->aSocket = 0;  /* Socket is endpoint-specific, set elsewhere */

    return noErr;
}
```

### Acceptance Criteria
1. `.MPP` driver opens successfully (PBOpenSync returns noErr)
2. `.DSP` driver opens successfully
3. All three UPPs created (completion_upp, listener_completion_upp, AND event_upp)
4. `GetNodeAddress()` returns valid network/node numbers
5. Context properly initialized
6. No crashes on init/shutdown cycle

---

## Session 7.2: NBP Discovery

### Objective
Implement NBP-based peer discovery - register our name and lookup other peers.

### NBP Entity Format

```
<PeerName>:PeerTalk@*

Examples:
  "Alice:PeerTalk@*"     - Alice's peer on any zone
  "=:PeerTalk@*"         - Wildcard search for all PeerTalk peers
```

### Tasks

#### Task 7.2.1: Create `src/appletalk/nbp_appletalk.c`

```c
/*
 * NBP Discovery for AppleTalk
 *
 * Uses Name Binding Protocol (NBP) for peer discovery.
 * - Register: Announce our presence as "name:PeerTalk@*"
 * - Lookup: Find all "=:PeerTalk@*" on the network
 */

#include "at_defs.h"
#include <Memory.h>
#include <string.h>

/* Logging macros - use PT_LOG_CAT_NETWORK for NBP operations */
#define NBP_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define NBP_LOG_INFO(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_INFO((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define NBP_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/*============================================================================
 * Name Registration
 *============================================================================*/

int pt_nbp_register(pt_at_context *ctx, const char *peer_name, short socket)
{
    OSErr err;
    MPPParamBlock pb;
    EntityName entity;
    pt_nbp_state_cold *cold;

    if (!ctx || !peer_name || !ctx->cold) return -1;

    cold = PT_NBP_COLD(ctx);

    NBP_LOG_DEBUG(ctx, "Registering NBP name: %s on socket %d", peer_name, socket);

    /* Convert C string to Pascal string in cold storage
     * (local_name moved from hot to cold - only accessed during registration)
     */
    cold->local_name[0] = strlen(peer_name);
    if (cold->local_name[0] > PT_NBP_OBJECT_MAX) {
        cold->local_name[0] = PT_NBP_OBJECT_MAX;
    }
    memcpy(&cold->local_name[1], peer_name, cold->local_name[0]);

    /* Build Names Table Entry (NTE)
     * NBPSetNTE fills the 108-byte NTE structure:
     * - nteAddress (AddrBlock - filled by NBP during registration)
     * - nteName (EntityName - the entity being registered)
     */
    NBPSetNTE(
        (Ptr)&cold->nte,
        cold->local_name,       /* Object: peer name (from cold storage) */
        PT_NBP_TYPE,            /* Type: "PeerTalk" */
        "\p*",                  /* Zone: wildcard = local zone */
        socket                  /* Socket to register */
    );

    /* Register the name */
    memset(&pb, 0, sizeof(pb));
    pb.NBPinterval = 8;         /* Retry interval (8 ticks = ~133ms) */
    pb.NBPcount = 3;            /* Retry count */
    pb.NBPentityPtr = (Ptr)&cold->nte;
    pb.NBPverifyFlag = 1;       /* Verify name is unique */

    err = PRegisterName(&pb, false);  /* Sync call */
    if (err != noErr) {
        NBP_LOG_ERR(ctx, "NBP registration failed for '%s': %d", peer_name, err);
        return err;
    }

    ctx->nbp.registered = true;
    NBP_LOG_INFO(ctx, "NBP registered: %s:PeerTalk@* on socket %d", peer_name, socket);
    return noErr;
}

/*============================================================================
 * Name Unregistration
 *============================================================================*/

int pt_nbp_unregister(pt_at_context *ctx)
{
    OSErr err;
    MPPParamBlock pb;
    pt_nbp_state_cold *cold;

    if (!ctx || !ctx->nbp.registered || !ctx->cold) return noErr;

    cold = PT_NBP_COLD(ctx);

    NBP_LOG_DEBUG(ctx, "Unregistering NBP name");

    /* Build entity to remove using cold storage temp_entity
     * (avoids ~99-byte EntityName on stack)
     */
    NBPSetEntity(
        (Ptr)&cold->temp_entity,
        cold->local_name,       /* From cold storage */
        PT_NBP_TYPE,
        "\p*"
    );

    memset(&pb, 0, sizeof(pb));
    pb.NBPentityPtr = (Ptr)&cold->temp_entity;

    err = PRemoveName(&pb, false);  /* Sync call */
    if (err != noErr) {
        NBP_LOG_ERR(ctx, "NBP unregister failed: %d", err);
    } else {
        NBP_LOG_INFO(ctx, "NBP name unregistered");
    }

    ctx->nbp.registered = false;
    return err;
}

/*============================================================================
 * Peer Lookup
 *
 * Search for all PeerTalk peers on the network.
 * Uses wildcard "=:PeerTalk@*" to find all.
 *============================================================================*/

int pt_nbp_lookup(pt_at_context *ctx)
{
    OSErr err;
    MPPParamBlock pb;
    pt_nbp_state_cold *cold;
    int i;

    if (!ctx || !ctx->cold) return -1;

    cold = PT_NBP_COLD(ctx);

    NBP_LOG_DEBUG(ctx, "Starting NBP lookup for PeerTalk peers");

    /* Build wildcard search entity using cold storage temp_entity
     * OPTIMIZATION: Avoid ~99-byte EntityName on stack per call
     * "=" means "match any object name"
     * "*" means "local zone" (or use "\p*" for all zones)
     */
    NBPSetEntity(
        (Ptr)&cold->temp_entity,
        "\p=",              /* Object: wildcard (any name) */
        PT_NBP_TYPE,        /* Type: "PeerTalk" */
        "\p*"               /* Zone: local zone */
    );

    /* Perform lookup */
    memset(&pb, 0, sizeof(pb));
    pb.NBPinterval = 4;                         /* Retry interval (4 ticks) */
    pb.NBPcount = 2;                            /* Retry count */
    pb.NBPentityPtr = (Ptr)&cold->temp_entity;
    pb.NBPretBuffPtr = (Ptr)cold->lookup_buf;
    pb.NBPretBuffSize = PT_NBP_LOOKUP_BUF_SIZE;
    pb.NBPmaxToGet = PT_NBP_MAX_ENTRIES;

    err = PLookupName(&pb, false);  /* Sync call */
    if (err != noErr && err != nbpNotFound) {
        NBP_LOG_ERR(ctx, "NBP lookup failed: %d", err);
        ctx->nbp.entry_count = 0;
        return err;
    }

    /* Extract results using cold temp_entity to avoid stack allocation */
    ctx->nbp.entry_count = 0;
    for (i = 1; i <= pb.NBPnumGotten && i <= PT_NBP_MAX_ENTRIES; i++) {
        AddrBlock addr;

        err = NBPExtract(
            (Ptr)cold->lookup_buf,
            pb.NBPnumGotten,
            i,
            &cold->temp_entity,  /* Reuse cold storage instead of stack */
            &addr
        );

        if (err == noErr) {
            uint8_t slot = ctx->nbp.entry_count;

            /* Store hot data (address) */
            cold->entries[slot].slot_index = slot;
            cold->entries[slot].addr = addr;

            /* Store cold data (name) - extract object name from entity */
            pt_nbp_get_name(&cold->temp_entity,
                           cold->entry_names[slot].name,
                           PT_NBP_OBJECT_MAX + 1);

            ctx->nbp.entry_count++;
        }
    }

    NBP_LOG_DEBUG(ctx, "NBP lookup found %d peers", ctx->nbp.entry_count);
    return noErr;
}

/*============================================================================
 * Get Discovered Peers
 *
 * Returns hot entry data (address only). Use pt_nbp_get_peer_name() to
 * retrieve the name from cold storage when needed for display.
 *============================================================================*/

int pt_nbp_get_peers(pt_at_context *ctx, pt_nbp_entry_hot *entries, int max_entries)
{
    int count;
    pt_nbp_state_cold *cold;

    if (!ctx || !entries || !ctx->cold) return 0;

    cold = PT_NBP_COLD(ctx);
    count = ctx->nbp.entry_count;
    if (count > max_entries) count = max_entries;

    memcpy(entries, cold->entries, count * sizeof(pt_nbp_entry_hot));
    return count;
}

/*============================================================================
 * Get Peer Name from Cold Storage
 *
 * Retrieves the name string for a peer entry. Call this when you need
 * to display the peer name, not during hot-path iteration.
 *============================================================================*/

int pt_nbp_get_peer_name(pt_at_context *ctx, uint8_t slot_index,
                         char *name_out, int max_len)
{
    pt_nbp_state_cold *cold;

    if (!ctx || !ctx->cold || !name_out || max_len <= 0) return -1;
    if (slot_index >= ctx->nbp.entry_count) return -1;

    cold = PT_NBP_COLD(ctx);

    /* Copy name from cold storage */
    strncpy(name_out, cold->entry_names[slot_index].name, max_len - 1);
    name_out[max_len - 1] = '\0';

    return 0;
}

/*============================================================================
 * Extract Peer Name from Entity
 *============================================================================*/

void pt_nbp_get_name(const EntityName *entity, char *name_out, int max_len)
{
    int len;

    if (!entity || !name_out || max_len <= 0) return;

    /* EntityName.objStr is a Pascal string */
    len = entity->objStr[0];
    if (len >= max_len) len = max_len - 1;

    memcpy(name_out, &entity->objStr[1], len);
    name_out[len] = '\0';
}
```

### Acceptance Criteria
1. `PRegisterName` succeeds with unique name
2. Lookup finds other PeerTalk peers on network
3. `NBPExtract` correctly parses results
4. Unregistration works cleanly
5. Works on both LocalTalk and EtherTalk

---

## Session 7.3: ADSP Stream Management

### Objective
Implement ADSP CCB initialization and cleanup.

### Tasks

#### Task 7.3.1: Create `src/appletalk/adsp_appletalk.c`

```c
/*
 * ADSP Connection Management
 *
 * Manages ADSP Connection Control Blocks (CCBs) and buffers.
 */

#include "at_defs.h"
#include <Devices.h>   /* For PBControl functions */
#include <Memory.h>    /* For NewPtrClear, DisposePtr */
#include <string.h>    /* For memset */

/* Logging macros for ADSP operations */
#define ADSP_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define ADSP_LOG_INFO(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_INFO((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define ADSP_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/*============================================================================
 * Allocate Connection Buffers
 *
 * All buffers must be locked (non-relocatable, non-purgeable).
 *============================================================================*/

/*
 * Buffer allocation for cold connection data
 * Note: These operate on cold struct, not hot struct
 *
 * LOGGING: Allocation failures are logged by the caller (pt_adsp_init_ccb)
 * which has access to the context. This function returns error codes only.
 */
static int pt_adsp_alloc_buffers(pt_adsp_connection_cold *cold, int *failed_buffer)
{
    *failed_buffer = 0;

    /* Allocate send queue */
    cold->send_queue = NewPtrClear(PT_ADSP_SEND_QUEUE_SIZE);
    if (!cold->send_queue) {
        *failed_buffer = 1;  /* send_queue */
        return memFullErr;
    }

    /* Allocate receive queue */
    cold->recv_queue = NewPtrClear(PT_ADSP_RECV_QUEUE_SIZE);
    if (!cold->recv_queue) {
        DisposePtr(cold->send_queue);
        cold->send_queue = NULL;
        *failed_buffer = 2;  /* recv_queue */
        return memFullErr;
    }

    /* Allocate attention buffer (exactly 570 bytes) */
    cold->attn_buffer = NewPtrClear(PT_ADSP_ATTN_BUF_SIZE);
    if (!cold->attn_buffer) {
        DisposePtr(cold->recv_queue);
        DisposePtr(cold->send_queue);
        cold->recv_queue = NULL;
        cold->send_queue = NULL;
        *failed_buffer = 3;  /* attn_buffer */
        return memFullErr;
    }

    return noErr;
}

static void pt_adsp_free_buffers(pt_adsp_connection_cold *cold)
{
    if (cold->attn_buffer) {
        DisposePtr(cold->attn_buffer);
        cold->attn_buffer = NULL;
    }
    if (cold->recv_queue) {
        DisposePtr(cold->recv_queue);
        cold->recv_queue = NULL;
    }
    if (cold->send_queue) {
        DisposePtr(cold->send_queue);
        cold->send_queue = NULL;
    }
}

/*============================================================================
 * Initialize CCB (dspInit)
 *============================================================================*/

/*
 * pt_adsp_init_ccb - Initialize CCB using hot/cold pattern
 *
 * Hot struct contains state and flags; cold struct contains CCB, buffers, PB.
 * The cold struct's CCB must be first member for userRoutine context recovery.
 */
int pt_adsp_init_ccb(pt_at_context *ctx, pt_adsp_connection_hot *conn, short socket)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_connection_cold *cold;

    if (!ctx || !conn) return -1;

    ADSP_LOG_DEBUG(ctx, "Initializing CCB slot %d (requested socket=%d)",
                   conn->slot_index, socket);

    /* Get cold struct for this connection */
    cold = PT_CONN_COLD(ctx, conn);

    /* Clear hot struct (preserve slot_index) */
    {
        uint8_t slot = conn->slot_index;
        memset(conn, 0, sizeof(pt_adsp_connection_hot));
        conn->slot_index = slot;
    }

    /* Clear cold struct (preserve hot pointer) */
    {
        pt_adsp_connection_hot *hot_ptr = cold->hot;
        memset(cold, 0, sizeof(pt_adsp_connection_cold));
        cold->hot = hot_ptr;
    }

    /* Allocate buffers in cold struct */
    {
        int failed_buffer;
        static const char *buffer_names[] = { "", "send_queue", "recv_queue", "attn_buffer" };
        static const int buffer_sizes[] = { 0, PT_ADSP_SEND_QUEUE_SIZE, PT_ADSP_RECV_QUEUE_SIZE, PT_ADSP_ATTN_BUF_SIZE };

        err = pt_adsp_alloc_buffers(cold, &failed_buffer);
        if (err != noErr) {
            ADSP_LOG_ERR(ctx, "CCB buffer allocation failed: %s (%d bytes), MaxBlock=%ld",
                         buffer_names[failed_buffer], buffer_sizes[failed_buffer], MaxBlock());
            return err;
        }
    }

    /* Set up extended param block context - points to COLD struct
     * because userRoutine receives CCB pointer (in cold struct) */
    cold->epb.context = cold;
    pb = &cold->epb.pb;

    /* Initialize CCB with dspInit */
    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspInit;
    pb->u.initParams.ccbPtr = (TPCCB)&cold->ccb;
    pb->u.initParams.userRoutine = ctx->event_upp;  /* ADSPConnectionEventUPP! */
    pb->u.initParams.sendQSize = PT_ADSP_SEND_QUEUE_SIZE;
    pb->u.initParams.sendQueue = cold->send_queue;
    pb->u.initParams.recvQSize = PT_ADSP_RECV_QUEUE_SIZE;
    pb->u.initParams.recvQueue = cold->recv_queue;
    pb->u.initParams.attnPtr = cold->attn_buffer;
    pb->u.initParams.localSocket = socket;  /* 0 = let ADSP assign */

    err = PBControlSync((ParmBlkPtr)pb);
    if (err != noErr) {
        ADSP_LOG_ERR(ctx, "dspInit failed: %d", err);
        pt_adsp_free_buffers(cold);
        return err;
    }

    /* Save assigned socket and refnum */
    cold->ccb_refnum = pb->ccbRefNum;
    cold->local_addr.aSocket = pb->u.initParams.localSocket;

    conn->state = PT_ADSP_IDLE;
    ADSP_LOG_DEBUG(ctx, "CCB initialized: refnum=%d socket=%d",
                   cold->ccb_refnum, cold->local_addr.aSocket);
    return noErr;
}

/*============================================================================
 * Remove CCB (dspRemove)
 *============================================================================*/

int pt_adsp_remove_ccb(pt_at_context *ctx, pt_adsp_connection_hot *conn)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_connection_cold *cold;

    if (!ctx || !conn) return -1;
    if (conn->state == PT_ADSP_UNUSED) return noErr;

    cold = PT_CONN_COLD(ctx, conn);
    ADSP_LOG_DEBUG(ctx, "Removing CCB refnum=%d", cold->ccb_refnum);

    pb = &cold->epb.pb;

    /* Remove the CCB */
    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspRemove;
    pb->ccbRefNum = cold->ccb_refnum;
    pb->u.closeParams.abort = 1;  /* Abort any pending operations */

    err = PBControlSync((ParmBlkPtr)pb);
    if (err != noErr) {
        ADSP_LOG_ERR(ctx, "dspRemove failed: %d", err);
    }

    /* Free buffers regardless of error */
    pt_adsp_free_buffers(cold);

    conn->state = PT_ADSP_UNUSED;
    ADSP_LOG_DEBUG(ctx, "CCB removed");
    return err;
}

/*============================================================================
 * Allocate Connection from Pool
 *============================================================================*/

/*
 * pt_adsp_alloc - Allocate a connection slot and add to active tracking
 *
 * Returns pointer to hot connection struct, or NULL if pool exhausted.
 * Uses bitmask tracking when PT_MAX_PEERS <= 32 for efficiency.
 */
pt_adsp_connection_hot *pt_adsp_alloc(pt_at_context *ctx)
{
    int i;
#if PT_MAX_PEERS <= 32
    int active_count;
#endif

    if (!ctx) return NULL;

    for (i = 0; i < PT_MAX_PEERS; i++) {
        if (ctx->connections[i].state == PT_ADSP_UNUSED) {
            /* Mark as initializing to reserve the slot */
            ctx->connections[i].state = PT_ADSP_INITIALIZING;

#if PT_MAX_PEERS <= 32
            /* Add to active bitmask */
            ctx->active_mask |= (1UL << i);
            active_count = pt_popcount(ctx->active_mask);
            ADSP_LOG_DEBUG(ctx, "Allocated connection slot %d (active=%d)",
                           i, active_count);
#else
            /* Add to active list */
            ctx->active_connections[ctx->active_count] = i;
            ctx->active_count++;
            ADSP_LOG_DEBUG(ctx, "Allocated connection slot %d (active=%d)",
                           i, ctx->active_count);
#endif
            return &ctx->connections[i];
        }
    }

    ADSP_LOG_ERR(ctx, "Connection pool exhausted");
    return NULL;  /* Pool exhausted */
}

/*============================================================================
 * Release Connection to Pool
 *============================================================================*/

/*
 * pt_adsp_release - Release a connection slot and remove from active tracking
 *
 * Uses O(1) bitmask clear when PT_MAX_PEERS <= 32, otherwise swap-and-pop.
 */
void pt_adsp_release(pt_at_context *ctx, pt_adsp_connection_hot *conn)
{
    int slot;
#if PT_MAX_PEERS <= 32
    int active_count;
#else
    int i;
#endif

    if (!ctx || !conn) return;

    slot = conn->slot_index;

    /* Close if connected */
    if (conn->state >= PT_ADSP_CONNECTED) {
        pt_adsp_remove_ccb(ctx, conn);
    }

    conn->state = PT_ADSP_UNUSED;

#if PT_MAX_PEERS <= 32
    /* Remove from active bitmask - O(1) operation */
    ctx->active_mask &= ~(1UL << slot);
    active_count = pt_popcount(ctx->active_mask);
    ADSP_LOG_DEBUG(ctx, "Released connection slot %d (active=%d)",
                   slot, active_count);
#else
    /* Remove from active list using swap-and-pop (O(1) removal)
     * We don't need to maintain order, so swap with last element. */
    for (i = 0; i < ctx->active_count; i++) {
        if (ctx->active_connections[i] == slot) {
            /* Swap with last element and decrement count */
            ctx->active_connections[i] = ctx->active_connections[ctx->active_count - 1];
            ctx->active_count--;
            ADSP_LOG_DEBUG(ctx, "Released connection slot %d (active=%d)",
                           slot, ctx->active_count);
            break;
        }
    }
#endif
}
```

### Acceptance Criteria
1. `dspInit` succeeds and returns valid CCB refnum
2. Buffers allocated and locked
3. Socket assigned (or specified socket used)
4. `dspRemove` cleans up without leaks
5. Multiple connections can be allocated from pool

---

## Session 7.4: ADSP Listen (Passive Mode)

### Objective
Implement connection listening using ADSP's connection listener mechanism.

### Connection Listener Pattern

ADSP provides two listening approaches:

1. **Simple Passive Open** (`dspOpen` with `ocPassive`): One-shot, accepts one connection
2. **Connection Listener** (`dspCLInit` + `dspCLListen`): More control, can deny connections

For PeerTalk's peer-to-peer model, we use the connection listener for flexibility.

### Important: dspCLInit vs dspInit Parameter Differences

Connection listeners (`dspCLInit`) and connection ends (`dspInit`) both use the `TRinitParams` union member of `DSPParamBlock`, but they require **different fields**:

| Field | dspInit (Connection End) | dspCLInit (Connection Listener) |
|-------|--------------------------|----------------------------------|
| `ccbPtr` | Required | Required |
| `localSocket` | Required (0 = auto-assign) | Required (0 = auto-assign) |
| `userRoutine` | Optional (for unsolicited events) | **NOT USED** - ignored |
| `sendQSize` / `sendQueue` | Required | **NOT USED** - ignored |
| `recvQSize` / `recvQueue` | Required | **NOT USED** - ignored |
| `attnPtr` | Required (570 bytes) | **NOT USED** - ignored |

**Why?** Connection listeners don't carry data - they only detect incoming connection requests. The actual data transfer happens on connection ends created via `dspInit` and accepted via `dspOpen`+`ocAccept`. Connection listeners receive incoming request notifications through their `ioCompletion` callback when `dspCLListen` completes, not through `userRoutine`.

### Tasks

#### Task 7.4.1: Create `src/appletalk/adsp_listen.c`

```c
/*
 * ADSP Connection Listener
 *
 * Implements the tilisten-like pattern for ADSP:
 * 1. Initialize connection listener (dspCLInit)
 * 2. Listen for incoming (dspCLListen) - async
 * 3. On connection request: accept (dspOpen+ocAccept) or deny (dspCLDeny)
 * 4. Re-arm listener for next connection
 */

#include "at_defs.h"
#include <Devices.h>   /* For PBControl functions */
#include <Memory.h>    /* For memory operations */
#include <string.h>    /* For memset */

/* Logging macros */
#define LISTEN_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define LISTEN_LOG_INFO(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_INFO((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define LISTEN_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/* Note: pt_listener_completion is defined in at_driver.c alongside other
 * completion routines. The UPP (ctx->listener_completion_upp) is created
 * during pt_at_init() and used here for dspCLListen. */

/*============================================================================
 * Initialize Connection Listener (dspCLInit)
 *============================================================================*/

int pt_adsp_listener_init(pt_at_context *ctx, short socket)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_listener *listener;

    if (!ctx) return -1;

    LISTEN_LOG_DEBUG(ctx, "Initializing connection listener (socket=%d)", socket);

    listener = &ctx->listener;
    memset(listener, 0, sizeof(pt_adsp_listener));

    /* Set up extended param block */
    listener->epb.context = listener;
    pb = &listener->epb.pb;

    /* Initialize connection listener
     * Note: dspCLInit does NOT require send/recv queues, attention buffer,
     * OR userRoutine. Connection listeners don't receive userRoutine callbacks -
     * incoming connection events are delivered through ioCompletion when
     * dspCLListen completes. */
    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspCLInit;
    pb->u.initParams.ccbPtr = (TPCCB)&listener->ccb;
    pb->u.initParams.localSocket = socket;  /* 0 = let ADSP assign */

    err = PBControlSync((ParmBlkPtr)pb);
    if (err != noErr) {
        LISTEN_LOG_ERR(ctx, "dspCLInit failed: %d", err);
        return err;
    }

    listener->ccb_refnum = pb->ccbRefNum;
    listener->state = PT_ADSP_IDLE;

    LISTEN_LOG_INFO(ctx, "Connection listener initialized: refnum=%d socket=%d",
                    listener->ccb_refnum, pb->u.initParams.localSocket);
    return noErr;
}

/*============================================================================
 * Start Listening (dspCLListen) - Async
 *============================================================================*/

int pt_adsp_listener_listen(pt_at_context *ctx)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_listener *listener;

    if (!ctx) return -1;

    listener = &ctx->listener;
    if (listener->state != PT_ADSP_IDLE) {
        LISTEN_LOG_ERR(ctx, "Cannot listen: invalid state %d", listener->state);
        return -1;
    }

    LISTEN_LOG_DEBUG(ctx, "Starting async listen");

    pb = &listener->epb.pb;

    /* Set up async listen */
    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspCLListen;
    pb->ccbRefNum = listener->ccb_refnum;
    pb->ioCompletion = ctx->listener_completion_upp;  /* Use UPP, not raw function ptr! */

    listener->async_pending = true;
    listener->connection_pending = false;
    listener->flags.async_complete = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        LISTEN_LOG_ERR(ctx, "dspCLListen failed: %d", err);
        listener->async_pending = false;
        return err;
    }

    listener->state = PT_ADSP_LISTENING;
    LISTEN_LOG_INFO(ctx, "Now listening for incoming connections");
    return noErr;
}

/*============================================================================
 * Accept Pending Connection
 *
 * Creates a new CCB and accepts the connection.
 * The listener CCB remains valid for re-arming.
 *============================================================================*/

int pt_adsp_listener_accept(pt_at_context *ctx, pt_adsp_connection *conn)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_listener *listener;

    if (!ctx || !conn) return -1;

    listener = &ctx->listener;
    if (!listener->connection_pending) {
        LISTEN_LOG_ERR(ctx, "Cannot accept: no connection pending");
        return -1;
    }

    LISTEN_LOG_INFO(ctx, "Accepting connection from %d.%d:%d",
                    listener->remote_addr.aNet, listener->remote_addr.aNode,
                    listener->remote_addr.aSocket);

    /* Initialize the accepting connection's CCB first */
    err = pt_adsp_init_ccb(ctx, conn, 0);  /* 0 = let ADSP assign socket */
    if (err != noErr) {
        LISTEN_LOG_ERR(ctx, "CCB init for accept failed: %d", err);
        return err;
    }

    pb = &conn->epb.pb;

    /* Accept the connection using dspOpen with ocAccept
     * CRITICAL: Must copy values from listener to accepting CCB:
     *   - remoteCID
     *   - remoteAddress
     *   - sendSeq
     *   - sendWindow
     *   - attnSendSeq
     */
    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspOpen;
    pb->ccbRefNum = conn->ccb_refnum;
    pb->u.openParams.ocMode = ocAccept;
    pb->u.openParams.remoteCID = listener->remote_cid;
    pb->u.openParams.remoteAddress = listener->remote_addr;
    pb->u.openParams.sendSeq = listener->send_seq;
    pb->u.openParams.sendWindow = listener->send_window;
    pb->u.openParams.attnSendSeq = listener->attn_send_seq;
    pb->ioCompletion = ctx->completion_upp;

    conn->async_pending = true;
    conn->flags.async_complete = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        LISTEN_LOG_ERR(ctx, "dspOpen (ocAccept) failed: %d", err);
        conn->async_pending = false;
        pt_adsp_remove_ccb(ctx, conn);
        return err;
    }

    conn->remote_addr = listener->remote_addr;
    conn->state = PT_ADSP_CONNECTING;

    /* Clear pending flag - connection handed off */
    listener->connection_pending = false;
    listener->state = PT_ADSP_IDLE;

    LISTEN_LOG_DEBUG(ctx, "Accept initiated, awaiting completion");
    return noErr;
}

/*============================================================================
 * Deny Pending Connection
 *============================================================================*/

int pt_adsp_listener_deny(pt_at_context *ctx)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_listener *listener;

    if (!ctx) return -1;

    listener = &ctx->listener;
    if (!listener->connection_pending) return noErr;

    LISTEN_LOG_INFO(ctx, "Denying connection from %d.%d:%d",
                    listener->remote_addr.aNet, listener->remote_addr.aNode,
                    listener->remote_addr.aSocket);

    pb = &listener->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspCLDeny;
    pb->ccbRefNum = listener->ccb_refnum;
    pb->u.openParams.remoteCID = listener->remote_cid;
    pb->u.openParams.remoteAddress = listener->remote_addr;

    err = PBControlSync((ParmBlkPtr)pb);
    if (err != noErr) {
        LISTEN_LOG_ERR(ctx, "dspCLDeny failed: %d", err);
    }

    listener->connection_pending = false;
    listener->state = PT_ADSP_IDLE;

    return err;
}

/*============================================================================
 * Remove Listener (dspCLRemove)
 *============================================================================*/

int pt_adsp_listener_remove(pt_at_context *ctx)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_listener *listener;

    if (!ctx) return -1;

    listener = &ctx->listener;
    if (listener->state == PT_ADSP_UNUSED) return noErr;

    LISTEN_LOG_DEBUG(ctx, "Removing connection listener");

    pb = &listener->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspCLRemove;
    pb->ccbRefNum = listener->ccb_refnum;
    pb->u.closeParams.abort = 1;

    err = PBControlSync((ParmBlkPtr)pb);
    if (err != noErr) {
        LISTEN_LOG_ERR(ctx, "dspCLRemove failed: %d", err);
    } else {
        LISTEN_LOG_INFO(ctx, "Connection listener removed");
    }

    listener->state = PT_ADSP_UNUSED;
    return err;
}
```

### Acceptance Criteria
1. `dspCLInit` succeeds
2. `dspCLListen` (async) completes when connection arrives
3. Accept transfers connection to new CCB correctly
4. Listener can be re-armed after accept
5. Deny properly rejects unwanted connections
6. No crashes after 10+ accept cycles

---

## Session 7.5: ADSP Connect (Request Mode)

### Objective
Implement active connection initiation.

### Tasks

#### Task 7.5.1: Create `src/appletalk/adsp_connect.c`

```c
/*
 * ADSP Active Connection
 *
 * Initiates outgoing connections to remote ADSP sockets.
 */

#include "at_defs.h"
#include <string.h>

/* Logging macros */
#define CONN_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define CONN_LOG_INFO(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_INFO((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define CONN_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/*============================================================================
 * Connect to Remote Peer (dspOpen with ocRequest)
 *============================================================================*/

int pt_adsp_connect(pt_at_context *ctx, pt_adsp_connection *conn,
                    AddrBlock *remote_addr)
{
    OSErr err;
    DSPParamBlock *pb;

    if (!ctx || !conn || !remote_addr) return -1;

    CONN_LOG_INFO(ctx, "Connecting to %d.%d:%d",
                  remote_addr->aNet, remote_addr->aNode, remote_addr->aSocket);

    /* Initialize CCB if not already done */
    if (conn->state == PT_ADSP_UNUSED) {
        err = pt_adsp_init_ccb(ctx, conn, 0);
        if (err != noErr) {
            CONN_LOG_ERR(ctx, "CCB init for connect failed: %d", err);
            return err;
        }
    }

    if (conn->state != PT_ADSP_IDLE) {
        CONN_LOG_ERR(ctx, "Cannot connect: invalid state %d", conn->state);
        return -1;  /* Must be idle to connect */
    }

    pb = &conn->epb.pb;

    /* Initiate connection request */
    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspOpen;
    pb->ccbRefNum = conn->ccb_refnum;
    pb->u.openParams.ocMode = ocRequest;
    pb->u.openParams.remoteAddress = *remote_addr;
    pb->u.openParams.filterAddress.aNet = 0;    /* Accept from any */
    pb->u.openParams.filterAddress.aNode = 0;
    pb->u.openParams.filterAddress.aSocket = 0;
    pb->ioCompletion = ctx->completion_upp;

    conn->async_pending = true;
    conn->flags.async_complete = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        CONN_LOG_ERR(ctx, "dspOpen (ocRequest) failed: %d", err);
        conn->async_pending = false;
        return err;
    }

    conn->remote_addr = *remote_addr;
    conn->state = PT_ADSP_CONNECTING;

    CONN_LOG_DEBUG(ctx, "Connect initiated, awaiting completion");
    return noErr;
}

/*============================================================================
 * Check Connection Status
 *
 * Call from poll loop to check if connection completed.
 *============================================================================*/

int pt_adsp_check_connect(pt_at_context *ctx, pt_adsp_connection *conn)
{
    if (!conn) return -1;

    if (conn->state != PT_ADSP_CONNECTING) {
        return -1;
    }

    /* Check if async operation completed */
    if (!conn->flags.async_complete) {
        return 1;  /* Still pending */
    }

    if (conn->async_result == noErr) {
        conn->state = PT_ADSP_CONNECTED;
        CONN_LOG_INFO(ctx, "Connection established: slot=%d, remote=%d.%d:%d",
                      conn->slot_index,
                      conn->remote_addr.aNet,
                      conn->remote_addr.aNode,
                      conn->remote_addr.aSocket);
        return noErr;
    }

    /* Connection failed */
    conn->state = PT_ADSP_ERROR;
    CONN_LOG_ERR(ctx, "Connection failed: slot=%d, remote=%d.%d:%d, err=%d",
                 conn->slot_index,
                 conn->remote_addr.aNet,
                 conn->remote_addr.aNode,
                 conn->remote_addr.aSocket,
                 conn->async_result);
    return conn->async_result;
}

/*============================================================================
 * Close Connection (dspClose)
 *============================================================================*/

int pt_adsp_close(pt_at_context *ctx, pt_adsp_connection *conn)
{
    OSErr err;
    DSPParamBlock *pb;

    if (!ctx || !conn) return -1;

    if (conn->state != PT_ADSP_CONNECTED) {
        CONN_LOG_ERR(ctx, "Cannot close: invalid state %d", conn->state);
        return -1;
    }

    CONN_LOG_INFO(ctx, "Closing connection to %d.%d:%d",
                  conn->remote_addr.aNet, conn->remote_addr.aNode,
                  conn->remote_addr.aSocket);

    pb = &conn->epb.pb;

    /* Initiate graceful close */
    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspClose;
    pb->ccbRefNum = conn->ccb_refnum;
    pb->u.closeParams.abort = 0;  /* Graceful close */
    pb->ioCompletion = ctx->completion_upp;

    conn->async_pending = true;
    conn->flags.async_complete = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        CONN_LOG_ERR(ctx, "dspClose failed: %d", err);
        conn->async_pending = false;
        /* Fall through to abort */
    }

    conn->state = PT_ADSP_CLOSING;
    CONN_LOG_DEBUG(ctx, "Graceful close initiated");
    return noErr;
}

/*============================================================================
 * Abort Connection (immediate close)
 *============================================================================*/

int pt_adsp_abort(pt_at_context *ctx, pt_adsp_connection *conn)
{
    DSPParamBlock *pb;

    if (!ctx || !conn) return -1;

    CONN_LOG_INFO(ctx, "Aborting connection to %d.%d:%d",
                  conn->remote_addr.aNet, conn->remote_addr.aNode,
                  conn->remote_addr.aSocket);

    pb = &conn->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspClose;
    pb->ccbRefNum = conn->ccb_refnum;
    pb->u.closeParams.abort = 1;  /* Abort immediately */

    PBControlSync((ParmBlkPtr)pb);

    conn->state = PT_ADSP_IDLE;
    CONN_LOG_DEBUG(ctx, "Connection aborted");
    return noErr;
}
```

### Acceptance Criteria
1. `dspOpen` with `ocRequest` initiates connection
2. Completion routine fires on success/failure
3. Connection state transitions correctly
4. Graceful close works
5. Abort immediately terminates connection

---

## Session 7.6: ADSP I/O

### Objective
Implement data sending and receiving.

### Tasks

#### Task 7.6.1: Create `src/appletalk/adsp_io.c`

```c
/*
 * ADSP Data I/O
 *
 * Send and receive data over ADSP connections.
 */

#include "at_defs.h"
#include <string.h>

/* Logging macros - refined categories for I/O operations
 * PT_LOG_CAT_SEND for write operations, PT_LOG_CAT_RECV for read operations */
#define IO_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define IO_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define IO_LOG_SEND(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define IO_LOG_RECV(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/*============================================================================
 * Send Data (dspWrite)
 *
 * eom = 1 to mark end of message (for message framing)
 *============================================================================*/

int pt_adsp_write(pt_at_context *ctx, pt_adsp_connection *conn,
                  const void *data, unsigned short len, Boolean eom)
{
    OSErr err;
    DSPParamBlock *pb;

    if (!ctx || !conn || !data || len == 0) return -1;

    if (conn->state != PT_ADSP_CONNECTED) {
        return -1;
    }

    /* Check if previous async still pending */
    if (conn->async_pending) {
        return 1;  /* Try again later */
    }

    pb = &conn->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspWrite;
    pb->ccbRefNum = conn->ccb_refnum;
    pb->u.ioParams.reqCount = len;
    pb->u.ioParams.dataPtr = (Ptr)data;
    pb->u.ioParams.eom = eom;
    pb->ioCompletion = ctx->completion_upp;

    conn->async_pending = true;
    conn->flags.async_complete = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        conn->async_pending = false;
        IO_LOG_ERR(ctx, "dspWrite async start failed: %d (slot=%d, len=%u)",
                   err, conn->slot_index, len);
        return err;
    }

    IO_LOG_SEND(ctx, "dspWrite started: slot=%d, len=%u, eom=%d",
                conn->slot_index, len, eom);
    return noErr;
}

/*============================================================================
 * Check Send Status
 *============================================================================*/

int pt_adsp_write_check(pt_at_context *ctx, pt_adsp_connection *conn,
                        unsigned short *bytes_sent)
{
    if (!conn) return -1;

    if (!conn->flags.async_complete) {
        return 1;  /* Still pending */
    }

    if (bytes_sent) {
        *bytes_sent = conn->epb.pb.u.ioParams.actCount;
    }

    conn->async_pending = false;

    /* Log completion - errors are critical, success is debug */
    if (conn->async_result != noErr) {
        IO_LOG_ERR(ctx, "dspWrite failed: %d (slot=%d)",
                   conn->async_result, conn->slot_index);
    } else {
        IO_LOG_SEND(ctx, "dspWrite complete: slot=%d, sent=%u",
                    conn->slot_index, conn->epb.pb.u.ioParams.actCount);
    }

    return conn->async_result;
}

/*============================================================================
 * Receive Data (dspRead)
 *============================================================================*/

int pt_adsp_read(pt_at_context *ctx, pt_adsp_connection *conn,
                 void *buffer, unsigned short buf_size)
{
    OSErr err;
    DSPParamBlock *pb;

    if (!ctx || !conn || !buffer || buf_size == 0) return -1;

    if (conn->state != PT_ADSP_CONNECTED) {
        return -1;
    }

    if (conn->async_pending) {
        return 1;  /* Operation in progress */
    }

    pb = &conn->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspRead;
    pb->ccbRefNum = conn->ccb_refnum;
    pb->u.ioParams.reqCount = buf_size;
    pb->u.ioParams.dataPtr = (Ptr)buffer;
    pb->ioCompletion = ctx->completion_upp;

    conn->async_pending = true;
    conn->flags.async_complete = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        conn->async_pending = false;
        IO_LOG_ERR(ctx, "dspRead async start failed: %d (slot=%d, buf_size=%u)",
                   err, conn->slot_index, buf_size);
        return err;
    }

    IO_LOG_RECV(ctx, "dspRead started: slot=%d, buf_size=%u",
                conn->slot_index, buf_size);
    return noErr;
}

/*============================================================================
 * Check Receive Status
 *============================================================================*/

int pt_adsp_read_check(pt_at_context *ctx, pt_adsp_connection *conn,
                       unsigned short *bytes_received, Boolean *eom)
{
    if (!conn) return -1;

    if (!conn->flags.async_complete) {
        return 1;  /* Still pending */
    }

    if (bytes_received) {
        *bytes_received = conn->epb.pb.u.ioParams.actCount;
    }
    if (eom) {
        *eom = conn->epb.pb.u.ioParams.eom;
    }

    conn->async_pending = false;

    /* Log completion - errors are critical, success is debug */
    if (conn->async_result != noErr) {
        IO_LOG_ERR(ctx, "dspRead failed: %d (slot=%d)",
                   conn->async_result, conn->slot_index);
    } else {
        IO_LOG_RECV(ctx, "dspRead complete: slot=%d, received=%u, eom=%d",
                    conn->slot_index, conn->epb.pb.u.ioParams.actCount,
                    conn->epb.pb.u.ioParams.eom);
    }

    return conn->async_result;
}

/*============================================================================
 * Check Buffer Space (dspStatus)
 *============================================================================*/

int pt_adsp_get_status(pt_at_context *ctx, pt_adsp_connection *conn,
                       unsigned short *send_free, unsigned short *recv_pending)
{
    OSErr err;
    DSPParamBlock *pb;

    if (!ctx || !conn) return -1;

    pb = &conn->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspStatus;
    pb->ccbRefNum = conn->ccb_refnum;

    err = PBControlSync((ParmBlkPtr)pb);
    if (err != noErr) {
        IO_LOG_ERR(ctx, "dspStatus failed: %d (slot=%d)",
                   err, conn->slot_index);
        return err;
    }

    if (send_free) {
        *send_free = pb->u.statusParams.sendQFree;
    }
    if (recv_pending) {
        *recv_pending = pb->u.statusParams.recvQPending;
    }

    return noErr;
}

/*============================================================================
 * Send Attention Message (out-of-band)
 *============================================================================*/

int pt_adsp_attention(pt_at_context *ctx, pt_adsp_connection *conn,
                      unsigned short code, const void *data, unsigned short len)
{
    OSErr err;
    DSPParamBlock *pb;

    if (!ctx || !conn) return -1;
    if (len > PT_ADSP_ATTN_BUF_SIZE) return -1;

    if (conn->state != PT_ADSP_CONNECTED) {
        return -1;
    }

    pb = &conn->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspAttention;
    pb->ccbRefNum = conn->ccb_refnum;
    pb->u.attnParams.attnCode = code;
    pb->u.attnParams.attnSize = len;
    pb->u.attnParams.attnData = (Ptr)data;
    pb->ioCompletion = ctx->completion_upp;

    conn->async_pending = true;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        conn->async_pending = false;
        return err;
    }

    return noErr;
}
```

### Acceptance Criteria
1. `dspWrite` sends data correctly
2. `dspRead` receives data correctly
3. EOM flag properly marks message boundaries
4. `dspStatus` reports accurate buffer space
5. Attention messages work for out-of-band signaling
6. No data corruption in 100+ message exchanges

---

## Session 7.7: Integration

### Objective
Wire AppleTalk networking into PeerTalk context and implement the main poll loop.

### Tasks

#### Task 7.7.1: Main Poll Loop Integration

```c
/*
 * AppleTalk Integration with PeerTalk
 *
 * Connects NBP discovery and ADSP connections to the main PeerTalk API.
 */

#include "at_defs.h"
#include "peertalk.h"

/* Logging macros for integration layer */
#define INT_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define INT_LOG_WARN(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_WARN((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define INT_LOG_INFO(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_INFO((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define INT_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) PT_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/*============================================================================
 * Poll Loop - Call Regularly from Main Application
 *============================================================================*/

void pt_at_poll(pt_at_context *ctx)
{
    pt_adsp_state old_state;
#if PT_MAX_PEERS <= 32
    uint32_t mask;
    int slot;
#else
    int i;
#endif

    if (!ctx || !ctx->drivers_open) return;

    /* Check listener for incoming connections */
    if (ctx->listener.state == PT_ADSP_LISTENING) {
        if (ctx->listener.flags & PT_FLAG_ASYNC_COMPLETE) {
            ctx->listener.flags &= ~PT_FLAG_ASYNC_COMPLETE;  /* Clear flag */

            if (ctx->listener.async_result == noErr &&
                ctx->listener.connection_pending) {
                INT_LOG_INFO(ctx, "Incoming connection from %d.%d:%d",
                             ctx->listener.remote_addr.aNet,
                             ctx->listener.remote_addr.aNode,
                             ctx->listener.remote_addr.aSocket);

                /* Incoming connection! Accept it */
                pt_adsp_connection *conn = pt_adsp_alloc(ctx);
                if (conn) {
                    if (pt_adsp_listener_accept(ctx, conn) == noErr) {
                        /* Notify application of new peer */
                        /* ... callback here ... */
                    }
                } else {
                    /* No room - deny */
#if PT_MAX_PEERS <= 32
                    INT_LOG_WARN(ctx, "Connection pool full (%d/%d), denying connection from %d.%d:%d",
                                 pt_popcount(ctx->active_mask), PT_MAX_PEERS,
                                 ctx->listener.remote_addr.aNet,
                                 ctx->listener.remote_addr.aNode,
                                 ctx->listener.remote_addr.aSocket);
#else
                    INT_LOG_WARN(ctx, "Connection pool full (%d/%d), denying connection from %d.%d:%d",
                                 ctx->active_count, PT_MAX_PEERS,
                                 ctx->listener.remote_addr.aNet,
                                 ctx->listener.remote_addr.aNode,
                                 ctx->listener.remote_addr.aSocket);
#endif
                    pt_adsp_listener_deny(ctx);
                }

                /* Re-arm listener for next connection */
                INT_LOG_DEBUG(ctx, "Re-arming listener after accept/deny");
                pt_adsp_listener_listen(ctx);
            } else if (ctx->listener.async_result != noErr) {
                INT_LOG_ERR(ctx, "Listener async failed: %d", ctx->listener.async_result);
                /* Re-arm listener on error */
                ctx->listener.state = PT_ADSP_IDLE;
                pt_adsp_listener_listen(ctx);
            }
        }
    }

    /* Check all ACTIVE connections only - O(active) not O(capacity)
     * This is a key optimization for cache efficiency on 68k systems.
     * We only touch hot data for active connections. */
#if PT_MAX_PEERS <= 32
    mask = ctx->active_mask;
    while (mask) {
        slot = pt_ffs(mask) - 1;
        mask &= ~(1UL << slot);
        pt_adsp_connection_hot *conn = &ctx->connections[slot];
#else
    for (i = 0; i < ctx->active_count; i++) {
        int slot = ctx->active_connections[i];
        pt_adsp_connection_hot *conn = &ctx->connections[slot];
#endif

        old_state = conn->state;

        /* Check for state transitions */
        if (conn->state == PT_ADSP_CONNECTING) {
            if (conn->flags & PT_FLAG_ASYNC_COMPLETE) {
                conn->flags &= ~PT_FLAG_ASYNC_COMPLETE;  /* Clear flag */

                if (conn->async_result == noErr) {
                    conn->state = PT_ADSP_CONNECTED;
                    INT_LOG_INFO(ctx, "Connection established to %d.%d:%d",
                                 conn->remote_addr.aNet, conn->remote_addr.aNode,
                                 conn->remote_addr.aSocket);
                    /* Notify: connected */
                } else {
                    conn->state = PT_ADSP_ERROR;
                    INT_LOG_ERR(ctx, "Connection failed to %d.%d:%d: %d",
                                conn->remote_addr.aNet, conn->remote_addr.aNode,
                                conn->remote_addr.aSocket, conn->async_result);
                    /* Notify: connection failed */
                }
            }
        }

        if (conn->state == PT_ADSP_CONNECTED) {
            pt_adsp_connection_cold *cold = PT_CONN_COLD(ctx, conn);

            /* Check for connection close (from userRoutine via userFlags)
             * NOTE: This is the ISR-safe pattern - userRoutine (interrupt level)
             * sets the flag, main loop (here) processes it and logs. */
            if (conn->flags & PT_FLAG_CONNECTION_CLOSED) {
                conn->flags &= ~PT_FLAG_CONNECTION_CLOSED;
                conn->state = PT_ADSP_CLOSING;
                INT_LOG_INFO(ctx, "Remote close from %d.%d:%d",
                             conn->remote_addr.aNet, conn->remote_addr.aNode,
                             conn->remote_addr.aSocket);
                /* Notify: peer disconnected */
            }

            /* Check for attention message (from userRoutine)
             * Logged at DEBUG level since attention is protocol-specific */
            if (conn->flags & PT_FLAG_ATTENTION) {
                conn->flags &= ~PT_FLAG_ATTENTION;
                INT_LOG_DEBUG(ctx, "Attention received from %d.%d:%d (code=%u, size=%u)",
                              conn->remote_addr.aNet, conn->remote_addr.aNode,
                              conn->remote_addr.aSocket,
                              cold->ccb.attnCode, cold->ccb.attnSize);
                /* Handle attention data in cold->ccb.attnPtr
                 * Code is in cold->ccb.attnCode
                 * Size is in cold->ccb.attnSize */
            }

            /* Check for forward reset - this is a warning condition */
            if (conn->flags & PT_FLAG_FWD_RESET) {
                conn->flags &= ~PT_FLAG_FWD_RESET;
                INT_LOG_WARN(ctx, "Forward reset from %d.%d:%d",
                             conn->remote_addr.aNet, conn->remote_addr.aNode,
                             conn->remote_addr.aSocket);
                /* Handle forward reset if needed */
            }
        }

        if (conn->state == PT_ADSP_CLOSING) {
            if (conn->flags & PT_FLAG_ASYNC_COMPLETE) {
                conn->flags &= ~PT_FLAG_ASYNC_COMPLETE;

                INT_LOG_DEBUG(ctx, "Connection close complete for %d.%d:%d",
                              conn->remote_addr.aNet, conn->remote_addr.aNode,
                              conn->remote_addr.aSocket);

                pt_adsp_remove_ccb(ctx, conn);
                conn->state = PT_ADSP_UNUSED;
            }
        }

        /* Log state transitions at DEBUG level */
        if (conn->state != old_state && conn->state != PT_ADSP_UNUSED) {
            INT_LOG_DEBUG(ctx, "Connection %d state: %d -> %d", slot, old_state, conn->state);
        }
    }
}

/*============================================================================
 * Periodic NBP Discovery
 *
 * Call this periodically (e.g., every 5 seconds) to refresh peer list.
 *============================================================================*/

void pt_at_discover(pt_at_context *ctx)
{
    int old_count;

    if (!ctx || !ctx->drivers_open) return;

    old_count = ctx->nbp.entry_count;

    /* Perform NBP lookup */
    pt_nbp_lookup(ctx);

    /* Log discovery results if changed */
    if (ctx->nbp.entry_count != old_count) {
        INT_LOG_INFO(ctx, "Discovery: %d peers found (was %d)",
                     ctx->nbp.entry_count, old_count);
    }

    /* Compare with known peers and notify of changes */
    /* ... implementation depends on peer tracking ... */
}

/*============================================================================
 * Startup/Shutdown
 *============================================================================*/

int pt_at_start(pt_at_context *ctx, PT_Log *log, const char *local_name, short socket)
{
    int err;

    if (!ctx) return -1;

    INT_LOG_INFO(ctx, "Starting AppleTalk networking as '%s' on socket %d",
                 local_name, socket);

    /* Initialize drivers */
    err = pt_at_init(ctx, log);
    if (err != noErr) {
        return err;
    }

    /* Register with NBP */
    err = pt_nbp_register(ctx, local_name, socket);
    if (err != noErr) {
        INT_LOG_ERR(ctx, "NBP registration failed, shutting down");
        pt_at_shutdown(ctx);
        return err;
    }

    /* Start connection listener */
    err = pt_adsp_listener_init(ctx, socket);
    if (err != noErr) {
        INT_LOG_ERR(ctx, "Listener init failed, shutting down");
        pt_nbp_unregister(ctx);
        pt_at_shutdown(ctx);
        return err;
    }

    err = pt_adsp_listener_listen(ctx);
    if (err != noErr) {
        INT_LOG_ERR(ctx, "Listener start failed, shutting down");
        pt_adsp_listener_remove(ctx);
        pt_nbp_unregister(ctx);
        pt_at_shutdown(ctx);
        return err;
    }

    INT_LOG_INFO(ctx, "AppleTalk networking started successfully");
    return noErr;
}

void pt_at_stop(pt_at_context *ctx)
{
    int conn_count = 0;
#if PT_MAX_PEERS <= 32
    uint32_t mask;
    int slot;
#else
    int i;
#endif

    if (!ctx) return;

    INT_LOG_INFO(ctx, "Stopping AppleTalk networking");

    /* Close all connections - O(active) using bitmask/active list
     *
     * OPTIMIZATION: Use active_mask instead of iterating all PT_MAX_PEERS slots.
     * This is critical on 68k where iterating unused slots wastes precious cycles.
     */
#if PT_MAX_PEERS <= 32
    mask = ctx->active_mask;
    while (mask) {
        /* Find first set bit (ffs returns 1-indexed, or 0 if no bits set) */
        slot = pt_ffs(mask) - 1;
        mask &= ~(1UL << slot);  /* Clear the bit */

        pt_adsp_abort(ctx, &ctx->connections[slot]);
        pt_adsp_remove_ccb(ctx, &ctx->connections[slot]);
        conn_count++;
    }
    ctx->active_mask = 0;
#else
    for (i = 0; i < ctx->active_count; i++) {
        int slot = ctx->active_connections[i];
        pt_adsp_abort(ctx, &ctx->connections[slot]);
        pt_adsp_remove_ccb(ctx, &ctx->connections[slot]);
        conn_count++;
    }
    ctx->active_count = 0;
#endif

    if (conn_count > 0) {
        INT_LOG_DEBUG(ctx, "Closed %d active connections", conn_count);
    }

    /* Remove listener */
    pt_adsp_listener_remove(ctx);

    /* Unregister from NBP */
    pt_nbp_unregister(ctx);

    /* Shutdown drivers */
    pt_at_shutdown(ctx);

    INT_LOG_INFO(ctx, "AppleTalk networking stopped");
}
```

### Acceptance Criteria
1. Full startup sequence completes
2. NBP discovery runs periodically
3. Listener accepts incoming connections
4. Connections properly established in both directions
5. Data flows bidirectionally
6. Clean shutdown without leaks
7. MaxBlock same before/after 50+ operations

---

## Phase 7 Complete Checklist

**Session 7.1 - Init & Types:**
- [ ] `.MPP` driver opens successfully
- [ ] `.DSP` driver opens successfully
- [ ] All three UPPs created (completion_upp, listener_completion_upp, AND event_upp)
- [ ] `GetNodeAddress()` returns valid network/node numbers
- [ ] Type definitions compile without errors

**Session 7.2 - NBP Discovery:**
- [ ] `PRegisterName` succeeds
- [ ] `PLookupName` finds peers
- [ ] `NBPExtract` parses results correctly
- [ ] `PRemoveName` cleans up
- [ ] Works on LocalTalk and EtherTalk

**Session 7.3 - ADSP Stream Management:**
- [ ] `dspInit` returns valid CCB refnum
- [ ] Buffers allocated and locked
- [ ] `dspRemove` cleans up without leaks
- [ ] Pool allocation/release works

**Session 7.4 - ADSP Listen:**
- [ ] `dspCLInit` succeeds
- [ ] `dspCLListen` async completes on connection
- [ ] Accept transfers connection to new CCB
- [ ] Listener re-arms after accept
- [ ] Deny works correctly

**Session 7.5 - ADSP Connect:**
- [ ] `dspOpen` with `ocRequest` works
- [ ] Completion fires on success/failure
- [ ] Graceful close works
- [ ] Abort works

**Session 7.6 - ADSP I/O:**
- [ ] `dspWrite` sends data
- [ ] `dspRead` receives data
- [ ] EOM flag works for framing
- [ ] `dspStatus` reports buffer state
- [ ] Attention messages work

**Session 7.7 - Integration:**
- [ ] Full startup succeeds
- [ ] Discovery finds peers
- [ ] Incoming connections accepted
- [ ] Outgoing connections succeed
- [ ] Data flows both directions
- [ ] Clean shutdown
- [ ] MaxBlock same before/after 50+ operations
- [ ] Works on LocalTalk (serial)
- [ ] Works on EtherTalk
- [ ] **Tested on real 68k Mac hardware**

---

## Common Pitfalls

1. **Two different callback types** - `ioCompletion` uses `ADSPCompletionUPP` (receives pb in A0), `userRoutine` uses `ADSPConnectionEventUPP` (receives CCB in A1). Do NOT mix them up!

2. **userFlags are NOT "data arrived"** - userFlags only contain eClosed, eTearDown, eAttention, eFwdReset. Data arrival is detected through async dspRead completion or dspStatus polling.

3. **userRoutine runs at interrupt level** - Same restrictions as MacTCP ASR: NO memory allocation, NO synchronous calls, only set volatile flags

4. **Clear userFlags after reading** - Per Programming With AppleTalk: "Failure to clear the userFlags will result in your connection hanging."

5. **CCB must be first in connection struct** - userRoutine receives TPCCB, so TRCCB must be first member to allow `(pt_adsp_connection *)ccb` cast

6. **Parameter blocks must persist** - For async operations, the DSPParamBlock must remain valid until completion

7. **Re-arm listener after accept/deny** - Connection listener is one-shot; must call `dspCLListen` again

8. **Connection listeners don't use userRoutine** - Unlike connection ends (dspInit), connection listeners (dspCLInit) don't receive userRoutine callbacks. Incoming connection requests are signaled through ioCompletion when dspCLListen completes.

9. **Copy sync fields for accept mode** - When accepting, must copy remoteCID, remoteAddress, sendSeq, sendWindow, attnSendSeq from listener

10. **Socket 0 means "let ADSP assign"** - Pass 0 to get automatic socket assignment; check returned value

11. **NBP EntityName is 102 bytes, NTE is 108 bytes** - EntityName uses Str32Field (34 bytes each × 3 = 102). NamesTableEntry is 108 bytes per AppleTalk.h (Ptr qNext=4 + NTElement=104). Don't confuse EntityName (for lookup) with NamesTableEntry (for registration).

12. **AddrBlock is 4 bytes, not 6** - AddrBlock is `{short aNet; char aNode; char aSocket}` = 4 bytes. A common mistake is to assume 2+2+2 bytes, but node and socket are single bytes.

13. **Free slot search is O(capacity)** - Consider using a free slot bitmask (invert of active_mask) for O(1) allocation: `free_mask = ~ctx->active_mask & ((1UL << PT_MAX_PEERS) - 1); slot = pt_ffs(free_mask) - 1;`

14. **memset on param blocks is expensive** - DSPParamBlock is a large union (~100+ bytes). Consider zeroing only used fields, or pre-initialize static fields once at init.

13. **Zone wildcard is asterisk (`*`)** - For local zone, use `\p*` not `\p=`

14. **EOM for message framing** - Set eom=1 on final write of each logical message

15. **Attention buffer exactly 570 bytes** - ADSP requires exactly this size for attention messages

16. **Listener needs its own UPP** - The connection listener uses a different completion routine than connection ends. Create a separate `listener_completion_upp` using `NewADSPCompletionUPP()`. Do NOT cast function pointers directly - this will crash on PPC systems using Code Fragment Manager.

17. **Use GetNodeAddress() for local address** - Don't rely on callback data for the local AppleTalk address. Call `GetNodeAddress()` to get the network and node numbers directly.

18. **Use pt_ffs() and pt_popcount() for portability** - The standard ffs() and __builtin_popcount() are not available on all Classic Mac compilers. Use the provided portable implementations in at_defs.h.

---

## References

- `books/Programming_With_AppleTalk_1991.txt`:
  - Chapter 5: Name Binding Protocol (NBP) - lines 2090-2700
  - Chapter 8: AppleTalk Data Stream Protocol (ADSP) - lines 4970-7350
- `ADSP.h` (Retro68 MPW Interfaces) - Authoritative callback type definitions
- `PHASE-5-MACTCP.md` - Pattern reference for async handling and ASR safety
- `PHASE-6-OPENTRANSPORT.md` - Pattern reference for state machine and flags
- `CSEND-LESSONS.md` - Completion routine gotchas (same ISR rules apply)
- `CLAUDE.md` - Quick reference for interrupt-safe operations
