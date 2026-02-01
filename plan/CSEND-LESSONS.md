# CSend Code Review - Lessons Learned

> **Review Date:** 2026-01-24
> **Applies To:** PeerTalk implementation (Phases 2, 5, and 6)
> **Source:** `/home/matthew/csend` - working reference implementation

This document captures critical gotchas, patterns, and fixes discovered from reviewing the CSend codebase. These lessons inform PeerTalk implementation to avoid repeating past mistakes.

## Architecture Confirmation

PeerTalk uses the correct approach (same as CSend): **compile-time platform detection with separate binaries**. This is correct because:
- MacTCP and Open Transport cannot run simultaneously
- APIs are fundamentally different
- Separate builds optimize code size for memory-constrained Macs
- 68k (MacTCP) and PPC (Open Transport) require different compilers

---

## Part A: MacTCP Lessons

**Source Files:**
- `/home/matthew/csend/classic_mac_mactcp/mactcp_impl.c`
- `/home/matthew/csend/classic_mac_mactcp/tcp_state_handlers.c`

### A.1: Async Operation Pool Sizing

**Problem:** CSend initially used 8 async handles, causing `-108 (memFullErr)` under burst traffic.

**Root Cause:** Each TCP connection needs up to 3 concurrent handles (connect, send, close). With 4 connections, worst case = 12 handles + margin.

**Solution:**
```c
/* Size TCP async pool at 16 handles minimum */
/* Calculation: (max_connections × 3) + margin = (4 × 3) + 4 = 16 */
#define PT_MACTCP_ASYNC_POOL_SIZE 16
```

**PeerTalk Action:** In Session 5.2 (Stream Management), size TCP async pool at **16 handles minimum**. Add comment documenting the calculation.

---

### A.2: One Operation Per Stream

**~~Problem:~~ OBSOLETE:** This pattern is unnecessary.

**Fact-Check Result:** The state machine (IDLE→LISTENING→CONNECTED→CLOSING) already prevents concurrent operations on the same stream. Each state allows only one type of operation, and state transitions are sequential.

The original CSend issue was likely caused by parameter block reuse, not a MacTCP limitation. Since each `pt_tcp_stream` has a single `TCPiopb pb`, you can't issue two concurrent operations anyway.

**PeerTalk Action:** ~~Not needed~~ - rely on state machine for serialization. No explicit `operation_in_progress` flag required.

---

### A.3: TCPAbort Returns Immediately (No Delay Needed)

**Problem:** CSend had 100ms reset delay after TCPAbort, causing **92% message loss** during burst traffic.

**Discovery:** Per MacTCP Programmer's Guide lines 3592-3595: "TCPAbort returns the TCP stream to its initial state" - IMMEDIATELY.

**Solution:**
```c
/* WRONG - unnecessary delay */
TCPAbort(stream);
wait_ticks(6);  /* DON'T DO THIS */
TCPPassiveOpen(stream, ...);

/* CORRECT - no delay needed */
TCPAbort(stream);
TCPPassiveOpen(stream, ...);  /* Stream ready immediately */
```

**PeerTalk Action:** In Session 5.4 (Connection Accept), NO reset delays after TCPAbort. Stream is ready for new TCPPassiveOpen immediately.

---

### A.4: Restart Listen BEFORE Processing Data

**Problem:** Processing data takes 50-200ms. Incoming connections during processing are refused.

**Solution:** Restart listen immediately, then process:
```c
/* WRONG - 50-200ms listen gap */
read_data(connection);
process_data();  /* Takes time! */
restart_listen();

/* CORRECT - <5ms listen gap */
restart_listen();  /* FIRST: Re-arm listener */
read_data(connection);
process_data();  /* RDS buffers still valid until TCPBfrReturn */
```

**Note:** RDS buffers remain valid until `TCPBfrReturn()` is called, so you can safely restart listen before processing the received data.

**PeerTalk Action:** In Session 5.4, after accepting connection:
1. FIRST: Restart listen operation
2. THEN: Process received message data

---

### A.5: ASR Callback Context

**~~Problem:~~ OBSOLETE:** The original claim that "MacTCP ASR only receives StreamPtr" is incorrect.

**Fact-Check Result (MacTCP.h):** The TCP ASR signature is:
```c
typedef CALLBACK_API( void , TCPNotifyProcPtr )(
    StreamPtr       tcpStream,
    unsigned short  eventCode,
    Ptr             userDataPtr,    /* <-- Application context IS provided! */
    unsigned short  terminReason,
    ICMPReport     *icmpMsg
);
```

The `userDataPtr` parameter is your application context, passed to TCPCreate. No dispatch table needed.

**Correct Solution:**
```c
/* Pass your stream wrapper as userDataPtr in TCPCreate */
create_pb.csParam.create.userDataPtr = (Ptr)tcp_stream;

/* In ASR, cast userDataPtr to recover context */
static pascal void tcp_asr(StreamPtr stream, unsigned short event,
                          Ptr userData, unsigned short terminReason,
                          ICMPReport *icmpMsg) {
    pt_tcp_stream *tcp = (pt_tcp_stream *)userData;
    /* Now you have your context directly - no table lookup needed */
}
```

**PeerTalk Action:** Use `userDataPtr` for context. No dispatch table required.

---

### A.6: Async Close for Throughput

**Problem:** Synchronous TCPClose blocks pool entry for up to 30 seconds.

**Solution:**
```c
/* WRONG - blocks for up to 30 seconds */
TCPClose(stream, &pb, false);  /* Synchronous */
/* Pool entry unavailable during this time */

/* CORRECT - returns immediately */
pb.csParam.close.ulpTimeoutValue = 3;  /* 3 seconds (LAN appropriate) */
pb.csParam.close.ulpTimeoutAction = 0; /* Abort on timeout */
TCPClose(stream, &pb, true);  /* Async */
/* Poll for completion in event loop */
```

**PeerTalk Action:** In Session 5.5 (Send/Receive), use async TCPClose with 3 second timeout. Expected improvement: 50-70% throughput under load.

---

### A.7: No Cancel API Exists

**Constraint:** MacTCP has no way to cancel async operations.

**Solution:**
```c
/* When abandoning an operation: */
stream->abandoned = true;
/* Let it complete in background */
/* Accept potential leak as safer than crash */

/* In completion handler: */
if (stream->abandoned) {
    /* Just mark as free, don't process result */
    stream->state = PT_STREAM_UNUSED;
    return;
}
```

**PeerTalk Action:** Document this limitation in `src/mactcp/mactcp_driver.c`. Mark abandoned operations as "free" and let complete in background.

---

### A.8: WDS/RDS Buffer Management

**WDS (Write Data Structure):** For sending data:
```c
/* 2-entry with NULL sentinel */
wdsEntry wds[3];
wds[0].length = data_len;
wds[0].ptr = data;
wds[1].length = 0;  /* SENTINEL */
wds[1].ptr = NULL;
```

**RDS (Read Data Structure):** For receiving data:
```c
rdsEntry rds[6];
err = TCPNoCopyRcv(stream, rds, 6, false, &pb);

/* Process data... */

/* CRITICAL: MUST return buffer to MacTCP */
err = TCPBfrReturn(stream, rds[0].ptr, &pb);
if (err != noErr) {
    /* Log error - this is a resource leak */
}
```

**PeerTalk Action:** WDS needs 2-entry with sentinel. RDS **MUST** call `TCPBfrReturn()` after processing.

---

## Part B: Open Transport Lessons

**Source Files:**
- `/home/matthew/csend/classic_mac_ot/opentransport_impl.c`
- `/home/matthew/csend/classic_mac_ot/main.c`

### B.1: UDP T_UDERR Must Be Cleared

**Problem:** If `T_UDERR` fires and you don't call `OTRcvUDErr()`, endpoint hangs permanently.

**Solution:**
```c
case T_UDERR:
    /* MUST call even if ignoring the result */
    {
        TUDErr udErr = {0};
        OTRcvUDErr(ep->ref, &udErr);
    }
    /* Per OT docs: "Failing to do this leaves the endpoint in a state
     * where it cannot do other sends." */
    break;
```

**PeerTalk Action:** In Session 6.2 (UDP Endpoint), add T_UDERR clearing in notifier.

---

### B.2: Endpoint State for Accept

**Fact Check (NetworkingOpenTransport.txt line 6940):** Per the state transition table, `OTAccept()` destination endpoint can be in **either** `T_IDLE` or `T_UNBND` state. CSend's claim that it "must be in T_UNBND" was incorrect.

**Best Practice (NetworkingOpenTransport.txt line 9560):** "Preallocate a pool of open, unbound endpoints into an endpoint cache. When a connection is requested (you receive a T_LISTEN event), you can dequeue an endpoint from this cache and pass it to the function OTAccept."

**Solution:**
```c
/* Best practice: Pool of unbound endpoints ready for OTAccept */
/* Endpoints can be in T_IDLE (bound) or T_UNBND (unbound) */

/* After disconnect, before reuse for a new accept: */
/* Option 1: If bound (T_IDLE), can use directly */
/* Option 2: Unbind to return to pool */
OSStatus err = OTUnbind(ep->ref);
if (err != noErr) {
    /* For connectionless endpoints, kOTLookErr may mean data arrived */
    /* Read data and retry, or close and recreate endpoint */
    OTCloseProvider(ep->ref);
    ep->ref = NULL;
    ep->state = PT_EP_UNUSED;
}
```

**PeerTalk Action:** In Session 6.5 (TCP Server), use endpoint pool pattern. Both T_IDLE and T_UNBND states are valid for `OTAccept()` destination.

---

### B.3: Receive Data BEFORE Checking Disconnect

**Problem:** When sender uses `OTSndOrderlyDisconnect()` after sending data, checking disconnect first loses buffered data.

**Solution:**
```c
/* CORRECT order: data first, then disconnect */
while (1) {
    result = OTRcv(ep->ref, buf, size, &flags);
    if (result > 0) {
        process_data(buf, result);
    } else if (result == kOTNoDataErr || result == kOTLookErr) {
        break;  /* No more data */
    } else {
        break;  /* Error */
    }
}

/* NOW check for disconnect events */
if (ep->flags.orderly_disconnect || ep->flags.disconnect) {
    handle_disconnect(ep);
}
```

**PeerTalk Action:** In Session 6.6 (Integration), update poll loop to always receive data before checking disconnect.

---

### B.4: Read Loop Exit Conditions

**Problem:** Loop must check BOTH `kOTNoDataErr` AND `kOTLookErr`.

**Solution:**
```c
/* Loop until no data OR look error */
do {
    result = OTRcv(ep->ref, buf, size, &flags);
    if (result > 0) {
        process_data(buf, result);
    }
} while (result != kOTNoDataErr && result != kOTLookErr);

/* If kOTLookErr, call OTLook() to determine what happened */
if (result == kOTLookErr) {
    OTEventCode event = OTLook(ep->ref);
    /* Handle event... */
}
```

**PeerTalk Action:** In `pt_ot_tcp_recv()`, loop until both conditions are checked.

---

### B.5: Clear T_GODATA After Timeout

**Problem:** If flow control timeout occurs and T_GODATA is pending, endpoint can't receive future T_DATA.

**Solution:**
```c
/* After send timeout: */
if (OTLook(ep->ref) == T_GODATA) {
    /* Event cleared by the OTLook call itself */
    /* Now endpoint can receive T_DATA again */
}
```

**PeerTalk Action:** In Session 6.4 (TCP Connect), after timeout call `OTLook()` to clear pending events.

---

### B.6: Use Orderly Disconnect for Clean Sends

**Problem:** `OTSndDisconnect()` (abortive) may discard buffered data.

**Solution:**
```c
/* WRONG - may discard buffered data */
OTSndDisconnect(ep->ref, NULL);

/* CORRECT - sends all buffered data first */
OTSndOrderlyDisconnect(ep->ref);
/* Per OT docs: "An orderly disconnect allows an endpoint to send all
 * data remaining in its send buffer before it breaks a connection." */
```

**PeerTalk Action:** In Session 6.3 (TCP Endpoint), use `OTSndOrderlyDisconnect()` after sending data.

---

## Part C: Cross-Platform Protocol Lessons

**Source Files:**
- `/home/matthew/csend/shared/protocol.c`
- `/home/matthew/csend/shared/peer.c`
- `/home/matthew/csend/shared/time_utils.c`

### C.1: Magic Number Byte Ordering

**Problem:** Magic numbers must be handled consistently for cross-platform compatibility.

**Two Valid Approaches:**

```c
/* Approach A: Write bytes individually (PREFERRED - inherently portable) */
buf[0] = 'P';
buf[1] = 'T';
buf[2] = 'L';
buf[3] = 'K';
/* No htonl needed - byte order is explicit */

/* Approach B: Write as 32-bit integer (requires byte swapping) */
uint32_t magic_net = htonl(PT_MSG_MAGIC);
memcpy(buf, &magic_net, 4);
```

**Decoding must match encoding:**
```c
/* If encoded byte-by-byte, decode byte-by-byte: */
if (buf[0] != 'P' || buf[1] != 'T' || buf[2] != 'L' || buf[3] != 'K')
    return PT_ERR_MAGIC;

/* If encoded with htonl, decode with ntohl: */
uint32_t magic_net;
memcpy(&magic_net, buf, 4);
if (ntohl(magic_net) != PT_MSG_MAGIC)
    return PT_ERR_MAGIC;
```

**PeerTalk Action:** Phase 2 uses Approach A (byte-by-byte) which is inherently portable. Use `htonl()`/`ntohl()` for other multi-byte values (ports, lengths, addresses).

---

### C.2: TickCount Wraparound

**Problem:** `TickCount()` is 32-bit and wraps after ~828 days. Does simple subtraction handle wraparound?

**Solution:** Yes - unsigned subtraction handles wraparound naturally due to two's complement arithmetic. No special handling needed.

```c
/* Simple subtraction works correctly even at wraparound */
unsigned long elapsed = current - last_seen;

/* Example: current=5, last_seen=0xFFFFFFF0
 * 5 - 0xFFFFFFF0 = 5 + (~0xFFFFFFF0 + 1) = 5 + 0x10 = 21 (correct!)
 */
```

**Why this works:** When `current < last_seen` due to wraparound, the unsigned subtraction underflows and wraps around to give the correct elapsed time. This is guaranteed behavior for unsigned integers in C.

**PeerTalk Action:** Use simple unsigned subtraction for elapsed time. No special wraparound function needed.

---

### C.3: Mac Epoch vs Unix Epoch

**Problem:** Classic Mac `time()` returns seconds since 1904-01-01, not 1970-01-01.

**Solution:**
```c
#define PT_MAC_EPOCH_OFFSET 2082844800UL  /* Seconds between 1904 and 1970 */

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
/* Convert Mac time to Unix time for protocol */
time_t unix_time = time(NULL) - PT_MAC_EPOCH_OFFSET;
#else
time_t unix_time = time(NULL);
#endif
```

**PeerTalk Action:** In `src/core/pt_compat.h`, define epoch offset and use it for time-based protocol fields.

---

### C.4: TickCount vs time() Units

**Problem:** `TickCount()` returns 60ths of a second, not seconds.

**Solution:**
```c
/* Platform-specific timeout conversion */
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
#define PT_TICKS_PER_SECOND 60
unsigned long timeout_ticks = timeout_seconds * PT_TICKS_PER_SECOND;
#else
#define PT_TICKS_PER_SECOND 1000  /* POSIX uses milliseconds */
unsigned long timeout_ms = timeout_seconds * PT_TICKS_PER_SECOND;
#endif
```

**PeerTalk Action:** Define `PT_TICKS_PER_SECOND` per platform in compatibility layer.

---

### C.5: No vsnprintf on Classic Mac

**Problem:** Classic Mac toolbox has `sprintf()` but not `vsnprintf()`.

**Solution:**
```c
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
/* Pre-size buffer to avoid overflow! */
#define PT_LOG_BUF_SIZE 256
char log_buf[PT_LOG_BUF_SIZE];
vsprintf(log_buf, format, args);  /* No bounds checking */
#else
vsnprintf(buffer, size, format, args);
#endif
```

**Warning:** This is unsafe. Keep format strings simple and ensure buffer is adequately sized.

**PeerTalk Action:** In `src/core/pt_log.c`, use `vsprintf()` on Mac with pre-sized buffer.

---

### C.6: Safe String Handling

**Problem:** Standard `strncpy()` may not null-terminate if source is too long.

**Solution:**
```c
void pt_safe_strncpy(char *dest, const char *src, size_t size) {
    if (!dest || size == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';  /* ALWAYS null-terminate */
}
```

**PeerTalk Action:** Add `pt_safe_strncpy()` to `src/core/pt_compat.c`. Use it everywhere instead of raw `strncpy()`.

---

### C.7: Single-Threaded Mutex No-Ops

Classic Mac is single-threaded cooperative, so mutexes are no-ops:
```c
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
#define pt_mutex_init(m)    ((void)0)
#define pt_mutex_lock(m)    ((void)0)
#define pt_mutex_unlock(m)  ((void)0)
#define pt_mutex_destroy(m) ((void)0)
#else
/* POSIX: use pthread_mutex_* */
#endif
```

**PeerTalk Action:** Already defined in `src/core/pt_compat.h` per Phase 1.2.

---

### C.8: Message Parsing Graceful Degradation

Handle malformed packets without crashing:
```c
/* Graceful degradation patterns: */
if (content_field_missing) {
    msg->content[0] = '\0';  /* Treat as empty, not error */
}

if (sender_ip_missing) {
    strcpy(msg->sender_ip, "unknown");
}

if (message_id_invalid) {
    msg->id = 0;  /* Use default */
}

if (buffer_too_small) {
    /* Truncate and log warning, don't fail */
    PT_LOG_WARN(ctx, PT_LOG_CAT_PROTOCOL, "Message truncated");
    memcpy(buf, data, buf_size - 1);
    buf[buf_size - 1] = '\0';
}

if (bad_magic_number) {
    /* Clear rejection with specific error */
    return PT_ERR_MAGIC;
}
```

**PeerTalk Action:** Implement defensive parsing throughout protocol layer.

---

## Verification Requirements

### MacTCP Testing (Real 68k Mac)

Before marking any MacTCP session complete:
- [ ] Async pool sized at 16+ handles (A.1)
- [x] ~~Operation-in-progress tracking~~ (A.2 - not needed, state machine handles it)
- [ ] No reset delays after TCPAbort (A.3)
- [ ] Listen restarts before data processing (A.4)
- [x] ~~ASR dispatch table~~ (A.5 - use userDataPtr instead)
- [ ] Async close used for pool connections (A.6)
- [ ] RDS buffers always returned via TCPBfrReturn (A.8)
- [ ] No -108 errors under burst traffic
- [ ] Listen restarts in <5ms
- [ ] 50+ operations without memory leak (MaxBlock check)

### Open Transport Testing (Real PPC Mac)

Before marking any OT session complete:
- [ ] T_UDERR cleared with OTRcvUDErr
- [ ] Endpoint unbind before reuse (or recreate on failure)
- [ ] Data received before disconnect check
- [ ] Read loop checks both kOTNoDataErr and kOTLookErr
- [ ] T_GODATA cleared after timeout
- [ ] Orderly disconnect used after sends
- [ ] UDP sends work after T_UDERR
- [ ] Endpoint reuse works after disconnect
- [ ] No data loss on orderly disconnect
- [ ] 50+ operations without memory leak (MaxBlock check)

### Cross-Platform Testing

- [ ] POSIX discovers Mac peers and vice versa
- [ ] Messages transmit correctly in both directions
- [ ] Protocol packets valid (verify with Wireshark)
- [ ] Magic numbers use network byte order
- [ ] Timestamps use Unix epoch in protocol

---

## Quick Reference: Files to Review

Before starting each phase, review these CSend files:

| Phase | Review These Files |
|-------|-------------------|
| Phase 2 (Protocol) | `shared/protocol.c`, `shared/peer.c`, `shared/time_utils.c` |
| Phase 5 (MacTCP) | `classic_mac_mactcp/mactcp_impl.c`, `classic_mac_mactcp/tcp_state_handlers.c` |
| Phase 6 (Open Transport) | `classic_mac_ot/opentransport_impl.c`, `classic_mac_ot/main.c` |
