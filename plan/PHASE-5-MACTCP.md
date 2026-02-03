# PHASE 5: MacTCP Networking

> **Status:** OPEN
> **Depends on:** Phase 2 (Protocol layer for shared types and encoding), optionally Phase 3 (Advanced Queues)
> **Required Phase 2 Addition:** Session 5.9 requires `pt_peer_find_by_name()` - must be added to Phase 2, Session 2.2
> **Validates against:** Phase 4 (POSIX - use as protocol reference and cross-platform test partner)
> **Produces:** Fully functional PeerTalk on System 6/7 with MacTCP
> **Risk Level:** HIGH (complex async model, memory constraints, no debugging)
> **Estimated Sessions:** 10 (8 TCP/IP + 2 Multi-Transport)
> **Unified Library:** `libpeertalk_mactcp_at.a` provides MacTCP + AppleTalk in one library
> **CSend Lessons:** See [CSEND-LESSONS.md](CSEND-LESSONS.md) Part A for critical MacTCP gotchas
> **Review Applied:** 2026-01-27 (implementability review - GetMyIPAddr.h removal, DOD hot/cold split, single-pass polling)
> **Review Applied:** 2026-01-29 (plan review - UDP buffer calc fix, hot struct alignment, state/completion logging, DOD peer_idx optimization)
> **Review Applied:** 2026-01-29 (plan review - peer_idx mandatory, completion log flags, pt_state_name helper, logging gaps fixed)

## Target Hardware

| Machine | System | RAM | MacTCP | Notes |
|---------|--------|-----|--------|-------|
| Mac SE | System 6.0.8 | 4MB | 2.1 | Primary test target - memory constrained |
| Performa 6200 | System 7.5.3 | 8MB+ | 2.1 | Secondary test target - "Road Apple" model, test extensively |

**Mac SE 4MB Memory Budget:**
- System 6.0.8: ~400-600KB
- Application partition: ~2.5-3MB available
- MacTCP internal buffers: ~64KB (allocated based on machine memory per MPG)
- PeerTalk target: 4-6 peer connections max

**Performa 6200 Note:** This is a "Road Apple" Mac (5200/6200/6300 series). Per VintageMacTCPIP_Reference.md, these models "may not run later OT versions." MacTCP should work but test extensively on this specific hardware.

**MacTCP Internal Buffer Allocation:**
Per MacTCP Programmer's Guide: "The amount of buffer space allocated is based on the amount of memory in the machine... depending on whether 1, 2, or 4 megabytes (MB) of memory is installed." Your 4MB Mac SE gets the largest internal buffer allocation, which helps with fragmentation/reassembly.

## Overview

Phase 5 implements the MacTCP networking layer for Classic Macintosh (68k). This is the most challenging phase due to MacTCP's unique asynchronous model, interrupt-time restrictions, and memory constraints.

### Reference Implementations

**AMENDMENT (2026-02-03):** These open-source projects demonstrate working MacTCP code patterns. Use them as references when implementing this phase.

| Project | Location | Pattern | Best For |
|---------|----------|---------|----------|
| **LaunchAPPL** | `/home/matthew/Retro68/LaunchAPPL/Server/MacTCPStream.cc` | Pure polling (ioResult checks) | Learning MacTCP, simple pattern |
| **Subtext** | External (GitHub) | Cooperative threading (uthread) | Complex apps, multiple connections |
| **CSend** | External (Mac FTP sites) | ASR + flags (similar to PeerTalk) | Production reference |

**LaunchAPPL** is particularly valuable:
- Included with Retro68 (readily available)
- Minimal viable MacTCP networking (~158 lines)
- WDS/RDS patterns verified (MacTCPStream.cc:96-98)
- Cleanup spin-wait pattern (MacTCPStream.cc:61-79)
- Polling ioResult pattern (MacTCPStream.cc:117-156)

**Key LaunchAPPL Lessons Applied to This Plan:**
- WDS two-element array pattern for zero-copy sends
- Spin-wait for pending async operations before TCPRelease
- Buffer sizes: 8KB TCP receive, 4KB work buffer
- Stack-allocated WDS safe for synchronous TCPSend

**When to Reference:** If implementation details are unclear, read LaunchAPPL's corresponding code section. All line numbers current as of 2026-02-03.

**Key Insights from MacTCP Programmer's Guide:**
- ASRs (Asynchronous Notification Routines) run at interrupt level - NO memory allocation, NO synchronous MacTCP calls
- **But:** "An ASR routine can issue additional asynchronous MacTCP driver calls" - useful for chaining operations
- TCPPassiveOpen only handles ONE connection at a time per stream (unlike BSD sockets)
- 64 stream limit for the entire system (shared by ALL apps)
- Buffer memory belongs to MacTCP while stream is open
- TCPNoCopyRcv is high-performance but requires TCPRcvBfrReturn
- UDPRead requires UDPBfrReturn after processing
- Command timeout minimum is 2 seconds (0 = infinite)
- TCP buffer sizing: optimal = 4 × MTU + 1024 (query UDPMTU for physical MTU)
- TCP buffer minimum: 4096 bytes, recommended 8192 for character apps, 16384 for block apps
- UDP buffer minimum: 2048 bytes, recommended 2N + 256 where N is largest expected datagram
- MacTCP internal buffers scale with machine RAM (1/2/4MB configurations)

**Async Pattern Summary:**
| Operation | Mode | Rationale |
|-----------|------|-----------|
| TCPCreate | Sync | Fast, one-time setup |
| TCPPassiveOpen | **Async** | Can wait indefinitely for connection |
| TCPActiveOpen | **Async** | Can take 30+ seconds to establish |
| TCPSend | Sync* | Simplifies buffer lifetime (*async for high throughput) |
| TCPNoCopyRcv | Sync | Only called when ASR indicates data buffered |
| TCPClose | **Async** | Can block 30+ seconds waiting for FIN-ACK |
| TCPRelease | Sync | Fast cleanup |
| UDPCreate | Sync | Fast, one-time setup |
| UDPRead | Sync | Only called when ASR indicates data buffered |
| UDPWrite | Sync | Fast for small packets |
| UDPRelease | Sync | Fast cleanup |

**Architecture Decision:**
We will use a **state machine with polling** approach. The ASR will only set flags; all actual processing happens in the main poll loop.

**Note on Subtext comparison:** Subtext uses a more complex **cooperative threading (uthread)** model with setjmp/longjmp and 50KB stacks per thread. Subtext can busy-wait on async operations because other threads get CPU via uthread_yield(). PeerTalk is simpler - a peer discovery library doesn't need cooperative threading. Our ASR-sets-flags approach is the MacTCP Programmer's Guide recommended pattern for single-threaded applications and matches PROJECT_GOALS.md's `PeerTalk_Poll()` model.

### Implementation Complexity Spectrum

**AMENDMENT (2026-02-03):** MacTCP implementations exist across a spectrum of complexity/capability trade-offs. PeerTalk's Mac platform implementation uses a middle-ground approach optimized for production use.

| Approach | Complexity | Diagnostics | Latency | CPU Overhead | Best For |
|----------|-----------|-------------|---------|--------------|----------|
| **Pure Polling** | Low | Minimal | Higher | Low | Simple tools, learning, single connections |
| **Flags + Polling** | Medium | Excellent | Medium | Medium | **Production apps (PeerTalk)** |
| **Callback-driven** | High | Complex | Lowest | Higher | Real-time applications, high throughput |
| **Cooperative Threading** | Very High | Good | Low | High | Complex apps (Subtext) |

**Current Plan: Flags + Polling (Middle Ground)**

This plan uses ASR callbacks to set atomic flags, with all processing in the main poll loop. Benefits:
- **Diagnostics:** Excellent logging via flag-based pattern (ASR sets flags, poll logs)
- **Simplicity:** No reentrancy concerns (ASR just sets flags)
- **Performance:** Good enough for peer discovery (~10ms poll latency acceptable)
- **Debuggability:** State machine visible, easy to trace

**Alternative: Pure Polling (LaunchAPPL Style)**

LaunchAPPL uses pure polling - no ASR callback processing, just check `ioResult` in poll loop:

```c
void idle() {
    if (readPB.ioResult == 0) {      // TCPPassiveOpen or TCPRcv completed
        if (!connected) {
            connected = true;         // Accept completed
            startReading();
        } else {
            processData();            // Data arrived
            startReading();
        }
    } else if (readPB.ioResult == connectionClosing) {
        connected = false;
        startListening();
    }
}
```

**Benefits:** Simpler (no flag management), easier to debug
**Drawbacks:** Higher latency (only checked during poll), no diagnostic info from ASR
**When to use:** Simple applications, learning MacTCP, single connection

**Alternative: Callback-driven (Complex but Fast)**

Process data directly in ASR callback (or queue deferred tasks):
- **Benefits:** Lowest latency (~1ms vs ~10ms for polling)
- **Drawbacks:** Complex (reentrancy, ISR restrictions), hard to debug
- **When to use:** Real-time applications, high-frequency updates

**PERFORMANCE RECOMMENDATION:** The user specified preferring the most performant option. For MacTCP, the **Flags + Polling** approach (current plan) strikes the best balance:
- Callback-driven is only ~9ms faster but significantly more complex
- For peer discovery (not real-time), 10ms poll latency is negligible
- Diagnostics/debuggability more valuable than marginal latency improvement
- Simpler code = fewer bugs = better performance in practice

**Simplification Path:** To simplify this plan (at cost of diagnostics), remove flag-based logging and just check `ioResult` directly (LaunchAPPL style). See `.claude/rules/mactcp.md` for this alternative pattern.

**Data-Oriented Design (DOD) Architecture:**

Classic Mac 68k CPUs have very limited cache (68030 has only 256 bytes data cache; 68000/68020 have none). Memory bandwidth is severely constrained (2-10 MB/s). This plan applies DOD principles to minimize memory access during the hot poll path:

| Pattern | Rationale | Implementation |
|---------|-----------|----------------|
| **Hot/Cold Split** | Separate frequently-accessed fields from rarely-accessed | `pt_tcp_stream_hot` (~14 bytes) vs `pt_tcp_stream_cold` (~200 bytes) |
| **Bitfield Flags** | Single memory access for all ASR flags | `pt_asr_flags` as `volatile uint8_t` with bit masks |
| **uint8_t Enums** | Avoid alignment padding from int-sized enums | `pt_stream_state` as `uint8_t` with #defines |
| **Parallel Arrays** | Contiguous hot data for cache-efficient iteration | `tcp_hot[N]` separate from `tcp_cold[N]` |
| **Single-Pass Poll** | One loop with state dispatch vs multiple filter loops | `pt_mactcp_poll()` dispatches by `hot->state` |

**Memory Impact:**
- Polling 8 streams: ~1600 bytes → ~112 bytes accessed per poll cycle
- Hot struct fits in 68030's 256-byte cache
- Cold struct only loaded when needed for I/O operations

### ISR-Safe Logging Pattern (CRITICAL)

**PT_Log CANNOT be called from ASR callbacks.** PT_Log calls may allocate memory, perform file I/O, or call Toolbox functions - all forbidden at interrupt level.

**The Flag-Based Logging Pattern:**

```c
/*============================================================================
 * ISR-Safe Logging - Flag + Main Loop Pattern
 *
 * ASR callbacks set flags; main loop does the actual logging.
 * This is the ONLY safe way to get diagnostic info from interrupt context.
 *============================================================================*/

/* ISR-safe logging fields are now integrated into pt_tcp_stream_hot
 * (defined in Task 5.1.1) with proper field ordering for 68k alignment:
 *
 * DOD Trade-off: These log fields are in hot struct (not cold) because:
 * - ASR only has hot struct pointer via userDataPtr
 * - Moving to cold would require index lookup in ASR (added complexity at interrupt time)
 * - Log fields add 3 bytes to hot struct but avoid pointer chase in ASR
 * - Alternative: parallel pt_log_buffer[] array indexed by stream_idx
 *
 * The current design prioritizes ASR simplicity over minimal hot struct size.
 *
 * Field placement in hot struct (see Task 5.1.1):
 *   volatile int16_t  log_error_code;   @ offset 10 (even offset for 68k)
 *   volatile uint8_t  log_events;       @ offset 16 (after other uint8_t fields)
 */

/* Log event bits (set by ASR or completion routines) */
#define PT_LOG_EVT_DATA_ARRIVED     0x01
#define PT_LOG_EVT_CONN_CLOSED      0x02
#define PT_LOG_EVT_TERMINATED       0x04
#define PT_LOG_EVT_ICMP             0x08
#define PT_LOG_EVT_ERROR            0x10
#define PT_LOG_EVT_LISTEN_COMPLETE  0x20  /* TCPPassiveOpen completed */
#define PT_LOG_EVT_CONNECT_COMPLETE 0x40  /* TCPActiveOpen completed */
#define PT_LOG_EVT_CLOSE_COMPLETE   0x80  /* TCPClose completed */

/*
 * In ASR callback - set flags only, NO PT_Log calls!
 */
static pascal void tcp_asr(StreamPtr stream, unsigned short event,
                           Ptr userDataPtr, unsigned short terminReason,
                           ICMPReport *icmpMsg) {
    pt_tcp_stream_hot *hot = (pt_tcp_stream_hot *)userDataPtr;

    switch (event) {
    case TCPDataArrival:
        hot->asr_flags |= PT_ASR_DATA_ARRIVED;
        hot->log_events |= PT_LOG_EVT_DATA_ARRIVED;  /* Mark for logging */
        break;
    case TCPTerminate:
        hot->asr_flags |= PT_ASR_CONN_CLOSED;
        hot->log_events |= PT_LOG_EVT_TERMINATED;
        hot->log_error_code = terminReason;  /* Save reason for logging */
        break;
    }
    /* NO PT_LOG_* calls here! */
}

/*
 * In main poll loop - process log events safely
 */
static void pt_mactcp_process_log_events(pt_mactcp_data *md, PT_Log *log) {
    int i;
    for (i = 0; i < PT_MAX_PEERS; i++) {
        pt_tcp_stream_hot *hot = &md->tcp_hot[i];

        if (hot->log_events == 0) continue;

        /* Now safe to call PT_Log - we're in main thread */
        if (hot->log_events & PT_LOG_EVT_DATA_ARRIVED) {
            PT_DEBUG(log, PT_LOG_CAT_NETWORK, "TCP stream %d: data arrived", i);
        }
        if (hot->log_events & PT_LOG_EVT_TERMINATED) {
            PT_INFO(log, PT_LOG_CAT_NETWORK,
                    "TCP stream %d: terminated (reason=%d)",
                    i, hot->log_error_code);
        }
        if (hot->log_events & PT_LOG_EVT_ERROR) {
            PT_ERR(log, PT_LOG_CAT_PLATFORM,
                   "TCP stream %d: error %d", i, hot->log_error_code);
        }

        /* Completion routine events (added 2026-01-29 review) */
        if (hot->log_events & PT_LOG_EVT_LISTEN_COMPLETE) {
            if (hot->async_result == noErr) {
                PT_INFO(log, PT_LOG_CAT_CONNECT,
                        "TCP stream %d: listen completed (connection accepted)", i);
            } else {
                PT_WARN(log, PT_LOG_CAT_CONNECT,
                        "TCP stream %d: listen failed (err=%d)", i, hot->async_result);
            }
        }
        if (hot->log_events & PT_LOG_EVT_CONNECT_COMPLETE) {
            if (hot->async_result == noErr) {
                PT_INFO(log, PT_LOG_CAT_CONNECT,
                        "TCP stream %d: connect completed", i);
            } else {
                PT_WARN(log, PT_LOG_CAT_CONNECT,
                        "TCP stream %d: connect failed (err=%d)", i, hot->async_result);
            }
        }
        if (hot->log_events & PT_LOG_EVT_CLOSE_COMPLETE) {
            PT_DEBUG(log, PT_LOG_CAT_NETWORK,
                     "TCP stream %d: close completed (result=%d)", i, hot->async_result);
        }

        /* Clear after logging */
        hot->log_events = 0;
    }
}

/*
 * Call from pt_mactcp_poll() at appropriate point
 */
int pt_mactcp_poll(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);

    /* Process any pending log events from ASR first */
    pt_mactcp_process_log_events(md, ctx->log);

    /* ... rest of poll processing ... */
}
```

**Key Rules:**
1. ASR sets `log_events` flags and stores data (error codes) in pre-allocated fields
2. ASR NEVER calls PT_Log, PT_ERR, PT_DEBUG, etc.
3. Main loop checks `log_events` and performs actual logging
4. Main loop clears flags after logging
5. Error codes and other diagnostic data stored in hot struct for main loop access

**Additional Logging Requirements (from 2026-01-29 review):**

6. **Log state transitions** - When `hot->state` changes, log at DEBUG level.
   Use this helper function (defined in mactcp_driver.c):
   ```c
   /* State name helper for logging */
   static const char *pt_state_name(pt_stream_state state) {
       static const char *names[] = {
           "UNUSED", "CREATING", "IDLE", "LISTENING",
           "CONNECTING", "CONNECTED", "CLOSING", "RELEASING"
       };
       return (state < 8) ? names[state] : "UNKNOWN";
   }

   /* Macro to set state with logging (call from main loop only) */
   #define PT_SET_STATE(ctx, hot, idx, new_state) do { \
       pt_stream_state _old = (hot)->state; \
       (hot)->state = (new_state); \
       PT_LOG_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, \
           "Stream %d: %s -> %s", (idx), \
           pt_state_name(_old), pt_state_name(new_state)); \
   } while(0)
   ```

7. **Log completion routine results** - Completion routines set flags and store ioResult.
   The completion routine callback receives `TCPiopb *pb` with the result:
   ```c
   /* Example: TCPPassiveOpen completion routine */
   static pascal void pt_tcp_listen_completion(TCPiopb *pb) {
       /* Recover hot struct from extended param block */
       pt_tcp_stream_hot *hot = PT_TCP_GET_HOT_FROM_PB(pb);

       /* Store result for main loop logging (ISR-safe) */
       hot->async_result = pb->ioResult;
       hot->log_events |= PT_LOG_EVT_LISTEN_COMPLETE;
       hot->async_pending = 0;
   }
   ```
   Main loop processes these in `pt_mactcp_process_log_events()`.

8. **Log peer state transitions** - When calling `pt_peer_set_state()`:
   ```c
   PT_LOG_INFO(log, PT_LOG_CAT_CONNECT, "Peer \"%s\": %s → %s",
               peer->info.name, pt_peer_state_name(old), pt_peer_state_name(new));
   ```

9. **Log buffer allocation fallbacks** - When buffer allocation retries:
   ```c
   PT_LOG_WARN(log, PT_LOG_CAT_MEMORY, "Buffer alloc failed (%lu), trying %lu",
               requested_size, fallback_size);
   ```

10. **Log RDS buffer lifecycle** - DEBUG level for acquire/return:
    ```c
    PT_LOG_DEBUG(log, PT_LOG_CAT_NETWORK, "Stream %d: RDS buffer acquired");
    PT_LOG_DEBUG(log, PT_LOG_CAT_NETWORK, "Stream %d: RDS buffer returned");
    ```

**Log Categories for MacTCP:**
- `PT_LOG_CAT_PLATFORM` - Driver init, stream creation, low-level errors
- `PT_LOG_CAT_NETWORK` - Connections, data transfer, ASR events
- `PT_LOG_CAT_CONNECT` - Connection establishment and teardown, peer state
- `PT_LOG_CAT_MEMORY` - Buffer allocation, memory warnings
- `PT_LOG_CAT_DISCOVERY` - Discovery packet send/receive (prefer over NETWORK for filtering)
- `PT_LOG_CAT_PROTOCOL` - Protocol encode/decode operations (cross-platform debugging)
- `PT_LOG_CAT_SEND` - Data transmission operations (UDPWrite, TCPSend)
- `PT_LOG_CAT_RECV` - Data reception operations (UDPRead, TCPNoCopyRcv)

**Additional Logging Points (added 2026-01-29 review):**

11. **Log protocol operations** - Use `PT_LOG_CAT_PROTOCOL` for encode/decode:
    ```c
    PT_LOG_DEBUG(log, PT_LOG_CAT_PROTOCOL,
        "Discovery packet encoded: type=%d len=%d", pkt.type, len);
    PT_LOG_DEBUG(log, PT_LOG_CAT_PROTOCOL,
        "Discovery packet decoded: type=%d name=\"%s\"", pkt.type, pkt.name);
    ```

12. **Log send/receive operations** - For cross-platform protocol validation:
    ```c
    PT_LOG_DEBUG(log, PT_LOG_CAT_SEND,
        "UDPWrite: %u bytes to 0x%08lX:%u", len, dest_ip, dest_port);
    PT_LOG_DEBUG(log, PT_LOG_CAT_RECV,
        "UDPRead: %u bytes from 0x%08lX:%u", data_len, from_ip, from_port);
    ```

13. **Log buffer return failures** - Critical for leak detection:
    ```c
    err = PBControl((ParmBlkPtr)&return_pb, false);
    if (err != noErr) {
        PT_LOG_ERR(log, PT_LOG_CAT_NETWORK,
            "UDPBfrReturn failed: %d (buffer leak possible)", err);
    }
    ```

14. **Log abort actions in timeout handlers** - For debugging recovery:
    ```c
    PT_LOG_WARN(log, PT_LOG_CAT_NETWORK,
        "Stream %d: issuing TCPAbort due to %s timeout", idx,
        (is_connect ? "connect" : "close"));
    ```

## Goals

1. Implement MacTCP driver access and stream management
2. Create polling-based state machine for connection lifecycle
3. Implement UDP discovery with ASR-safe receive
4. Implement TCP with proper buffer management
5. Test on REAL HARDWARE (not emulator - Basilisk II is for build validation only)

## Session Scope Table

| Session | Focus | Status | Files Created/Modified | Tests | Verify |
|---------|-------|--------|------------------------|-------|--------|
| 5.1 | MacTCP Driver & Types | [OPEN] | `src/mactcp/mactcp_defs.h`, `src/mactcp/mactcp_driver.c` | Real hardware | PBOpen succeeds |
| 5.2 | UDP Stream | [OPEN] | `src/mactcp/udp_mactcp.c` | Real hardware | Stream lifecycle |
| 5.3 | UDP Discovery | [OPEN] | `src/mactcp/discovery_mactcp.c` | Real hardware | Peers appear |
| 5.4 | TCP Stream | [OPEN] | `src/mactcp/tcp_mactcp.c` | Real hardware | Stream lifecycle |
| 5.5 | TCP Listen | [OPEN] | `src/mactcp/tcp_listen.c` | Real hardware | Accept works |
| 5.6 | TCP Connect | [OPEN] | `src/mactcp/tcp_connect.c` | Real hardware | Connect works |
| 5.7 | TCP I/O | [OPEN] | `src/mactcp/tcp_io.c` | Real hardware | Messages work |
| 5.8 | TCP/IP Integration | [OPEN] | All MacTCP TCP/IP files | Real 68k Mac hardware | End-to-end TCP/IP, MaxBlock check |
| **5.9** | **AppleTalk Integration** | [OPEN] | `src/mactcp/mactcp_multi.c` | Real hardware | Links with Phase 7 AT code |
| **5.10** | **Unified Library Build** | [OPEN] | `Makefile.retro68` update | Real hardware | `libpeertalk_mactcp_at.a` builds |

### Status Key
- **[OPEN]** - Not started
- **[IN PROGRESS]** - Currently being worked on
- **[READY TO TEST]** - Implementation complete, needs verification
- **[DONE]** - Verified and complete

---

## Session 5.1: MacTCP Driver & Types

### Objective
Establish MacTCP driver access and define all necessary types for the state machine.

### Alternative Pattern: Pure Polling (LaunchAPPL Style)

**AMENDMENT (2026-02-03):** This session defines types for the **Flags + Polling** pattern (current plan). For simpler implementations, consider the **Pure Polling** alternative used by LaunchAPPL.

**Pure Polling Pattern (Verified: LaunchAPPL MacTCPStream.cc:117-156):**

Instead of ASR callbacks setting flags, just check `ioResult` directly in poll loop:

```c
/* Simplified type - no flags needed */
typedef struct pt_tcp_stream_hot {
    StreamPtr         stream;
    pt_stream_state   state;
    struct pt_peer   *peer;
} pt_tcp_stream_hot;  /* ~9 bytes vs 14 bytes with flags */

/* Poll loop - check ioResult directly */
void poll_streams() {
    for (i = 0; i < PT_MAX_PEERS; i++) {
        pt_tcp_stream_hot *hot = &md->tcp_hot[i];
        pt_tcp_stream_cold *cold = &md->tcp_cold[i];

        if (hot->state == PT_STREAM_LISTENING) {
            if (cold->pb.ioResult == 0) {  /* TCPPassiveOpen completed */
                hot->state = PT_STREAM_CONNECTED;
                /* No ASR flag to check - just process directly */
            }
        }

        if (hot->state == PT_STREAM_CONNECTED) {
            if (cold->pb.ioResult == 0) {  /* Data arrived */
                process_data();
            } else if (cold->pb.ioResult == connectionClosing) {
                hot->state = PT_STREAM_CLOSING;
            }
        }
    }
}
```

**Trade-offs:**
- **Simpler:** No ASR flags, no log_events, no async_result tracking
- **Smaller:** Hot struct ~9 bytes vs 14 bytes (35% smaller)
- **Less diagnostic:** Can't log ASR events (no info from interrupt context)
- **Same latency:** Both check state in poll loop (~10ms typical)

**When to use Pure Polling:**
- Learning MacTCP (simpler mental model)
- Single connection applications (like LaunchAPPL)
- When diagnostics/logging not critical
- Prototyping before adding full instrumentation

**Current Plan (Flags + Polling):**
- **More diagnostic:** ASR sets log_events for detailed logging
- **Better debugging:** Async completions tracked via flags
- **Production-ready:** Full instrumentation for troubleshooting
- **Slightly larger:** Hot struct 14 bytes (still fits in 68030 cache)

**PERFORMANCE:** Both patterns have identical poll latency. Pure polling is slightly simpler but lacks diagnostics. For production code, the 5-byte overhead of Flags + Polling is worth it for the diagnostic capability. For learning or simple apps, Pure Polling is cleaner.

### Tasks

#### Task 5.1.1: Create `src/mactcp/mactcp_defs.h`

```c
#ifndef PT_MACTCP_DEFS_H
#define PT_MACTCP_DEFS_H

/* MacTCP includes - these must be in the Retro68 include path
 *
 * Note: GetMyIPAddr.h no longer exists as a separate header.
 * All IP configuration (GetAddrParamBlock, ipctlGetAddr) is
 * consolidated into MacTCP.h per the Developer Notes section.
 */
#include <MacTCP.h>
#include <AddressXlation.h>

/* Stream states (mirrors TCP state machine)
 *
 * DOD: Use uint8_t instead of enum to save 1-3 bytes per stream
 * and avoid alignment padding on 68k. Enum defaults to int (2-4 bytes).
 */
typedef uint8_t pt_stream_state;
#define PT_STREAM_UNUSED      0
#define PT_STREAM_CREATING    1
#define PT_STREAM_IDLE        2
#define PT_STREAM_LISTENING   3
#define PT_STREAM_CONNECTING  4
#define PT_STREAM_CONNECTED   5
#define PT_STREAM_CLOSING     6
#define PT_STREAM_RELEASING   7

/* ASR event flags - set by ASR, cleared by poll loop
 *
 * DOD: Use single volatile uint8_t bitfield instead of 5 separate bytes.
 * On 68k, each byte access requires a separate memory fetch. Collapsing
 * to one byte means a single memory access for all flag checks.
 * Original: 5-6 bytes (with padding). Now: 1 byte.
 */
typedef volatile uint8_t pt_asr_flags;
#define PT_ASR_DATA_ARRIVED     0x01
#define PT_ASR_CONN_CLOSED      0x02
#define PT_ASR_URGENT_DATA      0x04
#define PT_ASR_ICMP_RECEIVED    0x08
#define PT_ASR_TIMEOUT          0x10

/* TCP stream wrapper - from MacTCP Programmer's Guide
 *
 * DOD: Split into hot/cold structs for cache efficiency on 68k.
 * Hot struct contains fields checked every poll (~14 bytes).
 * Cold struct contains large/rarely-accessed data (~200+ bytes).
 *
 * On 68030 with 256-byte cache, polling 8 streams was loading
 * ~1600 bytes per poll cycle. With this split, only ~112 bytes.
 */

/* Hot path data - checked every poll loop iteration (~14 bytes)
 *
 * DOD: Fields ordered to ensure int16_t at even offsets (68k 2-byte alignment).
 * All 4-byte fields first, then 2-byte fields, then 1-byte fields.
 *
 * MANDATORY: Use int8_t peer_idx instead of struct pt_peer* pointer.
 * This saves 3 bytes per stream (24 bytes for 8 streams) and eliminates
 * a pointer dereference in the hot poll path. For 68030's 256-byte cache,
 * this is a significant improvement. Trade-off: requires bounds checking
 * but improves cache locality. Use -1 for "no peer".
 */
typedef struct pt_tcp_stream_hot {
    StreamPtr         stream;           /* 4 bytes - offset 0 */
    volatile int16_t  async_result;     /* 2 bytes - offset 4 */
    volatile int16_t  log_error_code;   /* 2 bytes - offset 6 (ISR-safe logging) */
    pt_stream_state   state;            /* 1 byte  - offset 8 */
    pt_asr_flags      asr_flags;        /* 1 byte  - offset 9 */
    volatile uint8_t  async_pending;    /* 1 byte  - offset 10 */
    uint8_t           rds_outstanding;  /* 1 byte  - offset 11 */
    volatile uint8_t  log_events;       /* 1 byte  - offset 12 (ISR-safe logging) */
    int8_t            peer_idx;         /* 1 byte  - offset 13 (-1 = no peer) */
    /* Total: 14 bytes, 2-byte aligned, fits in minimal cache line */
    /* Polling 8 streams loads ~112 bytes (was ~144 with pointer) */
} pt_tcp_stream_hot;

/* Helper macro to get peer from index (bounds-checked) */
#define PT_PEER_FROM_IDX(ctx, idx) \
    (((idx) >= 0 && (idx) < (ctx)->max_peers) ? &(ctx)->peers[(idx)] : NULL)

/* Cold path data - accessed during setup, I/O, teardown */
typedef struct pt_tcp_stream_cold {
    TCPiopb           pb;               /* Parameter block for calls (~100 bytes) */

    /* Buffer management (must be locked, non-relocatable) */
    Ptr               rcv_buffer;       /* Passed to TCPCreate */
    unsigned long     rcv_buffer_size;

    /* For TCPNoCopyRcv */
    rdsEntry          rds[6];           /* Read Data Structure */

    /* Connection info */
    ip_addr           local_ip;
    tcp_port          local_port;
    ip_addr           remote_ip;
    tcp_port          remote_port;

    /* Close timeout tracking */
    unsigned long     close_start;      /* Ticks when close was initiated */

    /* From TCPTerminate ASR - reason codes from MacTCP.h:
     * TCPRemoteAbort=2, TCPNetworkFailure=3, TCPSecPrecMismatch=4,
     * TCPULPTimeoutTerminate=5, TCPULPAbort=6, TCPULPClose=7, TCPServiceError=8
     */
    volatile unsigned short terminate_reason;

    /* User data for callbacks */
    Ptr               user_data;
} pt_tcp_stream_cold;

/* UDP stream wrapper
 *
 * DOD: Split into hot/cold structs matching TCP pattern.
 */

/* Hot path data - checked every poll loop iteration (~10 bytes)
 * DOD: Fields ordered largest-to-smallest to eliminate alignment padding.
 */
typedef struct pt_udp_stream_hot {
    StreamPtr         stream;           /* 4 bytes - offset 0 */
    volatile int16_t  async_result;     /* 2 bytes - offset 4 */
    pt_stream_state   state;            /* 1 byte  - offset 6 */
    pt_asr_flags      asr_flags;        /* 1 byte  - offset 7 */
    volatile uint8_t  async_pending;    /* 1 byte  - offset 8 */
    volatile uint8_t  data_ready;       /* 1 byte  - offset 9 */
    /* Total: 10 bytes, no padding */
} pt_udp_stream_hot;

/* Cold path data - accessed during setup, I/O, teardown */
typedef struct pt_udp_stream_cold {
    UDPiopb           pb;               /* Parameter block (~80 bytes) */

    Ptr               rcv_buffer;
    unsigned long     rcv_buffer_size;

    ip_addr           local_ip;
    udp_port          local_port;

    /* Pending receive data */
    ip_addr           last_remote_ip;
    udp_port          last_remote_port;
    Ptr               last_data;
    unsigned short    last_data_len;

    Ptr               user_data;
} pt_udp_stream_cold;

/* Platform-specific context extension
 *
 * DOD: Uses hot/cold split for TCP and UDP streams.
 * Hot arrays are contiguous for cache-efficient polling.
 * Cold arrays are separate and only accessed during I/O operations.
 */
typedef struct {
    /* MacTCP driver reference */
    short             driver_refnum;

    /* System limits (queried via TCPGlobalInfo at init) */
    unsigned short    max_tcp_connections;  /* System-wide limit */
    unsigned short    max_udp_streams;      /* System-wide limit */

    /* Local network info */
    ip_addr           local_ip;
    long              net_mask;

    /* Universal Procedure Pointers for callbacks
     * CRITICAL: MacTCP.h requires UPPs for all notifyProc and ioCompletion
     * fields. Raw function pointers will NOT work correctly.
     * Create once at init, dispose at shutdown.
     */
    TCPNotifyUPP      tcp_notify_upp;
    UDPNotifyUPP      udp_notify_upp;
    TCPIOCompletionUPP tcp_listen_completion_upp;
    TCPIOCompletionUPP tcp_connect_completion_upp;
    TCPIOCompletionUPP tcp_close_completion_upp;

    /* UDP discovery stream (hot/cold split) */
    pt_udp_stream_hot  discovery_hot;
    pt_udp_stream_cold discovery_cold;

    /* TCP listener stream (hot/cold split) */
    pt_tcp_stream_hot  listener_hot;
    pt_tcp_stream_cold listener_cold;

    /* Per-peer TCP streams (hot/cold split)
     *
     * DOD: Hot arrays are contiguous - polling all 8 streams loads
     * only ~112 bytes instead of ~1600 bytes with struct-of-arrays.
     */
    pt_tcp_stream_hot  tcp_hot[PT_MAX_PEERS];
    pt_tcp_stream_cold tcp_cold[PT_MAX_PEERS];

    /* Timing */
    unsigned long     last_announce_tick;
    unsigned long     ticks_per_second;  /* 60 on Mac */

} pt_mactcp_data;

/* Get platform data from context */
static inline pt_mactcp_data *pt_mactcp_get(struct pt_context *ctx) {
    return (pt_mactcp_data *)((char *)ctx + sizeof(struct pt_context));
}

/* Buffer size recommendations from MacTCP Programmer's Guide
 *
 * TCP (per MPG): "minimum of 4096 bytes", "at least 8192 bytes is recommended"
 *                for character apps, "16 KB is recommended" for block apps.
 *                Formula: optimal = 4 × MTU + 1024
 *
 * UDP (per MPG): "minimum allowed size is 2048 bytes, but it should be at
 *                least 2N + 256 bytes where N is the size in bytes of the
 *                largest UDP datagram you expect to receive"
 *
 * For Mac SE 4MB: Use conservative sizes to leave room for heap operations.
 * For Performa 6200 8MB+: Can use larger buffers for better throughput.
 */
#define PT_TCP_RCV_BUF_MIN      4096   /* Absolute minimum per MacTCP */
#define PT_TCP_RCV_BUF_CHAR     8192   /* "at least 8192 bytes is recommended" */
#define PT_TCP_RCV_BUF_BLOCK    16384  /* "16 KB is recommended" for block apps */
#define PT_TCP_RCV_BUF_MAX      65536  /* Up to 128KB can be useful but overkill */

#define PT_UDP_RCV_BUF_MIN      2048   /* Actual minimum per MacTCP */
#define PT_UDP_RCV_BUF_SIZE     1408   /* 2×576 + 256 = safe for max UDP datagram */

/* Memory thresholds for buffer sizing (conservative for 4MB Mac SE)
 * FreeMem() returns bytes available in application heap
 */
#define PT_MEM_PLENTY           (2048 * 1024)  /* >2MB: use optimal sizing */
#define PT_MEM_MODERATE         (1024 * 1024)  /* 1-2MB: use character app size */
#define PT_MEM_LOW              (512 * 1024)   /* 512K-1MB: use minimum */
#define PT_MEM_CRITICAL         (256 * 1024)   /* <512K: warn and use minimum */

/* Maximum simultaneous streams (MacTCP limit is 64 total)
 * For Mac SE 4MB: Recommend PT_MAX_PEERS = 4-6 to conserve memory
 */
#define PT_MAX_TCP_STREAMS      (PT_MAX_PEERS + 1)  /* +1 for listener */

#endif /* PT_MACTCP_DEFS_H */
```

#### Task 5.1.2: Create `src/mactcp/mactcp_driver.c`

```c
#include "mactcp_defs.h"
#include "pt_internal.h"
#include "log.h"
#include <string.h>

size_t pt_mactcp_extra_size(void) {
    return sizeof(pt_mactcp_data);
}

/*
 * Open MacTCP driver
 * Pattern from Subtext tcp.c _TCPInit()
 */
int pt_mactcp_driver_open(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    ParamBlockRec pb;
    OSErr err;

    memset(&pb, 0, sizeof(pb));

    /* Open the .IPP driver */
    pb.ioParam.ioCompletion = 0;
    pb.ioParam.ioNamePtr = "\p.IPP";
    pb.ioParam.ioPermssn = fsCurPerm;

    err = PBOpen(&pb, false);  /* Synchronous */

    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_GENERAL,
            "Failed to open MacTCP driver: %d", err);
        return -1;
    }

    md->driver_refnum = pb.ioParam.ioRefNum;
    md->ticks_per_second = 60;  /* Mac tick rate */

    PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL,
        "MacTCP driver opened: refnum=%d", md->driver_refnum);

    return 0;
}

/*
 * Query system limits via TCPGlobalInfo
 * Instead of hardcoding 64 streams, query the actual system limit.
 */
int pt_mactcp_query_limits(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    TCPiopb pb;
    OSErr err;

    pt_memset(&pb, 0, sizeof(pb));
    pb.csCode = TCPGlobalInfo;
    pb.ioCRefNum = md->driver_refnum;

    err = PBControl((ParmBlkPtr)&pb, false);

    if (err != noErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_GENERAL,
            "TCPGlobalInfo failed: %d, using defaults", err);
        md->max_tcp_connections = 64;  /* Default fallback */
        md->max_udp_streams = 64;
        return 0;  /* Non-fatal */
    }

    /* Extract limits from globalInfo structure */
    md->max_tcp_connections = pb.csParam.globalInfo.tcpParamPtr->maxConnections;

    PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL,
        "MacTCP limits: max_connections=%u",
        md->max_tcp_connections);

    return 0;
}

/*
 * Get local IP address
 * From MacTCP Programmer's Guide: ipctlGetAddr
 */
int pt_mactcp_get_local_ip(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    GetAddrParamBlock pb;
    OSErr err;

    memset(&pb, 0, sizeof(pb));
    pb.csCode = ipctlGetAddr;
    pb.ioCRefNum = md->driver_refnum;
    pb.ioResult = 1;

    err = PBControl((ParmBlkPtr)&pb, false);  /* Synchronous */

    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_GENERAL,
            "Failed to get local IP: %d", err);
        return -1;
    }

    md->local_ip = pb.ourAddress;
    md->net_mask = pb.ourNetMask;

    PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL,
        "Local IP: 0x%08lX netmask: 0x%08lX",
        md->local_ip, md->net_mask);

    return 0;
}

/* Forward declarations for ASR/completion routines (defined in later sessions) */
static pascal void pt_udp_asr(StreamPtr, unsigned short, Ptr, struct ICMPReport *);
static pascal void pt_tcp_asr(StreamPtr, unsigned short, Ptr, unsigned short, struct ICMPReport *);
static pascal void pt_tcp_listen_completion(TCPiopb *);
static pascal void pt_tcp_connect_completion(TCPiopb *);
static pascal void pt_tcp_close_completion(TCPiopb *);

/*
 * Initialize MacTCP platform layer
 */
int pt_mactcp_net_init(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    int i;

    /* Clear all data */
    memset(md, 0, sizeof(pt_mactcp_data));
    md->driver_refnum = -1;

    /* Initialize stream states (hot structs only) */
    md->discovery_hot.state = PT_STREAM_UNUSED;
    md->listener_hot.state = PT_STREAM_UNUSED;

    for (i = 0; i < PT_MAX_PEERS; i++) {
        md->tcp_hot[i].state = PT_STREAM_UNUSED;
    }

    /* Create Universal Procedure Pointers
     * CRITICAL: MacTCP.h requires UPPs for all callback registration.
     * From header: "you must set up a NewRoutineDescriptor for every
     * non-nil completion routine and/or notifyProc parameter."
     */
    md->udp_notify_upp = NewUDPNotifyUPP(pt_udp_asr);
    md->tcp_notify_upp = NewTCPNotifyUPP(pt_tcp_asr);
    md->tcp_listen_completion_upp = NewTCPIOCompletionUPP(pt_tcp_listen_completion);
    md->tcp_connect_completion_upp = NewTCPIOCompletionUPP(pt_tcp_connect_completion);
    md->tcp_close_completion_upp = NewTCPIOCompletionUPP(pt_tcp_close_completion);

    if (!md->udp_notify_upp || !md->tcp_notify_upp ||
        !md->tcp_listen_completion_upp || !md->tcp_connect_completion_upp ||
        !md->tcp_close_completion_upp) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to create UPPs for MacTCP callbacks");
        return -1;
    }

    /* Open driver */
    if (pt_mactcp_driver_open(ctx) < 0)
        return -1;

    /* Query system limits (non-fatal if it fails) */
    pt_mactcp_query_limits(ctx);

    /* Get local IP */
    if (pt_mactcp_get_local_ip(ctx) < 0)
        return -1;

    return 0;
}

/* Forward declarations for release functions (defined in Sessions 5.2 and 5.4) */
int pt_mactcp_udp_release(struct pt_context *ctx);
void pt_mactcp_tcp_release_all(struct pt_context *ctx);

/*
 * Shutdown MacTCP platform layer
 */
void pt_mactcp_net_shutdown(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);

    /* Release all streams (implemented in Sessions 5.2 and 5.4) */
    pt_mactcp_udp_release(ctx);
    pt_mactcp_tcp_release_all(ctx);

    /* Dispose Universal Procedure Pointers
     * CRITICAL: Must dispose after all streams are released,
     * as streams may still reference these UPPs until release.
     */
    if (md->udp_notify_upp) {
        DisposeUDPNotifyUPP(md->udp_notify_upp);
        md->udp_notify_upp = NULL;
    }
    if (md->tcp_notify_upp) {
        DisposeTCPNotifyUPP(md->tcp_notify_upp);
        md->tcp_notify_upp = NULL;
    }
    if (md->tcp_listen_completion_upp) {
        DisposeTCPIOCompletionUPP(md->tcp_listen_completion_upp);
        md->tcp_listen_completion_upp = NULL;
    }
    if (md->tcp_connect_completion_upp) {
        DisposeTCPIOCompletionUPP(md->tcp_connect_completion_upp);
        md->tcp_connect_completion_upp = NULL;
    }
    if (md->tcp_close_completion_upp) {
        DisposeTCPIOCompletionUPP(md->tcp_close_completion_upp);
        md->tcp_close_completion_upp = NULL;
    }

    /* Don't close the .IPP driver - it's shared by all apps */

    PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL, "MacTCP shutdown complete");
}

/*
 * Allocate locked, non-relocatable memory for MacTCP buffers
 * CRITICAL: MacTCP requires this memory to remain fixed while stream is open
 */
Ptr pt_mactcp_alloc_buffer(unsigned long size) {
    Ptr buffer;

    /* NewPtr allocates from application heap, non-relocatable */
    buffer = NewPtr(size);
    if (buffer == NULL)
        return NULL;

    /* Clear the buffer */
    memset(buffer, 0, size);

    return buffer;
}

void pt_mactcp_free_buffer(Ptr buffer) {
    if (buffer)
        DisposePtr(buffer);
}

/*
 * Get optimal TCP buffer size based on physical MTU
 *
 * From MacTCP Programmer's Guide: "An application should allocate memory
 * by first finding the MTU of the physical network (see the UDPMTU section).
 * The minimum memory allocation should be 4N + 1024, where N is the size
 * of the physical MTU returned by the UDPMTU call."
 *
 * This ensures the receive window can hold enough data for good throughput.
 */
unsigned long pt_mactcp_optimal_buffer_size(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    UDPiopb pb;
    OSErr err;
    unsigned short mtu;
    unsigned long optimal;

    memset(&pb, 0, sizeof(pb));
    pb.csCode = UDPMaxMTUSize;  /* Get physical MTU */
    pb.ioCRefNum = md->driver_refnum;

    /* remoteHost=0 gets local interface MTU */
    pb.csParam.mtu.remoteHost = 0;

    err = PBControl((ParmBlkPtr)&pb, false);

    if (err != noErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_GENERAL,
            "UDPMTU failed: %d, using default buffer size", err);
        return PT_TCP_RCV_BUF_CHAR;  /* Safe default */
    }

    mtu = pb.csParam.mtu.mtuSize;

    /* Per documentation: optimal = 4 * MTU + 1024 */
    optimal = (4UL * mtu) + 1024;

    /* Clamp to reasonable range */
    if (optimal < PT_TCP_RCV_BUF_MIN)
        optimal = PT_TCP_RCV_BUF_MIN;
    if (optimal > PT_TCP_RCV_BUF_MAX)
        optimal = PT_TCP_RCV_BUF_MAX;

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_GENERAL,
        "Physical MTU=%u, optimal buffer=%lu", mtu, optimal);

    return optimal;
}

/*
 * Memory-aware buffer sizing for constrained systems (Mac SE 4MB)
 *
 * Mac SE with 4MB RAM, System 6.0.8:
 * - System uses ~400-600KB
 * - App partition ~2.5-3MB available
 * - MacTCP allocates ~64KB internal buffers for 4MB machine
 * - Leave 500KB+ free for app heap operations
 *
 * This function considers available memory and returns appropriate buffer size.
 */
unsigned long pt_mactcp_buffer_size_for_memory(struct pt_context *ctx) {
    long free_mem = FreeMem();
    long max_block = MaxBlock();
    unsigned long buf_size;

    /* Log memory state for debugging on real hardware */
    PT_LOG_INFO(ctx, PT_LOG_CAT_MEMORY,
        "Memory check: FreeMem=%ld MaxBlock=%ld", free_mem, max_block);

    /*
     * Conservative sizing - leave room for heap operations
     * Mac SE 4MB typical: FreeMem ~2.5MB at app launch
     */
    if (free_mem > PT_MEM_PLENTY) {
        /* Plenty of memory - use optimal formula */
        buf_size = pt_mactcp_optimal_buffer_size(ctx);
    } else if (free_mem > PT_MEM_MODERATE) {
        /* Moderate memory - use 8KB (character app) */
        buf_size = PT_TCP_RCV_BUF_CHAR;
    } else if (free_mem > PT_MEM_LOW) {
        /* Low memory - use minimum viable */
        buf_size = PT_TCP_RCV_BUF_MIN;
    } else {
        /* Critical - warn and use minimum */
        PT_LOG_WARN(ctx, PT_LOG_CAT_MEMORY,
            "Low memory warning: FreeMem=%ld - using minimum buffers", free_mem);
        buf_size = PT_TCP_RCV_BUF_MIN;
    }

    /* Don't allocate more than MaxBlock can provide
     * Leave headroom for other allocations */
    if (buf_size > (unsigned long)max_block / 2) {
        buf_size = PT_TCP_RCV_BUF_MIN;
        PT_LOG_WARN(ctx, PT_LOG_CAT_MEMORY,
            "MaxBlock too small (%ld), using minimum buffer", max_block);
    }

    return buf_size;
}
```

#### Task 5.1.3: Create test for driver initialization

```c
/* tests/test_mactcp_driver.c - runs under Retro68 emulator */
#include "peertalk.h"
#include <stdio.h>

int main(void) {
    PeerTalk_Config config = {0};
    PeerTalk_Context *ctx;

    printf("Testing MacTCP driver initialization...\n");

    config.local_name = "MacTCPTest";
    config.max_peers = 8;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        printf("FAILED: PeerTalk_Init returned NULL\n");
        return 1;
    }

    printf("PeerTalk initialized successfully\n");

    /* Print local IP */
    char ip_str[32];
    PeerTalk_GetLocalIP(ctx, ip_str, sizeof(ip_str));
    printf("Local IP: %s\n", ip_str);

    PeerTalk_Shutdown(ctx);
    printf("TEST PASSED\n");

    return 0;
}
```

### Acceptance Criteria
1. MacTCP driver opens successfully (PBOpen returns noErr)
2. Driver reference number is valid
3. Local IP address is retrieved correctly
4. Buffer allocation works
5. **UPPs created successfully** (NewTCPNotifyUPP, NewUDPNotifyUPP, etc.)
6. Test compiles and runs under Retro68/Basilisk II

#### Task 5.1.4: Add ISR Safety Compile-Time Test

Create `tests/test_isr_safety_mactcp.c` to provide static verification that PT_Log cannot be called from interrupt context:

```c
/*
 * ISR Safety Compile-Time Test for MacTCP
 *
 * This file MUST NOT compile. It verifies that the PT_ISR_CONTEXT guard
 * macro correctly blocks PT_Log calls from interrupt-time code.
 *
 * DO NOT add this to the Makefile test target - it's intentionally designed
 * to fail compilation as a safety check.
 */

#define PT_ISR_CONTEXT  /* Mark as interrupt context */
#include "pt_log.h"
#include <MacTCP.h>

/*
 * Example ASR callback that violates ISR safety
 *
 * If PT_ISR_CONTEXT guard is working, this should produce:
 * "error: PT_Log functions cannot be called at interrupt time"
 */
static pascal void test_asr_violates_isr_safety(
    StreamPtr stream,
    unsigned short event,
    Ptr userDataPtr,
    struct ICMPReport *icmpMsg)
{
    PT_Log *log = (PT_Log *)userDataPtr;

    /* This should cause compile error */
    PT_LOG_ERR(log, PT_LOG_CAT_PLATFORM, "ASR event: %d", event);

    /* These should also fail */
    PT_LOG_INFO(log, PT_LOG_CAT_NETWORK, "Data arrived");
    PT_LOG_DEBUG(log, PT_LOG_CAT_PLATFORM, "Stream=%p", stream);
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
   m68k-apple-macos-gcc -c tests/test_isr_safety_mactcp.c -I include -I src/core
   ```

2. **Add to CI workflow** (`.github/workflows/ci.yml`):
   ```yaml
   - name: ISR safety check (MacTCP)
     run: |
       # Verify PT_ISR_CONTEXT blocks PT_Log calls
       if m68k-apple-macos-gcc -c tests/test_isr_safety_mactcp.c -I include -I src/core 2>&1 | \
          grep -q "cannot be called at interrupt time"; then
         echo "✓ ISR safety guard working correctly"
       else
         echo "✗ ISR safety guard failed - PT_Log may be callable from interrupts!"
         exit 1
       fi
   ```

3. **Expected result:** Compilation fails with explicit error message about interrupt-time restrictions.

**Why This Matters:**

Without this test, developers could accidentally call PT_Log from ASR callbacks, leading to:
- Memory corruption (if PT_Log allocates)
- Crashes (if File Manager is called at interrupt time)
- Subtle bugs (if TickCount() is disabled during interrupt)

The PT_ISR_CONTEXT guard provides compile-time safety, but only if it's actually tested. This file verifies the guard mechanism works before any actual ASR code is written.

**Integration with ASR Code:**

When implementing ASR callbacks in Sessions 5.2-5.8, add `#define PT_ISR_CONTEXT` at the top of each ASR function to enable compile-time checking:

```c
static pascal void pt_tcp_asr(...) {
    #define PT_ISR_CONTEXT  /* Enable ISR safety checks */

    /* ASR implementation - PT_Log calls will fail to compile */
    hot->asr_flags |= PT_ASR_DATA_ARRIVED;

    #undef PT_ISR_CONTEXT
}
```

This provides per-function ISR safety verification during development.

---

## Session 5.2: UDP Stream

### Objective
Implement UDP stream creation and release with proper ASR handling.

### Tasks

#### Task 5.2.1: Create `src/mactcp/udp_mactcp.c`

```c
#include "mactcp_defs.h"
#include "pt_internal.h"
#include "log.h"

/*
 * UDP Asynchronous Notification Routine
 *
 * CRITICAL: Called at INTERRUPT LEVEL
 * From MacTCP Programmer's Guide:
 * - Cannot allocate or release memory
 * - Cannot make synchronous MacTCP calls
 * - CAN issue additional ASYNCHRONOUS MacTCP calls if needed
 * - Must preserve registers A0-A2, D0-D2
 *
 * UDPNotifyProcPtr signature (from MacTCP.h):
 *   void (*)(StreamPtr, unsigned short eventCode, Ptr userDataPtr, ICMPReport*)
 *
 * Strategy: Set flags only, let main loop process
 */
static pascal void pt_udp_asr(
    StreamPtr stream,
    unsigned short event_code,
    Ptr user_data,
    struct ICMPReport *icmp_msg)
{
    pt_udp_stream_hot *udp = (pt_udp_stream_hot *)user_data;

    /* DOD: Use bitfield flags for single-byte atomic operations */
    switch (event_code) {
    case UDPDataArrival:
        udp->asr_flags |= PT_ASR_DATA_ARRIVED;
        break;

    case UDPICMPReceived:
        udp->asr_flags |= PT_ASR_ICMP_RECEIVED;
        break;
    }

    /* DO NOT do any other work here! */
}

/*
 * Create UDP stream
 * From MacTCP Programmer's Guide: UDPCreate
 *
 * UDP buffer sizing (per MPG): "minimum allowed size is 2048 bytes,
 * but it should be at least 2N + 256 bytes where N is the size in
 * bytes of the largest UDP datagram you expect to receive"
 *
 * For discovery: packets are ~100 bytes, so 1408 bytes (2×576+256) is plenty.
 * This saves 2.6KB vs the old 4096 size - matters on Mac SE 4MB.
 *
 * DOD: Uses hot/cold struct split for cache efficiency.
 */
int pt_mactcp_udp_create(struct pt_context *ctx, udp_port local_port) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_udp_stream_hot *hot = &md->discovery_hot;
    pt_udp_stream_cold *cold = &md->discovery_cold;
    OSErr err;

    if (hot->state != PT_STREAM_UNUSED) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_NETWORK,
            "UDP stream already exists");
        return -1;
    }

    /* Allocate receive buffer
     * PT_UDP_RCV_BUF_SIZE = 1408 = 2×576 + 256 (safe for max datagram)
     * Saves 2.6KB vs old 4096 size */
    cold->rcv_buffer = pt_mactcp_alloc_buffer(PT_UDP_RCV_BUF_SIZE);
    if (!cold->rcv_buffer) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to allocate UDP receive buffer");
        return -1;
    }
    cold->rcv_buffer_size = PT_UDP_RCV_BUF_SIZE;

    /* Clear ASR flags (single byte bitfield) */
    hot->asr_flags = 0;

    /* Setup parameter block (in cold struct) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = UDPCreate;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.ioResult = 1;

    cold->pb.csParam.create.rcvBuff = cold->rcv_buffer;
    cold->pb.csParam.create.rcvBuffLen = cold->rcv_buffer_size;
    /* CRITICAL: Use UPP created at init, NOT a cast function pointer.
     * MacTCP.h: "you must set up a NewRoutineDescriptor for every
     * non-nil completion routine and/or notifyProc parameter."
     * Pass hot struct as userDataPtr - ASR only touches hot data. */
    cold->pb.csParam.create.notifyProc = md->udp_notify_upp;
    cold->pb.csParam.create.localPort = local_port;
    cold->pb.csParam.create.userDataPtr = (Ptr)hot;

    hot->state = PT_STREAM_CREATING;

    /* Synchronous call - safe from main loop */
    err = PBControl((ParmBlkPtr)&cold->pb, false);

    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_NETWORK,
            "UDPCreate failed: %d", err);
        pt_mactcp_free_buffer(cold->rcv_buffer);
        cold->rcv_buffer = NULL;
        hot->state = PT_STREAM_UNUSED;
        return -1;
    }

    hot->stream = cold->pb.udpStream;
    cold->local_port = local_port;
    cold->local_ip = md->local_ip;
    hot->state = PT_STREAM_IDLE;

    PT_LOG_INFO(ctx, PT_LOG_CAT_NETWORK,
        "UDP stream created: stream=0x%08lX port=%u",
        (unsigned long)hot->stream, local_port);

    return 0;
}

/*
 * Release UDP stream
 * From MacTCP Programmer's Guide: UDPRelease
 *
 * DOD: Uses hot/cold struct split.
 */
int pt_mactcp_udp_release(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_udp_stream_hot *hot = &md->discovery_hot;
    pt_udp_stream_cold *cold = &md->discovery_cold;
    OSErr err;

    if (hot->state == PT_STREAM_UNUSED)
        return 0;  /* Already released */

    /* Setup release call (in cold struct) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = UDPRelease;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.udpStream = hot->stream;

    hot->state = PT_STREAM_RELEASING;

    err = PBControl((ParmBlkPtr)&cold->pb, false);

    if (err != noErr && err != connectionDoesntExist) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
            "UDPRelease returned: %d", err);
    }

    /* Free buffer - ownership returned to us */
    if (cold->rcv_buffer) {
        pt_mactcp_free_buffer(cold->rcv_buffer);
        cold->rcv_buffer = NULL;
    }

    hot->stream = NULL;
    hot->state = PT_STREAM_UNUSED;

    PT_LOG_INFO(ctx, PT_LOG_CAT_NETWORK, "UDP stream released");

    return 0;
}

/*
 * Send UDP datagram
 * From MacTCP Programmer's Guide: UDPWrite
 *
 * DOD: Uses hot/cold struct split. Send uses cold for pb.
 */
int pt_mactcp_udp_send(struct pt_context *ctx,
                       ip_addr dest_ip, udp_port dest_port,
                       const void *data, unsigned short len) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_udp_stream_hot *hot = &md->discovery_hot;
    pt_udp_stream_cold *cold = &md->discovery_cold;
    wdsEntry wds[2];
    OSErr err;

    if (hot->state != PT_STREAM_IDLE) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
            "UDP stream not idle: state=%d", hot->state);
        return -1;
    }

    /* Build WDS (Write Data Structure) */
    wds[0].length = len;
    wds[0].ptr = (Ptr)data;
    wds[1].length = 0;  /* Terminator */
    wds[1].ptr = NULL;

    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = UDPWrite;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.udpStream = hot->stream;

    cold->pb.csParam.send.remoteHost = dest_ip;
    cold->pb.csParam.send.remotePort = dest_port;
    cold->pb.csParam.send.wdsPtr = (Ptr)wds;
    cold->pb.csParam.send.checkSum = 1;  /* Calculate checksum */

    /* Synchronous send */
    err = PBControl((ParmBlkPtr)&cold->pb, false);

    if (err != noErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
            "UDPWrite failed: %d", err);
        return -1;
    }

    return 0;
}

/*
 * Receive UDP datagram (non-blocking)
 *
 * From MacTCP Programmer's Guide: "The minimum allowed value for the
 * command timeout is 2 seconds. A zero command timeout means infinite."
 *
 * Strategy: Since we ONLY call this after ASR signals data arrival,
 * we know data is already buffered. UDPRead with minimum timeout (2s)
 * will return immediately with buffered data. If ASR fired but no data
 * (race condition), we'll get commandTimeout after 2s - acceptable since
 * this should be rare.
 *
 * DOD: Uses hot/cold struct split. Checks hot flags, uses cold for pb.
 */
int pt_mactcp_udp_recv(struct pt_context *ctx,
                       ip_addr *from_ip, udp_port *from_port,
                       void *data, unsigned short *len) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_udp_stream_hot *hot = &md->discovery_hot;
    pt_udp_stream_cold *cold = &md->discovery_cold;
    OSErr err;

    if (hot->state != PT_STREAM_IDLE)
        return 0;

    /* Quick check: ASR flag indicates data is already buffered */
    if (!(hot->asr_flags & PT_ASR_DATA_ARRIVED))
        return 0;

    /* Clear flag (atomic on 68k) */
    hot->asr_flags &= ~PT_ASR_DATA_ARRIVED;

    /* Issue UDPRead - data should be immediately available since ASR fired */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = UDPRead;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.udpStream = hot->stream;

    /* Per docs: minimum timeout is 2 seconds, 0 = infinite.
     * We use 2 since data should already be buffered (ASR fired).
     * This is effectively non-blocking when data exists. */
    cold->pb.csParam.receive.timeOut = 2;

    /* Synchronous call - data is already buffered so returns immediately */
    err = PBControl((ParmBlkPtr)&cold->pb, false);

    if (err == commandTimeout) {
        /* No data despite flag - rare race condition, try again later */
        return 0;
    }

    if (err != noErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
            "UDPRead failed: %d", err);
        return -1;
    }

    /* Extract data */
    *from_ip = cold->pb.csParam.receive.remoteHost;
    *from_port = cold->pb.csParam.receive.remotePort;

    unsigned short data_len = cold->pb.csParam.receive.rcvBuffLen;
    if (data_len > *len)
        data_len = *len;

    pt_memcpy(data, cold->pb.csParam.receive.rcvBuff, data_len);
    *len = data_len;

    /* CRITICAL: Return buffer to MacTCP for reuse */
    if (data_len > 0) {
        UDPiopb return_pb;
        pt_memset(&return_pb, 0, sizeof(return_pb));
        return_pb.csCode = UDPBfrReturn;
        return_pb.ioCRefNum = md->driver_refnum;
        return_pb.udpStream = hot->stream;
        return_pb.csParam.receive.rcvBuff = cold->pb.csParam.receive.rcvBuff;

        err = PBControl((ParmBlkPtr)&return_pb, false);
        if (err != noErr) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
                "UDPBfrReturn failed: %d (buffer leak possible)", err);
        }
    }

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_NETWORK,
        "UDP recv %u bytes from 0x%08lX:%u",
        (unsigned)data_len, (unsigned long)*from_ip, (unsigned)*from_port);

    return 1;  /* Got data */
}
```

### Acceptance Criteria
1. UDP stream creates successfully
2. ASR is registered and fires on data arrival
3. UDP send works (broadcast)
4. UDP receive works with ASR flag checking
5. Buffer is properly returned with UDPBfrReturn
6. Stream releases cleanly

---

## Session 5.3: UDP Discovery

### Objective
Implement the discovery protocol over UDP using the stream from Session 5.2.

### Tasks

#### Task 5.3.1: Create `src/mactcp/discovery_mactcp.c`

```c
#include "mactcp_defs.h"
#include "protocol.h"
#include "peer.h"
#include "pt_internal.h"
#include "log.h"

/* Use config port or default */
#define DEFAULT_DISCOVERY_PORT 7353
#define DISCOVERY_PORT(ctx) \
    ((ctx)->config.discovery_port > 0 ? (ctx)->config.discovery_port : DEFAULT_DISCOVERY_PORT)

/*
 * Start discovery - create UDP stream and send initial announce
 */
int pt_mactcp_discovery_start(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    int result;

    /* Create UDP stream */
    result = pt_mactcp_udp_create(ctx, DISCOVERY_PORT(ctx));
    if (result < 0)
        return result;

    /* Send initial announcement */
    pt_mactcp_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);

    md->last_announce_tick = Ticks;

    PT_LOG_INFO(ctx, PT_LOG_CAT_NETWORK,
        "Discovery started on port %u", DISCOVERY_PORT(ctx));

    return 0;
}

/*
 * Stop discovery
 */
void pt_mactcp_discovery_stop(struct pt_context *ctx) {
    /* Send goodbye */
    pt_mactcp_discovery_send(ctx, PT_DISC_TYPE_GOODBYE);

    /* Release UDP stream */
    pt_mactcp_udp_release(ctx);

    PT_LOG_INFO(ctx, PT_LOG_CAT_NETWORK, "Discovery stopped");
}

/*
 * Send discovery packet (announce, query, or goodbye)
 */
int pt_mactcp_discovery_send(struct pt_context *ctx, uint8_t type) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_discovery_packet pkt;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    int len;
    ip_addr broadcast;

    /* Build packet */
    pkt.type = type;
    pkt.flags = PT_DISC_FLAG_ACCEPTING;
    pkt.sender_port = ctx->config.tcp_port > 0 ?
                      ctx->config.tcp_port : PT_DEFAULT_TCP_PORT;

    strncpy(pkt.name, ctx->local_name, PT_PEER_NAME_MAX);
    pkt.name_len = strlen(pkt.name);

    len = pt_discovery_encode(&pkt, buf, sizeof(buf));
    if (len < 0) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_NETWORK,
            "Failed to encode discovery packet");
        return -1;
    }

    /* Calculate broadcast address */
    broadcast = (md->local_ip & md->net_mask) | ~md->net_mask;

    return pt_mactcp_udp_send(ctx, broadcast, DISCOVERY_PORT(ctx), buf, len);
}

/*
 * Poll for discovery packets
 */
int pt_mactcp_discovery_poll(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    unsigned short len = sizeof(buf);
    ip_addr from_ip;
    udp_port from_port;
    pt_discovery_packet pkt;
    struct pt_peer *peer;
    int result;

    /* Try to receive */
    result = pt_mactcp_udp_recv(ctx, &from_ip, &from_port, buf, &len);
    if (result <= 0)
        return result;

    /* Ignore our own broadcasts */
    if (from_ip == md->local_ip)
        return 0;

    /* Decode packet */
    if (pt_discovery_decode(buf, len, &pkt) != 0) {
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_NETWORK,
            "Invalid discovery packet from 0x%08lX", from_ip);
        return 0;
    }

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_NETWORK,
        "Discovery %s from \"%s\" at 0x%08lX:%u",
        pt_discovery_type_str(pkt.type), pkt.name, from_ip, pkt.sender_port);

    switch (pkt.type) {
    case PT_DISC_TYPE_ANNOUNCE:
        peer = pt_peer_create(ctx, pkt.name, from_ip, pkt.sender_port);
        if (peer && ctx->callbacks.on_peer_discovered) {
            PeerTalk_PeerInfo info;
            pt_peer_get_info(peer, &info);
            ctx->callbacks.on_peer_discovered((PeerTalk_Context *)ctx,
                                              &info, ctx->callbacks.user_data);
        }
        break;

    case PT_DISC_TYPE_QUERY:
        pt_mactcp_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);
        break;

    case PT_DISC_TYPE_GOODBYE:
        peer = pt_peer_find_by_addr(ctx, from_ip, pkt.sender_port);
        if (peer) {
            if (ctx->callbacks.on_peer_lost) {
                ctx->callbacks.on_peer_lost((PeerTalk_Context *)ctx,
                                            peer->info.id, ctx->callbacks.user_data);
            }
            pt_peer_destroy(ctx, peer);
        }
        break;
    }

    return 1;
}
```

### Acceptance Criteria
1. Discovery packets broadcast correctly
2. Incoming packets are received via ASR flag polling
3. Peer list updates on ANNOUNCE
4. GOODBYE removes peers
5. QUERY triggers ANNOUNCE response
6. Own broadcasts are ignored

---

## Session 5.4: TCP Stream

### Objective
Implement TCP stream creation and release with proper buffer management.

### Tasks

#### Task 5.4.1: Create `src/mactcp/tcp_mactcp.c`

```c
#include "mactcp_defs.h"
#include "pt_internal.h"
#include "log.h"

/*
 * TCP Asynchronous Notification Routine
 *
 * CRITICAL: Called at INTERRUPT LEVEL
 * From MacTCP Programmer's Guide:
 * - Cannot allocate or release memory
 * - Cannot make synchronous MacTCP calls
 * - CAN issue additional ASYNCHRONOUS MacTCP calls if needed
 * - Must preserve registers (compiler handles for pascal functions)
 *
 * TCPNotifyProcPtr signature (from MacTCP.h):
 *   void (*)(StreamPtr, unsigned short eventCode, Ptr userDataPtr,
 *            unsigned short terminReason, ICMPReport*)
 *
 * Event codes (TCPClosing=1, TCPULPTimeout=2, TCPTerminate=3,
 *              TCPDataArrival=4, TCPUrgent=5, TCPICMPReceived=6):
 * - TCPClosing: remote is closing (send pending data, then close)
 * - TCPULPTimeout: ULP timer expired
 * - TCPTerminate: connection gone - terminReason tells why:
 *     TCPRemoteAbort=2, TCPNetworkFailure=3, TCPSecPrecMismatch=4,
 *     TCPULPTimeoutTerminate=5, TCPULPAbort=6, TCPULPClose=7
 * - TCPDataArrival: data waiting to be read
 * - TCPUrgent: urgent data received
 * - TCPICMPReceived: ICMP message (details in icmpMsg)
 */
static pascal void pt_tcp_asr(
    StreamPtr stream,
    unsigned short event_code,
    Ptr user_data,
    unsigned short terminate_reason,
    struct ICMPReport *icmp_msg)
{
    /*
     * DOD: user_data points to hot struct. Cold data accessed via
     * array index calculated from hot struct pointer.
     */
    pt_tcp_stream_hot *tcp = (pt_tcp_stream_hot *)user_data;

    /* DOD: Use bitfield flags for single-byte atomic operations */
    switch (event_code) {
    case TCPDataArrival:
        tcp->asr_flags |= PT_ASR_DATA_ARRIVED;
        break;

    case TCPClosing:
        tcp->asr_flags |= PT_ASR_CONN_CLOSED;
        /* Note: should try to send pending data before closing */
        break;

    case TCPTerminate:
        tcp->asr_flags |= PT_ASR_CONN_CLOSED;
        /* terminate_reason stored in cold struct - need index lookup
         * Since this is interrupt time, we can't safely access cold struct.
         * Store in a pre-allocated field or ignore (connection is dead anyway).
         * Most implementations just set the flag and let poll loop handle cleanup.
         */
        break;

    case TCPULPTimeout:
        tcp->asr_flags |= PT_ASR_TIMEOUT;
        break;

    case TCPUrgent:
        tcp->asr_flags |= PT_ASR_URGENT_DATA;
        break;

    case TCPICMPReceived:
        tcp->asr_flags |= PT_ASR_ICMP_RECEIVED;
        /* Could copy icmp_msg to pre-allocated buffer if needed */
        break;
    }

    /* CRITICAL: Do nothing else at interrupt time! */
}

/*
 * Create TCP stream
 * From MacTCP Programmer's Guide: TCPCreate
 *
 * Buffer sizing (from guide):
 * - Minimum: 4096 bytes
 * - Character apps: 8192 bytes ("at least 8192 bytes is recommended")
 * - Block apps: 16384 bytes or more ("16 KB is recommended")
 * - Formula: 4*MTU + 1024 for good performance
 *
 * For Mac SE 4MB: Use memory-aware sizing to leave room for heap operations.
 * For Performa 6200 8MB+: Can use optimal formula for better throughput.
 *
 * DOD: Uses hot/cold struct split. Takes index to access parallel arrays.
 */
int pt_mactcp_tcp_create(struct pt_context *ctx, int idx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot = &md->tcp_hot[idx];
    pt_tcp_stream_cold *cold = &md->tcp_cold[idx];
    OSErr err;
    unsigned long buf_size;

    if (hot->state != PT_STREAM_UNUSED) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "TCP stream already in use");
        return -1;
    }

    /* Determine buffer size based on available memory
     * Uses conservative thresholds appropriate for Mac SE 4MB
     * See pt_mactcp_buffer_size_for_memory() for details
     */
    buf_size = pt_mactcp_buffer_size_for_memory(ctx);

    /* Allocate receive buffer with fallback to smaller sizes
     * This handles low-memory situations gracefully
     */
    {
        unsigned long original_size = buf_size;
        while (buf_size >= PT_TCP_RCV_BUF_MIN) {
            cold->rcv_buffer = pt_mactcp_alloc_buffer(buf_size);
            if (cold->rcv_buffer)
                break;
            /* Log allocation fallback (2026-01-29 review) */
            PT_LOG_WARN(ctx, PT_LOG_CAT_MEMORY,
                "Buffer alloc failed (%lu), trying %lu", buf_size, buf_size / 2);
            buf_size /= 2;  /* Try smaller buffer */
        }
        if (cold->rcv_buffer && buf_size < original_size) {
            PT_LOG_INFO(ctx, PT_LOG_CAT_MEMORY,
                "TCP buffer allocated at reduced size: %lu (requested %lu)",
                buf_size, original_size);
        }
    }

    if (!cold->rcv_buffer) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to allocate TCP receive buffer (tried down to %lu bytes)",
            PT_TCP_RCV_BUF_MIN);
        return -1;
    }
    cold->rcv_buffer_size = buf_size;

    /* Clear ASR flags (single byte bitfield) */
    hot->asr_flags = 0;

    /* Setup parameter block (in cold struct) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPCreate;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.ioResult = 1;

    cold->pb.csParam.create.rcvBuff = cold->rcv_buffer;
    cold->pb.csParam.create.rcvBuffLen = cold->rcv_buffer_size;
    /* CRITICAL: Use UPP created at init, NOT a cast function pointer.
     * MacTCP.h: "you must set up a NewRoutineDescriptor for every
     * non-nil completion routine and/or notifyProc parameter."
     * Pass hot struct as userDataPtr - ASR only touches hot data. */
    cold->pb.csParam.create.notifyProc = md->tcp_notify_upp;
    cold->pb.csParam.create.userDataPtr = (Ptr)hot;

    hot->state = PT_STREAM_CREATING;

    /* Synchronous create */
    err = PBControl((ParmBlkPtr)&cold->pb, false);

    if (err != noErr) {
        /* Handle system-wide 64 stream limit gracefully */
        if (err == insufficientResources) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "System TCP stream limit (64) reached");
            pt_mactcp_free_buffer(cold->rcv_buffer);
            cold->rcv_buffer = NULL;
            hot->state = PT_STREAM_UNUSED;
            return PEERTALK_ERR_RESOURCES;
        }

        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "TCPCreate failed: %d", err);
        pt_mactcp_free_buffer(cold->rcv_buffer);
        cold->rcv_buffer = NULL;
        hot->state = PT_STREAM_UNUSED;
        return -1;
    }

    hot->stream = cold->pb.tcpStream;
    cold->local_ip = md->local_ip;
    hot->state = PT_STREAM_IDLE;
    hot->rds_outstanding = 0;

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "TCP stream created: stream=0x%08lX bufsize=%lu",
        (unsigned long)hot->stream, buf_size);

    return 0;
}

/*
 * Release TCP stream
 * From MacTCP Programmer's Guide: TCPRelease
 *
 * DOD: Uses hot/cold struct split. Takes index to access parallel arrays.
 */
int pt_mactcp_tcp_release(struct pt_context *ctx, int idx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot = &md->tcp_hot[idx];
    pt_tcp_stream_cold *cold = &md->tcp_cold[idx];
    OSErr err;

    if (hot->state == PT_STREAM_UNUSED)
        return 0;

    /* Return any outstanding RDS buffers first */
    if (hot->rds_outstanding) {
        TCPiopb return_pb;
        pt_memset(&return_pb, 0, sizeof(return_pb));
        return_pb.csCode = TCPRcvBfrReturn;
        return_pb.ioCRefNum = md->driver_refnum;
        return_pb.tcpStream = hot->stream;
        return_pb.csParam.receive.rdsPtr = (Ptr)cold->rds;

        err = PBControl((ParmBlkPtr)&return_pb, false);
        if (err != noErr) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
                "TCPRcvBfrReturn failed: %d (buffer leak possible)", err);
        }
        hot->rds_outstanding = 0;
    }

    /* Issue release (using cold struct's pb) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPRelease;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;

    hot->state = PT_STREAM_RELEASING;

    err = PBControl((ParmBlkPtr)&cold->pb, false);

    if (err != noErr && err != connectionDoesntExist) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "TCPRelease returned: %d", err);
    }

    /* Free buffer */
    if (cold->rcv_buffer) {
        pt_mactcp_free_buffer(cold->rcv_buffer);
        cold->rcv_buffer = NULL;
    }

    hot->stream = NULL;
    hot->peer = NULL;
    hot->state = PT_STREAM_UNUSED;

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT, "TCP stream released");

    return 0;
}

/*
 * Helper indices for listener stream (uses same arrays as peers) */
#define PT_LISTENER_IDX  PT_MAX_PEERS  /* Listener uses slot after peer slots */

/*
 * Release all TCP streams
 */
/* AMENDMENT (2026-02-03): LaunchAPPL Cleanup Pattern
 *
 * Verified from LaunchAPPL MacTCPStream.cc:61-79 - proper cleanup sequence:
 * 1. TCPAbort all streams (cancels pending operations)
 * 2. Spin-wait: while(ioResult > 0) {} for ALL parameter blocks
 * 3. TCPRelease each stream
 *
 * CRITICAL: Step 2 ensures async operations complete before releasing streams.
 * Without this, TCPRelease may fail or corrupt MacTCP driver state.
 *
 * Current implementation (below) goes straight to TCPRelease, which assumes
 * no async operations are pending. This is SAFE if:
 * - All async ops use completion routines that clear async_pending flag
 * - Poll loop has been stopped before calling shutdown
 * - No async ops were issued after last poll
 *
 * RECOMMENDED IMPROVEMENT for robustness:
 * Add spin-wait before TCPRelease to guarantee safety even if async ops
 * are still pending (e.g., during abnormal shutdown, Quit without poll, etc.)
 */
void pt_mactcp_tcp_release_all(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    TCPiopb abort_pb;
    int i;
    Boolean any_pending;

    /* Step 1: TCPAbort all active streams (listener + peers)
     * This cancels any pending async operations (TCPPassiveOpen, TCPActiveOpen, TCPClose)
     */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        pt_tcp_stream_hot *hot = &md->tcp_hot[i];
        if (hot->state != PT_STREAM_UNUSED && hot->stream != NULL) {
            pt_memset(&abort_pb, 0, sizeof(abort_pb));
            abort_pb.csCode = TCPAbort;
            abort_pb.ioCRefNum = md->driver_refnum;
            abort_pb.tcpStream = hot->stream;
            PBControlSync((ParmBlkPtr)&abort_pb);
            /* Ignore error - stream may already be dead */
        }
    }

    /* Abort listener if active */
    pt_tcp_stream_hot *listener_hot = &md->tcp_hot[PT_LISTENER_IDX];
    if (listener_hot->state != PT_STREAM_UNUSED && listener_hot->stream != NULL) {
        pt_memset(&abort_pb, 0, sizeof(abort_pb));
        abort_pb.csCode = TCPAbort;
        abort_pb.ioCRefNum = md->driver_refnum;
        abort_pb.tcpStream = listener_hot->stream;
        PBControlSync((ParmBlkPtr)&abort_pb);
    }

    /* Step 2: Spin-wait for all pending async operations to complete
     * LaunchAPPL Pattern: while(readPB.ioResult > 0 || writePB.ioResult > 0) {}
     *
     * ioResult values (from MacTCP.h):
     *   > 0 : Operation in progress
     *   = 0 : Completed successfully
     *   < 0 : Completed with error
     *
     * PERFORMANCE NOTE: This can spin for up to ~2 seconds if operations
     * were truly async and TCPAbort needs to propagate. In practice, most
     * operations complete within a few milliseconds after TCPAbort.
     */
    do {
        any_pending = false;
        for (i = 0; i < PT_MAX_PEERS; i++) {
            pt_tcp_stream_hot *hot = &md->tcp_hot[i];
            pt_tcp_stream_cold *cold = &md->tcp_cold[i];
            if (hot->state != PT_STREAM_UNUSED) {
                if (cold->pb.ioResult > 0) {
                    any_pending = true;
                    /* Optional: add timeout to prevent infinite loop if driver is wedged
                     * unsigned long timeout_start = Ticks;
                     * if ((Ticks - timeout_start) > 5*60) break;  // 5 seconds
                     */
                }
            }
        }
    } while (any_pending);

    /* Step 3: Now safe to release - all async ops guaranteed complete */
    pt_mactcp_tcp_release_listener(ctx);
    for (i = 0; i < PT_MAX_PEERS; i++) {
        pt_mactcp_tcp_release(ctx, i);
    }
}
```

### Acceptance Criteria
1. TCP stream creates successfully
2. Buffer sized appropriately for available memory
3. ASR registered correctly
4. Stream releases with buffer return
5. All streams released on shutdown

---

## Session 5.5: TCP Listen

### Objective
Implement TCPPassiveOpen for accepting incoming connections.

### Key Insight from MacTCP Programmer's Guide:
> "TCPPassiveOpen listens for an incoming connection. The command is completed when a connection is established or when an error occurs."

**This means:** Unlike BSD sockets, you must issue a NEW TCPPassiveOpen after each accepted connection. This is a one-shot operation, not a persistent listener.

### Tasks

#### Task 5.5.1: Create `src/mactcp/tcp_listen.c`

```c
#include "mactcp_defs.h"
#include "peer.h"
#include "pt_internal.h"
#include "log.h"

/*
 * Completion routine for async TCPPassiveOpen
 * Called when connection arrives or timeout
 *
 * DOD: userDataPtr points to hot struct - only hot fields modified.
 */
static pascal void pt_tcp_listen_completion(TCPiopb *pb) {
    pt_tcp_stream_hot *hot = (pt_tcp_stream_hot *)pb->csParam.open.userDataPtr;

    hot->async_result = pb->ioResult;
    hot->async_pending = 0;

    /* Connection info stored in pb - main loop will copy to cold struct */
}

/*
 * Start listening on a port
 * This issues an async TCPPassiveOpen
 *
 * DOD: Uses hot/cold struct split for listener.
 */
int pt_mactcp_listen_start(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot = &md->listener_hot;
    pt_tcp_stream_cold *cold = &md->listener_cold;
    tcp_port port;
    OSErr err;

    /* Create stream if needed */
    if (hot->state == PT_STREAM_UNUSED) {
        if (pt_mactcp_tcp_create_listener(ctx) < 0)
            return -1;
    }

    if (hot->state != PT_STREAM_IDLE) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "Listener not idle: state=%d", hot->state);
        return -1;
    }

    port = ctx->config.tcp_port > 0 ? ctx->config.tcp_port : PT_DEFAULT_TCP_PORT;

    /* Setup async passive open (pb in cold struct) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPPassiveOpen;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;
    /* Use UPP for completion routine */
    cold->pb.ioCompletion = md->tcp_listen_completion_upp;

    /* From MacTCP Programmer's Guide: validity bits for timeout params */
    cold->pb.csParam.open.ulpTimeoutValue = 0;   /* Use default */
    cold->pb.csParam.open.ulpTimeoutAction = 1;  /* Abort */
    cold->pb.csParam.open.validityFlags = 0xC0;
    cold->pb.csParam.open.commandTimeoutValue = 0;  /* No command timeout */

    /* Accept connections from any remote */
    cold->pb.csParam.open.remoteHost = 0;
    cold->pb.csParam.open.remotePort = 0;
    cold->pb.csParam.open.localHost = 0;
    cold->pb.csParam.open.localPort = port;

    cold->pb.csParam.open.tosFlags = 0x1;  /* Low delay */
    cold->pb.csParam.open.userDataPtr = (Ptr)hot;  /* Completion gets hot struct */

    hot->async_pending = 1;
    hot->async_result = 1;  /* In progress */
    hot->state = PT_STREAM_LISTENING;

    /* Issue async call */
    err = PBControl((ParmBlkPtr)&cold->pb, true);  /* true = async */

    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "TCPPassiveOpen failed: %d", err);
        hot->state = PT_STREAM_IDLE;
        hot->async_pending = 0;
        return -1;
    }

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Listening on port %u", port);

    return 0;
}

/*
 * Poll for incoming connection
 * Returns 1 if connection accepted, 0 if still waiting, -1 on error
 *
 * DOD: Uses hot/cold struct split. Transfer copies both hot and cold data.
 */
int pt_mactcp_listen_poll(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *listener_hot = &md->listener_hot;
    pt_tcp_stream_cold *listener_cold = &md->listener_cold;
    struct pt_peer *peer;
    int client_idx;

    if (listener_hot->state != PT_STREAM_LISTENING)
        return 0;

    /* Check if async operation completed */
    if (listener_hot->async_pending)
        return 0;

    /* Check result */
    if (listener_hot->async_result == commandTimeout) {
        /* No connection yet - restart listener */
        listener_hot->state = PT_STREAM_IDLE;
        pt_mactcp_listen_start(ctx);
        return 0;
    }

    if (listener_hot->async_result != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "Listen failed: %d", listener_hot->async_result);
        listener_hot->state = PT_STREAM_IDLE;
        pt_mactcp_listen_start(ctx);
        return -1;
    }

    /* Connection accepted! Extract info from pb */
    ip_addr remote_ip = listener_cold->pb.csParam.open.remoteHost;
    tcp_port remote_port = listener_cold->pb.csParam.open.remotePort;

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Incoming connection from 0x%08lX:%u", remote_ip, remote_port);

    /*
     * CRITICAL: The listener stream now holds the connection.
     * We need to:
     * 1. Find or create peer
     * 2. Transfer stream to peer's tcp slot
     * 3. Create new stream for listener
     */

    /* Find peer or create one */
    peer = pt_peer_find_by_addr(ctx, remote_ip, 0);
    if (!peer) {
        peer = pt_peer_create(ctx, "", remote_ip, remote_port);
    }

    if (!peer) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No peer slot for incoming connection");
        /* Abort this connection */
        TCPiopb abort_pb;
        pt_memset(&abort_pb, 0, sizeof(abort_pb));
        abort_pb.csCode = TCPAbort;
        abort_pb.ioCRefNum = md->driver_refnum;
        abort_pb.tcpStream = listener_hot->stream;
        PBControl((ParmBlkPtr)&abort_pb, false);

        listener_hot->state = PT_STREAM_IDLE;
        pt_mactcp_listen_start(ctx);
        return 0;
    }

    /* Find free client stream slot */
    client_idx = -1;
    for (int i = 0; i < PT_MAX_PEERS; i++) {
        if (md->tcp_hot[i].state == PT_STREAM_UNUSED) {
            client_idx = i;
            break;
        }
    }

    if (client_idx < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No free TCP stream slot");
        /* Same abort handling */
        listener_hot->state = PT_STREAM_IDLE;
        pt_mactcp_listen_start(ctx);
        return 0;
    }

    /*
     * Transfer listener's stream to client slot
     * This is the MacTCP pattern - the listener's stream
     * becomes the connected stream.
     *
     * DOD: Copy both hot and cold structs to client slot.
     * Buffer pointer transfers (not copied) - now owned by client.
     */
    md->tcp_hot[client_idx] = *listener_hot;
    md->tcp_cold[client_idx] = *listener_cold;

    /* Update client hot struct */
    md->tcp_hot[client_idx].peer = peer;
    md->tcp_hot[client_idx].state = PT_STREAM_CONNECTED;

    /* Update client cold struct with connection info */
    md->tcp_cold[client_idx].remote_ip = remote_ip;
    md->tcp_cold[client_idx].remote_port = remote_port;
    md->tcp_cold[client_idx].local_port = listener_cold->pb.csParam.open.localPort;

    /* Clear listener for new listen (hot and cold) */
    pt_memset(listener_hot, 0, sizeof(*listener_hot));
    pt_memset(listener_cold, 0, sizeof(*listener_cold));
    listener_hot->state = PT_STREAM_UNUSED;

    /* Create new listener stream */
    if (pt_mactcp_listen_start(ctx) < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "Failed to restart listener");
    }

    /* Update peer state */
    pt_peer_set_state(peer, PT_PEER_CONNECTED);
    /* Ticks is a low-memory global - safe to read (unlike TickCount()) */
    peer->last_seen = Ticks;
    peer->tcp_stream_idx = client_idx;  /* Store index for later lookup */

    /* Fire callback */
    if (ctx->callbacks.on_peer_connected) {
        ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                         peer->info.id, ctx->callbacks.user_data);
    }

    return 1;
}

/*
 * Stop listening
 *
 * DOD: Uses hot/cold struct split for listener.
 */
void pt_mactcp_listen_stop(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot = &md->listener_hot;

    if (hot->state == PT_STREAM_LISTENING) {
        /* Abort pending listen */
        TCPiopb abort_pb;
        pt_memset(&abort_pb, 0, sizeof(abort_pb));
        abort_pb.csCode = TCPAbort;
        abort_pb.ioCRefNum = md->driver_refnum;
        abort_pb.tcpStream = hot->stream;
        PBControl((ParmBlkPtr)&abort_pb, false);
    }

    pt_mactcp_tcp_release_listener(ctx);

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT, "Listen stopped");
}
```

### Acceptance Criteria
1. TCPPassiveOpen issued asynchronously
2. Completion routine fires when connection arrives
3. Incoming connection creates/updates peer
4. Stream transferred to peer's slot
5. New listener started for next connection
6. Callbacks fire correctly

---

## Session 5.6: TCP Connect

### Objective
Implement TCPActiveOpen for outgoing connections.

### Tasks

#### Task 5.6.1: Create `src/mactcp/tcp_connect.c`

```c
#include "mactcp_defs.h"
#include "peer.h"
#include "pt_internal.h"
#include "log.h"

/*
 * Completion routine for async TCPClose
 *
 * DOD: userDataPtr points to hot struct - only hot fields modified.
 */
static pascal void pt_tcp_close_completion(TCPiopb *pb) {
    pt_tcp_stream_hot *hot = (pt_tcp_stream_hot *)pb->csParam.close.userDataPtr;

    hot->async_result = pb->ioResult;
    hot->async_pending = 0;
}

/*
 * Completion routine for async TCPActiveOpen
 *
 * DOD: userDataPtr points to hot struct - only hot fields modified.
 * Connection info (local_ip, local_port) stored in pb - main loop copies to cold.
 */
static pascal void pt_tcp_connect_completion(TCPiopb *pb) {
    pt_tcp_stream_hot *hot = (pt_tcp_stream_hot *)pb->csParam.open.userDataPtr;

    hot->async_result = pb->ioResult;
    hot->async_pending = 0;

    /* Note: local_ip and local_port are in pb - main loop will copy to cold struct */
}

/*
 * Initiate connection to peer
 *
 * DOD: Uses hot/cold struct split. Stores stream index in peer.
 */
int pt_mactcp_connect(struct pt_context *ctx, struct pt_peer *peer) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    int idx;
    OSErr err;

    if (!peer || peer->magic != PT_PEER_MAGIC)
        return PEERTALK_ERR_INVALID;

    if (peer->state != PT_PEER_DISCOVERED)
        return PEERTALK_ERR_STATE;

    /* Find free stream slot */
    idx = -1;
    for (int i = 0; i < PT_MAX_PEERS; i++) {
        if (md->tcp_hot[i].state == PT_STREAM_UNUSED) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No free TCP stream for connect");
        return PEERTALK_ERR_RESOURCES;
    }

    pt_tcp_stream_hot *hot = &md->tcp_hot[idx];
    pt_tcp_stream_cold *cold = &md->tcp_cold[idx];

    /* Create stream */
    if (pt_mactcp_tcp_create(ctx, idx) < 0)
        return PEERTALK_ERR_RESOURCES;

    /* Setup async active open (pb in cold struct) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPActiveOpen;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;
    /* Use UPP for completion routine */
    cold->pb.ioCompletion = md->tcp_connect_completion_upp;

    cold->pb.csParam.open.ulpTimeoutValue = 30;  /* 30 second timeout */
    cold->pb.csParam.open.ulpTimeoutAction = 1;  /* Abort on timeout */
    cold->pb.csParam.open.validityFlags = 0xC0;

    cold->pb.csParam.open.remoteHost = peer->info.address;
    cold->pb.csParam.open.remotePort = peer->info.port;
    cold->pb.csParam.open.localHost = 0;
    cold->pb.csParam.open.localPort = 0;  /* Let MacTCP assign */

    cold->pb.csParam.open.tosFlags = 0x1;  /* Low delay */
    cold->pb.csParam.open.userDataPtr = (Ptr)hot;  /* Completion gets hot struct */

    cold->remote_ip = peer->info.address;
    cold->remote_port = peer->info.port;
    hot->peer = peer;
    hot->async_pending = 1;
    hot->async_result = 1;
    hot->state = PT_STREAM_CONNECTING;

    /* Link peer to stream */
    peer->tcp_stream_idx = idx;
    pt_peer_set_state(peer, PT_PEER_CONNECTING);
    /* Ticks is a low-memory global - safe to read (unlike TickCount()) */
    peer->connect_start = Ticks;

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Connecting to peer %u (%s) at 0x%08lX:%u",
        peer->info.id, peer->info.name, peer->info.address, peer->info.port);

    /* Issue async call */
    err = PBControl((ParmBlkPtr)&cold->pb, true);

    if (err != noErr) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "TCPActiveOpen failed: %d", err);
        hot->state = PT_STREAM_IDLE;
        hot->async_pending = 0;
        peer->tcp_stream_idx = -1;
        pt_peer_set_state(peer, PT_PEER_FAILED);
        pt_mactcp_tcp_release(ctx, idx);
        return PEERTALK_ERR_NETWORK;
    }

    return PEERTALK_SUCCESS;
}

/*
 * Poll connecting streams for completion
 *
 * CRITICAL: Must check for connection timeout. TCPActiveOpen
 * can hang indefinitely if remote host is unreachable. Monitor
 * connect_start and abort after reasonable timeout (30 seconds).
 *
 * DOD: Uses hot/cold struct split. Single-pass iteration over hot array.
 * NOTE: This function is now integrated into pt_mactcp_poll() as
 * pt_mactcp_poll_connecting(). Kept here for reference.
 */
#define PT_CONNECT_TIMEOUT_TICKS  (30 * 60)  /* 30 seconds at 60 ticks/sec */

int pt_mactcp_connect_poll(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    int i;
    int processed = 0;
    /* Ticks is a low-memory global - safe to read (unlike TickCount()) */
    unsigned long now = Ticks;

    for (i = 0; i < PT_MAX_PEERS; i++) {
        pt_tcp_stream_hot *hot = &md->tcp_hot[i];
        pt_tcp_stream_cold *cold = &md->tcp_cold[i];

        if (hot->state != PT_STREAM_CONNECTING)
            continue;

        struct pt_peer *peer = hot->peer;
        if (!peer)
            continue;

        /* Check for connection timeout BEFORE checking async_pending
         * Use signed comparison to handle Ticks wrap-around correctly */
        if (hot->async_pending &&
            (int32_t)(now - peer->connect_start) > (int32_t)PT_CONNECT_TIMEOUT_TICKS) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "Connect to peer %u timed out after 30s, aborting",
                peer->info.id);

            /* Abort the pending connection */
            TCPiopb abort_pb;
            pt_memset(&abort_pb, 0, sizeof(abort_pb));
            abort_pb.csCode = TCPAbort;
            abort_pb.ioCRefNum = md->driver_refnum;
            abort_pb.tcpStream = hot->stream;
            PBControl((ParmBlkPtr)&abort_pb, false);

            hot->async_pending = 0;
            hot->async_result = connectionTerminated;
        }

        if (hot->async_pending)
            continue;  /* Still waiting (and not timed out) */

        /* Connection attempt completed */
        if (hot->async_result == noErr) {
            /* Success! Copy local address from pb to cold struct */
            cold->local_ip = cold->pb.csParam.open.localHost;
            cold->local_port = cold->pb.csParam.open.localPort;

            PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
                "Connected to peer %u (%s)", peer->info.id, peer->info.name);

            hot->state = PT_STREAM_CONNECTED;
            pt_peer_set_state(peer, PT_PEER_CONNECTED);
            peer->last_seen = Ticks;

            if (ctx->callbacks.on_peer_connected) {
                ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                                 peer->info.id,
                                                 ctx->callbacks.user_data);
            }
        } else {
            /* Failed */
            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "Connect to peer %u failed: %d",
                peer->info.id, hot->async_result);

            pt_peer_set_state(peer, PT_PEER_FAILED);
            peer->tcp_stream_idx = -1;

            pt_mactcp_tcp_release(ctx, i);
        }

        processed++;
    }

    return processed;
}

/*
 * Disconnect from peer
 *
 * DOD: Uses hot/cold struct split. Looks up stream by index stored in peer.
 */
int pt_mactcp_disconnect(struct pt_context *ctx, struct pt_peer *peer) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    OSErr err;

    if (!peer || peer->magic != PT_PEER_MAGIC)
        return PEERTALK_ERR_INVALID;

    int idx = peer->tcp_stream_idx;
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return PEERTALK_ERR_STATE;

    pt_tcp_stream_hot *hot = &md->tcp_hot[idx];
    pt_tcp_stream_cold *cold = &md->tcp_cold[idx];

    if (hot->state == PT_STREAM_CONNECTED) {
        /*
         * Graceful close - USE ASYNC to avoid 30+ second blocking!
         * From MacTCP Programmer's Guide: TCPClose can block waiting
         * for FIN-ACK from remote, which may take 30+ seconds if
         * remote is unresponsive. Use async with timeout monitoring.
         */
        pt_memset(&cold->pb, 0, sizeof(cold->pb));
        cold->pb.csCode = TCPClose;
        cold->pb.ioCRefNum = md->driver_refnum;
        cold->pb.tcpStream = hot->stream;
        /* Use UPP for completion routine */
        cold->pb.ioCompletion = md->tcp_close_completion_upp;

        cold->pb.csParam.close.ulpTimeoutValue = 10;
        cold->pb.csParam.close.ulpTimeoutAction = 1;  /* Abort on timeout */
        cold->pb.csParam.close.validityFlags = 0xC0;
        cold->pb.csParam.close.userDataPtr = (Ptr)hot;  /* Completion gets hot struct */

        hot->state = PT_STREAM_CLOSING;
        hot->async_pending = 1;
        cold->close_start = Ticks;  /* Track for timeout monitoring (in cold) */

        /* ASYNC close - returns immediately, completion routine called later */
        err = PBControl((ParmBlkPtr)&cold->pb, true);

        if (err != noErr && err != connectionClosing) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "TCPClose returned: %d", err);
        }

        /* Main loop will monitor close_start and force abort if too slow */
        /* DO NOT release stream here - poll loop handles completion */
        return PEERTALK_SUCCESS;
    }

    /* Stream not connected - release immediately */
    pt_mactcp_tcp_release(ctx, idx);
    peer->tcp_stream_idx = -1;

    /* Update peer state */
    if (ctx->callbacks.on_peer_disconnected) {
        ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                            peer->info.id, PEERTALK_ERR_NONE,
                                            ctx->callbacks.user_data);
    }

    pt_peer_set_state(peer, PT_PEER_UNUSED);

    return PEERTALK_SUCCESS;
}

/*
 * Poll closing streams for completion
 * CRITICAL: Must be called from main poll loop to handle async TCPClose
 *
 * DOD: Uses hot/cold struct split. Single-pass iteration over hot array.
 * NOTE: This function is now integrated into pt_mactcp_poll() as
 * pt_mactcp_poll_closing(). Kept here for reference.
 */
#define PT_CLOSE_TIMEOUT_TICKS (10 * 60)  /* 10 seconds */

int pt_mactcp_close_poll(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    int i;
    int processed = 0;
    /* Ticks is a low-memory global - safe to read (unlike TickCount()) */
    unsigned long now = Ticks;

    for (i = 0; i < PT_MAX_PEERS; i++) {
        pt_tcp_stream_hot *hot = &md->tcp_hot[i];
        pt_tcp_stream_cold *cold = &md->tcp_cold[i];

        if (hot->state != PT_STREAM_CLOSING)
            continue;

        /* Check for close timeout (signed comparison for Ticks wrap) */
        if (hot->async_pending &&
            (int32_t)(now - cold->close_start) > (int32_t)PT_CLOSE_TIMEOUT_TICKS) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                "TCPClose timeout, forcing abort");

            TCPiopb abort_pb;
            pt_memset(&abort_pb, 0, sizeof(abort_pb));
            abort_pb.csCode = TCPAbort;
            abort_pb.ioCRefNum = md->driver_refnum;
            abort_pb.tcpStream = hot->stream;
            PBControl((ParmBlkPtr)&abort_pb, false);

            hot->async_pending = 0;
        }

        if (hot->async_pending)
            continue;

        /* Close completed - log result */
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_CONNECT,
            "TCPClose completed: result=%d", hot->async_result);

        /* Close completed - release stream */
        struct pt_peer *peer = hot->peer;
        pt_mactcp_tcp_release(ctx, i);

        if (peer) {
            peer->tcp_stream_idx = -1;

            if (ctx->callbacks.on_peer_disconnected) {
                ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                                    peer->info.id, PEERTALK_ERR_NONE,
                                                    ctx->callbacks.user_data);
            }

            pt_peer_set_state(peer, PT_PEER_UNUSED);
        }

        processed++;
    }

    return processed;
}
```

### Acceptance Criteria
1. TCPActiveOpen issued asynchronously
2. Connect completion detected via polling
3. Peer state transitions correctly
4. Connection timeout handled
5. Graceful disconnect with TCPClose
6. Stream released properly

---

## Session 5.7: TCP I/O

### Objective
Implement send and receive with proper WDS/RDS handling.

### Key Insight from MacTCP Programmer's Guide:
> "Using the TCPNoCopyRcv routine is the high-performance method. Data is delivered to the user directly from the internal TCP receive buffers and no copy is required."

### Tasks

#### Task 5.7.1: Create `src/mactcp/tcp_io.c`

```c
#include "mactcp_defs.h"
#include "protocol.h"
#include "peer.h"
#include "pt_internal.h"
#include "log.h"

/*
 * Send data on TCP stream
 * From MacTCP Programmer's Guide: TCPSend with WDS
 *
 * DOD: Uses hot/cold struct split. Looks up stream by index stored in peer.
 */
int pt_mactcp_tcp_send(struct pt_context *ctx, struct pt_peer *peer,
                       const void *data, unsigned short len) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_message_header hdr;
    uint8_t header_buf[PT_MESSAGE_HEADER_SIZE];
    uint8_t crc_buf[2];
    uint16_t crc;
    wdsEntry wds[4];
    OSErr err;

    if (!peer || peer->magic != PT_PEER_MAGIC)
        return PEERTALK_ERR_INVALID;

    int idx = peer->tcp_stream_idx;
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return PEERTALK_ERR_STATE;

    pt_tcp_stream_hot *hot = &md->tcp_hot[idx];
    pt_tcp_stream_cold *cold = &md->tcp_cold[idx];

    if (hot->state != PT_STREAM_CONNECTED)
        return PEERTALK_ERR_STATE;

    if (len > PT_MESSAGE_MAX_PAYLOAD)
        return PEERTALK_ERR_INVALID;

    /* Build message header */
    hdr.type = PT_MSG_DATA;
    hdr.flags = 0;
    hdr.sequence = peer->send_seq++;
    hdr.payload_len = len;

    pt_message_encode_header(&hdr, header_buf);

    /* Calculate CRC */
    crc = pt_crc16(header_buf, PT_MESSAGE_HEADER_SIZE);
    if (len > 0)
        crc = pt_crc16_update(crc, data, len);
    crc_buf[0] = (crc >> 8) & 0xFF;
    crc_buf[1] = crc & 0xFF;

    /* Build WDS: header + payload + CRC
     *
     * AMENDMENT (2026-02-03): LaunchAPPL Pattern Verification
     * Verified from LaunchAPPL MacTCPStream.cc:96-98 - WDS array pattern:
     *   wdsEntry wds[2] = { {(unsigned short)n, (Ptr)p}, {0, nullptr} };
     *
     * LaunchAPPL uses stack-allocated WDS because TCPSend is synchronous
     * (PBControlSync blocks until complete). Stack allocation is safe.
     *
     * For async TCPSend (PBControl with async=true), WDS and all referenced
     * buffers MUST persist until completion callback fires. Consider:
     *   - Allocating WDS in cold struct (persistent storage)
     *   - Pool of pre-allocated WDS buffers
     *   - Ref-counted buffer management
     *
     * PERFORMANCE: Async send enables higher throughput but adds complexity.
     * Current implementation uses sync for simplicity per PROJECT_GOALS.md.
     */
    wds[0].length = PT_MESSAGE_HEADER_SIZE;
    wds[0].ptr = (Ptr)header_buf;
    wds[1].length = len;
    wds[1].ptr = (Ptr)data;
    wds[2].length = 2;
    wds[2].ptr = (Ptr)crc_buf;
    wds[3].length = 0;  /* Terminator - WDS array MUST end with zero-length entry */
    wds[3].ptr = NULL;

    /* Setup send call (pb in cold struct) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPSend;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;

    cold->pb.csParam.send.ulpTimeoutValue = 30;
    cold->pb.csParam.send.ulpTimeoutAction = 1;
    cold->pb.csParam.send.validityFlags = 0xC0;
    cold->pb.csParam.send.pushFlag = 1;  /* Push immediately */
    cold->pb.csParam.send.urgentFlag = 0;
    cold->pb.csParam.send.wdsPtr = (Ptr)wds;

    /* Synchronous send - simplifies buffer lifetime
     *
     * From MacTCP Programmer's Guide: "The command is completed when all
     * data has been sent and acknowledged or when an error occurs."
     *
     * NOTE: This can block for 30+ seconds on slow/lossy connections.
     * For higher throughput, consider async TCPSend with a pool of
     * WDS buffers (see Performance Note at end of phase). However,
     * async send requires careful buffer lifetime management - the WDS
     * and all referenced buffers must remain valid until completion.
     */
    err = PBControl((ParmBlkPtr)&cold->pb, false);

    if (err != noErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
            "TCPSend failed: %d", err);

        if (err == connectionClosing || err == connectionTerminated) {
            hot->asr_flags |= PT_ASR_CONN_CLOSED;
        }

        return PEERTALK_ERR_NETWORK;
    }

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_NETWORK,
        "Sent %u bytes to peer %u (seq=%u)", len, peer->info.id, hdr.sequence);

    /* Ticks is a low-memory global - safe to read (unlike TickCount()) */
    peer->last_seen = Ticks;
    pt_peer_check_canaries(peer);

    return PEERTALK_SUCCESS;
}

/*
 * Receive data using TCPNoCopyRcv (high-performance method)
 *
 * NOTE: For simpler implementations or debugging, TCPRcv can be used
 * instead. TCPRcv copies data to your buffer directly, eliminating the
 * need for TCPRcvBfrReturn, at the cost of an extra memory copy. Use
 * TCPNoCopyRcv (this function) for production performance.
 *
 * DOD: Uses hot/cold struct split. Looks up stream by index stored in peer.
 */
int pt_mactcp_tcp_recv(struct pt_context *ctx, struct pt_peer *peer) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_message_header hdr;
    uint8_t *data_ptr;
    unsigned short data_len;
    uint16_t crc_expected, crc_actual;
    OSErr err;

    if (!peer || peer->magic != PT_PEER_MAGIC)
        return PEERTALK_ERR_INVALID;

    int idx = peer->tcp_stream_idx;
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return 0;

    pt_tcp_stream_hot *hot = &md->tcp_hot[idx];
    pt_tcp_stream_cold *cold = &md->tcp_cold[idx];

    /* Check for connection close (hot struct flags) */
    if (hot->asr_flags & PT_ASR_CONN_CLOSED) {
        hot->asr_flags &= ~PT_ASR_CONN_CLOSED;
        return -1;  /* Trigger disconnect */
    }

    /* Check for data (hot struct flags) */
    if (!(hot->asr_flags & PT_ASR_DATA_ARRIVED))
        return 0;

    hot->asr_flags &= ~PT_ASR_DATA_ARRIVED;

    /* Return any previous RDS buffers (rds in cold struct) */
    if (hot->rds_outstanding) {
        TCPiopb return_pb;
        pt_memset(&return_pb, 0, sizeof(return_pb));
        return_pb.csCode = TCPRcvBfrReturn;
        return_pb.ioCRefNum = md->driver_refnum;
        return_pb.tcpStream = hot->stream;
        return_pb.csParam.receive.rdsPtr = (Ptr)cold->rds;

        err = PBControl((ParmBlkPtr)&return_pb, false);
        if (err != noErr) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
                "TCPRcvBfrReturn failed: %d (buffer leak possible)", err);
        }
        hot->rds_outstanding = 0;
    }

    /* Issue TCPNoCopyRcv (pb and rds in cold struct)
     * From MacTCP Programmer's Guide: "The minimum value of the command
     * timeout is 2 seconds; 0 means infinite."
     *
     * Since we only call this after ASR signals data arrival, data is
     * already buffered and will return immediately. The 2s timeout is
     * only a fallback for rare race conditions.
     */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPNoCopyRcv;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;

    cold->pb.csParam.receive.commandTimeoutValue = 2;  /* Minimum per docs */
    cold->pb.csParam.receive.rdsPtr = (Ptr)cold->rds;
    cold->pb.csParam.receive.rdsLength = sizeof(cold->rds) / sizeof(cold->rds[0]);

    err = PBControl((ParmBlkPtr)&cold->pb, false);

    if (err == commandTimeout) {
        /* No data ready */
        return 0;
    }

    if (err != noErr) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
            "TCPNoCopyRcv failed: %d", err);

        if (err == connectionClosing || err == connectionTerminated) {
            return -1;
        }

        return 0;
    }

    /* Mark RDS as needing return (hot struct flag) */
    hot->rds_outstanding = 1;

    /* Process received data - may be in multiple RDS entries (in cold struct) */
    /* For simplicity, copy to peer's ibuf and process there */
    data_len = 0;
    int rds_idx;
    for (rds_idx = 0; cold->rds[rds_idx].length > 0; rds_idx++) {
        unsigned short chunk_len = cold->rds[rds_idx].length;
        if (data_len + chunk_len > sizeof(peer->ibuf)) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
                "Received data exceeds ibuf");
            break;
        }
        pt_memcpy(peer->ibuf + data_len, cold->rds[rds_idx].ptr, chunk_len);
        data_len += chunk_len;
    }

    if (data_len < PT_MESSAGE_HEADER_SIZE + 2) {
        /* Not enough data for header + CRC - partial message */
        return 0;
    }

    /* Parse header */
    if (pt_message_decode_header(peer->ibuf, data_len, &hdr) < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
            "Invalid message header from peer %u", peer->info.id);
        return 0;
    }

    /* Check we have complete message */
    unsigned short expected_len = PT_MESSAGE_HEADER_SIZE + hdr.payload_len + 2;
    if (data_len < expected_len) {
        /* Partial message - need more data */
        return 0;
    }

    /* Verify CRC */
    data_ptr = peer->ibuf + PT_MESSAGE_HEADER_SIZE;
    crc_expected = (peer->ibuf[PT_MESSAGE_HEADER_SIZE + hdr.payload_len] << 8) |
                    peer->ibuf[PT_MESSAGE_HEADER_SIZE + hdr.payload_len + 1];
    crc_actual = pt_crc16(peer->ibuf, PT_MESSAGE_HEADER_SIZE);
    if (hdr.payload_len > 0)
        crc_actual = pt_crc16_update(crc_actual, data_ptr, hdr.payload_len);

    if (crc_actual != crc_expected) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
            "CRC mismatch: expected=%04X actual=%04X",
            crc_expected, crc_actual);
        return 0;
    }

    /* Update peer state */
    peer->last_seen = Ticks;
    peer->recv_seq = hdr.sequence;
    pt_peer_check_canaries(peer);

    /* Handle by message type */
    switch (hdr.type) {
    case PT_MSG_DATA:
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_NETWORK,
            "Received %u bytes from peer %u (seq=%u)",
            hdr.payload_len, peer->info.id, hdr.sequence);

        if (ctx->callbacks.on_message_received) {
            ctx->callbacks.on_message_received(
                (PeerTalk_Context *)ctx,
                peer->info.id, data_ptr, hdr.payload_len,
                ctx->callbacks.user_data);
        }
        break;

    case PT_MSG_PING:
        pt_mactcp_tcp_send_control(ctx, idx, PT_MSG_PONG);
        break;

    case PT_MSG_PONG:
        /* Update latency estimate */
        break;

    case PT_MSG_DISCONNECT:
        PT_LOG_INFO(ctx, PT_LOG_CAT_NETWORK,
            "Received DISCONNECT from peer %u", peer->info.id);
        return -1;
    }

    return 1;
}

/*
 * Send control message (ping/pong/disconnect)
 *
 * DOD: Uses hot/cold struct split. Takes stream index.
 */
int pt_mactcp_tcp_send_control(struct pt_context *ctx,
                               int stream_idx,
                               uint8_t msg_type) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot = &md->tcp_hot[stream_idx];
    pt_tcp_stream_cold *cold = &md->tcp_cold[stream_idx];
    pt_message_header hdr;
    uint8_t buf[PT_MESSAGE_HEADER_SIZE + 2];
    uint16_t crc;
    wdsEntry wds[2];
    OSErr err;

    hdr.type = msg_type;
    hdr.flags = 0;
    hdr.sequence = 0;
    hdr.payload_len = 0;

    pt_message_encode_header(&hdr, buf);

    crc = pt_crc16(buf, PT_MESSAGE_HEADER_SIZE);
    buf[PT_MESSAGE_HEADER_SIZE] = (crc >> 8) & 0xFF;
    buf[PT_MESSAGE_HEADER_SIZE + 1] = crc & 0xFF;

    wds[0].length = sizeof(buf);
    wds[0].ptr = (Ptr)buf;
    wds[1].length = 0;
    wds[1].ptr = NULL;

    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPSend;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;
    cold->pb.csParam.send.pushFlag = 1;
    cold->pb.csParam.send.wdsPtr = (Ptr)wds;

    err = PBControl((ParmBlkPtr)&cold->pb, false);

    if (err == noErr) {
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_NETWORK,
            "Control message sent: type=%d", msg_type);
    } else {
        PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
            "Control message send failed: type=%d err=%d", msg_type, err);
    }

    return (err == noErr) ? 0 : -1;
}
```

### Acceptance Criteria
1. TCPSend with WDS works correctly
2. TCPNoCopyRcv receives data efficiently
3. TCPRcvBfrReturn called after receive
4. Message framing/CRC validation works
5. Control messages (ping/pong/disconnect) work
6. Buffer canaries checked after operations

---

## Session 5.8: Integration

### Objective
Integrate all MacTCP components and test end-to-end on emulator.

### Tasks

#### Task 5.8.1: Create main poll function

```c
/* src/mactcp/poll_mactcp.c
 *
 * DOD: Single-pass polling with state dispatch.
 * Instead of multiple loops filtering by state, we iterate once
 * and dispatch based on state. This reduces from 3× array traversals
 * to 1×, and only accesses hot struct data during the scan.
 */

int pt_mactcp_poll(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    /* Ticks is a low-memory global - safe to read (unlike TickCount()) */
    unsigned long now = Ticks;
    int i;

    /* Process discovery */
    while (pt_mactcp_discovery_poll(ctx) > 0)
        ;

    /* Process listener */
    pt_mactcp_listen_poll(ctx);

    /* DOD: Single-pass polling of all TCP streams
     * Combines what was connect_poll, close_poll, and connected I/O poll
     * into one iteration over the hot struct array.
     */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        pt_tcp_stream_hot *hot = &md->tcp_hot[i];
        pt_tcp_stream_cold *cold = &md->tcp_cold[i];
        struct pt_peer *peer = hot->peer;

        /* Dispatch based on state - only access cold struct when needed */
        switch (hot->state) {
        case PT_STREAM_CONNECTING:
            pt_mactcp_poll_connecting(ctx, md, i, hot, cold, peer, now);
            break;

        case PT_STREAM_CONNECTED:
            pt_mactcp_poll_connected(ctx, i, hot, cold, peer);
            break;

        case PT_STREAM_CLOSING:
            pt_mactcp_poll_closing(ctx, md, i, hot, cold, peer, now);
            break;

        /* PT_STREAM_UNUSED, PT_STREAM_IDLE, etc. - nothing to poll */
        default:
            break;
        }
    }

    /* Periodic discovery announce
     * Mac SE 4MB: Use 15 seconds to reduce CPU/network load
     * Performa 6200+: Can use 10 seconds
     * Configurable via ctx->config.discovery_interval_ticks
     */
    unsigned long announce_interval = ctx->config.discovery_interval_ticks;
    if (announce_interval == 0)
        announce_interval = 15 * 60;  /* Default 15 seconds for memory-constrained Macs */

    /* Signed comparison for Ticks wrap */
    if (ctx->discovery_active &&
        (int32_t)(now - md->last_announce_tick) > (int32_t)announce_interval) {
        pt_mactcp_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);
        md->last_announce_tick = now;
    }

    /* Check for peer timeouts - hoist invariant calculation */
    {
        unsigned long timeout_ticks = 30 * 60;  /* 30 seconds at 60 ticks/sec */

        for (i = 0; i < ctx->max_peers; i++) {
            struct pt_peer *peer = &ctx->peers[i];

            /* Signed comparison for Ticks wrap */
            if (peer->state == PT_PEER_DISCOVERED &&
                (int32_t)(now - peer->last_seen) > (int32_t)timeout_ticks) {

                if (ctx->callbacks.on_peer_lost) {
                    ctx->callbacks.on_peer_lost((PeerTalk_Context *)ctx,
                                                peer->info.id, ctx->callbacks.user_data);
                }

                pt_peer_destroy(ctx, peer);
            }
        }
    }

    return 0;
}

/*
 * DOD: State-specific poll handlers
 * These are called from the single-pass loop above.
 */

static void pt_mactcp_poll_connecting(struct pt_context *ctx,
                                       pt_mactcp_data *md,
                                       int idx,
                                       pt_tcp_stream_hot *hot,
                                       pt_tcp_stream_cold *cold,
                                       struct pt_peer *peer,
                                       unsigned long now) {
    /* DOD: md passed as parameter to avoid re-fetching */

    if (!peer)
        return;

    /* Check for connection timeout BEFORE checking async_pending
     * Use signed comparison to handle Ticks wrap-around correctly */
    if (hot->async_pending &&
        (int32_t)(now - peer->connect_start) > (int32_t)(30 * 60)) {  /* 30 seconds */
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "Connect to peer %u timed out after 30s, aborting",
            peer->info.id);

        /* Abort the pending connection */
        TCPiopb abort_pb;
        pt_memset(&abort_pb, 0, sizeof(abort_pb));
        abort_pb.csCode = TCPAbort;
        abort_pb.ioCRefNum = md->driver_refnum;
        abort_pb.tcpStream = hot->stream;
        PBControl((ParmBlkPtr)&abort_pb, false);

        hot->async_pending = 0;
        hot->async_result = connectionTerminated;
    }

    if (hot->async_pending)
        return;  /* Still waiting (and not timed out) */

    /* Connection attempt completed */
    if (hot->async_result == noErr) {
        /* Success! */
        PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
            "Connected to peer %u (%s)", peer->info.id, peer->info.name);

        hot->state = PT_STREAM_CONNECTED;
        pt_peer_set_state(peer, PT_PEER_CONNECTED);
        peer->last_seen = Ticks;

        if (ctx->callbacks.on_peer_connected) {
            ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                             peer->info.id,
                                             ctx->callbacks.user_data);
        }
    } else {
        /* Failed */
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "Connect to peer %u failed: %d",
            peer->info.id, hot->async_result);

        pt_peer_set_state(peer, PT_PEER_FAILED);
        peer->tcp_stream_idx = -1;

        pt_mactcp_tcp_release(ctx, idx);
    }
}

static void pt_mactcp_poll_connected(struct pt_context *ctx, int idx,
                                      pt_tcp_stream_hot *hot,
                                      pt_tcp_stream_cold *cold,
                                      struct pt_peer *peer) {
    if (!peer)
        return;

    /* Receive data */
    int result = pt_mactcp_tcp_recv(ctx, peer);
    if (result < 0) {
        /* Connection lost */
        if (ctx->callbacks.on_peer_disconnected) {
            ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                                peer->info.id, PEERTALK_ERR_NETWORK,
                                                ctx->callbacks.user_data);
        }

        peer->tcp_stream_idx = -1;
        pt_peer_destroy(ctx, peer);
        pt_mactcp_tcp_release(ctx, idx);
    }
}

static void pt_mactcp_poll_closing(struct pt_context *ctx,
                                    pt_mactcp_data *md,
                                    int idx,
                                    pt_tcp_stream_hot *hot,
                                    pt_tcp_stream_cold *cold,
                                    struct pt_peer *peer,
                                    unsigned long now) {
    /* DOD: md passed as parameter to avoid re-fetching */

    /* Check for close timeout (signed comparison for Ticks wrap) */
    if (hot->async_pending &&
        (int32_t)(now - cold->close_start) > (int32_t)(10 * 60)) {  /* 10 seconds */
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "TCPClose timeout, forcing abort");

        TCPiopb abort_pb;
        pt_memset(&abort_pb, 0, sizeof(abort_pb));
        abort_pb.csCode = TCPAbort;
        abort_pb.ioCRefNum = md->driver_refnum;
        abort_pb.tcpStream = hot->stream;
        PBControl((ParmBlkPtr)&abort_pb, false);

        hot->async_pending = 0;
    }

    if (hot->async_pending)
        return;

    /* Close completed - release stream */
    pt_mactcp_tcp_release(ctx, idx);

    if (peer) {
        peer->tcp_stream_idx = -1;

        if (ctx->callbacks.on_peer_disconnected) {
            ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                                peer->info.id, PEERTALK_ERR_NONE,
                                                ctx->callbacks.user_data);
        }

        pt_peer_set_state(peer, PT_PEER_UNUSED);
    }
}
```

### Acceptance Criteria
1. All MacTCP components work together
2. Discovery finds peers
3. Connections can be established
4. Messages can be exchanged
5. Disconnections handled cleanly
6. **Builds and loads in Basilisk II emulator** (build validation only)
7. **MUST verify on REAL 68k Mac hardware:**
   - Primary: Mac SE 4MB, System 6.0.8, MacTCP 2.1
   - Secondary: Performa 6200, System 7.5.3, MacTCP 2.1
8. No memory leaks: **Check MaxBlock before/after 50+ operations** - values must match
9. Connection timeout works (30s max wait for unresponsive hosts)
10. TCPClose completes without blocking main loop (async close)
11. **Cross-platform interop:** POSIX peer discovers Mac peer and vice versa
12. **Cross-platform messaging:** Send message from POSIX→Mac and Mac→POSIX
13. **Mac SE 4MB specific:** FreeMem > 500KB after init, 4 peers work simultaneously
14. **Performa 6200 specific:** Extended stress test passes (100+ operations)

**MaxBlock Verification Code:**
```c
/* Run this test on real hardware to verify no leaks */
void pt_mactcp_leak_test(struct pt_context *ctx) {
    long block_before = MaxBlock();
    long free_before = FreeMem();
    int i;

    PT_LOG_INFO(ctx, PT_LOG_CAT_MEMORY,
        "Leak test start: MaxBlock=%ld FreeMem=%ld", block_before, free_before);

    /* Perform 50 connect/disconnect cycles */
    for (i = 0; i < 50; i++) {
        /* ... create stream, connect, disconnect, release ... */
    }

    long block_after = MaxBlock();
    long free_after = FreeMem();

    PT_LOG_INFO(ctx, PT_LOG_CAT_MEMORY,
        "Leak test end: MaxBlock=%ld (delta=%ld) FreeMem=%ld (delta=%ld)",
        block_after, block_after - block_before,
        free_after, free_after - free_before);

    /* MaxBlock should be same or higher (fragmentation reduces it) */
    /* FreeMem delta should be small (< 1KB acceptable for heap overhead) */
    if (block_after < block_before - 1024) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_MEMORY, "WARNING: Possible memory leak!");
    }
}
```

---

## Session 5.9: AppleTalk Integration

### Objective
Create the multi-transport infrastructure to support both MacTCP (TCP/IP) and AppleTalk in a unified library. This session establishes the types and poll integration; Phase 7 provides the AppleTalk implementation.

### Key Insight
A Mac with both TCP/IP (Ethernet) and AppleTalk (LocalTalk or EtherTalk) can participate in **both** networks simultaneously. The unified library `libpeertalk_mactcp_at.a` enables this.

### Phase 2 Prerequisite

This session requires `pt_peer_find_by_name()` which must exist in Phase 2, Session 2.2.

**Status:** ⚠️ Verify before starting 5.9 - Check PHASE-2-PROTOCOL.md Session 2.2 includes this function.

**Required signature:**
```c
/* Find existing peer by name string (not ID).
 * Used for multi-transport deduplication in Phase 5.9 and Phase 6.8.
 * Returns: Peer pointer if found, NULL if not found.
 */
struct pt_peer *pt_peer_find_by_name(struct pt_context *ctx, const char *name);
```

See `src/core/peer.h` and `src/core/peer.c` in Phase 2 plan for the declaration and implementation.

### Tasks

#### Task 5.9.1: Create `src/mactcp/mactcp_multi.h`

```c
#ifndef PT_MACTCP_MULTI_H
#define PT_MACTCP_MULTI_H

#include "mactcp_defs.h"

/* Forward declaration for AppleTalk data (defined in Phase 7) */
struct pt_appletalk_data;

/* Transport flags for peer discovery */
#define PT_TRANSPORT_NONE       0x00
#define PT_TRANSPORT_TCP        0x01
#define PT_TRANSPORT_UDP        0x02
#define PT_TRANSPORT_APPLETALK  0x04

/*
 * Multi-transport context extension
 * This extends pt_mactcp_data when AppleTalk is also linked.
 */
typedef struct {
    /* MacTCP data (always present) */
    pt_mactcp_data          mactcp;

    /* AppleTalk data (present when linked with Phase 7) */
    struct pt_appletalk_data *appletalk;

    /* Multi-transport state */
    uint8_t                 transports_available;  /* Bitmask of available transports */
    Boolean                 appletalk_enabled;     /* AppleTalk initialized successfully */

} pt_mactcp_multi_data;

/* Get multi-transport data from context */
static inline pt_mactcp_multi_data *pt_mactcp_multi_get(struct pt_context *ctx) {
    return (pt_mactcp_multi_data *)((char *)ctx + sizeof(struct pt_context));
}

/* Multi-transport initialization */
int pt_mactcp_multi_init(struct pt_context *ctx);
void pt_mactcp_multi_shutdown(struct pt_context *ctx);

/* Unified poll - processes both MacTCP and AppleTalk events */
int pt_mactcp_multi_poll(struct pt_context *ctx);

/* Transport query */
uint8_t pt_mactcp_multi_get_transports(struct pt_context *ctx);

/*
 * Peer deduplication
 * When the same peer is discovered on both TCP/IP and AppleTalk,
 * we merge them into a single peer entry with multiple transports.
 */
struct pt_peer *pt_mactcp_multi_find_or_create_peer(
    struct pt_context *ctx,
    const char *name,
    ip_addr tcp_ip,           /* 0 if not discovered via TCP */
    tcp_port tcp_port,        /* 0 if not discovered via TCP */
    /* AppleTalk address fields would be added by Phase 7 */
    uint8_t transport_flags
);

/* Update peer's available transports */
void pt_mactcp_multi_peer_add_transport(
    struct pt_peer *peer,
    uint8_t transport_flag
);

#endif /* PT_MACTCP_MULTI_H */
```

#### Task 5.9.2: Create `src/mactcp/mactcp_multi.c`

```c
#include "mactcp_multi.h"
#include "pt_internal.h"
#include "peer.h"
#include "log.h"

/* Weak symbol for AppleTalk init - linked by Phase 7 if present */
extern int pt_appletalk_init(struct pt_context *ctx) __attribute__((weak));
extern void pt_appletalk_shutdown(struct pt_context *ctx) __attribute__((weak));
extern int pt_appletalk_poll(struct pt_context *ctx) __attribute__((weak));

size_t pt_mactcp_multi_extra_size(void) {
    return sizeof(pt_mactcp_multi_data);
}

/*
 * Initialize multi-transport layer
 * Called instead of pt_mactcp_net_init when using unified library
 */
int pt_mactcp_multi_init(struct pt_context *ctx) {
    pt_mactcp_multi_data *md = pt_mactcp_multi_get(ctx);
    int result;

    /* Clear multi-transport state */
    md->appletalk = NULL;
    md->transports_available = PT_TRANSPORT_NONE;
    md->appletalk_enabled = false;

    /* Initialize MacTCP (always) */
    result = pt_mactcp_net_init(ctx);
    if (result == 0) {
        md->transports_available |= (PT_TRANSPORT_TCP | PT_TRANSPORT_UDP);
        PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL,
            "MacTCP initialized successfully");
    } else {
        PT_LOG_WARN(ctx, PT_LOG_CAT_GENERAL,
            "MacTCP init failed: %d (continuing with AppleTalk only)", result);
    }

    /* Initialize AppleTalk if linked (weak symbol check) */
    if (pt_appletalk_init != NULL) {
        result = pt_appletalk_init(ctx);
        if (result == 0) {
            md->transports_available |= PT_TRANSPORT_APPLETALK;
            md->appletalk_enabled = true;
            PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL,
                "AppleTalk initialized successfully");
        } else {
            PT_LOG_WARN(ctx, PT_LOG_CAT_GENERAL,
                "AppleTalk init failed: %d (continuing with MacTCP only)", result);
        }
    }

    if (md->transports_available == PT_TRANSPORT_NONE) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_GENERAL,
            "No transports available - both MacTCP and AppleTalk failed");
        return -1;
    }

    PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL,
        "Multi-transport init complete: transports=0x%02X", md->transports_available);

    return 0;
}

/*
 * Shutdown multi-transport layer
 */
void pt_mactcp_multi_shutdown(struct pt_context *ctx) {
    pt_mactcp_multi_data *md = pt_mactcp_multi_get(ctx);

    /* Shutdown AppleTalk if it was enabled */
    if (md->appletalk_enabled && pt_appletalk_shutdown != NULL) {
        pt_appletalk_shutdown(ctx);
        md->appletalk_enabled = false;
    }

    /* Shutdown MacTCP */
    pt_mactcp_net_shutdown(ctx);

    md->transports_available = PT_TRANSPORT_NONE;

    PT_LOG_INFO(ctx, PT_LOG_CAT_GENERAL, "Multi-transport shutdown complete");
}

/*
 * Unified poll loop - processes events from all active transports
 */
int pt_mactcp_multi_poll(struct pt_context *ctx) {
    pt_mactcp_multi_data *md = pt_mactcp_multi_get(ctx);
    int events = 0;

    /* Poll MacTCP if available */
    if (md->transports_available & (PT_TRANSPORT_TCP | PT_TRANSPORT_UDP)) {
        events += pt_mactcp_poll(ctx);
    }

    /* Poll AppleTalk if enabled and linked */
    if (md->appletalk_enabled && pt_appletalk_poll != NULL) {
        events += pt_appletalk_poll(ctx);
    }

    return events;
}

/*
 * Query available transports
 */
uint8_t pt_mactcp_multi_get_transports(struct pt_context *ctx) {
    pt_mactcp_multi_data *md = pt_mactcp_multi_get(ctx);
    return md->transports_available;
}

/*
 * Find or create peer, handling deduplication across transports
 *
 * When the same peer appears on both TCP/IP and AppleTalk (identified by name),
 * we merge into a single peer entry rather than creating duplicates.
 */
struct pt_peer *pt_mactcp_multi_find_or_create_peer(
    struct pt_context *ctx,
    const char *name,
    ip_addr tcp_ip,
    tcp_port tcp_port,
    uint8_t transport_flags)
{
    struct pt_peer *peer;

    /* First, try to find by name (cross-transport deduplication)
     * NOTE: pt_peer_find_by_name() must be added to Phase 2, Session 2.2
     * It iterates ctx->peers looking for matching info.name.
     */
    peer = pt_peer_find_by_name(ctx, name);

    if (peer) {
        /* Existing peer - add new transport if discovered via different network */
        pt_mactcp_multi_peer_add_transport(peer, transport_flags);

        /* Update TCP address if this is a TCP discovery */
        if ((transport_flags & PT_TRANSPORT_TCP) && tcp_ip != 0) {
            peer->info.address = tcp_ip;
            peer->info.port = tcp_port;
        }

        /* Ticks is a low-memory global - safe to read (unlike TickCount()) */
        peer->last_seen = Ticks;
        return peer;
    }

    /* New peer - create with initial transport */
    if (transport_flags & PT_TRANSPORT_TCP) {
        peer = pt_peer_create(ctx, name, tcp_ip, tcp_port);
    } else {
        /* AppleTalk-only peer - no IP address */
        peer = pt_peer_create(ctx, name, 0, 0);
    }

    if (peer) {
        peer->info.transports_available = transport_flags;
    }

    return peer;
}

/*
 * Add transport to existing peer
 */
void pt_mactcp_multi_peer_add_transport(
    struct pt_peer *peer,
    uint8_t transport_flag)
{
    if (peer && peer->magic == PT_PEER_MAGIC) {
        peer->info.transports_available |= transport_flag;
    }
}
```

### Acceptance Criteria
1. Multi-transport types compile cleanly
2. Init succeeds with MacTCP only (AppleTalk not linked)
3. Init succeeds with both transports (when AppleTalk linked)
4. Peer deduplication works (same name = same peer)
5. Transport flags correctly track available transports

---

## Session 5.10: Unified Library Build

### Objective
Update the Retro68 Makefile to build `libpeertalk_mactcp_at.a` - the unified library with both MacTCP and AppleTalk support.

### Tasks

#### Task 5.10.1: Update `Makefile.retro68`

Add the following targets to the existing Makefile:

```makefile
# Unified MacTCP + AppleTalk library
# Links Phase 5 (MacTCP) and Phase 7 (AppleTalk) into single library

MACTCP_SRCS = \
    src/mactcp/mactcp_driver.c \
    src/mactcp/udp_mactcp.c \
    src/mactcp/discovery_mactcp.c \
    src/mactcp/tcp_mactcp.c \
    src/mactcp/tcp_listen.c \
    src/mactcp/tcp_connect.c \
    src/mactcp/tcp_io.c \
    src/mactcp/poll_mactcp.c \
    src/mactcp/mactcp_multi.c

APPLETALK_SRCS = \
    src/appletalk/at_driver.c \
    src/appletalk/nbp_appletalk.c \
    src/appletalk/adsp_appletalk.c \
    src/appletalk/adsp_listen.c \
    src/appletalk/adsp_connect.c \
    src/appletalk/adsp_io.c \
    src/appletalk/poll_appletalk.c

CORE_SRCS = \
    src/core/context.c \
    src/core/peer.c \
    src/core/protocol.c \
    src/core/queue.c \
    src/core/log.c \
    src/core/pt_compat.c

# MacTCP-only library (TCP/IP only)
libpeertalk_mactcp.a: $(CORE_SRCS:.c=.o) $(MACTCP_SRCS:.c=.o)
	$(AR) rcs $@ $^

# AppleTalk-only library
libpeertalk_at.a: $(CORE_SRCS:.c=.o) $(APPLETALK_SRCS:.c=.o)
	$(AR) rcs $@ $^

# Unified MacTCP + AppleTalk library
libpeertalk_mactcp_at.a: $(CORE_SRCS:.c=.o) $(MACTCP_SRCS:.c=.o) $(APPLETALK_SRCS:.c=.o)
	$(AR) rcs $@ $^

# Build targets
mactcp: libpeertalk_mactcp.a
appletalk: libpeertalk_at.a
unified: libpeertalk_mactcp_at.a

.PHONY: mactcp appletalk unified
```

#### Task 5.10.2: Add conditional compilation flags

```makefile
# Compiler flags for unified build
CFLAGS_UNIFIED = -DPT_MULTI_TRANSPORT=1

# When building unified library, define PT_MULTI_TRANSPORT
# This enables the multi-transport code paths

$(MACTCP_SRCS:.c=_unified.o): CFLAGS += $(CFLAGS_UNIFIED)
$(APPLETALK_SRCS:.c=_unified.o): CFLAGS += $(CFLAGS_UNIFIED)
```

#### Task 5.10.3: Document library selection in README

Add to documentation:

```
## Library Selection Guide

| Library | Use When | Size (approx) |
|---------|----------|---------------|
| libpeertalk_mactcp.a | TCP/IP only (most common) | ~40KB |
| libpeertalk_at.a | AppleTalk only (LocalTalk networks) | ~35KB |
| libpeertalk_mactcp_at.a | Both networks (maximum compatibility) | ~70KB |

### Unified Library Benefits

With `libpeertalk_mactcp_at.a`:
- Discover peers on both TCP/IP and AppleTalk simultaneously
- Same peer appearing on both networks shows as single entry
- Connect via whichever transport is available
- Failover: if TCP fails, try AppleTalk (and vice versa)

### Memory Impact

The unified library uses ~30KB more code than single-transport.
For Mac SE 4MB, single-transport (`libpeertalk_mactcp.a`) is recommended.
For 8MB+ systems, unified library is preferred for maximum reach.
```

### Acceptance Criteria
1. `make mactcp` builds `libpeertalk_mactcp.a`
2. `make unified` builds `libpeertalk_mactcp_at.a` (after Phase 7 complete)
3. Unified library links without errors
4. Test app using unified library discovers peers on both networks
5. Peer deduplication verified (same peer, different networks = one entry)

---

## Phase 5 Complete Checklist

### Core Functionality
- [ ] MacTCP driver opens successfully
- [ ] Local IP retrieved
- [ ] **UPPs created at init** (tcp_notify, udp_notify, completions)
- [ ] **UPPs disposed at shutdown** (after all streams released)
- [ ] UDP stream create/release
- [ ] UDP ASR fires on data (via UPP)
- [ ] UDP send (broadcast)
- [ ] UDP receive with buffer return (UDPBfrReturn called)
- [ ] Discovery packets work
- [ ] TCP stream create/release
- [ ] TCP ASR fires correctly (via UPP)
- [ ] TCPPassiveOpen async (completion via UPP)
- [ ] Listen poll detects connections
- [ ] Stream transfer pattern works
- [ ] TCPActiveOpen async with connection timeout (completion via UPP)
- [ ] Connect poll detects completion
- [ ] TCPClose async (non-blocking) with timeout monitoring (completion via UPP)
- [ ] Close poll detects completion and releases streams
- [ ] TCPSend with WDS
- [ ] TCPNoCopyRcv works
- [ ] TCPRcvBfrReturn called after every successful receive
- [ ] Message framing over TCP
- [ ] Main poll integrates all
- [ ] Build validated in Basilisk II emulator

### Mac SE 4MB Verification (Primary Target)
- [ ] **Tested on Mac SE with 4MB RAM, System 6.0.8, MacTCP 2.1**
- [ ] FreeMem > 500KB after PeerTalk init
- [ ] MaxBlock > 100KB (can allocate new streams)
- [ ] 4 simultaneous peer connections work
- [ ] No -108 (memFullErr) during normal operation
- [ ] MaxBlock same before/after 10+ operations
- [ ] Memory-aware buffer sizing uses 8KB TCP buffers

### Performa 6200 Verification (Secondary Target)
- [ ] **Tested on Performa 6200, System 7.5.3, MacTCP 2.1**
- [ ] MacTCP driver loads and configures correctly
- [ ] 8 simultaneous peer connections work
- [ ] Extended stress test passes (100+ operations)
- [ ] No hangs or unusual timing issues

### Cross-Platform Verification
- [ ] **POSIX peer discovers Mac peer**
- [ ] **Mac peer discovers POSIX peer**
- [ ] **Messages exchange POSIX→Mac**
- [ ] **Messages exchange Mac→POSIX**
- [ ] Protocol packets valid (verify with Wireshark if needed)

### CSend Lessons Applied (see [CSEND-LESSONS.md](CSEND-LESSONS.md) Part A)
- [ ] Async pool sized at 16+ handles (A.1)
- [x] ~~Operation-in-progress tracking (A.2)~~ - Not needed; state machine prevents concurrent ops
- [ ] No reset delays after TCPAbort (A.3)
- [ ] Listen restarts BEFORE data processing (A.4)
- [x] ~~ASR dispatch table (A.5)~~ - Using userDataPtr instead (simpler, per MacTCP.h)
- [ ] Async close used for pool connections (A.6)
- [ ] Abandoned operations marked free, not cancelled (A.7)
- [ ] WDS uses 2-entry with NULL sentinel (A.8)
- [ ] RDS buffers ALWAYS returned via TCPRcvBfrReturn (A.8)
- [ ] No -108 (memFullErr) under burst traffic
- [ ] Listen restarts in <5ms

### Multi-Transport Integration (Sessions 5.9-5.10)
- [ ] `mactcp_multi.h` types compile cleanly
- [ ] `mactcp_multi.c` compiles with MacTCP-only (no AppleTalk linked)
- [ ] Multi-transport init succeeds with MacTCP only
- [ ] Transport flags correctly set (PT_TRANSPORT_TCP | PT_TRANSPORT_UDP)
- [ ] Peer deduplication by name works
- [ ] `make mactcp` builds `libpeertalk_mactcp.a`
- [ ] `make unified` builds `libpeertalk_mactcp_at.a` (requires Phase 7)
- [ ] Unified library links without errors (after Phase 7 complete)
- [ ] Test app discovers peers on both networks (after Phase 7 complete)

### Data-Oriented Design (DOD) Verification
- [ ] `pt_stream_state` is `uint8_t` (not enum)
- [ ] `pt_asr_flags` is single `volatile uint8_t` bitfield
- [ ] `pt_tcp_stream_hot` struct is ≤18 bytes (with log fields)
- [ ] `pt_udp_stream_hot` struct is ≤12 bytes
- [ ] `pt_mactcp_data` uses hot/cold arrays (not array-of-large-structs)
- [ ] `pt_mactcp_poll()` uses single-pass state dispatch
- [ ] Poll loop only accesses cold struct when state requires I/O
- [ ] ASR callbacks receive hot struct pointer (not full struct)
- [ ] All `int16_t` fields in hot structs are at even offsets (68k alignment)

### DOD Performance Optimizations (from 2026-01-29 review)

**Architectural (Fix Now - hard to change later):**

1. **Consider `peer_idx` instead of peer pointer in hot struct:**
   - Current: `struct pt_peer *peer` (4 bytes, requires pointer chase)
   - Alternative: `int8_t peer_idx` (1 byte, -1 = no peer, direct array index)
   - Benefit: Saves 3 bytes, eliminates cache miss during iteration
   - Trade-off: Requires bounds checking, slightly more complex access
   - Implementation: `&ctx->peers[hot->peer_idx]` vs `hot->peer`

2. **Add `name_hash` to `pt_peer` for multi-transport deduplication:**
   ```c
   typedef struct pt_peer {
       /* ... existing fields ... */
       uint16_t name_hash;  /* CRC16 of info.name for fast rejection */
   } pt_peer;
   ```
   - Used in `pt_peer_find_by_name()` for fast rejection before strcmp
   - Critical when PT_MAX_PEERS > 4-6

**Implementation (Fix Later - can optimize without API changes):**

3. **Hoist loop bounds:** In poll loops, assign `ctx->max_peers` to local variable.

4. **Move WDS to cold struct:** Avoid 32-byte stack allocation per send.

5. **Use pointer arithmetic in RDS loops:** `while (rds->length > 0) { ... rds++; }`

6. **Quick magic check before discovery decode:**
   ```c
   if (len < 4 || buf[0] != 'P' || buf[1] != 'T' || buf[2] != 'L' || buf[3] != 'K')
       return 0;  /* Fast rejection */
   ```

## Performance Note: Async Pool Sizing

From csend performance testing: When under burst load, 8 async operations
was insufficient and caused message drops. **Consider pool size of 16** for
production use. This applies to:
- TCPSend async operations
- Discovery packet sends
- Any other burst-prone operations

## Common Pitfalls (from MacTCP Programmer's Guide and MacTCP.h review)

1. **UPPs are REQUIRED for callbacks** - MacTCP.h states: "you must set up a NewRoutineDescriptor for every non-nil completion routine and/or notifyProc parameter. Otherwise, the 68K driver code will not correctly call your routine." Use `NewTCPNotifyUPP()`, `NewUDPNotifyUPP()`, `NewTCPIOCompletionUPP()` etc. Create once at init, dispose at shutdown.

2. **Command timeout minimum is 2 seconds** - Using timeout=1 is invalid; the actual behavior is undefined. Use 2 for polling scenarios (only call when ASR indicates data available).

3. **TCPSend blocks until acknowledged** - Can block 30+ seconds on lossy connections. Use async with completion routine for high-throughput scenarios.

4. **TCPClose can block indefinitely** - Must use async with timeout monitoring. Force TCPAbort after 10 seconds if no completion.

5. **Buffer sizing affects window size** - Small buffers = small TCP window = poor throughput. Use UDPMTU to calculate optimal: 4 × MTU + 1024.

6. **TCPRcvBfrReturn is MANDATORY** - Every successful TCPNoCopyRcv must be followed by TCPRcvBfrReturn or MacTCP buffers leak.

7. **UDPBfrReturn is MANDATORY** - Every successful UDPRead with nonzero data must call UDPBfrReturn. Per MacTCP Programmer's Guide: "call UDPBfrReturn" after processing received data.

8. **Completion routine must set userDataPtr** - The completion routine receives the parameter block; you need userDataPtr to find your state structure.

9. **64 stream limit is SYSTEM-WIDE** - Not per-app. Handle `insufficientResources` gracefully.

10. **ASR can be reentered** - Set flags atomically. Don't assume single entry.

11. **Don't close the .IPP driver** - It's shared by all apps. Only your streams should be released.

12. **Dispose UPPs only after streams are released** - Streams may still reference UPPs until TCPRelease/UDPRelease completes.

13. **UDP buffer minimum is 2048, not 4096** - Per MacTCP Programmer's Guide: "minimum allowed size is 2048 bytes, but it should be at least 2N + 256 bytes where N is the size in bytes of the largest UDP datagram you expect to receive." For discovery packets (~100 bytes), 1408 bytes (2×576+256) is sufficient and saves memory.

14. **TCP buffer minimum is 4096** - Per MacTCP Programmer's Guide: "The memory—a minimum of 4096 bytes." Character apps should use 8192, block apps 16384.

---

## Mac SE 4MB Specific Recommendations

For the primary test target (Mac SE with 4MB RAM, System 6.0.8, MacTCP 2.1):

### Memory Budget
| Component | Size | Notes |
|-----------|------|-------|
| System 6.0.8 | ~400-600KB | Varies with extensions |
| MacTCP internal | ~64KB | Allocated at driver load |
| App partition | ~2.5-3MB | What's left for PeerTalk |
| PeerTalk target | ~200KB | Context + streams + peers |

### Recommended Settings
```c
/* For Mac SE 4MB chat app */
#define PT_MAX_PEERS            4       /* Conservative: 4-6 peers max */
#define PT_TCP_RCV_BUF_SIZE     8192    /* 8KB per connection */
#define PT_UDP_RCV_BUF_SIZE     1408    /* Single discovery stream (2×576+256) */
#define PT_DISCOVERY_INTERVAL   (15*60) /* 15 seconds (reduce CPU/network) */
```

### Memory Footprint Estimate
- Context structure: ~2KB
- Per TCP stream: 8KB buffer + 256 bytes overhead = ~8.5KB
- Per peer: ~512 bytes
- UDP discovery: 1.4KB buffer + 128 bytes overhead = ~1.5KB
- **Total for 4 peers:** ~2KB + (4 × 8.5KB) + (4 × 0.5KB) + 1.5KB = ~39.5KB

This leaves plenty of room in a 2.5MB app partition.

---

## Emulator-Based Smoke Tests (Build Validation)

**Purpose:** Verify builds execute without crashes. Does NOT replace real hardware testing.

**Critical Limitation:** Emulators (Mini vMac, Basilisk II) are useful for build validation but **MUST** be followed by verification on REAL 68k hardware. From lines 3041-3050 of this plan:

> Emulators (Mini vMac, Basilisk II) are useful for build validation,
> but MUST verify on REAL 68k Mac hardware for final acceptance:
> - Memory manager behavior differs
> - Interrupt timing differs
> - MacTCP driver behavior differs

**Emulator Capabilities:**

| Emulator | System | MacTCP | Use Case | Limitations |
|----------|--------|--------|----------|-------------|
| **Mini vMac** | 6.0.8 | 2.1 | 68k emulation, basic functionality | No network by default, timing differs |
| **Basilisk II** | 7.5.5 | 2.1 | 68k emulation, better networking | Still not real hardware timing |

**What Emulators CAN Verify:**
- ✅ Application launches without crash
- ✅ PT_Log file created successfully
- ✅ UDP socket creation succeeds (UDPCreate returns noErr)
- ✅ TCP stream creation succeeds (TCPCreate returns noErr)
- ✅ Basic API calls don't crash immediately
- ✅ Memory allocation patterns work (no immediate memFullErr)

**What Emulators CANNOT Verify:**
- ❌ Discovery (network behavior differs from real hardware)
- ❌ Actual network I/O (unreliable in emulators)
- ❌ Timing-sensitive operations (emulator timing differs)
- ❌ ASR callback behavior (interrupt timing differs)
- ❌ Memory fragmentation under load (emulator heap differs)
- ❌ Real-world performance characteristics

**Emulator Smoke Test Workflow:**

```bash
# 1. Build for MacTCP
make mactcp

# 2. Transfer to Mini vMac shared folder
cp build/mactcp/PeerTalk.bin /path/to/minivmac/shared/

# 3. Launch in emulator and check for:
#    - No crash on startup
#    - PT_Log file appears in System Folder
#    - No error dialogs
#    - Application responds to user input
```

**Emulator Smoke Test Checklist:**
- [ ] Application launches (no bus error, address error, or System Error dialog)
- [ ] PT_Log file created in System Folder (logging works)
- [ ] About box displays correct version
- [ ] FreeMem shows reasonable values (no obvious memory leak)
- [ ] Quit works cleanly (no hang)
- [ ] No "Application unexpectedly quit" dialogs

**When to Use Emulators:**

1. **During development:** Quick iteration on build issues without hardware deploy cycle
2. **Pre-hardware validation:** Smoke test before deploying to SE/30 or Performa 6200
3. **Regression testing:** Quick check that changes didn't break basic functionality
4. **NOT for final acceptance:** Always verify on real hardware before marking session DONE

**Example Emulator Test Session:**

```
1. Build: make mactcp
2. Deploy to emulator: copy .bin to shared folder
3. Boot Mini vMac with System 6.0.8 disk
4. Launch PeerTalk application
5. Check Console for PT_Log output
6. Verify no crashes for 60 seconds
7. Quit application
8. If smoke test passes → Deploy to real SE/30 for ACTUAL testing
9. If smoke test fails → Fix build issues before hardware deploy
```

**Integration with Hardware Testing:**

Emulator smoke tests are a **pre-flight check**, not a replacement. The workflow is:

```
Code Change → Emulator Smoke Test → Real Hardware Test → DONE
                     ↓ (if fail)              ↓ (if fail)
                Fix Build Issue         Fix Hardware Issue
```

This two-stage approach saves time by catching build/crash issues early, before the overhead of hardware deployment and testing.

---

### Testing Checklist for Mac SE 4MB
- [ ] FreeMem > 500KB after init (healthy margin)
- [ ] MaxBlock > 100KB (can allocate new streams)
- [ ] 4 simultaneous peer connections work
- [ ] No -108 (memFullErr) during normal operation
- [ ] Discovery packets send/receive correctly
- [ ] 10+ connect/disconnect cycles without leak
- [ ] App remains responsive (no long blocking)

---

## Performa 6200 Specific Recommendations

For the secondary test target (Performa 6200, System 7.5.3, MacTCP 2.1):

### "Road Apple" Warning
The 5200/6200/6300 series are known as "Road Apple" Macs with quirks. Per VintageMacTCPIP_Reference.md: these models "may not run later OT versions." MacTCP should work but:

- Test extensively on this specific hardware
- Watch for unusual timing issues
- Verify network operations complete correctly

### Recommended Settings
```c
/* For Performa 6200 with 8MB+ RAM */
#define PT_MAX_PEERS            8       /* More memory available */
#define PT_TCP_RCV_BUF_SIZE     16384   /* 16KB for better throughput */
#define PT_UDP_RCV_BUF_SIZE     1408    /* Same as Mac SE (2×576+256) */
#define PT_DISCOVERY_INTERVAL   (10*60) /* Standard 10 seconds */
```

### Testing Checklist for Performa 6200
- [ ] MacTCP 2.1 installs and configures correctly
- [ ] PBOpen succeeds (driver loads properly)
- [ ] Network operations don't hang
- [ ] 8 simultaneous peer connections work
- [ ] Cross-platform with POSIX peer works
- [ ] Extended stress test (100+ operations)

---

## References

- MacTCP Programmer's Guide (1989/1993): Complete API reference, ASR restrictions, buffer sizing formulas
- MacTCP_programming.txt: Buffer formula "4N + 1024 where N is the physical MTU"
- VintageMacTCPIP_Reference.md: System version compatibility, Road Apple warnings
- Subtext tcp.c: Production MacTCP wrapper code
- Subtext uthread.c: Cooperative multitasking pattern
- Inside Macintosh Volume VI: Memory Manager, Table B-3 (interrupt-safe routines)
- csend MacTCP implementation: Performance tuning lessons

---

## Fact-Check Summary

This phase document was fact-checked against authoritative sources on 2026-01-24 and 2026-01-29:

| Claim | Source | Status |
|-------|--------|--------|
| 64 stream limit system-wide | MacTCP Programmer's Guide | ✅ Confirmed |
| Command timeout minimum 2 seconds | MacTCP Programmer's Guide | ✅ Confirmed |
| ASR cannot allocate/release memory | MacTCP Programmer's Guide | ✅ Confirmed |
| ASR can issue async MacTCP calls | MacTCP Programmer's Guide | ✅ Confirmed |
| Register preservation A0-A2, D0-D2 | MacTCP Programmer's Guide p.1510 | ✅ Confirmed |
| TCP buffer minimum 4096 | MacTCP Programmer's Guide | ✅ Confirmed |
| TCP buffer 8192 for character apps | MacTCP Programmer's Guide | ✅ Confirmed |
| TCP buffer 16KB for block apps | MacTCP Programmer's Guide | ✅ Confirmed |
| Buffer formula: 4×MTU + 1024 | MacTCP_programming.txt | ✅ Confirmed |
| UDP buffer minimum 2048 | MacTCP Programmer's Guide | ✅ Confirmed (was 4096, fixed) |
| UDP buffer formula: 2N + 256 | MacTCP Programmer's Guide | ✅ Added |
| UDPBfrReturn required | MacTCP Programmer's Guide | ✅ Confirmed |
| TCPRcvBfrReturn required | MacTCP Programmer's Guide | ✅ Confirmed |
| MacTCP internal buffers scale with RAM | MacTCP Programmer's Guide | ✅ Added note |
| UDP buffer size calculation | Math verification | ✅ Fixed (2×576+256=1408, not 2560) |
| TCP callback signature 5 params | MacTCP.h, Retro68 headers | ✅ Verified |
| UDP callback signature 4 params | MacTCP.h, Retro68 headers | ✅ Verified |
| GetMyIPAddr.h consolidated | MacTCP.h header comments | ✅ Confirmed (Developer Notes) |
| All TCP command codes (30-43) | MacTCP.h, Retro68 headers | ✅ Verified |
| All UDP command codes (20-29) | MacTCP.h, Retro68 headers | ✅ Verified |
| All event codes (TCP 1-6, UDP 1-2) | MacTCP.h, Retro68 headers | ✅ Verified |
| Termination reasons (2-8) | MacTCP.h, Retro68 headers | ✅ Verified |
| UPP creation macros required | MacTCP.h, Retro68 headers | ✅ Required for callbacks |
| TickCount() not in Table B-3 | Inside Macintosh Vol VI | ✅ Confirmed (plan correctly avoids) |
| BlockMove not in Table B-3 | Inside Macintosh Vol VI | ✅ Confirmed (plan uses pt_memcpy_isr) |
| 68k 2-byte alignment | CPU specifications | ✅ Hot struct fields reordered |
| 68030 256-byte data cache | CPU specifications | ✅ DOD optimizations documented |
| peer_idx saves 24 bytes for 8 streams | DOD analysis | ✅ Now mandatory (was optional) |
| TCPPassiveOpen one-shot | MacTCP Programmer's Guide | ✅ Confirmed |
| TCPAbort no delay needed | MacTCP Programmer's Guide p.3592 | ✅ Confirmed via CSEND-LESSONS |
| Async pool 16+ handles | CSEND-LESSONS A.1 | ✅ Verified |
| Phase 2 pt_peer_find_by_name() | PHASE-2-PROTOCOL.md line 1398 | ✅ Available (implemented in Phase 2) |
