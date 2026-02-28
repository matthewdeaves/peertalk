# Phase 8.0: Platform Consolidation Review

## Overview

Reviewed all four platform implementations side-by-side for common patterns
that could be extracted to shared code:

| Platform | Directory | Files | ~LOC |
|----------|-----------|-------|------|
| POSIX | src/posix/ | 2 | 2000 |
| MacTCP | src/mactcp/ | 10 | 8000 |
| Open Transport | src/opentransport/ | 11 | 9000 |
| AppleTalk | src/appletalk/ | 7 | 1600 |
| Core (shared) | src/core/ | 11 | 7000 |

## Patterns Reviewed

### 1. Logging Macros

**Finding:** Each platform file defines its own logging macros (e.g., AT_LOG_ERR,
ADSP_LOG_ERR, LISTEN_LOG_ERR). All follow the same pattern:

```c
#define XXX_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
```

AppleTalk has 7 files each defining 3-4 variants = ~21 macro definitions.

**Decision: No extraction.** The per-module prefixes (ADSP_LOG_, NBP_LOG_, etc.)
are intentional for grep-friendly log filtering. The boilerplate is minimal (3 lines
per macro) and each module's macros are self-contained. Extracting would either
lose the module prefix or add indirection without real benefit.

### 2. Connection State Tracking

**Finding:** Four different state enums exist:

- Core: `pt_peer_state` (DISCOVERED, CONNECTING, CONNECTED, DISCONNECTING)
- MacTCP: `pt_stream_state` (IDLE, LISTENING, CONNECTING, CONNECTED, CLOSING)
- OT: `pt_endpoint_state` (UNUSED, OPENING, UNBOUND, IDLE, OUTGOING, INCOMING, DATAXFER, CLOSING)
- AppleTalk: `pt_adsp_state` (UNUSED, INITIALIZING, IDLE, LISTENING, CONNECTING, CONNECTED, CLOSING)

**Decision: No extraction.** Platform-specific states map to different API
semantics. MacTCP streams, OT endpoints, and ADSP CCBs each have states
dictated by their respective APIs. The core peer state machine in `peer.c`
already provides the unified abstraction layer.

### 3. Error Code Mapping

**Finding:** No platforms implement error-to-PeerTalk conversion functions.
Platform-specific error codes are logged as diagnostics but the API uses
simple success/failure returns.

**Decision: No extraction needed.** This is intentional - platform errors are
diagnostic only. Error code reference docs already exist in `.claude/rules/`.

### 4. Poll Loop Structure

**Finding:** All four platforms follow the same high-level structure:

```
for each active connection:
    check connect completion
    check incoming data
    check send completion
    handle disconnect events
```

But event detection differs radically per platform (select vs ASR flags vs
notifier callbacks vs CCB state). Hot/cold separation in Mac platforms is
intentionally tuned for 68030 cache performance.

**Decision: No extraction.** The structural similarity is coincidental - each
implementation is optimized for its platform's event model. Sharing poll code
would hurt cache locality on 68k.

### 5. Byte Order Helpers

**Finding:** Already consolidated in `src/core/pt_compat.h`:

```c
#if defined(PT_PLATFORM_POSIX)
    #define pt_htons(x) htons(x)
    /* etc. */
#else
    /* Classic Mac: big-endian, no conversion needed */
    #define pt_htons(x) (x)
#endif
```

**Decision: Already done.** No changes needed.

### 6. Buffer Management

**Finding:** Four different allocation strategies:

- POSIX: Centralized buffer pool (`buffer_pool.c`)
- MacTCP: Per-stream RDS arrays + receive buffers via NewPtrClear
- OT: Per-endpoint TCall structures + receive buffers
- AppleTalk: Per-CCB send/recv queues (2048 each) + attention buffer (570)

Shared allocator exists in `pt_compat.c` (`pt_alloc`/`pt_free` routing to
NewPtr vs malloc).

**Decision: No extraction.** Each platform's buffer strategy is optimal for
its API requirements. The shared `pt_alloc`/`pt_free` abstraction is sufficient.

### 7. String/Name Handling

**Finding:** Core peer name functions consolidated in `peer.c` and `pt_compat.h`.
Platform-specific name handling (NBP entity parsing, Pascal string conversion)
is appropriately in platform directories.

**Decision: Already consolidated.** No changes needed.

## Summary

| Pattern | Shared? | Action |
|---------|---------|--------|
| Logging macros | Per-module, intentional | No change |
| State tracking | Per-platform, API-driven | No change |
| Error mapping | Not done, not needed | No change |
| Poll loops | Similar structure, different event models | No change |
| Byte order | Already in pt_compat.h | No change |
| Buffer mgmt | Already in pt_compat.c | No change |
| String/names | Already in peer.c/pt_compat.h | No change |

## Conclusion

The codebase is already well-consolidated at the architecture level. The core
layer (src/core/) provides the right abstractions, and platform-specific code
remains appropriately separated. The ~250 lines of logging macro boilerplate
across AppleTalk files is the largest duplication, but it's intentional for
grep-friendly module-level log filtering.

No `pt_common.h/c` files created - review confirms current factoring is sound.
