# PHASE 8: Example Chat Application

## Plan Review Fixes Applied (2026-01-29)

This plan was reviewed by the `/review` skill and the following fixes were applied:

### Critical Fixes (2026-01-29 Review #2)

1. **API field corrections** - Fixed all `peer->name` accesses to use `PeerTalk_GetPeerName(ctx, peer->name_idx)`. Fixed all `peer->transport` accesses to use `peer->transports_available` per Phase 1 field names.

2. **O(n) peer lookup replaced with PeerTalk_GetPeerByID()** - Example callbacks now use the required O(1) lookup API instead of linear scans. The plan already required this API but example code didn't use it.

3. **PeerTalk_PeerInfoEx removed** - Redundant with Phase 1's `PeerTalk_PeerInfo` which already includes `transports_available` and `transport_connected` fields.

4. **queue_pressure range fixed** - Corrected to "0-100" (was incorrectly "0-1000") to match Phase 1 API.

5. **68030 cache claim rephrased** - Changed misleading "fits in 256-byte cache" to accurate "smaller struct = less memory bandwidth" since 68030 has instruction cache, not data cache.

6. **Dependency statement expanded** - Added Phases 0-3 to dependency list (PT_Log, foundation APIs, protocol, queues).

7. **ISR-safety warning added** - Clarified that application callbacks (from Poll) are safe for logging, but SDK internal callbacks (ASR/notifiers) are NOT.

8. **Logging documentation expanded** - Added all PeerTalk log categories, platform differences, callback safety, auto-flush patterns.

### Critical Fixes (2026-01-29 Review #1)

9. **PeerTalk_PeerInfo struct reconciled with Phase 1** - Now uses Phase 1's 20-byte struct with `name_idx` for cold storage. With 20-byte struct, iterating 12 peers fetches 240 bytes - efficient for 68k memory bandwidth.

10. **Session 8.3 explicitly marked BLOCKED** - Multi-transport gateway chat cannot proceed until Phase 6.8-6.10 API contract is defined AND Phase 7 completes. Added explicit BLOCKED status and dependency list.

11. **System 6 compatibility claim corrected** - Fixed incorrect claim that "H* variants require System 7+". Per Inside Macintosh Volume IV p.8968-8970, H* variants work with 64K ROM (System 6.0.8+).

12. **Phase 1 API dependencies added** - Marked `PeerTalk_GetPeerByID()`, `PeerTalk_GetPeersVersion()`, and `PeerTalk_GetPeerName()` as REQUIRED Phase 1 deliverables (not optional enhancements).

13. **Example code updated for name_idx pattern** - All `peer->name` accesses must use `PeerTalk_GetPeerName(ctx, peer->name_idx)` per Phase 1's DOD design.

### Logging Fixes (2026-01-28 Review)

14. **PT_Log integration added to all examples** - Chat apps now demonstrate proper logging with:
    - Log context creation/destruction
    - File logging (`:chat_mac.log` for Mac path)
    - Auto-flush for crash-safe debugging
    - Appropriate log levels (INFO for operations, DEBUG for packets, ERR for failures)

15. **Callbacks updated to use PT_Log** - Replaced raw `printf()` statements in callbacks with `PT_INFO/PT_DEBUG/PT_WARN` macros for unified diagnostics.

### New Content (2026-01-28 Review)

16. **Memory budget guidelines added** - Documented RAM constraints for 4MB/8MB/16MB Macs with appropriate peer and message limits.

17. **`examples/minimal_example.c` task added** - 50-100 line example showing simplest possible PeerTalk usage pattern, ISR-safe on all platforms.

18. **Cross-platform test protocol documented** - Task 8.4.6 creates `notes/TESTING.md` with step-by-step manual testing procedure.

19. **SDK enhancement APIs documented** - Added `PeerTalk_GetPeersVersion()`, `PeerTalk_GetPeerByID()`, and `PeerTalk_GetPeerName()` as REQUIRED Phase 1 deliverables for O(1) peer lookup and change detection.

### DOD Improvements (2026-01-28 Review)

20. **chat_message struct improved** - Changed `int is_local` to `uint8_t is_local` to save 3-7 bytes per message. Added note about using sender_id instead of sender name in production.

21. **Additional anti-patterns documented** - Added memmove performance warning (~15-30ms on 68030 for 29KB), refresh_peer_list() every frame warning, and version-based change detection pattern.

### API Clarifications (2026-01-28 Review)

22. **API naming table added** - Documented `FrontWindow()` vs `GetFrontWindow()`, `MacShowWindow()` vs `ShowWindow()`, and `MaxBlock()` vs `FreeMem()` differences.

### Documentation Clarifications (2026-01-29 Review #1)

23. **OTAllocMem clarification** - Per NetworkingOpenTransport.txt p.9143-9148, OTAllocMem CAN be called from notifiers but MUST handle NULL returns due to memory pool exhaustion at deferred task time.

24. **Benchmark logging separation documented** - Session 8.5 uses separate TSV-format logging for machine-parseable output; this is intentional, not an oversight.

---

> **Status:** OPEN
> **Depends on:** Phases 0-7 (Phase 0: PT_Log API; Phase 1: Foundation APIs including GetPeerByID/GetPeersVersion/GetPeerName; Phases 2-3: Protocol/Queues; Phases 4-7: All networking)
> **Session 8.3 Status:** ⛔ **BLOCKED** - Requires Phase 6.8-6.10 API contract AND Phase 7 completion
> **Produces:** Functional cross-platform chat application demonstrating the PeerTalk SDK
> **Risk Level:** LOW
> **Estimated Sessions:** 5
>
> **Build Order for Session 8.3:**
> ```
> Phase 2 → Phase 4 → Phase 5 → Phase 6.1-6.6 → Phase 7 → Phase 6.8-6.10 → Session 8.3
> ```
> Sessions 8.1-8.2 can proceed after their respective single-transport phases complete.
>
> **Multi-Transport API Contract (Must be defined in Phase 6.8):**
> ```c
> /* Required APIs for Session 8.3 - must be specified in Phase 6.8 */
> typedef enum {
>     PT_TRANSPORT_TCP   = 0x0001,
>     PT_TRANSPORT_UDP   = 0x0002,
>     PT_TRANSPORT_ADSP  = 0x0004,
>     PT_TRANSPORT_NBP   = 0x0008,
> } PT_TransportMask;
>
> uint16_t PeerTalk_GetAvailableTransports(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id);
> PeerTalk_Error PeerTalk_ConnectVia(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id, uint16_t transport);
> PeerTalk_Error PeerTalk_SetGatewayMode(PeerTalk_Context *ctx, Boolean enabled);
> PeerTalk_Error PeerTalk_GetGatewayStats(PeerTalk_Context *ctx, PeerTalk_GatewayStats *stats_out);
> ```

## Overview

Phase 8 creates a functional chat application that demonstrates the PeerTalk SDK. This application serves multiple purposes:

1. **Dogfooding**: Validates that the PeerTalk API is ergonomic and complete
2. **Reference Implementation**: Shows developers how to properly use the SDK
3. **Cross-Platform Testing**: Provides a tool for testing POSIX ↔ Classic Mac communication
4. **Multi-Transport Gateway**: Demonstrates bridging TCP/IP and AppleTalk networks
5. **Documentation by Example**: Real, working code is the best documentation
6. **PT_Log Showcase**: Gold-standard example of production logging with PT_Log

### PT_Log Integration

The example chat app demonstrates PT_Log best practices that application developers should follow:

```c
/* App-specific log categories */
#define LOG_UI      PT_LOG_CAT_APP1   /* UI events (window, input) */
#define LOG_CHAT    PT_LOG_CAT_APP2   /* Chat messages */
#define LOG_NET     PT_LOG_CAT_NETWORK /* Network (shared with PeerTalk) */

/* Create unified log for app + PeerTalk */
PT_Log *log = PT_LogCreate();
PT_LogSetFile(log, "chat.log");
PT_LogSetLevel(log, PT_LOG_INFO);  /* Production: INFO. Debug: DEBUG */
PT_LogSetOutput(log, PT_LOG_OUT_FILE | PT_LOG_OUT_CALLBACK);

/* Callback displays errors in status bar */
PT_LogSetCallback(log, chat_status_handler, &chat_ui);

/* Share log with PeerTalk */
PeerTalk_Config config = {0};
config.log = log;
ctx = PeerTalk_Init(&config);

/* App logging throughout */
PT_INFO(log, LOG_UI, "Chat started");
PT_INFO(log, LOG_CHAT, "<%s> %s", sender, message);
PT_DEBUG(log, LOG_NET, "Packet: %d bytes to %s", len, peer_name);
PT_ERR(log, LOG_NET, "Connection failed: %s", error_msg);
```

**Key patterns demonstrated:**
- Single shared log for app + SDK (unified diagnostics)
- App-defined categories for filtering
- Callback for real-time UI feedback on errors
- Appropriate level usage (INFO for operations, DEBUG for packets, ERR for failures)
- Production-safe logging (no PT_LOG_STRIP, runtime level control)

The chat application uses a **text-based interface** for simplicity and maximum compatibility:
- **POSIX**: ncurses terminal UI
- **Classic Mac**: Console-style UI using standard I/O with event polling

## Multi-Transport Architecture

A key SDK enhancement demonstrated in this phase is **multi-transport support** on Open Transport systems. This allows a single Mac to bridge between TCP/IP and AppleTalk networks.

### Why Multi-Transport Matters

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         LOCAL AREA NETWORK                                   │
│                                                                             │
│   ┌────────────────┐        ┌────────────────┐        ┌────────────────┐   │
│   │  Linux PC      │        │  Power Mac 7500 │        │  Mac SE        │   │
│   │  (POSIX)       │        │  (OT Gateway)   │        │  (AppleTalk)   │   │
│   │                │        │                 │        │                │   │
│   │  TCP/IP only   │◄──────►│  TCP/IP + ADSP  │◄──────►│  ADSP only     │   │
│   │                │  TCP   │                 │ ADSP   │                │   │
│   └────────────────┘        └────────────────┘        └────────────────┘   │
│                                    ▲                                        │
│                                    │ ADSP                                   │
│                                    ▼                                        │
│                             ┌────────────────┐                              │
│                             │  Mac IIci      │                              │
│                             │  (MacTCP+AT)   │                              │
│                             │                │                              │
│                             │  TCP/IP + ADSP │                              │
│                             └────────────────┘                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Key Insight from Research:**

Open Transport provides **both TCP/IP AND AppleTalk** through the same unified API:

| Protocol | OT Configuration String | Address Type |
|----------|------------------------|--------------|
| TCP | `"tcp"` | `InetAddress` |
| UDP | `"udp"` | `InetAddress` |
| ADSP | `"adsp"` | `DDPAddress` or `NBPAddress` |
| DDP | `"ddp"` | `DDPAddress` |

This means a **single application** on an OT-capable Mac can:
1. Open TCP endpoints for POSIX communication
2. Open ADSP endpoints for AppleTalk communication
3. Bridge messages between the two networks
4. Discover peers on BOTH UDP broadcast AND NBP

### Network Topology Scenarios

| Scenario | POSIX | MacTCP Mac | OT Mac (Gateway) | AppleTalk-Only Mac |
|----------|-------|------------|------------------|-------------------|
| A: TCP/IP Only | ✓ talks to all TCP peers | ✓ talks to all TCP peers | ✓ TCP mode only | ✗ cannot participate |
| B: AppleTalk Only | ✗ cannot participate | ✓ if AT driver loaded | ✓ ADSP mode only | ✓ talks to all AT peers |
| C: **Bridged** | ✓ via gateway | ✓ directly or via gateway | ✓ **bridges both** | ✓ via gateway |

**Scenario C** is the most interesting: the OT Mac acts as a **message gateway**, allowing POSIX peers to chat with AppleTalk-only Macs.

### SDK Enhancement: Multi-Transport Context

> **IMPORTANT: Proposed Enhancement**
>
> The multi-transport flags (`PT_TRANSPORT_TCP`, `PT_TRANSPORT_ADSP`, etc.) and
> gateway functionality described below are **proposed SDK enhancements** that
> will be implemented in **Phase 6.7-6.10** (which requires Phase 7 completion).
>
> These features are NOT part of the Phase 4-7 single-transport baseline.
> Session 8.3 depends on these enhancements being complete.

The SDK can be enhanced to support simultaneous transports:

```c
/* Extended configuration for multi-transport */
typedef struct {
    /* Standard config */
    const char *local_name;
    uint16_t max_peers;

    /* Transport selection (bitfield) */
    uint32_t transports;  /* PT_TRANSPORT_TCP | PT_TRANSPORT_ADSP */

    /* TCP/IP settings */
    uint16_t tcp_port;
    uint16_t udp_port;

    /* AppleTalk settings */
    const char *nbp_type;     /* Default: "PeerTalk" */
    const char *nbp_zone;     /* Default: "*" (local zone) */

    /* Gateway mode */
    Boolean enable_gateway;   /* Relay messages between transports */
} PeerTalk_Config;

/* Transport flags */
#define PT_TRANSPORT_TCP   0x0001  /* TCP/IP via MacTCP or OT */
#define PT_TRANSPORT_UDP   0x0002  /* UDP discovery and messaging */
#define PT_TRANSPORT_ADSP  0x0004  /* AppleTalk ADSP connections */
#define PT_TRANSPORT_NBP   0x0008  /* AppleTalk NBP discovery */

/* Peer info - matches Phase 1's PeerTalk_PeerInfo definition
 *
 * MEMORY BANDWIDTH OPTIMIZATION: Fields ordered for efficient iteration on 68k.
 * Hot data (checked during iteration) is first; name is stored separately
 * via name_idx to keep the struct compact.
 *
 * With 20-byte struct, iterating 12 peers fetches only 240 bytes of memory.
 * On 68k memory bus (2-10 MB/s), this is 24-120 microseconds - much faster
 * than a 50-byte struct which would cost 600 bytes (60-300 microseconds).
 * Note: 68030 has instruction cache (256 bytes), NOT a data cache.
 *
 * NOTE: This struct MUST match Phase 1's PeerTalk_PeerInfo definition exactly.
 * Multi-transport fields are already included per Phase 1 specification.
 */
typedef struct {
    /*--- HOT DATA (first 16 bytes) - checked during peer iteration ---*/
    PeerTalk_PeerID id;               /* 2 bytes: Peer identifier */
    uint8_t flags;                    /* 1 byte: Connection flags (connected, etc.) */
    uint8_t name_idx;                 /* 1 byte: Index into cold name storage */
    uint32_t address;                 /* 4 bytes: IP address (network byte order) */
    uint16_t port;                    /* 2 bytes: TCP port */
    uint16_t latency_ms;              /* 2 bytes: Last measured RTT */
    uint16_t queue_pressure;          /* 2 bytes: Send queue fill level (0-100) */
    uint16_t transports_available;    /* 2 bytes: Which transport(s) this peer supports */
    uint16_t transport_connected;     /* 2 bytes: Which transport we're connected via */
} PeerTalk_PeerInfo;                  /* 20 bytes total - per Phase 1 definition */

/* Cold storage for peer names - accessed only when displaying */
/* PeerTalk_GetPeerName(ctx, peer->name_idx) returns the name string */
```

## Goals

1. Create a working chat application on all platforms
2. Demonstrate proper PeerTalk SDK usage patterns
3. Enable cross-platform communication testing
4. Showcase peer discovery, connection, and messaging
5. **NEW:** Demonstrate multi-transport gateway bridging TCP/IP ↔ AppleTalk

### SDK Enhancement: Peer Resolution in Callbacks

**Problem:** The current callback signature passes `PeerTalk_PeerID`, requiring applications to perform O(n) lookup to find peer info:

```c
/* Current - requires O(n) lookup per message */
static void on_message_received(PeerTalk_Context *ctx,
                                 PeerTalk_PeerID from_peer,  /* Just an ID */
                                 const void *data,
                                 uint16_t length,
                                 void *user_data) {
    /* ANTI-PATTERN: O(n) lookup per message - costs ~3-5ms on 68k for 16 peers! */
    const PeerTalk_PeerInfo *peer = PeerTalk_GetPeerByID(ctx, from_peer);  /* USE THIS! */
    const char *sender = peer ? PeerTalk_GetPeerName(ctx, peer->name_idx) : "Unknown";
}
```

**Solution:** SDK should resolve peer before invoking callback:

```c
/* RECOMMENDED: Pass resolved peer info directly */
static void on_message_received(PeerTalk_Context *ctx,
                                 const PeerTalk_PeerInfo *from_peer,  /* Already resolved */
                                 const void *data,
                                 uint16_t length,
                                 void *user_data) {
    /* O(1) - peer already resolved by SDK, use name accessor */
    const char *sender = PeerTalk_GetPeerName(ctx, from_peer->name_idx);
}

/* ALTERNATIVE: Add O(1) lookup function to SDK */
const PeerTalk_PeerInfo *PeerTalk_GetPeerByID(PeerTalk_Context *ctx,
                                               PeerTalk_PeerID id);
```

**REQUIRED Phase 1 Deliverables (BLOCKING Sessions 8.1-8.2):**

> **DEPENDENCY NOTE:** These APIs MUST be implemented in Phase 1 before Sessions 8.1-8.2 can proceed.
> The O(n) lookup pattern shown in example code is unacceptable on 68k hardware - costs ~3-5ms per lookup.

1. **O(1) Peer Lookup:** `PeerTalk_GetPeerByID(ctx, id)` - Returns `const PeerTalk_PeerInfo*` directly
2. **Peer Version Counter:** `PeerTalk_GetPeersVersion(ctx)` - Returns version number that increments when peer list changes, allowing apps to detect changes without copying entire list
3. **Name Accessor:** `PeerTalk_GetPeerName(ctx, name_idx)` - Returns name string from cold storage

```c
/* Required signatures for Phase 1 */
const PeerTalk_PeerInfo *PeerTalk_GetPeerByID(PeerTalk_Context *ctx, PeerTalk_PeerID id);
uint32_t PeerTalk_GetPeersVersion(PeerTalk_Context *ctx);
const char *PeerTalk_GetPeerName(PeerTalk_Context *ctx, uint8_t name_idx);
```

These APIs enable efficient polling patterns:
```c
static uint32_t g_peers_version = 0;

while (running) {
    PeerTalk_Poll(ctx);

    /* Only refresh when peer list actually changed */
    if (PeerTalk_GetPeersVersion(ctx) != g_peers_version) {
        refresh_peer_list();
        g_peers_version = PeerTalk_GetPeersVersion(ctx);
    }
}
```

---

### Logging Best Practices

The example chat app demonstrates production-ready logging patterns:

> ⚠️ **ISR-SAFETY WARNING**
>
> **Application callbacks ARE SAFE for logging:**
> - `on_message_received`, `on_peer_discovered`, etc. run synchronously during `PeerTalk_Poll()`
> - They execute in your thread, at a predictable time in the main event loop
> - You can safely call PT_Log, printf, malloc, and any other functions
>
> **SDK INTERNAL callbacks are NOT SAFE for logging:**
> - MacTCP ASR (Asynchronous Status Routine) - runs at interrupt level
> - Open Transport notifiers - run at deferred task level
> - ADSP completion routines - run at interrupt level
> - These CANNOT call PT_Log, allocate memory, or make sync calls
> - See Phases 5-7 for ISR-safe flag-based patterns that defer logging to main loop
>
> **PT_Log callback deadlock warning:**
> - PT_LogSetCallback callbacks run synchronously from PT_LogWrite()
> - On POSIX, PT_LogWrite() holds a mutex during callback dispatch
> - Calling PT_Log from INSIDE a callback causes deadlock
> - Callbacks should only: display to UI, write to custom stream, send to network

**1. Log Categories (All PeerTalk Categories)**

| Category | Constant | Use For |
|----------|----------|---------|
| UI Events | `PT_LOG_CAT_APP1` | Window events, input handling, display updates |
| Chat Messages | `PT_LOG_CAT_APP2` | Message send/receive, chat history |
| Network | `PT_LOG_CAT_NETWORK` | Connections, disconnections, peer discovery |
| Protocol | `PT_LOG_CAT_PROTOCOL` | Packet encoding/decoding errors (Phase 3) |
| Memory | `PT_LOG_CAT_MEMORY` | Allocation failures, buffer management (Phase 5) |
| Platform | `PT_LOG_CAT_PLATFORM` | Driver init, low-level errors |
| Performance | `PT_LOG_CAT_PERF` | Timing measurements, throughput stats |
| Initialization | `PT_LOG_CAT_INIT` | Startup/shutdown, driver load (Phases 4-7) |
| Discovery | `PT_LOG_CAT_DISCOVERY` | UDP broadcast, NBP lookup (Phase 4) |
| Send | `PT_LOG_CAT_SEND` | TCP/UDP/ADSP transmit events (Phase 3.5) |
| Receive | `PT_LOG_CAT_RECV` | TCP/UDP/ADSP receive events (Phases 4-7) |

**2. Log Levels by Severity**

| Level | Use When | Example |
|-------|----------|---------|
| `PT_LOG_ERR` | Operation failed | `"Connection failed: %d"` |
| `PT_LOG_WARN` | Degraded but working | `"Retry 2/3 for peer %s"` |
| `PT_LOG_INFO` | Normal operations | `"Connected to %s"` |
| `PT_LOG_DEBUG` | Diagnostic details | `"Packet: %d bytes"` |

**3. Critical Operations That MUST Be Logged**

| Operation | Error Condition | Category | Level |
|-----------|-----------------|----------|-------|
| Initialization | Driver load failure | `PT_LOG_CAT_INIT` | ERR |
| Initialization | Out of memory for streams | `PT_LOG_CAT_MEMORY` | ERR |
| Connection | Connection timeout | `PT_LOG_CAT_NETWORK` | WARN |
| Connection | Invalid peer ID | `PT_LOG_CAT_NETWORK` | ERR |
| Discovery | Packet decode failure | `PT_LOG_CAT_PROTOCOL` | ERR |
| Discovery | NBP lookup timeout | `PT_LOG_CAT_DISCOVERY` | WARN |
| Send | Buffer allocation failure | `PT_LOG_CAT_MEMORY` | WARN |
| Send | Stream closed unexpectedly | `PT_LOG_CAT_NETWORK` | WARN |
| Receive | Malformed message | `PT_LOG_CAT_PROTOCOL` | WARN |
| Shutdown | Stream cleanup failure | `PT_LOG_CAT_NETWORK` | WARN |

**4. Configuration for Different Environments**

```c
/* Development - verbose output */
PT_LogSetLevel(log, PT_LOG_DEBUG);
PT_LogSetOutput(log, PT_LOG_OUT_FILE | PT_LOG_OUT_STDERR);
PT_LogSetAutoFlush(log, true);  /* Don't lose logs on crash (~2-5x slower) */

/* Production - errors and warnings only */
PT_LogSetLevel(log, PT_LOG_WARN);
PT_LogSetOutput(log, PT_LOG_OUT_FILE);
PT_LogSetAutoFlush(log, false);  /* Better performance */

/* Troubleshooting - enable specific subsystems */
PT_LogSetCategories(log, PT_LOG_CAT_NETWORK | PT_LOG_CAT_PROTOCOL);
PT_LogSetLevel(log, PT_LOG_DEBUG);
```

**5. Platform-Specific Logging Differences**

| Platform | Console | File | Max Message | Notes |
|----------|---------|------|-------------|-------|
| POSIX | stderr (PT_LOG_OUT_CONSOLE) | fopen/fwrite | Unlimited | Full vsnprintf |
| Classic Mac | NO console (no-op) | File Manager | 192 bytes | vsprintf - no bounds checking! |

**Classic Mac requires callback-based logging for UI feedback:**
```c
/* Classic Mac: Display errors in status bar via callback */
static void chat_log_callback(PT_Log *log, PT_LogLevel level,
                               uint16_t category, const char *msg, void *ctx) {
    /* WARNING: Do NOT call PT_Log from this callback! */
    if (level <= PT_LOG_WARN) {
        SetStatusText(msg);  /* Update TextEdit or status bar */
    }
}
PT_LogSetCallback(log, chat_log_callback, &ui_context);
```

**6. Initialization Logging Pattern**

```c
/* Create PT_Log BEFORE PeerTalk_Init to capture initialization errors */
PT_Log *log = PT_LogCreate();
PT_LogSetFile(log, "app.log");
PT_LogSetLevel(log, PT_LOG_DEBUG);

PeerTalk_Config config = { .log = log };  /* Share log with SDK */
PeerTalk_Context *ctx = PeerTalk_Init(&config);
if (!ctx) {
    PT_LOG_ERR(log, PT_LOG_CAT_INIT, "PeerTalk initialization failed");
    PT_LogDestroy(log);
    return -1;
}
PT_LOG_INFO(log, PT_LOG_CAT_INIT, "PeerTalk initialized successfully");
```

**7. Shutdown Logging Pattern**

```c
PT_LOG_INFO(log, PT_LOG_CAT_INIT, "Shutting down PeerTalk");
PeerTalk_Shutdown(ctx);
PT_LOG_INFO(log, PT_LOG_CAT_INIT, "PeerTalk shutdown complete");
PT_LogFlush(log);  /* Ensure all logs written before destroy */
PT_LogDestroy(log);
```

---

### Example App Anti-Patterns

The example chat applications use simplified patterns for readability. Production applications should avoid these patterns:

**1. Circular Buffer vs memmove**

```c
/* EXAMPLE CODE - simple but O(n) on buffer full */
if (g_message_count >= MAX_MESSAGES) {
    memmove(&g_messages[0], &g_messages[1],
            sizeof(chat_message) * (MAX_MESSAGES - 1));  /* 29KB move! */
}

/* PRODUCTION - O(1) circular buffer */
typedef struct {
    chat_message messages[MAX_MESSAGES];
    int head, tail, count;
} message_ring;
/* Insert at tail, remove from head, wrap around */
```

**2. Dirty-Flag Rendering vs Full Redraw**

```c
/* EXAMPLE CODE - redraws everything every frame */
while (running) {
    PeerTalk_Poll(ctx);
    draw_all();  /* Redraws entire UI 50x/second */
    usleep(20000);
}

/* PRODUCTION - only redraw what changed */
if (ui_dirty_flags & DIRTY_PEER_LIST) {
    draw_peer_list();
    ui_dirty_flags &= ~DIRTY_PEER_LIST;
}
```

**3. Global State vs user_data**

```c
/* EXAMPLE CODE - globals for simplicity */
static PeerTalk_Context *g_ctx;
static PeerTalk_PeerInfo g_peers[MAX_PEERS];

/* PRODUCTION - pass state through user_data */
typedef struct {
    PeerTalk_Context *ctx;
    PeerTalk_PeerInfo peers[MAX_PEERS];
    /* ... other app state ... */
} AppState;

callbacks.user_data = &app_state;
```

**4. Sync File I/O in Benchmarks**

```c
/* EXAMPLE CODE - sync write per log line */
bench_log(...);  /* Calls PBWriteSync - slow on 68k (~15ms per write) */

/* PRODUCTION - buffer and batch writes */
static char log_buffer[4096];
static int log_pos = 0;
/* Flush periodically or when buffer full */
```

**5. memmove on Message Buffer Full**

```c
/* EXAMPLE CODE - O(n) on buffer full
 * Timing estimate: ~15-30ms on 68030 for 29KB (assumes 1-2 MB/s memory throughput)
 * Actual timing varies by CPU clock and memory type */
if (g_message_count >= MAX_MESSAGES) {
    memmove(&g_messages[0], &g_messages[1],
            sizeof(chat_message) * (MAX_MESSAGES - 1));
}

/* PRODUCTION - O(1) circular buffer (see anti-pattern #1 above) */
```

**6. refresh_peer_list() Every Main Loop Iteration**

```c
/* EXAMPLE CODE - copies all peer data every frame (50x/second) */
while (running) {
    PeerTalk_Poll(ctx);
    refresh_peer_list();  /* Unnecessary copy if nothing changed */
}

/* PRODUCTION - only refresh when peers change */
static uint32_t peers_version = 0;
if (PeerTalk_GetPeersVersion(ctx) != peers_version) {
    refresh_peer_list();
    peers_version = PeerTalk_GetPeersVersion(ctx);
}
```

These patterns are acceptable in example code where clarity matters more than performance, but should be replaced in production applications, especially on Classic Mac hardware.

---

### Memory Budget Guidelines

Classic Mac systems have limited RAM. Example apps should respect these constraints:

| System | Total RAM | System Overhead | Available | Chat Constraints |
|--------|-----------|-----------------|-----------|------------------|
| Mac SE (System 6) | 4 MB | ~600 KB | ~3.4 MB | Max 8 peers, 1024 messages, no ncurses |
| Mac IIci (System 7) | 8 MB | ~800 KB | ~7 MB | Max 16 peers, 4096 messages |
| Power Mac (Mac OS 8) | 16+ MB | ~2 MB | 14+ MB | Max 32 peers, unlimited messages |

**Buffer Sizing with MaxBlock():**

```c
/* Check CONTIGUOUS free memory, not just total free */
long avail = MaxBlock();  /* NOT FreeMem() - that may be fragmented */

if (avail > 500000) {
    max_messages = 4096;
    max_peers = 32;
} else if (avail > 200000) {
    max_messages = 1024;
    max_peers = 16;
} else {
    max_messages = 256;
    max_peers = 8;
}
```

**Memory Leak Detection:**

```c
/* At startup and shutdown, compare MaxBlock values */
long initial_maxblock = MaxBlock();

/* ... run application ... */

long final_maxblock = MaxBlock();
if (final_maxblock < initial_maxblock - 1024) {
    printf("WARNING: Possible memory leak: %ld bytes\n",
           initial_maxblock - final_maxblock);
}
```

## Session Scope Table

| Session | Focus | Status | Files Created/Modified | Tests | Verify |
|---------|-------|--------|------------------------|-------|--------|
| 8.0 | Platform Consolidation Review | [OPEN] | Refactored common patterns | Code review | Common patterns extracted, no regressions |
| 8.1 | POSIX Chat App | [OPEN] | `examples/chat_posix.c` | Manual test | Chat works between two POSIX instances |
| 8.2 | Classic Mac Chat App (Single Transport) | [OPEN] | `examples/chat_mac.c` | Manual test | Chat works on real Mac hardware |
| 8.3 | Multi-Transport OT Chat (Gateway) | ⛔ **[BLOCKED]** | `examples/chat_mac_gateway.c` | Real hardware | TCP/IP ↔ AppleTalk bridging works. **Requires Phase 6.8-6.10 + Phase 7** |
| 8.4 | Cross-Network Integration Testing | [OPEN] | `examples/minimal_example.c`, `notes/TESTING.md` | Real hardware | POSIX ↔ Mac works, minimal example compiles |
| 8.5 | Benchmark Mode | [OPEN] | Benchmark additions to chat apps | Cross-platform | All benchmark rounds pass |

### Status Key
- **[OPEN]** - Not started
- **[IN PROGRESS]** - Currently being worked on
- **[READY TO TEST]** - Implementation complete, needs verification
- **[DONE]** - Verified and complete

---

## Session 8.0: Platform Consolidation Review

### Objective
Review the four platform implementations (POSIX, MacTCP, Open Transport, AppleTalk) completed in Phases 4-7 and extract any common patterns that emerged during implementation.

### Rationale
After implementing four distinct networking backends, common patterns often emerge that weren't visible during initial design. This session identifies duplication and extracts shared code before building the example applications, ensuring the SDK is as clean as possible for the reference examples.

### Tasks

#### Task 8.0.1: Review Platform Implementations for Common Patterns

Compare the four platform implementations side-by-side:
- `src/posix/` - POSIX sockets
- `src/mactcp/` - MacTCP streams
- `src/opentransport/` - Open Transport endpoints
- `src/appletalk/` - AppleTalk ADSP/NBP

Look for:
1. **Duplicated helper functions** - String handling, buffer management, timeout calculations
2. **Similar state machines** - Connection lifecycle, discovery polling patterns
3. **Repeated error handling** - Common error-to-PeerTalk_Error mapping patterns
4. **Redundant definitions** - Constants or macros defined in multiple places

#### Task 8.0.2: Extract Common Patterns to Core

For any patterns found in Task 8.0.1, refactor:
- Move shared code to `src/core/` if platform-independent
- Create shared internal headers if patterns are consistent across platforms
- Update platform files to use the extracted common code

**Guidelines:**
- Only extract if 3+ platforms share the pattern
- Keep ISR-safety rules in mind (some patterns can't be shared due to interrupt-level restrictions)
- Don't over-abstract - if platforms handle something differently for good reasons, leave it

#### Task 8.0.3: Verify No Regressions

After any refactoring:
1. Run `make test` - all POSIX tests pass
2. Rebuild MacTCP target with Retro68 - compiles without errors
3. Rebuild Open Transport target with Retro68 - compiles without errors
4. Rebuild AppleTalk target with Retro68 - compiles without errors

### Deliverables

Session 8.0 MUST produce the following, even if no refactoring is performed:

| Deliverable | Location | Purpose |
|-------------|----------|---------|
| `src/core/pt_common.h` | Source tree | Common macros, inline helpers (if any extracted) |
| `src/core/pt_common.c` | Source tree | Shared implementations (if any extracted) |
| `tests/test_common.c` | Test suite | Validates shared code works on all platforms |
| `notes/PHASE-8.0-NOTES.md` | Documentation | Session findings: patterns reviewed, decisions made |

**If no common patterns are extracted:**
- `pt_common.h/c` may be empty stubs or omitted
- `PHASE-8.0-NOTES.md` MUST document what was reviewed and why nothing was extracted
- This confirms the review happened and is a valid outcome

### Acceptance Criteria
1. All four platform implementations reviewed for common patterns
2. Any extracted common code documented in code comments
3. All POSIX tests pass after refactoring
4. All Retro68 builds succeed after refactoring
5. `notes/PHASE-8.0-NOTES.md` exists and documents the review findings
6. If `pt_common.h/c` created, `tests/test_common.c` verifies correctness

### Notes
- This session may result in no code changes if implementations are already well-factored
- Document findings even if no refactoring is done - this confirms the review happened
- If significant refactoring is needed, it's better to do it now before the example apps reference the code

---

## Session 8.1: POSIX Chat Application

### Objective
Create an ncurses-based chat application for POSIX systems that demonstrates the PeerTalk SDK.

### Design Notes

**Callback Pattern:**

PeerTalk uses a polling model with callbacks. The application calls `PeerTalk_Poll()` regularly (typically in its main loop), and callbacks fire synchronously during that call. This means:
- Callbacks run in your thread, at a predictable time
- You can safely update UI, allocate memory, etc. in callbacks
- No need for mutexes or thread synchronization

**Connection Handling:**

The current SDK auto-accepts all incoming connections. This is appropriate for peer-to-peer chat. Applications requiring connection filtering (e.g., game lobbies with player limits) would need to extend the SDK with an `on_connection_requested` callback - see PROJECT_GOALS.md for the intended callback list.

**user_data Usage:**

This example uses global variables for simplicity. Production applications should set `callbacks.user_data` to pass application state to callbacks, keeping the code modular and avoiding globals.

### Tasks

#### Task 8.1.1: Create `examples/chat_posix.c`

```c
/*
 * PeerTalk Example: Chat Application for POSIX (ncurses)
 *
 * This example demonstrates:
 * - Initializing PeerTalk with callbacks
 * - Peer discovery on the local network
 * - Connecting to discovered peers
 * - Sending and receiving messages
 * - Proper cleanup on shutdown
 *
 * Layout:
 * +------------------------------------------+
 * |           PeerTalk Chat v1.0             |
 * +------------------------------------------+
 * | Peers:                  | Messages:      |
 * |  [C] Alice - connected  |  Alice: Hello  |
 * |  [D] Bob   - discovered |  You: Hi there |
 * |  [ ] Carol - idle       |  Alice: How... |
 * |                         |                |
 * +------------------------------------------+
 * | Status: Connected to 1 peer              |
 * +------------------------------------------+
 * | > _                                      |
 * +------------------------------------------+
 *
 * Commands:
 *   /list          - Show discovered peers
 *   /connect <id>  - Connect to peer by ID
 *   /disconnect    - Disconnect from current peer
 *   /quit          - Exit the application
 *   <text>         - Send message to connected peer(s)
 */

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>
#include "peertalk.h"

/*============================================================================
 * Configuration
 *============================================================================*/

#define MAX_MESSAGES    100
#define MAX_MSG_LEN     256
#define MAX_INPUT_LEN   256

/*============================================================================
 * Message History
 *============================================================================*/

typedef struct {
    char text[MAX_MSG_LEN];              /* 256 bytes: Message content */
    char sender[PT_MAX_PEER_NAME + 1];   /* 32 bytes: Sender name (example only) */
    uint8_t is_local;                    /* 1 byte: 1 if sent by us */
    uint8_t _pad[3];                     /* 3 bytes: Alignment padding */
} chat_message;
/* NOTE: Production apps should use sender_id (2 bytes) instead of sender name
 * to save 30 bytes per message. See "Example App Anti-Patterns" section. */

static chat_message g_messages[MAX_MESSAGES];
static int g_message_count = 0;

/*============================================================================
 * Application State
 *============================================================================*/

static PeerTalk_Context *g_ctx = NULL;
static PT_Log *g_log = NULL;  /* PT_Log for unified logging */
static PeerTalk_PeerInfo g_peers[PT_MAX_PEERS];
static int g_peer_count = 0;
static int g_selected_peer = -1;  /* Peer ID of selected peer, -1 if none */
static int g_running = 1;
static char g_local_name[PT_MAX_PEER_NAME + 1] = "User";
static char g_status[128] = "Starting...";

/* App-specific log categories */
#define LOG_UI      PT_LOG_CAT_APP1
#define LOG_CHAT    PT_LOG_CAT_APP2
#define LOG_NET     PT_LOG_CAT_NETWORK

/*============================================================================
 * ncurses Windows
 *============================================================================*/

static WINDOW *g_win_peers = NULL;
static WINDOW *g_win_messages = NULL;
static WINDOW *g_win_status = NULL;
static WINDOW *g_win_input = NULL;

/*============================================================================
 * Helper Functions
 *============================================================================*/

static void add_message(const char *sender, const char *text, int is_local) {
    if (g_message_count >= MAX_MESSAGES) {
        /* Shift messages up */
        memmove(&g_messages[0], &g_messages[1],
                sizeof(chat_message) * (MAX_MESSAGES - 1));
        g_message_count = MAX_MESSAGES - 1;
    }

    chat_message *msg = &g_messages[g_message_count++];
    strncpy(msg->sender, sender, PT_MAX_PEER_NAME);
    msg->sender[PT_MAX_PEER_NAME] = '\0';
    strncpy(msg->text, text, MAX_MSG_LEN - 1);
    msg->text[MAX_MSG_LEN - 1] = '\0';
    msg->is_local = is_local;
}

static void set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, ap);
    va_end(ap);
}

static void refresh_peer_list(void) {
    uint16_t count;
    PeerTalk_GetPeers(g_ctx, g_peers, PT_MAX_PEERS, &count);
    g_peer_count = count;
}

/*============================================================================
 * PeerTalk Callbacks
 *============================================================================*/

static void on_peer_discovered(PeerTalk_Context *ctx,
                                const PeerTalk_PeerInfo *peer,
                                void *user_data) {
    /* Use name accessor - peer->name_idx references cold storage */
    const char *name = PeerTalk_GetPeerName(ctx, peer->name_idx);
    PT_INFO(g_log, LOG_NET, "Discovered peer: %s (ID %u)", name, peer->id);
    set_status("Discovered: %s", name);
    refresh_peer_list();
}

static void on_peer_lost(PeerTalk_Context *ctx,
                         PeerTalk_PeerID peer_id,
                         void *user_data) {
    PT_INFO(g_log, LOG_NET, "Peer lost: ID %u", peer_id);
    set_status("Peer lost: ID %u", peer_id);
    refresh_peer_list();

    if (g_selected_peer == peer_id) {
        g_selected_peer = -1;
    }
}

static void on_peer_connected(PeerTalk_Context *ctx,
                              PeerTalk_PeerID peer_id,
                              void *user_data) {
    /* Use O(1) lookup - required for 68k performance */
    const PeerTalk_PeerInfo *peer = PeerTalk_GetPeerByID(ctx, peer_id);
    const char *name = peer ? PeerTalk_GetPeerName(ctx, peer->name_idx) : "Unknown";

    refresh_peer_list();

    PT_INFO(g_log, LOG_NET, "Connected to peer: %s (ID %u)", name, peer_id);
    set_status("Connected to %s", name);
    add_message("System", "Connected", 0);

    if (g_selected_peer < 0) {
        g_selected_peer = peer_id;
    }
}

static void on_peer_disconnected(PeerTalk_Context *ctx,
                                  PeerTalk_PeerID peer_id,
                                  PeerTalk_Error reason,
                                  void *user_data) {
    PT_WARN(g_log, LOG_NET, "Disconnected from peer %u (reason: %d)", peer_id, reason);
    set_status("Disconnected (reason: %d)", reason);
    add_message("System", "Disconnected", 0);
    refresh_peer_list();

    if (g_selected_peer == peer_id) {
        g_selected_peer = -1;
    }
}

static void on_message_received(PeerTalk_Context *ctx,
                                 PeerTalk_PeerID from_peer,
                                 const void *data,
                                 uint16_t length,
                                 void *user_data) {
    /* Use O(1) lookup - required for 68k performance */
    const PeerTalk_PeerInfo *peer = PeerTalk_GetPeerByID(ctx, from_peer);
    const char *sender = peer ? PeerTalk_GetPeerName(ctx, peer->name_idx) : "Unknown";

    PT_DEBUG(g_log, LOG_CHAT, "Message from %s (%u): %u bytes", sender, from_peer, length);

    /* Add to message history */
    char text[MAX_MSG_LEN];
    int len = (length < MAX_MSG_LEN - 1) ? length : MAX_MSG_LEN - 1;
    memcpy(text, data, len);
    text[len] = '\0';

    add_message(sender, text, 0);
}

/*============================================================================
 * UI Drawing
 *============================================================================*/

static void draw_peers(void) {
    werase(g_win_peers);
    box(g_win_peers, 0, 0);
    mvwprintw(g_win_peers, 0, 2, " Peers ");

    int y = 1;
    int max_y = getmaxy(g_win_peers) - 2;

    for (int i = 0; i < g_peer_count && y <= max_y; i++) {
        char state = ' ';
        if (g_peers[i].connected) {
            state = 'C';  /* Connected */
        } else {
            state = 'D';  /* Discovered */
        }

        /* Highlight selected peer */
        if (g_peers[i].id == g_selected_peer) {
            wattron(g_win_peers, A_REVERSE);
        }

        const char *name = PeerTalk_GetPeerName(g_ctx, g_peers[i].name_idx);
        mvwprintw(g_win_peers, y, 1, "[%c] %u: %.12s",
                  state, g_peers[i].id, name);

        if (g_peers[i].id == g_selected_peer) {
            wattroff(g_win_peers, A_REVERSE);
        }

        y++;
    }

    if (g_peer_count == 0) {
        mvwprintw(g_win_peers, 1, 1, "(no peers)");
    }

    wrefresh(g_win_peers);
}

static void draw_messages(void) {
    werase(g_win_messages);
    box(g_win_messages, 0, 0);
    mvwprintw(g_win_messages, 0, 2, " Messages ");

    int max_y = getmaxy(g_win_messages) - 2;
    int max_x = getmaxx(g_win_messages) - 2;
    int start = (g_message_count > max_y) ? g_message_count - max_y : 0;

    int y = 1;
    for (int i = start; i < g_message_count && y <= max_y; i++) {
        chat_message *msg = &g_messages[i];

        if (msg->is_local) {
            wattron(g_win_messages, A_BOLD);
        }

        /* Truncate message to fit */
        char line[256];
        snprintf(line, sizeof(line), "%s: %s", msg->sender, msg->text);
        if (strlen(line) > max_x) {
            line[max_x - 3] = '.';
            line[max_x - 2] = '.';
            line[max_x - 1] = '.';
            line[max_x] = '\0';
        }

        mvwprintw(g_win_messages, y, 1, "%s", line);

        if (msg->is_local) {
            wattroff(g_win_messages, A_BOLD);
        }

        y++;
    }

    wrefresh(g_win_messages);
}

static void draw_status(void) {
    werase(g_win_status);
    box(g_win_status, 0, 0);
    mvwprintw(g_win_status, 1, 1, "%s", g_status);
    wrefresh(g_win_status);
}

static void draw_input(const char *input) {
    werase(g_win_input);
    box(g_win_input, 0, 0);
    mvwprintw(g_win_input, 1, 1, "> %s_", input);
    wrefresh(g_win_input);
}

static void draw_all(const char *input) {
    draw_peers();
    draw_messages();
    draw_status();
    draw_input(input);
}

/*============================================================================
 * Command Handling
 *============================================================================*/

static void handle_command(const char *input) {
    /* Commands start with / */
    if (input[0] != '/') {
        /* Regular message - send to selected peer */
        if (g_selected_peer > 0) {
            PeerTalk_Error err = PeerTalk_Send(g_ctx, g_selected_peer,
                                               input, strlen(input) + 1);
            if (err == PT_OK) {
                add_message(g_local_name, input, 1);
            } else {
                set_status("Send failed: %s", PeerTalk_ErrorString(err));
            }
        } else {
            set_status("No peer selected. Use /connect <id>");
        }
        return;
    }

    /* Parse command */
    if (strncmp(input, "/quit", 5) == 0 || strncmp(input, "/q", 2) == 0) {
        g_running = 0;
        return;
    }

    if (strncmp(input, "/list", 5) == 0 || strncmp(input, "/l", 2) == 0) {
        refresh_peer_list();
        set_status("Found %d peers", g_peer_count);
        return;
    }

    if (strncmp(input, "/connect ", 9) == 0 || strncmp(input, "/c ", 3) == 0) {
        int id;
        const char *num = (input[1] == 'c' && input[2] == ' ') ?
                          input + 3 : input + 9;
        if (sscanf(num, "%d", &id) == 1) {
            set_status("Connecting to peer %d...", id);
            PeerTalk_Error err = PeerTalk_Connect(g_ctx, id);
            if (err != PT_OK) {
                set_status("Connect failed: %s", PeerTalk_ErrorString(err));
            }
        } else {
            set_status("Usage: /connect <peer_id>");
        }
        return;
    }

    if (strncmp(input, "/disconnect", 11) == 0 || strncmp(input, "/d", 2) == 0) {
        if (g_selected_peer > 0) {
            PeerTalk_Disconnect(g_ctx, g_selected_peer);
            g_selected_peer = -1;
        } else {
            set_status("Not connected");
        }
        return;
    }

    if (strncmp(input, "/help", 5) == 0 || strncmp(input, "/h", 2) == 0) {
        add_message("Help", "/list - show peers", 0);
        add_message("Help", "/connect <id> - connect to peer", 0);
        add_message("Help", "/disconnect - disconnect", 0);
        add_message("Help", "/quit - exit", 0);
        return;
    }

    set_status("Unknown command. Type /help for help.");
}

/*============================================================================
 * Signal Handler
 *============================================================================*/

static void signal_handler(int sig) {
    g_running = 0;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(int argc, char **argv) {
    PeerTalk_Config config = {0};
    PeerTalk_Callbacks callbacks = {0};
    char input[MAX_INPUT_LEN] = {0};
    int input_pos = 0;

    /* Parse arguments */
    if (argc > 1) {
        strncpy(g_local_name, argv[1], PT_MAX_PEER_NAME);
        g_local_name[PT_MAX_PEER_NAME] = '\0';
    }

    /* Set up signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /*========================================================================
     * Initialize PT_Log BEFORE ncurses (so errors are visible)
     *========================================================================*/
    g_log = PT_LogCreate();
    if (g_log) {
        PT_LogSetFile(g_log, "chat_posix.log");
        PT_LogSetLevel(g_log, PT_LOG_INFO);  /* Production: INFO. Debug: DEBUG */
        PT_LogSetOutput(g_log, PT_LOG_OUT_FILE);  /* File only - ncurses owns console */
        PT_LogSetAutoFlush(g_log, true);  /* Crash-safe logging */
        PT_INFO(g_log, LOG_UI, "Chat starting as '%s'", g_local_name);
    }

    /* Initialize ncurses */
    initscr();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    keypad(stdscr, TRUE);

    /* Get terminal size */
    int term_height, term_width;
    getmaxyx(stdscr, term_height, term_width);

    /* Create windows */
    int peer_width = term_width / 3;
    int msg_width = term_width - peer_width;
    int content_height = term_height - 6;  /* Leave room for status and input */

    g_win_peers = newwin(content_height, peer_width, 0, 0);
    g_win_messages = newwin(content_height, msg_width, 0, peer_width);
    g_win_status = newwin(3, term_width, content_height, 0);
    g_win_input = newwin(3, term_width, content_height + 3, 0);

    /* Initialize PeerTalk with shared log */
    config.local_name = g_local_name;
    config.max_peers = PT_MAX_PEERS;
    config.log = g_log;  /* Share log with SDK for unified diagnostics */

    /* Set up callbacks */
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_lost = on_peer_lost;
    callbacks.on_peer_connected = on_peer_connected;
    callbacks.on_peer_disconnected = on_peer_disconnected;
    callbacks.on_message_received = on_message_received;

    g_ctx = PeerTalk_Init(&config);
    if (!g_ctx) {
        PT_ERR(g_log, LOG_NET, "Failed to initialize PeerTalk");
        endwin();
        if (g_log) PT_LogDestroy(g_log);
        fprintf(stderr, "Failed to initialize PeerTalk\n");
        return 1;
    }
    PT_INFO(g_log, LOG_NET, "PeerTalk initialized successfully");

    PeerTalk_SetCallbacks(g_ctx, &callbacks);

    /* Start discovery */
    PeerTalk_StartDiscovery(g_ctx);
    set_status("Discovering peers as '%s'...", g_local_name);

    add_message("System", "PeerTalk Chat started. Type /help for commands.", 0);

    /* Main loop */
    while (g_running) {
        /* Poll PeerTalk for network events */
        PeerTalk_Poll(g_ctx);

        /* Refresh peer list periodically */
        refresh_peer_list();

        /* Draw UI */
        draw_all(input);

        /* Handle keyboard input (non-blocking) */
        int ch = getch();
        if (ch != ERR) {
            if (ch == '\n' || ch == '\r') {
                if (input_pos > 0) {
                    input[input_pos] = '\0';
                    handle_command(input);
                    input_pos = 0;
                    input[0] = '\0';
                }
            } else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
                if (input_pos > 0) {
                    input[--input_pos] = '\0';
                }
            } else if (ch >= 32 && ch < 127 && input_pos < MAX_INPUT_LEN - 1) {
                input[input_pos++] = ch;
                input[input_pos] = '\0';
            }
        }

        /* Small delay to avoid busy-waiting */
        usleep(20000);  /* 20ms = ~50 FPS */
    }

    /* Cleanup */
    set_status("Shutting down...");
    draw_all(input);
    PT_INFO(g_log, LOG_UI, "Chat shutting down");

    PeerTalk_StopDiscovery(g_ctx);
    PeerTalk_Shutdown(g_ctx);

    delwin(g_win_peers);
    delwin(g_win_messages);
    delwin(g_win_status);
    delwin(g_win_input);
    endwin();

    PT_INFO(g_log, LOG_UI, "Chat shutdown complete");
    if (g_log) PT_LogDestroy(g_log);

    printf("Goodbye!\n");
    return 0;
}
```

#### Task 8.1.2: Add to Makefile

```makefile
# Add to Makefile

EXAMPLES = chat_posix

chat_posix: examples/chat_posix.c $(LIBNAME)
	$(CC) $(CFLAGS) -o $@ $< -L. -lpeertalk -lncurses
```

### Acceptance Criteria
1. Application compiles and runs on Linux/macOS
2. Peer discovery works (sees other PeerTalk instances)
3. Can connect to discovered peers
4. Messages send and receive correctly
5. UI updates in real-time
6. Clean shutdown on /quit or Ctrl-C
7. **PT_Log Integration:**
   - [ ] PT_Log context created at startup
   - [ ] Log file `chat_posix.log` created
   - [ ] Callbacks use PT_INFO/PT_DEBUG/PT_ERR (not printf)
   - [ ] Auto-flush enabled for crash-safe debugging
   - [ ] Log destroyed at shutdown

---

## Session 8.2: Classic Mac Chat Application

### Objective
Create a console-style chat application for Classic Mac using standard I/O with event polling. This demonstrates the PeerTalk SDK on resource-constrained systems.

### Design Notes

Classic Mac doesn't have a "standard console" in the UNIX sense. We use **SIOW (Simple Input/Output Window)** which Retro68 provides - it creates a window that handles printf/scanf.

**Critical SIOW Integration:**

SIOW owns the event loop during I/O operations. Calling `GetNextEvent()` directly while using SIOW's printf/scanf causes conflicts. Instead, we use SIOW's event hook mechanism:

```c
extern Boolean (*__siowEventHook)(EventRecord *theEvent);
```

This hook is called by SIOW during its internal event processing (while waiting for input). We install a hook that:
1. Polls PeerTalk for network events
2. Handles Cmd-Q for quit
3. Returns `false` to let SIOW process the event normally

This approach is simpler and more reliable than manual event handling.

**SIOW Include Note:**

The `__siowEventHook` extern is declared in `SIOW.h` (located in RIncludes, not CIncludes). Retro68 applications linking `SIOW.o` get this symbol automatically. The explicit extern declaration in the example code below serves as documentation and works with or without including the header directly.

**Callback Safety Note:**

PeerTalk callbacks are called from within `PeerTalk_Poll()`, NOT from interrupt level (ASR/notifier). This means callbacks can safely:
- Call printf() and other I/O functions
- Allocate memory
- Update UI elements
- Call other PeerTalk functions

This is the correct design for Classic Mac cooperative multitasking.

**Responsiveness Note:**

With SIOW, network events only process while waiting for input (during `fgets()`). Once the user starts typing, events queue until Enter is pressed. For a more responsive app, you'd need a proper Mac event loop with `WaitNextEvent()`. This simple approach is adequate for a demonstration.

**user_data Note:**

This example uses global variables for simplicity. Production applications should use the `callbacks.user_data` field to pass application context to callbacks, avoiding globals:
```c
callbacks.user_data = my_app_state;
// Then in callbacks: MyAppState *state = (MyAppState *)user_data;
```

### Tasks

#### Task 8.2.1: Create `examples/chat_mac.c`

```c
/*
 * PeerTalk Example: Chat Application for Classic Mac
 *
 * This example demonstrates:
 * - Initializing PeerTalk with callbacks
 * - Integrating with SIOW's event loop via __siowEventHook
 * - Peer discovery on the local network
 * - Connecting to and chatting with peers
 *
 * Build with Retro68 using SIOW for console I/O.
 *
 * Commands:
 *   L        - List discovered peers
 *   C <id>   - Connect to peer by ID
 *   D        - Disconnect from current peer
 *   S <msg>  - Send message
 *   H        - Help
 *   Q        - Quit (or Cmd-Q)
 */

#include <stdio.h>
#include <string.h>
#include <Events.h>
#include "peertalk.h"

/*============================================================================
 * SIOW Event Hook
 *
 * SIOW (Simple Input/Output Window) owns the event loop during I/O.
 * We install a hook that gets called during SIOW's event processing,
 * allowing us to poll PeerTalk without conflicting with SIOW.
 *
 * Declared in SIOW library - we provide the extern declaration here.
 *============================================================================*/

extern Boolean (*__siowEventHook)(EventRecord *theEvent);

/*============================================================================
 * Application State
 *============================================================================*/

static PeerTalk_Context *g_ctx = NULL;
static PT_Log *g_log = NULL;  /* PT_Log for unified logging */
static PeerTalk_PeerInfo g_peers[8];
static int g_peer_count = 0;
static int g_selected_peer = -1;
static int g_running = 1;
static int g_in_poll = 0;  /* Reentrancy guard */

/* App-specific log categories */
#define LOG_UI      PT_LOG_CAT_APP1
#define LOG_CHAT    PT_LOG_CAT_APP2
#define LOG_NET     PT_LOG_CAT_NETWORK

/*============================================================================
 * SIOW Event Hook Implementation
 *
 * Called by SIOW during its internal event loop (while waiting for input).
 * Return true to consume the event, false to let SIOW handle it.
 *
 * IMPORTANT: We use a reentrancy guard because:
 *   1. This hook calls PeerTalk_Poll()
 *   2. Poll may fire callbacks
 *   3. Callbacks call printf()
 *   4. printf triggers SIOW
 *   5. SIOW might call this hook again
 * Without the guard, we'd get recursive Poll calls and potential stack overflow.
 *============================================================================*/

static Boolean siow_event_hook(EventRecord *theEvent) {
    /* Prevent reentrancy - callbacks may trigger printf which triggers SIOW */
    if (g_in_poll) {
        return false;
    }

    /* Poll PeerTalk for network events */
    if (g_ctx) {
        g_in_poll = 1;
        PeerTalk_Poll(g_ctx);
        g_in_poll = 0;
    }

    /* Handle Cmd-Q for quit */
    if (theEvent->what == keyDown) {
        char c = theEvent->message & charCodeMask;
        if ((theEvent->modifiers & cmdKey) && (c == 'q' || c == 'Q')) {
            g_running = 0;
            return true;  /* Consume the event */
        }
    }

    return false;  /* Let SIOW handle the event normally */
}

/*============================================================================
 * PeerTalk Callbacks
 *============================================================================*/

static void on_peer_discovered(PeerTalk_Context *ctx,
                                const PeerTalk_PeerInfo *peer,
                                void *user_data) {
    /* Use name accessor - peer->name_idx references cold storage */
    const char *name = PeerTalk_GetPeerName(ctx, peer->name_idx);
    printf("\n+ DISCOVERED: %s (ID %u)\n", name, peer->id);
    printf("> ");
    fflush(stdout);
}

static void on_peer_lost(PeerTalk_Context *ctx,
                         PeerTalk_PeerID peer_id,
                         void *user_data) {
    printf("\n- LOST: Peer %u\n", peer_id);
    printf("> ");
    fflush(stdout);

    if (g_selected_peer == (int)peer_id) {
        g_selected_peer = -1;
    }
}

static void on_peer_connected(PeerTalk_Context *ctx,
                              PeerTalk_PeerID peer_id,
                              void *user_data) {
    printf("\n* CONNECTED to peer %u\n", peer_id);
    printf("> ");
    fflush(stdout);

    if (g_selected_peer < 0) {
        g_selected_peer = peer_id;
    }
}

static void on_peer_disconnected(PeerTalk_Context *ctx,
                                  PeerTalk_PeerID peer_id,
                                  PeerTalk_Error reason,
                                  void *user_data) {
    printf("\n* DISCONNECTED from peer %u (reason: %d)\n",
           peer_id, (int)reason);
    printf("> ");
    fflush(stdout);

    if (g_selected_peer == (int)peer_id) {
        g_selected_peer = -1;
    }
}

static void on_message_received(PeerTalk_Context *ctx,
                                 PeerTalk_PeerID from_peer,
                                 const void *data,
                                 uint16_t length,
                                 void *user_data) {
    printf("\n< [%u]: %.*s\n", from_peer, (int)length, (const char *)data);
    printf("> ");
    fflush(stdout);
}

/*============================================================================
 * Command Handlers
 *============================================================================*/

static void cmd_list(void) {
    uint16_t count;
    int i;

    PeerTalk_GetPeers(g_ctx, g_peers, 8, &count);
    g_peer_count = count;

    printf("\n=== Discovered Peers (%d) ===\n", g_peer_count);
    for (i = 0; i < g_peer_count; i++) {
        char state = g_peers[i].connected ? 'C' : 'D';
        const char *name = PeerTalk_GetPeerName(g_ctx, g_peers[i].name_idx);
        printf("  [%c] ID %u: %s\n", state, g_peers[i].id, name);
    }
    printf("=============================\n");
}

static void cmd_connect(int peer_id) {
    PeerTalk_Error err;

    printf("Connecting to peer %d...\n", peer_id);
    err = PeerTalk_Connect(g_ctx, peer_id);

    if (err != PT_OK) {
        printf("Connect failed: %s\n", PeerTalk_ErrorString(err));
    } else {
        g_selected_peer = peer_id;
    }
}

static void cmd_disconnect(void) {
    if (g_selected_peer > 0) {
        printf("Disconnecting from peer %d...\n", g_selected_peer);
        PeerTalk_Disconnect(g_ctx, g_selected_peer);
        g_selected_peer = -1;
    } else {
        printf("Not connected to any peer.\n");
    }
}

static void cmd_send(const char *message) {
    PeerTalk_Error err;

    if (g_selected_peer <= 0) {
        printf("Not connected. Use C <id> to connect first.\n");
        return;
    }

    err = PeerTalk_Send(g_ctx, g_selected_peer, message, strlen(message) + 1);
    if (err == PT_OK) {
        printf("> [you]: %s\n", message);
    } else {
        printf("Send failed: %s\n", PeerTalk_ErrorString(err));
    }
}

static void cmd_help(void) {
    printf("\n=== Commands ===\n");
    printf("  L        - List discovered peers\n");
    printf("  C <id>   - Connect to peer by ID\n");
    printf("  D        - Disconnect\n");
    printf("  S <msg>  - Send message\n");
    printf("  H        - This help\n");
    printf("  Q        - Quit (or Cmd-Q)\n");
    printf("================\n");
}

/*============================================================================
 * Input Reading (uses SIOW's blocking I/O)
 *
 * fgets() triggers SIOW's internal event loop. Our hook polls PeerTalk
 * during this time, so network events are processed while waiting for input.
 *============================================================================*/

static int read_line(char *buf, int max_len) {
    if (fgets(buf, max_len, stdin)) {
        /* Remove trailing newline */
        int len = strlen(buf);
        if (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[--len] = '\0';
        }
        return len;
    }
    return 0;
}

/*============================================================================
 * Command Processing
 *============================================================================*/

static void process_command(const char *input) {
    char cmd;
    int id;

    if (input[0] == '\0') {
        return;  /* Empty input */
    }

    cmd = input[0];
    if (cmd >= 'a' && cmd <= 'z') {
        cmd -= 32;  /* Convert to uppercase */
    }

    switch (cmd) {
    case 'Q':
        g_running = 0;
        break;

    case 'L':
        cmd_list();
        break;

    case 'H':
    case '?':
        cmd_help();
        break;

    case 'C':
        if (input[1] == ' ' || input[1] == '\t') {
            if (sscanf(input + 2, "%d", &id) == 1) {
                cmd_connect(id);
            } else {
                printf("Usage: C <peer_id>\n");
            }
        } else {
            printf("Usage: C <peer_id>\n");
        }
        break;

    case 'D':
        cmd_disconnect();
        break;

    case 'S':
        if (input[1] == ' ') {
            cmd_send(input + 2);
        } else {
            printf("Usage: S <message>\n");
        }
        break;

    default:
        printf("Unknown command '%c'. Type H for help.\n", cmd);
        break;
    }
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
    PeerTalk_Config config = {0};
    PeerTalk_Callbacks callbacks = {0};
    char input[256];

    printf("PeerTalk Chat for Classic Mac\n");
    printf("=============================\n\n");

    /*========================================================================
     * Initialize PT_Log BEFORE SIOW hook
     * Classic Mac: Use file logging since there's no console
     *========================================================================*/
    g_log = PT_LogCreate();
    if (g_log) {
        PT_LogSetFile(g_log, ":chat_mac.log");  /* Mac path with : prefix */
        PT_LogSetLevel(g_log, PT_LOG_INFO);
        PT_LogSetOutput(g_log, PT_LOG_OUT_FILE);
        PT_LogSetAutoFlush(g_log, true);  /* Crash-safe - important for 68k debugging */
        PT_INFO(g_log, LOG_UI, "Chat starting");
    }

    /* Install SIOW event hook BEFORE any I/O
     * This allows PeerTalk polling during SIOW's event loop */
    __siowEventHook = siow_event_hook;

    /* Initialize PeerTalk with shared log */
    config.local_name = "MacChat";
    config.max_peers = 8;
    config.log = g_log;  /* Share log with SDK */

    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_lost = on_peer_lost;
    callbacks.on_peer_connected = on_peer_connected;
    callbacks.on_peer_disconnected = on_peer_disconnected;
    callbacks.on_message_received = on_message_received;

    g_ctx = PeerTalk_Init(&config);
    if (!g_ctx) {
        PT_ERR(g_log, LOG_NET, "Failed to initialize PeerTalk");
        printf("ERROR: Failed to initialize PeerTalk\n");
        printf("Check that MacTCP or Open Transport is installed.\n");
        if (g_log) PT_LogDestroy(g_log);
        return 1;
    }
    PT_INFO(g_log, LOG_NET, "PeerTalk initialized");

    PeerTalk_SetCallbacks(g_ctx, &callbacks);

    /* Start discovery */
    printf("Starting peer discovery...\n");
    PT_INFO(g_log, LOG_NET, "Starting peer discovery");
    PeerTalk_StartDiscovery(g_ctx);

    cmd_help();

    /* Main loop - simple and clean
     * SIOW handles the event loop during fgets(), our hook polls PeerTalk */
    while (g_running) {
        printf("> ");
        fflush(stdout);

        if (read_line(input, sizeof(input)) > 0) {
            process_command(input);
        }

        /* Also poll here in case fgets returns immediately (e.g., EOF) */
        PeerTalk_Poll(g_ctx);
    }

    /* Cleanup */
    printf("\nShutting down...\n");
    PT_INFO(g_log, LOG_UI, "Chat shutting down");
    PeerTalk_StopDiscovery(g_ctx);
    PeerTalk_Shutdown(g_ctx);

    /* Clear the hook before exit */
    __siowEventHook = NULL;

    PT_INFO(g_log, LOG_UI, "Chat shutdown complete");
    if (g_log) PT_LogDestroy(g_log);

    printf("Goodbye!\n");
    return 0;
}
```

#### Task 8.2.2: Add to Retro68 Makefile

```makefile
# Add to Makefile.retro68

chat_mac: examples/chat_mac.c $(LIBNAME)
	$(CC) $(CFLAGS) -o $@ $< -L. -lpeertalk_$(PLATFORM)
```

### Acceptance Criteria
1. Application compiles with Retro68 (links against SIOW)
2. Runs on real Mac hardware (68k with MacTCP or PPC with Open Transport)
3. SIOW event hook installs correctly (`__siowEventHook` not NULL)
4. Peer discovery works (sees POSIX peers on the network)
5. Can connect to POSIX peer
6. Messages send and receive correctly
7. Cmd-Q quits the application
8. Network events fire during input wait (via SIOW hook)
9. Clean shutdown (hook cleared before exit)
10. **PT_Log Integration:**
    - [ ] PT_Log context created at startup
    - [ ] Log file `:chat_mac.log` created (Mac path)
    - [ ] Auto-flush enabled (crash-safe debugging on 68k)
    - [ ] Key operations logged (init, connect, disconnect)
    - [ ] Log destroyed at shutdown

---

## Session 8.3: Multi-Transport OT Chat (Gateway Mode)

> **SDK Dependency:** This session requires `libpeertalk_ot_at.a` from Phase 6 Sessions 6.7-6.10.
> If Phase 6 multi-transport sessions are not complete, this session is **BLOCKED**.
>
> **Required SDK APIs (must be defined in Phase 6.7-6.10):**
> - `PeerTalk_ConnectVia(ctx, peer_id, transport_flags)` - Connect using specific transport
> - `PeerTalk_GetGatewayStats(ctx, stats_out)` - Get gateway relay statistics
> - `PeerTalk_SetGatewayMode(ctx, enabled)` - Enable/disable message relay
> - `PeerTalk_GatewayStats` struct - Statistics structure

### Objective
Create an Open Transport chat application that runs **both TCP/IP and AppleTalk (ADSP)** simultaneously, acting as a gateway between the two networks.

### Prerequisites
- Mac with Open Transport 1.1.1+ (System 7.6.1+, Mac OS 8/9)
- Ethernet (for TCP/IP) AND either LocalTalk or EtherTalk (for AppleTalk)
- Or: EtherTalk Phase 2 which carries both TCP/IP and AppleTalk over Ethernet

### Design Notes

**Open Transport's Unified API:**

The key insight from `NetworkingOpenTransport.txt` is that OT provides the **same API** for both TCP/IP and AppleTalk protocols. The only difference is the configuration string and address structures:

```c
/* TCP endpoint */
EndpointRef tcp_ep = OTOpenEndpoint(OTCreateConfiguration("tcp"), 0, NULL, &err);

/* ADSP endpoint - SAME API! */
EndpointRef adsp_ep = OTOpenEndpoint(OTCreateConfiguration("adsp"), 0, NULL, &err);
```

**NBP for AppleTalk Discovery:**

Unlike TCP/IP which uses UDP broadcast, AppleTalk uses NBP (Name Binding Protocol):

```c
/* Register our name */
OTRegisterName(mapper, &req, &result);  /* "ChatApp:PeerTalk@*" */

/* Lookup other peers */
OTLookupName(mapper, &req, &result);    /* "=:PeerTalk@*" */
```

**Gateway Message Flow:**

```
   POSIX Peer                OT Gateway Mac              AppleTalk-Only Mac
       │                          │                            │
       │   TCP: "Hello Alice"     │                            │
       │ ────────────────────────►│                            │
       │                          │   ADSP: "Hello Alice"      │
       │                          │ ───────────────────────────►
       │                          │                            │
       │                          │   ADSP: "Hi Bob!"          │
       │                          │◄───────────────────────────
       │   TCP: "Hi Bob!"         │                            │
       │◄────────────────────────│                            │
       │                          │                            │
```

### SDK Prerequisite

**Important:** This session requires the multi-transport SDK implemented in **Phase 6 Sessions 6.7-6.10**:

| Phase 6 Session | Deliverable | Description |
|-----------------|-------------|-------------|
| 6.7 | `src/opentransport/ot_multi.h` | Multi-transport types, transport flags, endpoint wrappers |
| 6.8 | `src/opentransport/ot_adsp.c`, `ot_nbp.c` | ADSP endpoints + NBP discovery via OT unified API |
| 6.9 | `Makefile.retro68` update | Unified library build (`libpeertalk_ot_at.a`) |
| 6.10 | `src/opentransport/ot_multi.c` | Multi-transport poll integration, peer deduplication |

The SDK provides:
- `PT_TRANSPORT_*` flags for transport selection
- Unified `PeerTalk_Poll()` that handles all enabled transports
- Automatic peer deduplication when same peer appears on multiple transports
- Gateway relay logic (SDK handles routing, not the application)
- `libpeertalk_ot_at.a` library with full multi-transport support

### Tasks

#### Task 8.3.1: Create `examples/chat_mac_gateway.c`

This example demonstrates how simple multi-transport becomes with proper SDK design. The application code is minimal - all complexity is handled by the SDK.

```c
/*
 * PeerTalk Example: Multi-Transport Gateway Chat
 *
 * This example demonstrates:
 * - Running TCP/IP AND AppleTalk simultaneously via OT
 * - Acting as a gateway between networks
 * - NBP discovery for AppleTalk peers
 * - UDP broadcast for TCP/IP peers
 * - Message relay between transports (handled by SDK)
 *
 * IMPORTANT: This example requires libpeertalk_ot_at.a which provides
 * multi-transport support. See Phase 6 Sessions 6.7-6.10 for SDK implementation.
 *
 * Commands:
 *   L        - List all discovered peers (both transports)
 *   C <id>   - Connect to peer by ID
 *   D        - Disconnect from current peer
 *   S <msg>  - Send message (relays if gateway mode)
 *   G        - Toggle gateway mode
 *   T        - Show transport status
 *   H        - Help
 *   Q        - Quit
 */

#include <stdio.h>
#include <string.h>
#include <Events.h>
#include <Gestalt.h>
#include "peertalk.h"

/*============================================================================
 * SIOW Event Hook
 *============================================================================*/

extern Boolean (*__siowEventHook)(EventRecord *theEvent);

/*============================================================================
 * Application State
 *============================================================================*/

static PeerTalk_Context *g_ctx = NULL;
static PeerTalk_PeerInfo g_peers[16];
static int g_peer_count = 0;
static int g_selected_peer = -1;
static int g_running = 1;
static int g_gateway_mode = 1;  /* Gateway enabled by default */
static int g_in_poll = 0;

/*============================================================================
 * Helper: Get transport name
 *============================================================================*/

static const char *transport_name(uint32_t transport) {
    if (transport & PT_TRANSPORT_TCP) return "TCP/IP";
    if (transport & PT_TRANSPORT_ADSP) return "ADSP";
    return "Unknown";
}

/*============================================================================
 * SIOW Event Hook
 *============================================================================*/

static Boolean siow_event_hook(EventRecord *theEvent) {
    if (g_in_poll) return false;

    if (g_ctx) {
        g_in_poll = 1;
        PeerTalk_Poll(g_ctx);  /* SDK polls ALL enabled transports */
        g_in_poll = 0;
    }

    /* Cmd-Q to quit */
    if (theEvent->what == keyDown) {
        char c = theEvent->message & charCodeMask;
        if ((theEvent->modifiers & cmdKey) && (c == 'q' || c == 'Q')) {
            g_running = 0;
            return true;
        }
    }

    return false;
}

/*============================================================================
 * Callbacks - Display Only, SDK Handles Everything
 *============================================================================*/

static void on_peer_discovered(PeerTalk_Context *ctx,
                                const PeerTalk_PeerInfo *peer,
                                void *user_data) {
    /* SDK already handled deduplication - just display */
    /* Use name accessor - peer->name_idx references cold storage */
    const char *name = PeerTalk_GetPeerName(ctx, peer->name_idx);
    printf("\n+ DISCOVERED [%s]: %s (ID %u)\n",
           transport_name(peer->transport), name, peer->id);
    printf("> ");
    fflush(stdout);
}

static void on_peer_lost(PeerTalk_Context *ctx,
                         PeerTalk_PeerID peer_id,
                         void *user_data) {
    printf("\n- LOST: Peer %u\n", peer_id);
    printf("> ");
    fflush(stdout);

    if (g_selected_peer == (int)peer_id) {
        g_selected_peer = -1;
    }
}

static void on_peer_connected(PeerTalk_Context *ctx,
                              PeerTalk_PeerID peer_id,
                              void *user_data) {
    printf("\n* CONNECTED to peer %u\n", peer_id);
    printf("> ");
    fflush(stdout);

    if (g_selected_peer < 0) {
        g_selected_peer = peer_id;
    }
}

static void on_peer_disconnected(PeerTalk_Context *ctx,
                                  PeerTalk_PeerID peer_id,
                                  PeerTalk_Error reason,
                                  void *user_data) {
    printf("\n* DISCONNECTED from peer %u\n", peer_id);
    printf("> ");
    fflush(stdout);

    if (g_selected_peer == (int)peer_id) {
        g_selected_peer = -1;
    }
}

static void on_message_received(PeerTalk_Context *ctx,
                                 PeerTalk_PeerID from_peer,
                                 const void *data,
                                 uint16_t length,
                                 void *user_data) {
    /* Get peer info to show transport */
    int i;
    uint32_t from_transport = 0;
    /* Use O(1) lookup - required for 68k performance */
    const PeerTalk_PeerInfo *peer = PeerTalk_GetPeerByID(ctx, from_peer);
    const char *from_name = peer ? PeerTalk_GetPeerName(ctx, peer->name_idx) : "Unknown";
    uint32_t from_transport = peer ? peer->transports_available : 0;

    printf("\n< [%s] %s: %.*s\n",
           transport_name(from_transport),
           from_name,
           (int)length, (const char *)data);
    printf("> ");
    fflush(stdout);

    /* NOTE: Gateway relay happens automatically in SDK's PeerTalk_Poll()
     * if config.enable_gateway was set to true at init time.
     * The application does NOT need to implement relay logic! */
}

/*============================================================================
 * Commands
 *============================================================================*/

static void cmd_list(void) {
    uint16_t count;
    int i;

    PeerTalk_GetPeers(g_ctx, g_peers, 16, &count);
    g_peer_count = count;

    printf("\n=== Discovered Peers (%d) ===\n", g_peer_count);
    printf("  Transport  ID  State  Name\n");
    printf("  ---------  --  -----  ----\n");

    for (i = 0; i < g_peer_count; i++) {
        char state = g_peers[i].connected ? 'C' : 'D';
        const char *name = PeerTalk_GetPeerName(g_ctx, g_peers[i].name_idx);
        printf("  %-9s  %2u  [%c]    %s\n",
               transport_name(g_peers[i].transports_available),
               g_peers[i].id, state, name);
    }

    printf("==============================\n");
}

static void cmd_transport_status(void) {
    PeerTalk_GatewayStats stats;
    PeerTalk_GetGatewayStats(g_ctx, &stats);

    printf("\n=== Transport Status ===\n");
    printf("  TCP/IP enabled:   YES\n");
    printf("  AppleTalk enabled: YES\n");
    printf("  Gateway mode:     %s\n", g_gateway_mode ? "ON" : "OFF");
    printf("  Messages relayed: %u\n", stats.messages_relayed);
    printf("========================\n");
}

static void cmd_toggle_gateway(void) {
    g_gateway_mode = !g_gateway_mode;
    PeerTalk_SetGatewayMode(g_ctx, g_gateway_mode);
    printf("Gateway mode: %s\n", g_gateway_mode ? "ON" : "OFF");
}

static void cmd_connect(int peer_id) {
    PeerTalk_Error err;

    printf("Connecting to peer %d...\n", peer_id);
    /* SDK automatically selects best transport */
    err = PeerTalk_Connect(g_ctx, peer_id);

    if (err != PT_OK) {
        printf("Connect failed: %s\n", PeerTalk_ErrorString(err));
    } else {
        g_selected_peer = peer_id;
    }
}

static void cmd_disconnect(void) {
    if (g_selected_peer > 0) {
        printf("Disconnecting from peer %d...\n", g_selected_peer);
        PeerTalk_Disconnect(g_ctx, g_selected_peer);
        g_selected_peer = -1;
    } else {
        printf("Not connected to any peer.\n");
    }
}

static void cmd_send(const char *message) {
    PeerTalk_Error err;

    if (g_selected_peer <= 0) {
        printf("Not connected. Use C <id> to connect first.\n");
        return;
    }

    /* SDK routes to correct transport automatically */
    err = PeerTalk_Send(g_ctx, g_selected_peer, message, strlen(message) + 1);
    if (err == PT_OK) {
        printf("> [you]: %s\n", message);
    } else {
        printf("Send failed: %s\n", PeerTalk_ErrorString(err));
    }
}

static void cmd_help(void) {
    printf("\n=== Multi-Transport Gateway Chat ===\n");
    printf("  L        - List all peers (both transports)\n");
    printf("  C <id>   - Connect to peer by ID\n");
    printf("  D        - Disconnect\n");
    printf("  S <msg>  - Send message\n");
    printf("  G        - Toggle gateway mode (relay messages)\n");
    printf("  T        - Show transport status\n");
    printf("  H        - This help\n");
    printf("  Q        - Quit\n");
    printf("====================================\n");
}

/*============================================================================
 * Input Handling
 *============================================================================*/

static int read_line(char *buf, int max_len) {
    if (fgets(buf, max_len, stdin)) {
        int len = strlen(buf);
        if (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[--len] = '\0';
        }
        return len;
    }
    return 0;
}

static void process_command(const char *input) {
    char cmd;
    int id;

    if (input[0] == '\0') return;

    cmd = input[0];
    if (cmd >= 'a' && cmd <= 'z') {
        cmd -= 32;
    }

    switch (cmd) {
    case 'Q': g_running = 0; break;
    case 'L': cmd_list(); break;
    case 'H':
    case '?': cmd_help(); break;
    case 'T': cmd_transport_status(); break;
    case 'G': cmd_toggle_gateway(); break;
    case 'C':
        if (sscanf(input + 2, "%d", &id) == 1) {
            cmd_connect(id);
        } else {
            printf("Usage: C <peer_id>\n");
        }
        break;
    case 'D': cmd_disconnect(); break;
    case 'S':
        if (input[1] == ' ') {
            cmd_send(input + 2);
        } else {
            printf("Usage: S <message>\n");
        }
        break;
    default:
        printf("Unknown command '%c'. Type H for help.\n", cmd);
        break;
    }
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
    PeerTalk_Config config = {0};
    PeerTalk_Callbacks callbacks = {0};
    char input[256];
    long gestalt_response;
    OSErr err;

    printf("PeerTalk Multi-Transport Gateway Chat\n");
    printf("======================================\n\n");

    /* Check for Open Transport */
    err = Gestalt(gestaltOpenTpt, &gestalt_response);
    if (err != noErr || !(gestalt_response & gestaltOpenTptPresentMask)) {
        printf("ERROR: Open Transport not available.\n");
        printf("This application requires System 7.6.1+ with OT.\n");
        return 1;
    }

    printf("Open Transport detected.\n");

    /* Install SIOW hook */
    __siowEventHook = siow_event_hook;

    /*
     * Configure for multi-transport - ALL complexity hidden in SDK!
     * Just set config flags and the SDK handles everything.
     */
    config.local_name = "GatewayMac";
    config.max_peers = 16;
    config.transports = PT_TRANSPORT_ALL;  /* TCP + UDP + ADSP + NBP */
    config.enable_gateway = true;          /* Enable relay */

    /* Set NBP registration info */
    strcpy(config.nbp_type, "PeerTalk");
    strcpy(config.nbp_zone, "*");  /* Local zone */

    /* Set callbacks */
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_lost = on_peer_lost;
    callbacks.on_peer_connected = on_peer_connected;
    callbacks.on_peer_disconnected = on_peer_disconnected;
    callbacks.on_message_received = on_message_received;

    /* Initialize PeerTalk with multi-transport support */
    g_ctx = PeerTalk_Init(&config);
    if (!g_ctx) {
        printf("ERROR: Failed to initialize PeerTalk\n");
        return 1;
    }

    PeerTalk_SetCallbacks(g_ctx, &callbacks);

    /* Start discovery on BOTH transports - SDK handles this */
    printf("Starting discovery (TCP/IP + AppleTalk)...\n");
    PeerTalk_StartDiscovery(g_ctx);

    cmd_help();

    /* Main loop - SDK handles everything */
    while (g_running) {
        printf("> ");
        fflush(stdout);

        if (read_line(input, sizeof(input)) > 0) {
            process_command(input);
        }

        PeerTalk_Poll(g_ctx);  /* Polls TCP, ADSP, handles gateway relay */
    }

    /* Cleanup */
    printf("\nShutting down...\n");
    PeerTalk_StopDiscovery(g_ctx);
    PeerTalk_Shutdown(g_ctx);

    __siowEventHook = NULL;

    printf("Goodbye!\n");
    return 0;
}
```

#### Task 8.3.2: Add to Retro68 Makefile

```makefile
# Multi-transport gateway requires libpeertalk_ot_at.a
chat_mac_gateway: examples/chat_mac_gateway.c $(LIB_OT_AT)
	$(CC) $(CFLAGS) -o $@ $< -L. -lpeertalk_ot_at
```

### Acceptance Criteria

1. Application compiles with Retro68 for PPC
2. Runs on Mac OS 7.6.1+ with Open Transport
3. Discovers TCP/IP peers via UDP broadcast
4. Discovers AppleTalk peers via NBP lookup
5. Peer list shows transport type for each peer
6. Can connect to TCP/IP peers
7. Can connect to AppleTalk (ADSP) peers
8. Messages send/receive on both transports
9. Gateway mode relays messages between transports
10. POSIX peer can send message that reaches AppleTalk-only Mac via gateway
11. Clean shutdown
12. OTBind completes successfully (T_BINDCOMPLETE or sync mode)
13. NBP lookup correctly parses variable-length entity names

**Note:** SDK acceptance criteria (ADSP endpoint creation, NBP registration, etc.) are verified in Phase 6 Sessions 6.7-6.10, not here. This session tests the example application, not the SDK internals.

---

## Session 8.4: Cross-Network Integration Testing

### Objective
Verify complete cross-platform communication including the multi-transport gateway.

### Test Hardware Required

| Machine | System | Network | Role |
|---------|--------|---------|------|
| Linux PC | Ubuntu | Ethernet | POSIX peer |
| Power Mac 7500 | Mac OS 8.6 | Ethernet | Gateway (OT + AppleTalk) |
| Mac IIci | System 7.1 | Ethernet | MacTCP + AppleTalk peer |
| Mac SE | System 6.0.8 | LocalTalk | AppleTalk-only peer |

**Network Configuration:**
```
                    Ethernet Hub/Switch
                           │
         ┌─────────────────┼─────────────────┐
         │                 │                 │
    Linux PC         Power Mac 7500     Mac IIci
    (POSIX)          (OT Gateway)       (MacTCP+AT)
                           │
                    LocalTalk Bridge
                           │
                        Mac SE
                    (AppleTalk only)
```

### Tasks

#### Task 8.4.1: Basic Connectivity Tests

| Test | Steps | Expected Result |
|------|-------|-----------------|
| T1: POSIX ↔ Gateway TCP | 1. Start chat on Linux<br>2. Start gateway chat on Power Mac<br>3. Connect and send message | Messages flow via TCP |
| T2: Gateway ↔ MacTCP | 1. Start chat on Mac IIci<br>2. Connect from Gateway<br>3. Exchange messages | Messages flow via TCP |
| T3: Gateway ↔ LocalTalk | 1. Start chat on Mac SE<br>2. NBP lookup from Gateway<br>3. Connect via ADSP | Messages flow via ADSP |

#### Task 8.4.2: Gateway Relay Tests

| Test | Steps | Expected Result |
|------|-------|-----------------|
| T4: POSIX → AppleTalk | 1. All apps running<br>2. POSIX sends "Hello AT"<br>3. Gateway relays to Mac SE | Mac SE receives "[Linux via gateway] Hello AT" |
| T5: AppleTalk → POSIX | 1. Mac SE sends "Hello TCP"<br>2. Gateway relays to Linux | Linux receives "[Mac SE via gateway] Hello TCP" |
| T6: Multi-hop | 1. POSIX sends to Gateway<br>2. Gateway has ADSP to Mac SE and MacTCP to IIci<br>3. All receive | All peers see message |

#### Task 8.4.3: Discovery Tests

| Test | Steps | Expected Result |
|------|-------|-----------------|
| T7: NBP registration | 1. Start Gateway chat<br>2. On Mac SE: run Chooser or NBP lookup tool | Gateway appears as "GatewayMac:PeerTalk@*" |
| T8: UDP discovery | 1. Start Gateway chat<br>2. Start POSIX chat | Both see each other via UDP broadcast |
| T9: Dual discovery | 1. Gateway discovers Mac SE (NBP)<br>2. Gateway discovers Linux (UDP)<br>3. Check peer list | Both appear with correct transport types |

#### Task 8.4.4: Stress Tests

| Test | Steps | Expected Result |
|------|-------|-----------------|
| T10: Rapid messages | Send 100 messages in quick succession | All delivered, no crashes |
| T11: Connect/disconnect | Connect/disconnect 20 times | No memory leaks (MaxBlock same) |
| T12: Multi-peer | Connect to 4 peers simultaneously | All connections stable |

#### Task 8.4.5: Create `examples/minimal_example.c`

A minimal working PeerTalk program demonstrating the simplest possible usage pattern. This serves as a copy-paste starting point for developers embedding PeerTalk.

```c
/*
 * PeerTalk Minimal Example
 *
 * Demonstrates:
 * - Basic PeerTalk initialization
 * - Simple callback pattern (ISR-safe on all platforms)
 * - Main loop integration with PeerTalk_Poll()
 * - Clean shutdown
 *
 * Works identically on POSIX and Classic Mac.
 * Expected size: 50-100 lines
 */

#include <stdio.h>
#include "peertalk.h"
#include "pt_log.h"

static PeerTalk_Context *ctx = NULL;
static PT_Log *log = NULL;
static int running = 1;

/* Simple callback - safe on all platforms including Classic Mac */
static void on_peer_discovered(PeerTalk_Context *ctx,
                                const PeerTalk_PeerInfo *peer,
                                void *user_data) {
    /* Callbacks run from PeerTalk_Poll(), NOT interrupt level */
    /* Use name accessor - peer->name_idx references cold storage */
    const char *name = PeerTalk_GetPeerName(ctx, peer->name_idx);
    printf("Discovered: %s (ID %u)\n", name, peer->id);
    PT_INFO(log, PT_LOG_CAT_NETWORK, "Peer discovered: %s", name);
}

static void on_message_received(PeerTalk_Context *ctx,
                                 PeerTalk_PeerID from,
                                 const void *data,
                                 uint16_t len,
                                 void *user_data) {
    printf("Message from %u: %.*s\n", from, (int)len, (const char *)data);
}

int main(void) {
    PeerTalk_Config config = {0};
    PeerTalk_Callbacks callbacks = {0};

    /* Initialize logging */
    log = PT_LogCreate();
    PT_LogSetLevel(log, PT_LOG_INFO);

    /* Initialize PeerTalk */
    config.local_name = "MinimalExample";
    config.max_peers = 8;
    config.log = log;

    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_message_received = on_message_received;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        PT_ERR(log, PT_LOG_CAT_NETWORK, "Failed to initialize");
        return 1;
    }
    PeerTalk_SetCallbacks(ctx, &callbacks);
    PeerTalk_StartDiscovery(ctx);

    printf("PeerTalk running. Press Ctrl-C to quit.\n");

    /* Main loop - call Poll regularly */
    while (running) {
        PeerTalk_Poll(ctx);

        /* Platform-specific delay */
#ifdef PT_PLATFORM_POSIX
        usleep(50000);  /* 50ms */
#else
        /* On Classic Mac, yield to system */
        EventRecord event;
        WaitNextEvent(everyEvent, &event, 3, NULL);
#endif
    }

    /* Cleanup */
    PeerTalk_StopDiscovery(ctx);
    PeerTalk_Shutdown(ctx);
    PT_LogDestroy(log);

    return 0;
}
```

**Acceptance Criteria:**
- [ ] Compiles on POSIX with zero warnings
- [ ] Compiles with Retro68 for Classic Mac
- [ ] Demonstrates correct ISR-safe callback pattern
- [ ] Uses PT_Log for unified logging
- [ ] Under 100 lines of code

#### Task 8.4.6: Document Cross-Platform Test Protocol

Create `notes/TESTING.md` with the manual test procedure:

```markdown
# PeerTalk Cross-Platform Testing Protocol

## Test Setup

| Machine | Role | System | Network |
|---------|------|--------|---------|
| Linux PC | POSIX peer | Ubuntu | Ethernet |
| Mac (any) | Classic Mac peer | System 6.0.8+ | Ethernet |

## Test Procedure

### Phase 1: Discovery (10 seconds max)
1. Start chat_posix on Linux with name "Linux"
2. Start chat_mac on Mac with name "Mac"
3. [ ] Linux sees Mac appear in peer list
4. [ ] Mac sees Linux appear in peer list

### Phase 2: Connection (5 seconds max)
5. On Linux: `/connect <mac_id>`
6. [ ] Linux shows "Connected" status
7. [ ] Mac shows "Connected" notification

### Phase 3: Messaging
8. On Linux: send "Hello from Linux"
9. [ ] Mac receives and displays the message
10. On Mac: `S Hello from Mac`
11. [ ] Linux receives and displays the message

### Phase 4: Disconnect
12. On Linux: `/disconnect`
13. [ ] Both show disconnected status

### Phase 5: Reverse Direction
14. On Mac: `C <linux_id>`
15. [ ] Connection succeeds
16. Exchange messages in both directions

## Success Criteria

All 16 steps complete without:
- Crashes
- Hangs
- Message corruption
- Memory leaks (MaxBlock same before/after)
```

### Acceptance Criteria

1. All basic connectivity tests pass
2. Gateway relay works in both directions
3. NBP and UDP discovery coexist
4. No crashes during stress tests
5. MaxBlock same before/after 50+ operations
6. All messages delivered correctly (no corruption)

---

## Cross-Platform Testing Procedure

Once both applications are complete, verify cross-platform communication:

### Test 1: POSIX discovers Mac
1. Start `chat_posix` on Linux/macOS
2. Start `chat_mac` on real Mac
3. Verify POSIX sees Mac peer appear
4. Verify Mac sees POSIX peer appear

### Test 2: POSIX connects to Mac
1. On POSIX: `/connect <mac_peer_id>`
2. Verify connection succeeds on both sides

### Test 3: Bidirectional messaging
1. On POSIX: send a message
2. Verify Mac receives it
3. On Mac: `S Hello back!`
4. Verify POSIX receives it

### Test 4: Mac connects to POSIX
1. Restart both applications
2. On Mac: `C <posix_peer_id>`
3. Verify connection succeeds
4. Exchange messages in both directions

---

## Phase 8 Complete Checklist

### Session 8.1: POSIX Chat
- [ ] POSIX chat application compiles
- [ ] POSIX chat shows discovered peers
- [ ] POSIX chat connects to peers
- [ ] POSIX chat sends/receives messages
- [ ] **PT_Log:** Log file created, callbacks use PT_INFO/DEBUG/ERR

### Session 8.2: Classic Mac Chat (Single Transport)
- [ ] Mac chat application compiles with Retro68 (links SIOW)
- [ ] Mac chat runs on real hardware
- [ ] SIOW event hook works (network events during input wait)
- [ ] Mac chat discovers peers
- [ ] **PT_Log:** `:chat_mac.log` created, auto-flush enabled
- [ ] Mac chat connects to peers
- [ ] Mac chat sends/receives messages
- [ ] Cross-platform: POSIX ↔ Mac communication works

### Session 8.3: Multi-Transport Gateway Chat ⛔ BLOCKED
> **Status:** BLOCKED on Phase 6.8-6.10 and Phase 7 completion

- [ ] Gateway chat compiles for PPC (Open Transport)
- [ ] TCP/IP connections work (via SDK from Phase 6.8-6.10)
- [ ] ADSP connections work (via SDK from Phase 6.8-6.10)
- [ ] Peer list shows correct transport type
- [ ] Messages flow on both transports
- [ ] Gateway relay: TCP → ADSP works
- [ ] Gateway relay: ADSP → TCP works
- [ ] Clean shutdown
- [ ] **PT_Log:** Transport-specific logging with PT_LOG_CAT_PLATFORM

### Session 8.4: Cross-Network Integration
- [ ] POSIX ↔ Gateway (TCP) works
- [ ] Gateway ↔ MacTCP Mac works
- [ ] Gateway ↔ AppleTalk-only Mac (ADSP) works
- [ ] Full relay: POSIX → Gateway → AppleTalk-only Mac
- [ ] Full relay: AppleTalk-only Mac → Gateway → POSIX
- [ ] Stress test: 100 rapid messages succeed
- [ ] Stress test: 20 connect/disconnect cycles succeed
- [ ] Memory leak check: MaxBlock same before/after
- [ ] Multi-peer: 4 simultaneous connections stable
- [ ] **NEW:** `examples/minimal_example.c` compiles on POSIX and Retro68
- [ ] **NEW:** `notes/TESTING.md` documents cross-platform test protocol

---

## SDK vs Example Application Architecture

**CRITICAL:** The multi-transport support MUST be in the SDK layer, not just the example application. The example app should only contain UI and command handling.

**Implementation Note:** The multi-transport SDK is implemented in **Phase 6 Sessions 6.7-6.10**, not Phase 8. Phase 8 only provides the example application that uses the SDK.

### What Goes in the SDK (`libpeertalk_ot_at.a`)

| Component | Location | Responsibility |
|-----------|----------|----------------|
| `pt_ot_multi.h` | `src/opentransport/` | Multi-transport types and API |
| `pt_ot_multi.c` | `src/opentransport/` | Context init, poll loop integration |
| `pt_ot_adsp.c` | `src/opentransport/` | ADSP endpoint management via OT |
| `pt_ot_nbp.c` | `src/opentransport/` | NBP mapper for discovery |
| Extended `peertalk.h` | `include/` | Public API with transport flags |

**Note:** See **Phase 6 Sessions 6.7-6.10** for the complete SDK implementation. The code is not duplicated here - Phase 8 focuses on the example application that uses the SDK.

### What Goes in the Example App (`chat_mac_gateway.c`)

| Component | Location | Responsibility |
|-----------|----------|----------------|
| `chat_mac_gateway.c` | `examples/` | UI, commands, display only |

**Example App Pattern:**

The example app (already provided in Task 8.3.1 above) demonstrates how simple multi-transport becomes with the SDK:
- Callbacks just update the UI - SDK handles all networking complexity
- Commands just call SDK functions - no transport-specific code needed
- Gateway relay is automatic when `config.enable_gateway = true`

See Task 8.3.1 for the complete example application code.

---

### Key Architecture Points

1. **SDK handles complexity**: Transport selection, peer deduplication, gateway relay
2. **App handles UI**: Display, user commands, simple SDK API calls
3. **No app-level transport code**: The app doesn't know or care which transport is used
4. **Automatic gateway**: SDK relays messages between transports transparently

### Library Selection

| Machine Type | Library | Notes |
|--------------|---------|-------|
| POSIX | `libpeertalk.a` | TCP/UDP only |
| 68k Mac (MacTCP) | `libpeertalk_mactcp.a` | TCP/UDP only |
| 68k Mac (MacTCP + AT) | `libpeertalk_mactcp_at.a` | Requires .MPP/.DSP drivers from Phase 7 |
| PPC Mac (TCP only) | `libpeertalk_ot.a` | OT TCP/UDP, smaller binary |
| PPC Mac (AppleTalk only) | `libpeertalk_ot_at.a` | OT's ADSP is preferred over .DSP |
| PPC Mac (Gateway) | `libpeertalk_ot_at.a` | Full multi-transport support |

The `libpeertalk_ot_at.a` library uses OT's unified API for both protocols, enabling gateway functionality with **zero application code changes**.

---

### Implementation Location Summary

The multi-transport SDK support is implemented in **Phase 6** (not Phase 8):

| Session | Focus | Files |
|---------|-------|-------|
| **6.7** | Multi-Transport Types | `src/opentransport/ot_multi.h` |
| **6.8** | AppleTalk via OT | `src/opentransport/ot_adsp.c`, `ot_nbp.c` |
| **6.9** | Unified Library Build | `Makefile.retro68` update |
| **6.10** | Multi-Transport Poll | `src/opentransport/ot_multi.c` |

Phase 8 focuses ONLY on the example applications that use the SDK:
- Session 8.1: POSIX chat (ncurses)
- Session 8.2: Classic Mac chat (SIOW)
- Session 8.3: Multi-transport gateway chat (OT)
- Session 8.4: Cross-network integration testing

---

## Architecture Decision: Separate Libraries vs Multi-Transport

**Original design (from PROJECT_GOALS.md):**
```
libpeertalk_ot.a      - Open Transport (TCP/IP only)
libpeertalk_at.a      - AppleTalk (ADSP only)
```

**Enhanced design with SDK multi-transport:**
```
libpeertalk_ot.a      - Open Transport (TCP/IP only) - smaller binary
libpeertalk_at.a      - AppleTalk via legacy .MPP/.DSP drivers (68k)
libpeertalk_ot_at.a   - Open Transport (TCP/IP + ADSP unified) - full features
```

The `libpeertalk_ot_at.a` library uses OT's unified API for both protocols, enabling gateway functionality with **zero application code changes**.

**When to use which:**

| Use Case | Library | Why |
|----------|---------|-----|
| POSIX development | `libpeertalk.a` | Reference implementation |
| 68k Mac (System 6-7.5.5) | `libpeertalk_mactcp.a` + `libpeertalk_at.a` | Requires legacy drivers |
| PPC Mac (TCP/IP only) | `libpeertalk_ot.a` | Simpler, smaller binary |
| PPC Mac (AppleTalk only) | `libpeertalk_ot_at.a` | OT's ADSP is preferred over .DSP |
| PPC Mac (Gateway) | `libpeertalk_ot_at.a` | Full multi-transport support |

---

## Session 8.5: Benchmark Mode

### Objective
Add benchmark command to all example chat applications for cross-platform performance testing and SDK validation.

### Prerequisites
- Phase 1 Session 1.4 (Logging System with performance extensions)
- Sessions 8.1-8.4 complete (example apps working)

### Design Goals

1. **Connect to ALL discovered peers** - not just selected peer
2. **Test each transport separately** on gateway Macs (TCP, then ADSP)
3. **Write structured log file** for post-analysis
4. **Run predefined test patterns** for reproducible results
5. **Work on all platforms** - POSIX, MacTCP, Open Transport, AppleTalk

### Test Patterns

| Round | Name | Count | Size | Purpose |
|-------|------|-------|------|---------|
| 1 | Small | 20 | 32 bytes | Latency baseline |
| 2 | Medium | 20 | 256 bytes | Typical chat message |
| 3 | Large | 10 | 1024 bytes | Throughput test |
| 4 | Rapid | 100 | 32 bytes | Stress test / queue handling |
| 5 | Echo | 10 | 64 bytes | Round-trip time (peer echoes back) |

### Log File Format

Benchmark logs use TSV (tab-separated values) for easy parsing on all platforms including Classic Mac:

```
# PeerTalk Benchmark Log
# Platform: MacTCP/68k
# Local: Alice's Mac
# Started: 1994-03-15 14:30:00
# Rounds: 5
#
SEQ	TIME_MS	DIR	PEER	TRANSPORT	SIZE	TYPE	RESULT
1	0	SEND	2	TCP	32	DATA	0
2	15	RECV	2	TCP	32	ACK	0
3	18	SEND	2	TCP	32	DATA	0
...
# Round 1 complete: 20 sent, 20 recv, 0 errors, avg_latency=12ms
...
# Benchmark complete: 160 sent, 155 recv, 5 errors
# Duration: 4523ms
```

**Field definitions:**
- `SEQ`: Global sequence number (for correlating sender/receiver logs)
- `TIME_MS`: Milliseconds since benchmark start
- `DIR`: SEND or RECV
- `PEER`: Peer ID
- `TRANSPORT`: TCP, UDP, ADSP, or NBP
- `SIZE`: Message payload size in bytes
- `TYPE`: Message type (DATA, ACK, ECHO_REQ, ECHO_RSP)
- `RESULT`: 0=success, negative=error code

### Tasks

#### Task 8.5.1: Add Benchmark Command to POSIX Chat

**Add to `examples/chat_posix.c`:**

```c
/*============================================================================
 * Benchmark Mode
 *
 * Runs standardized test patterns against all discovered peers.
 * Writes structured log to peertalk_bench_YYYYMMDD_HHMMSS.log
 *============================================================================*/

#include <time.h>
#include <sys/time.h>

/* Benchmark message types */
#define BENCH_MSG_DATA      1
#define BENCH_MSG_ACK       2
#define BENCH_MSG_ECHO_REQ  3
#define BENCH_MSG_ECHO_RSP  4

/* Benchmark state */
static FILE *g_bench_log = NULL;
static uint32_t g_bench_seq = 0;
static struct timeval g_bench_start;
static int g_bench_sent = 0;
static int g_bench_recv = 0;
static int g_bench_errors = 0;

static uint32_t bench_elapsed_ms(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (uint32_t)((now.tv_sec - g_bench_start.tv_sec) * 1000 +
                      (now.tv_usec - g_bench_start.tv_usec) / 1000);
}

static void bench_log(const char *dir, PeerTalk_PeerID peer,
                      const char *transport, int size, int type, int result) {
    if (g_bench_log) {
        fprintf(g_bench_log, "%u\t%u\t%s\t%u\t%s\t%d\t%d\t%d\n",
                ++g_bench_seq, bench_elapsed_ms(), dir, peer,
                transport, size, type, result);
    }
}

static void bench_open_log(void) {
    char filename[64];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);

    snprintf(filename, sizeof(filename),
             "peertalk_bench_%04d%02d%02d_%02d%02d%02d.log",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec);

    g_bench_log = fopen(filename, "w");
    if (g_bench_log) {
        fprintf(g_bench_log, "# PeerTalk Benchmark Log\n");
        fprintf(g_bench_log, "# Platform: POSIX\n");
        fprintf(g_bench_log, "# Local: %s\n", g_local_name);
        fprintf(g_bench_log, "# Started: %04d-%02d-%02d %02d:%02d:%02d\n",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec);
        fprintf(g_bench_log, "#\n");
        fprintf(g_bench_log, "SEQ\tTIME_MS\tDIR\tPEER\tTRANSPORT\tSIZE\tTYPE\tRESULT\n");

        printf("Benchmark log: %s\n", filename);
    } else {
        printf("WARNING: Could not create log file\n");
    }

    gettimeofday(&g_bench_start, NULL);
    g_bench_seq = 0;
    g_bench_sent = 0;
    g_bench_recv = 0;
    g_bench_errors = 0;
}

static void bench_close_log(void) {
    if (g_bench_log) {
        fprintf(g_bench_log, "# Benchmark complete: %d sent, %d recv, %d errors\n",
                g_bench_sent, g_bench_recv, g_bench_errors);
        fprintf(g_bench_log, "# Duration: %ums\n", bench_elapsed_ms());
        fclose(g_bench_log);
        g_bench_log = NULL;
    }
}

static int bench_send_pattern(PeerTalk_PeerID peer, const char *transport,
                               int count, int size, int msg_type) {
    char buf[1024];
    int i, sent = 0;
    PeerTalk_Error err;

    /* Fill buffer with pattern */
    for (i = 0; i < size && i < (int)sizeof(buf); i++) {
        buf[i] = (char)('A' + (i % 26));
    }

    for (i = 0; i < count; i++) {
        err = PeerTalk_Send(g_ctx, peer, buf, size);
        bench_log("SEND", peer, transport, size, msg_type, err);

        if (err == PT_OK) {
            sent++;
            g_bench_sent++;
        } else {
            g_bench_errors++;
        }

        /* Small delay between messages */
        usleep(10000);  /* 10ms */
    }

    return sent;
}

static void bench_run_round(const char *name, int count, int size, int msg_type) {
    PeerTalk_PeerInfo peers[16];
    uint16_t peer_count;
    int i, total_sent = 0;
    uint32_t start_ms;

    printf("  Round: %s (%d x %d bytes)...\n", name, count, size);
    start_ms = bench_elapsed_ms();

    /* Get all peers */
    PeerTalk_GetPeers(g_ctx, peers, 16, &peer_count);

    /* Connect to and test each peer */
    for (i = 0; i < peer_count; i++) {
        const char *transport = "TCP";  /* POSIX only has TCP */

        /* Connect if not connected */
        if (!peers[i].connected) {
            PeerTalk_Error err = PeerTalk_Connect(g_ctx, peers[i].id);
            if (err != PT_OK) {
                printf("    Failed to connect to peer %u\n", peers[i].id);
                continue;
            }
            /* Wait for connection */
            int wait = 0;
            while (!peers[i].connected && wait < 50) {
                PeerTalk_Poll(g_ctx);
                usleep(100000);  /* 100ms */
                PeerTalk_GetPeers(g_ctx, peers, 16, &peer_count);
                wait++;
            }
        }

        if (peers[i].connected) {
            total_sent += bench_send_pattern(peers[i].id, transport,
                                              count, size, msg_type);
        }
    }

    if (g_bench_log) {
        fprintf(g_bench_log, "# Round %s complete: %d sent in %ums\n",
                name, total_sent, bench_elapsed_ms() - start_ms);
    }
}

static void cmd_benchmark(int rounds) {
    printf("\n=== Starting Benchmark ===\n");
    printf("Rounds: %d\n", rounds > 0 ? rounds : 5);

    bench_open_log();

    if (rounds <= 0) rounds = 5;

    if (rounds >= 1) bench_run_round("Small", 20, 32, BENCH_MSG_DATA);
    if (rounds >= 2) bench_run_round("Medium", 20, 256, BENCH_MSG_DATA);
    if (rounds >= 3) bench_run_round("Large", 10, 1024, BENCH_MSG_DATA);
    if (rounds >= 4) bench_run_round("Rapid", 100, 32, BENCH_MSG_DATA);
    if (rounds >= 5) bench_run_round("Echo", 10, 64, BENCH_MSG_ECHO_REQ);

    printf("\n=== Benchmark Complete ===\n");
    printf("Sent: %d, Received: %d, Errors: %d\n",
           g_bench_sent, g_bench_recv, g_bench_errors);

    bench_close_log();
}

/* Add to command handler: */
/*
    case 'B':
        {
            int rounds = 5;
            if (input[1] == ' ') {
                sscanf(input + 2, "%d", &rounds);
            }
            cmd_benchmark(rounds);
        }
        break;
*/
```

#### Task 8.5.2: Add Benchmark Command to Classic Mac Chat

**Add to `examples/chat_mac.c`:**

```c
/*============================================================================
 * Benchmark Mode for Classic Mac
 *
 * Uses File Manager for log output.
 * Uses TickCount() for timing (converted to milliseconds).
 *============================================================================*/

#include <Files.h>
#include <OSUtils.h>
#include <DateTimeUtils.h>

#define BENCH_MSG_DATA      1
#define BENCH_MSG_ACK       2
#define BENCH_MSG_ECHO_REQ  3
#define BENCH_MSG_ECHO_RSP  4

static short g_bench_refnum = 0;
static uint32_t g_bench_seq = 0;
static uint32_t g_bench_start_ticks = 0;
static int g_bench_sent = 0;
static int g_bench_recv = 0;
static int g_bench_errors = 0;

static uint32_t bench_elapsed_ms(void) {
    uint32_t elapsed_ticks = TickCount() - g_bench_start_ticks;
    /* Convert ticks (1/60 sec) to milliseconds: ticks * 50 / 3 */
    return (elapsed_ticks * 50) / 3;
}

static void bench_write_line(const char *line) {
    ParamBlockRec pb;
    long len;

    if (!g_bench_refnum) return;

    len = strlen(line);
    pt_memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioRefNum = g_bench_refnum;
    pb.ioParam.ioBuffer = (Ptr)line;
    pb.ioParam.ioReqCount = len;
    pb.ioParam.ioPosMode = fsAtMark;
    PBWriteSync(&pb);
}

static void bench_log(const char *dir, PeerTalk_PeerID peer,
                      const char *transport, int size, int type, int result) {
    char line[128];

    if (!g_bench_refnum) return;

    sprintf(line, "%lu\t%lu\t%s\t%u\t%s\t%d\t%d\t%d\r",
            (unsigned long)++g_bench_seq,
            (unsigned long)bench_elapsed_ms(),
            dir, peer, transport, size, type, result);
    bench_write_line(line);
}

static void bench_open_log(void) {
    OSErr err;
    Str255 filename;
    DateTimeRec dt;
    unsigned long secs;
    char header[256];

    /* Generate filename with timestamp */
    GetDateTime(&secs);
    SecondsToDate(secs, &dt);

    sprintf((char *)&filename[1], "peertalk_bench_%04d%02d%02d_%02d%02d%02d.log",
            dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    filename[0] = strlen((char *)&filename[1]);

    /* Delete existing file
     * NOTE: Using FSDelete/Create/FSOpen (not HDelete/HCreate/HOpen)
     * for maximum portability. Per Inside Macintosh Vol IV p.8968-8970,
     * H* variants ARE available with 64K ROM (System 6.0.8+), but FS*
     * variants are more universally portable across all ROM versions. */
    FSDelete(filename, 0);

    /* Create and open */
    err = Create(filename, 0, 'PTLK', 'TEXT');
    if (err != noErr && err != dupFNErr) {
        printf("Cannot create benchmark log\n");
        return;
    }

    err = FSOpen(filename, 0, &g_bench_refnum);
    if (err != noErr) {
        printf("Cannot open benchmark log\n");
        g_bench_refnum = 0;
        return;
    }

    /* Write header */
    sprintf(header, "# PeerTalk Benchmark Log\r");
    bench_write_line(header);

#if defined(PT_PLATFORM_MACTCP)
    sprintf(header, "# Platform: MacTCP/68k\r");
#elif defined(PT_PLATFORM_OT)
    sprintf(header, "# Platform: OpenTransport\r");
#else
    sprintf(header, "# Platform: Classic Mac\r");
#endif
    bench_write_line(header);

    sprintf(header, "# Started: %04d-%02d-%02d %02d:%02d:%02d\r",
            dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    bench_write_line(header);

    bench_write_line("#\r");
    bench_write_line("SEQ\tTIME_MS\tDIR\tPEER\tTRANSPORT\tSIZE\tTYPE\tRESULT\r");

    printf("Benchmark log created\n");

    g_bench_start_ticks = TickCount();
    g_bench_seq = 0;
    g_bench_sent = 0;
    g_bench_recv = 0;
    g_bench_errors = 0;
}

static void bench_close_log(void) {
    char line[128];

    if (g_bench_refnum) {
        sprintf(line, "# Benchmark complete: %d sent, %d recv, %d errors\r",
                g_bench_sent, g_bench_recv, g_bench_errors);
        bench_write_line(line);

        sprintf(line, "# Duration: %lums\r", (unsigned long)bench_elapsed_ms());
        bench_write_line(line);

        FSClose(g_bench_refnum);
        g_bench_refnum = 0;
    }
}

static int bench_send_pattern(PeerTalk_PeerID peer, const char *transport,
                               int count, int size, int msg_type) {
    char buf[1024];
    int i, sent = 0;
    PeerTalk_Error err;

    /* Fill buffer with pattern */
    for (i = 0; i < size && i < (int)sizeof(buf); i++) {
        buf[i] = (char)('A' + (i % 26));
    }

    for (i = 0; i < count; i++) {
        err = PeerTalk_Send(g_ctx, peer, buf, size);
        bench_log("SEND", peer, transport, size, msg_type, err);

        if (err == PT_OK) {
            sent++;
            g_bench_sent++;
        } else {
            g_bench_errors++;
        }

        /* Poll to allow sends to complete and not block */
        PeerTalk_Poll(g_ctx);
    }

    return sent;
}

static void bench_run_round(const char *name, int count, int size, int msg_type) {
    PeerTalk_PeerInfo peers[8];
    uint16_t peer_count;
    int i, total_sent = 0;
    uint32_t start_ms;
    char line[128];

    printf("  Round: %s (%d x %d bytes)...\n", name, count, size);
    start_ms = bench_elapsed_ms();

    PeerTalk_GetPeers(g_ctx, peers, 8, &peer_count);

    for (i = 0; i < peer_count; i++) {
        const char *transport;

        /* Determine transport name */
#if defined(PT_TRANSPORT_TCP)
        if (peers[i].transports_available & PT_TRANSPORT_TCP) {
            transport = "TCP";
        } else
#endif
#if defined(PT_TRANSPORT_ADSP)
        if (peers[i].transports_available & PT_TRANSPORT_ADSP) {
            transport = "ADSP";
        } else
#endif
        {
            transport = "???";
        }

        /* Connect if needed */
        if (!peers[i].connected) {
            PeerTalk_Error err = PeerTalk_Connect(g_ctx, peers[i].id);
            if (err != PT_OK) {
                printf("    Cannot connect to peer %u\n", peers[i].id);
                continue;
            }
            /* Wait for connection with polling */
            {
                int wait;
                for (wait = 0; wait < 50 && !peers[i].connected; wait++) {
                    PeerTalk_Poll(g_ctx);
                    /* Get updated peer info */
                    PeerTalk_GetPeers(g_ctx, peers, 8, &peer_count);
                }
            }
        }

        if (peers[i].connected) {
            total_sent += bench_send_pattern(peers[i].id, transport,
                                              count, size, msg_type);
        }
    }

    if (g_bench_refnum) {
        sprintf(line, "# Round %s complete: %d sent in %lums\r",
                name, total_sent, (unsigned long)(bench_elapsed_ms() - start_ms));
        bench_write_line(line);
    }
}

static void cmd_benchmark(int rounds) {
    printf("\n=== Starting Benchmark ===\n");

    bench_open_log();

    if (rounds <= 0) rounds = 5;

    if (rounds >= 1) bench_run_round("Small", 20, 32, BENCH_MSG_DATA);
    if (rounds >= 2) bench_run_round("Medium", 20, 256, BENCH_MSG_DATA);
    if (rounds >= 3) bench_run_round("Large", 10, 1024, BENCH_MSG_DATA);
    if (rounds >= 4) bench_run_round("Rapid", 100, 32, BENCH_MSG_DATA);
    if (rounds >= 5) bench_run_round("Echo", 10, 64, BENCH_MSG_ECHO_REQ);

    printf("\n=== Benchmark Complete ===\n");
    printf("Sent: %d, Errors: %d\n", g_bench_sent, g_bench_errors);

    bench_close_log();
}

/* Add 'B' command to process_command(): */
/*
    case 'B':
        {
            int rounds = 5;
            if (input[1] == ' ' || input[1] == '\t') {
                sscanf(input + 2, "%d", &rounds);
            }
            cmd_benchmark(rounds);
        }
        break;
*/
```

#### Task 8.5.3: Add Benchmark to Gateway Chat (Multi-Transport)

**Add to `examples/chat_mac_gateway.c`:**

The gateway chat needs special handling to test each transport separately:

```c
/*============================================================================
 * Gateway Benchmark Mode
 *
 * Tests each transport separately to identify transport-specific issues.
 * For peers reachable via multiple transports, tests each path.
 *============================================================================*/

static void bench_run_round_by_transport(const char *name, int count, int size,
                                          int msg_type, uint32_t transport_filter) {
    PeerTalk_PeerInfo peers[16];
    uint16_t peer_count;
    int i, total_sent = 0;
    uint32_t start_ms;
    char line[128];
    const char *transport_name;

    /* Determine transport name for logging */
    if (transport_filter & PT_TRANSPORT_TCP) {
        transport_name = "TCP";
    } else if (transport_filter & PT_TRANSPORT_ADSP) {
        transport_name = "ADSP";
    } else {
        transport_name = "???";
    }

    printf("  Round: %s via %s (%d x %d bytes)...\n",
           name, transport_name, count, size);
    start_ms = bench_elapsed_ms();

    PeerTalk_GetPeers(g_ctx, peers, 16, &peer_count);

    for (i = 0; i < peer_count; i++) {
        /* Skip peers not reachable via this transport */
        if (!(peers[i].transports_available & transport_filter)) {
            continue;
        }

        /* Connect if needed, specifying transport */
        if (!peers[i].connected) {
            /* Use transport-specific connect if available */
            PeerTalk_Error err = PeerTalk_ConnectVia(
                g_ctx, peers[i].id, transport_filter);
            if (err != PT_OK) {
                printf("    Cannot connect to peer %u via %s\n",
                       peers[i].id, transport_name);
                continue;
            }

            /* Wait for connection */
            {
                int wait;
                for (wait = 0; wait < 50 && !peers[i].connected; wait++) {
                    PeerTalk_Poll(g_ctx);
                    PeerTalk_GetPeers(g_ctx, peers, 16, &peer_count);
                }
            }
        }

        if (peers[i].connected) {
            total_sent += bench_send_pattern(peers[i].id, transport_name,
                                              count, size, msg_type);
        }
    }

    if (g_bench_refnum) {
        sprintf(line, "# Round %s [%s] complete: %d sent in %lums\r",
                name, transport_name, total_sent,
                (unsigned long)(bench_elapsed_ms() - start_ms));
        bench_write_line(line);
    }
}

static void cmd_benchmark_gateway(int rounds) {
    printf("\n=== Starting Gateway Benchmark ===\n");
    printf("Testing each transport separately\n");

    bench_open_log();

    if (rounds <= 0) rounds = 5;

    /* Test TCP/IP first */
    printf("\n--- TCP/IP Transport ---\n");
    if (rounds >= 1) bench_run_round_by_transport("Small", 20, 32,
                                                   BENCH_MSG_DATA, PT_TRANSPORT_TCP);
    if (rounds >= 2) bench_run_round_by_transport("Medium", 20, 256,
                                                   BENCH_MSG_DATA, PT_TRANSPORT_TCP);
    if (rounds >= 3) bench_run_round_by_transport("Large", 10, 1024,
                                                   BENCH_MSG_DATA, PT_TRANSPORT_TCP);

    /* Then test ADSP */
    printf("\n--- ADSP Transport ---\n");
    if (rounds >= 1) bench_run_round_by_transport("Small", 20, 32,
                                                   BENCH_MSG_DATA, PT_TRANSPORT_ADSP);
    if (rounds >= 2) bench_run_round_by_transport("Medium", 20, 256,
                                                   BENCH_MSG_DATA, PT_TRANSPORT_ADSP);
    if (rounds >= 3) bench_run_round_by_transport("Large", 10, 1024,
                                                   BENCH_MSG_DATA, PT_TRANSPORT_ADSP);

    /* Stress tests on both */
    printf("\n--- Stress Tests ---\n");
    if (rounds >= 4) {
        bench_run_round_by_transport("Rapid", 100, 32,
                                      BENCH_MSG_DATA, PT_TRANSPORT_TCP);
        bench_run_round_by_transport("Rapid", 100, 32,
                                      BENCH_MSG_DATA, PT_TRANSPORT_ADSP);
    }

    printf("\n=== Gateway Benchmark Complete ===\n");
    printf("Sent: %d, Errors: %d\n", g_bench_sent, g_bench_errors);

    bench_close_log();
}
```

#### Task 8.5.4: Add Echo Response Handler

For round-trip time measurement, peers need to echo back ECHO_REQ messages:

```c
/*============================================================================
 * Echo Handler (add to message receive callback)
 *
 * When receiving BENCH_MSG_ECHO_REQ, immediately send back BENCH_MSG_ECHO_RSP
 * with the same payload. This allows measuring round-trip time.
 *============================================================================*/

/* In on_message_received callback: */
static void on_message_received(PeerTalk_Context *ctx,
                                 PeerTalk_PeerID from_peer,
                                 const void *data,
                                 uint16_t length,
                                 void *user_data) {
    /* Check for echo request (first byte is message type) */
    if (length > 0) {
        const uint8_t *bytes = (const uint8_t *)data;

        /* Echo request detection: check for BENCH_MSG_ECHO_REQ marker */
        if (bytes[0] == BENCH_MSG_ECHO_REQ) {
            /* Echo back with ECHO_RSP type */
            uint8_t response[1024];
            if (length <= sizeof(response)) {
                pt_memcpy(response, data, length);
                response[0] = BENCH_MSG_ECHO_RSP;
                PeerTalk_Send(ctx, from_peer, response, length);
            }
            return;  /* Don't display echo traffic */
        }

        /* Echo response - log for benchmark */
        if (bytes[0] == BENCH_MSG_ECHO_RSP && g_bench_log) {
            /* Log the received echo response */
            bench_log("RECV", from_peer, "???", length, BENCH_MSG_ECHO_RSP, 0);
            g_bench_recv++;
            return;
        }
    }

    /* Normal message display... */
    printf("\n< [%u]: %.*s\n", from_peer, (int)length, (const char *)data);
    printf("> ");
    fflush(stdout);
}
```

#### Task 8.5.5: Update Help Text

Add benchmark command to help in all example apps:

```c
static void cmd_help(void) {
    printf("\n=== Commands ===\n");
    printf("  L        - List discovered peers\n");
    printf("  C <id>   - Connect to peer by ID\n");
    printf("  D        - Disconnect\n");
    printf("  S <msg>  - Send message\n");
    printf("  B [n]    - Run benchmark (n rounds, default 5)\n");  /* NEW */
    printf("  H        - This help\n");
    printf("  Q        - Quit\n");
    printf("================\n");
}
```

### Acceptance Criteria

1. **POSIX chat** has working `B` command that:
   - Connects to all discovered peers
   - Runs 5 test rounds (small, medium, large, rapid, echo)
   - Writes `peertalk_bench_*.log` file

2. **Classic Mac chat** has working `B` command that:
   - Uses File Manager (FSOpen/FSWrite/FSClose) for log
   - Uses TickCount() for timing (converted to ms)
   - Works on System 6.0.8+ with MacTCP

3. **Gateway chat** has working `B` command that:
   - Tests TCP/IP and ADSP separately
   - Identifies transport-specific issues

4. **Echo mode** works:
   - ECHO_REQ messages are echoed back as ECHO_RSP
   - Round-trip time can be calculated from logs

5. **Log files** are:
   - TSV format readable on all platforms
   - Contain all required fields (SEQ, TIME_MS, etc.)
   - Have header with platform and timestamp
   - Have summary at end (sent/recv/errors/duration)

6. **Cross-platform correlation**:
   - Sequence numbers allow matching sender/receiver logs
   - Timestamps allow identifying latency vs loss

### Test Procedure

1. Start chat apps on: Linux, Mac IIci (MacTCP), Power Mac (OT Gateway), Mac SE (AppleTalk)
2. Wait for all peers to discover each other
3. On each peer, run: `B 5`
4. Collect all `peertalk_bench_*.log` files
5. Analyze for:
   - Message loss (sent vs received counts)
   - Latency (timestamp differences)
   - Error patterns
   - Transport-specific issues

---

## References

- ncurses documentation (POSIX)
- SIOW.h (Retro68 MPW Interfaces) - `__siowEventHook` declaration and usage
- Inside Macintosh: Toolbox Event Manager
- PeerTalk API (peertalk.h)
- Networking With Open Transport (1997):
  - Ch.2: Initialization and provider opening
  - Ch.3: Notifiers (same pattern for TCP and ADSP)
  - Ch.11: AppleTalk protocols via OT
  - Protocol configuration strings: `"tcp"`, `"udp"`, `"adsp"`, `"nbp"`
- OpenTransportProviders.h (Retro68 MPW Interfaces):
  - DDPAddress, NBPAddress, DDPNBPAddress structures
  - NBP mapper functions: OTRegisterName, OTLookupName, OTDeleteName
  - AppleTalk service constants: kADSPName, kNBPName, AF_ATALK_*

---

## API Naming Clarifications

**MPW/Retro68 API names differ from common assumptions:**

| Common Name | Actual API | Notes |
|-------------|------------|-------|
| `GetFrontWindow()` | `FrontWindow()` | Returns `WindowRef`, opcode 0xA924 |
| `GetNextWindow()` | `MacGetNextWindow()` | Carbon-compatible name, or use `((WindowPeek)w)->nextWindow` |
| `ShowWindow()` | `MacShowWindow()` | Carbon-compatible name, opcode 0xA915 |
| `FreeMem()` (for sizing) | `MaxBlock()` | Use MaxBlock for contiguous block size, not FreeMem (may be fragmented) |

**Memory Sizing:**

```c
/* WRONG - FreeMem returns total free but may be fragmented */
long avail = FreeMem();

/* RIGHT - MaxBlock returns largest contiguous free block */
long avail = MaxBlock();
if (avail < MIN_BUFFER_SIZE) {
    /* Handle low memory */
}
```
