# Phase 10: Gateway Bridging

> **Status:** DEFERRED (implement after core phases complete)
> **Depends on:** Phase 6 (Open Transport) or Phase 5+7 (MacTCP + AppleTalk)
> **Purpose:** Allow POSIX peers to communicate with AppleTalk-only peers via a Mac gateway
> **Priority:** Low (most networks don't need this)
> **Review Applied:** 2026-01-29 - Struct reorder for cache efficiency, logging gaps documented, dependency gap noted, DOD optimizations

## Overview

This feature would allow a Mac with both TCP/IP and AppleTalk to act as a **message relay/bridge** between the two networks. Without this, POSIX peers cannot reach AppleTalk-only peers (like a Mac 512 or Mac Plus on LocalTalk).

```
┌─────────────┐                    ┌─────────────┐                    ┌─────────────┐
│   Linux     │ ──── TCP/IP ────── │  Mac SE     │ ── AppleTalk ───── │  Mac Plus   │
│   (POSIX)   │                    │  (Gateway)  │                    │  (AT only)  │
└─────────────┘                    └─────────────┘                    └─────────────┘
      │                                   │                                   │
      │            TCP/IP                 │           AppleTalk               │
      │                                   │                                   │
      └───────────────────────── Messages relayed through gateway ────────────┘
```

## Why Deferred

1. **Most networks don't need it** - Mixed TCP/IP + AppleTalk-only networks are rare
2. **Adds complexity** - Gateway logic requires message queuing, routing decisions
3. **Performance overhead** - Every relayed message has extra latency
4. **Core functionality first** - Focus on direct peer-to-peer before bridging

## AppleTalk Zone Considerations

> **Important:** Real AppleTalk networks often span multiple zones. Cross-zone relay requires:
> - Zone enumeration via ZIP (Zone Information Protocol) or static configuration
> - Zone-aware NBP lookups for peer discovery
> - Zone routing policy decisions in gateway logic
>
> **LocalTalk Performance:** LocalTalk operates at 230.4 kbit/s (~25 KB/s effective).
> Gateway relays to LocalTalk-only peers will be ~40x slower than EtherTalk.
>
> **ADSP Half-Open Timeout:** ADSP connections become half-open after losing contact
> and will close after 2 minutes. Gateway should detect and notify TCP/IP peers when
> AppleTalk connections fail.

## Message Size Constraint

> **DDP Packet Limit:** AppleTalk DDP packets hold a maximum of 572 bytes of ADSP data.
> With the gateway prefix (`[sender via gateway] ` = ~20-30 bytes), the effective
> payload must be ≤550 bytes to avoid DDP fragmentation.
>
> When gateway is enabled, `PT_MAX_MESSAGE_SIZE` should be capped at 550 bytes,
> or the prefix should be sent as protocol metadata rather than payload prefix.

## Implementation Reference

The following code was designed for Open Transport but the pattern applies to MacTCP+AppleTalk unified builds as well.

### Gateway Queue Structure

```c
/*
 * Pre-allocated message for gateway relay.
 * Fields ordered for CACHE EFFICIENCY: metadata first (hot), data last (cold).
 * This ensures queue traversal stays in cache - critical on 68030 (256-byte cache).
 *
 * Layout (68k-aligned):
 *   Offset 0:  next (4)           - queue traversal (HOT)
 *   Offset 4:  from_transport (4) - routing decision (HOT)
 *   Offset 8:  data_offset (2)    - send calculation (WARM)
 *   Offset 10: len (2)            - send calculation (WARM)
 *   Offset 12: from_peer (2)      - logging (WARM)
 *   Offset 14: slot_idx (2)       - O(1) slot free (WARM)
 *   Offset 16: from_name[32]      - prefix formatting (WARM)
 *   Offset 48: data[]             - payload copy (COLD)
 *
 * Total size: 48 + PT_MAX_MESSAGE_SIZE + 64 = ~PT_MAX_MESSAGE_SIZE + 112 bytes
 */
typedef struct pt_gateway_msg {
    /* HOT - accessed during queue traversal (fits in first cache line) */
    struct pt_gateway_msg *next;    /* 4 bytes - queue link */
    uint32_t        from_transport; /* 4 bytes - routing decision */

    /* WARM - accessed during relay processing */
    uint16_t        data_offset;    /* 2 bytes - where payload starts */
    uint16_t        len;            /* 2 bytes - payload length */
    PeerTalk_PeerID from_peer;      /* 2 bytes - sender ID for logging */
    int16_t         slot_idx;       /* 2 bytes - O(1) slot free (set on alloc) */

    /* Cached sender name - avoids O(n) lookup during relay */
    char            from_name[32];  /* 32 bytes - prefix formatting */

    /* COLD - only accessed when copying payload data */
    uint8_t         data[PT_MAX_MESSAGE_SIZE + 64];
} pt_gateway_msg;

/*
 * Gateway queue with bitmap-based slot allocation.
 * Design choice: bitmap provides clear slot state tracking vs simple counter.
 *
 * Memory: 16 messages × ~(PT_MAX_MESSAGE_SIZE + 112) bytes
 * With PT_MAX_MESSAGE_SIZE=550: ~10.6 KB pool
 * With PT_MAX_MESSAGE_SIZE=4096: ~67 KB pool
 */
typedef struct {
    pt_gateway_msg *pool;           /* Dynamically allocated when gateway enabled */
    pt_gateway_msg *head;           /* Queue head (oldest message) */
    pt_gateway_msg *tail;           /* Queue tail (newest message) */
    uint16_t        slot_in_use;    /* Bitmap: bit N = pool[N] is allocated */
    int16_t         pool_size;      /* Number of slots allocated (typically 16) */
    int16_t         count;          /* Messages currently queued */
    int16_t         overflow_count; /* Dropped messages since last log */
} pt_gateway_queue;

/* Prefix reserve at start of data buffer */
#define PT_GATEWAY_PREFIX_RESERVE 64
```

### Gateway Initialization

```c
/*
 * Initialize gateway queue. Called from pt_context_init().
 * Does NOT allocate pool - that happens in pt_gateway_enable().
 */
int pt_gateway_init(struct pt_context *ctx) {
    pt_gateway_queue *q = &ctx->gateway_queue;

    q->pool = NULL;
    q->head = NULL;
    q->tail = NULL;
    q->slot_in_use = 0;
    q->pool_size = 0;
    q->count = 0;
    q->overflow_count = 0;

    PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_INIT,
        "Gateway queue initialized (pool not yet allocated)");

    return 0;
}

/*
 * Shutdown gateway queue. Called from pt_context_destroy().
 */
void pt_gateway_shutdown(struct pt_context *ctx) {
    pt_gateway_queue *q = &ctx->gateway_queue;

    if (q->pool) {
        DisposePtr((Ptr)q->pool);
        q->pool = NULL;
    }

    PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_INIT,
        "Gateway queue shutdown");
}
```

### Queue a Message for Gateway Relay

```c
/*
 * Called when a message is received - queues it for relay to other transport.
 * Uses pre-allocated message pool with bitmap allocation to avoid fragmentation.
 *
 * SAFE to call from main loop only - uses pt_memcpy() which may call BlockMoveData.
 * Do NOT call from ASR or notifier.
 */
int pt_gateway_queue_msg(struct pt_context *ctx,
                         PeerTalk_PeerID from_peer,
                         uint32_t from_transport,
                         const void *data, uint16_t len) {
    pt_gateway_queue *q = &ctx->gateway_queue;
    pt_gateway_msg *msg;
    int slot;

    if (!ctx->gateway_enabled || !q->pool) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_SEND,
            "Gateway message dropped: %s",
            ctx->gateway_enabled ? "pool not allocated" : "gateway disabled");
        return 0;
    }

    /* Validate transport */
    if (!(from_transport & (PT_TRANSPORT_TCP | PT_TRANSPORT_APPLETALK))) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_PROTOCOL,
            "Gateway: invalid from_transport 0x%08lX", (unsigned long)from_transport);
        return -1;
    }

    /* Find free slot using bitmap */
    slot = -1;
    for (int i = 0; i < q->pool_size; i++) {
        if (!(q->slot_in_use & (1 << i))) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        q->overflow_count++;
        if (q->overflow_count == 1) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_SEND,
                "Gateway queue full, dropping message");
        } else if ((q->overflow_count & 0x0F) == 0) {
            /* Log every 16th overflow to avoid log spam */
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_SEND,
                "Gateway queue overflow (%d dropped)", q->overflow_count);
        }
        return -1;
    }

    /* Mark slot as in use */
    q->slot_in_use |= (1 << slot);

    msg = &q->pool[slot];
    msg->from_peer = from_peer;
    msg->from_transport = from_transport;

    /* Clamp message length to fit in buffer with prefix reserve */
    uint16_t max_payload = PT_MAX_MESSAGE_SIZE;
    if (len > max_payload) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_PROTOCOL,
            "Gateway message truncated: %u -> %u bytes", len, max_payload);
    }
    msg->len = (len < max_payload) ? len : max_payload;
    msg->slot_idx = (int16_t)slot;  /* Store for O(1) free later */

    /* Store data after prefix reserve area */
    msg->data_offset = PT_GATEWAY_PREFIX_RESERVE;
    pt_memcpy(msg->data + msg->data_offset, data, msg->len);

    /* Cache sender name now to avoid lookup during relay */
    struct pt_peer *from = pt_find_peer_by_id(ctx, from_peer);
    if (from) {
        pt_strncpy(msg->from_name, from->info.name, sizeof(msg->from_name) - 1);
        msg->from_name[sizeof(msg->from_name) - 1] = '\0';
    } else {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "Gateway: peer 0x%04X not found, using 'Unknown'", from_peer);
        pt_strcpy(msg->from_name, "Unknown");
    }

    msg->next = NULL;

    /* Add to queue tail */
    if (q->tail) {
        q->tail->next = msg;
    } else {
        q->head = msg;
    }
    q->tail = msg;
    q->count++;

    PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_PROTOCOL,
        "Gateway queued message from peer 0x%04X: %u bytes, slot %d, queue: %d/%d",
        from_peer, msg->len, slot, q->count, q->pool_size);

    return 0;
}
```

### Process Gateway Queue

```c
/*
 * Relays queued messages to peers on the OTHER transport.
 * Called from main poll loop - safe to use pt_memcpy, pt_snprintf, etc.
 *
 * Uses pre-formatted prefix in reserved buffer space to avoid double-copy.
 */
void pt_gateway_process(struct pt_context *ctx) {
    pt_gateway_queue *q = &ctx->gateway_queue;
    int processed = 0;
    int relay_failures = 0;

    while (q->head) {
        pt_gateway_msg *msg = q->head;
        q->head = msg->next;
        if (!q->head) q->tail = NULL;
        q->count--;

        /* Determine target transport (relay to the OTHER side) */
        uint32_t target = (msg->from_transport & PT_TRANSPORT_TCP) ?
                          PT_TRANSPORT_APPLETALK : PT_TRANSPORT_TCP;

        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_SEND,
            "Gateway routing: transport 0x%08lX -> 0x%08lX",
            (unsigned long)msg->from_transport, (unsigned long)target);

        /* Prepend gateway prefix in reserved space */
        int prefix_len = pt_snprintf((char *)(msg->data + msg->data_offset - PT_GATEWAY_PREFIX_RESERVE),
            PT_GATEWAY_PREFIX_RESERVE, "[%s via gateway] ", msg->from_name);

        /* Validate prefix formatting */
        if (prefix_len <= 0 || prefix_len >= PT_GATEWAY_PREFIX_RESERVE) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_PROTOCOL,
                "Gateway prefix formatting failed: len=%d", prefix_len);
            prefix_len = (prefix_len > 0) ? PT_GATEWAY_PREFIX_RESERVE - 1 : 0;
        }

        /* Adjust offset to include prefix */
        uint16_t send_offset = msg->data_offset - prefix_len;
        uint16_t send_len = prefix_len + msg->len;

        /* Check for truncation (shouldn't happen with proper sizing) */
        if (send_len > PT_MAX_MESSAGE_SIZE + PT_GATEWAY_PREFIX_RESERVE) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_PROTOCOL,
                "Gateway relay truncated: %d -> %d bytes",
                (int)send_len, PT_MAX_MESSAGE_SIZE + PT_GATEWAY_PREFIX_RESERVE);
            send_len = PT_MAX_MESSAGE_SIZE + PT_GATEWAY_PREFIX_RESERVE;
        }

        /* Relay to all connected peers on target transport */
        int relay_count = 0;
        for (int i = 0; i < ctx->peer_count; i++) {
            struct pt_peer *peer = &ctx->peers[i];

            if (peer->state != PT_PEER_STATE_CONNECTED) continue;
            if (!(peer->preferred_transport & target)) continue;

            /* Send via appropriate transport */
            int result = PeerTalk_SendVia((PeerTalk_Context *)ctx, peer->info.id,
                             msg->data + send_offset, send_len,
                             target, PT_PRIORITY_NORMAL, 0, 0);

            if (result != PT_OK) {
                PT_LOG_WARN(ctx->log, PT_LOG_CAT_SEND,
                    "Gateway relay to %s failed: %d", peer->info.name, result);
                relay_failures++;
            } else {
                PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_SEND,
                    "Gateway relay: %s -> %s", msg->from_name, peer->info.name);
                relay_count++;
            }
        }

        if (relay_count == 0) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_SEND,
                "Gateway: no target peers for transport 0x%08lX", (unsigned long)target);
        }

        /* Free slot using stored index - O(1) instead of O(n) address search */
        if (msg->slot_idx >= 0 && msg->slot_idx < q->pool_size) {
            q->slot_in_use &= ~(1 << msg->slot_idx);
        }

        processed++;
    }

    if (processed > 0) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_SEND,
            "Gateway processed %d messages (%d relay failures)",
            processed, relay_failures);
    }

    /* Reset overflow counter after successful processing */
    if (q->overflow_count > 0 && q->count == 0) {
        PT_LOG_INFO(ctx->log, PT_LOG_CAT_SEND,
            "Gateway queue drained (total dropped: %d)", q->overflow_count);
        q->overflow_count = 0;
    }
}
```

### Gateway Control API

```c
/*
 * Enable/disable gateway mode at runtime.
 * Allocates pool on first enable, frees on disable.
 *
 * Pool size: 16 messages × ~(PT_MAX_MESSAGE_SIZE + 110) bytes
 */
#define PT_GATEWAY_POOL_SIZE 16

int pt_gateway_enable(struct pt_context *ctx, int enable) {
    pt_gateway_queue *q = &ctx->gateway_queue;

    if (enable && ctx->gateway_enabled) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_GENERAL,
            "Gateway already enabled");
        return 0;
    }

    if (!enable && !ctx->gateway_enabled) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_GENERAL,
            "Gateway already disabled");
        return 0;
    }

    if (enable) {
        /* Allocate pool if not already allocated */
        if (!q->pool) {
            Size pool_bytes = sizeof(pt_gateway_msg) * PT_GATEWAY_POOL_SIZE;
            q->pool = (pt_gateway_msg *)NewPtrClear(pool_bytes);
            if (!q->pool) {
                PT_LOG_ERR(ctx->log, PT_LOG_CAT_MEMORY,
                    "Gateway pool allocation failed (%ld bytes)", (long)pool_bytes);
                return PT_ERR_NO_MEMORY;
            }
            q->pool_size = PT_GATEWAY_POOL_SIZE;
            PT_LOG_INFO(ctx->log, PT_LOG_CAT_GENERAL,
                "Gateway pool allocated: %d messages, %ld bytes",
                PT_GATEWAY_POOL_SIZE, (long)pool_bytes);
        }

        ctx->gateway_enabled = 1;
        PT_LOG_INFO(ctx->log, PT_LOG_CAT_GENERAL,
            "Gateway mode enabled (queue: %d/%d)", q->count, q->pool_size);
    } else {
        /* Log queue status before disabling */
        if (q->count > 0) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_GENERAL,
                "Gateway disabled with %d messages in queue (will be dropped)", q->count);
        }

        ctx->gateway_enabled = 0;

        /* Clear queue state but keep pool allocated for quick re-enable */
        q->head = NULL;
        q->tail = NULL;
        q->slot_in_use = 0;
        q->count = 0;

        PT_LOG_INFO(ctx->log, PT_LOG_CAT_GENERAL,
            "Gateway mode disabled");
    }

    return 0;
}

/* Public API wrapper */
PeerTalk_Error PeerTalk_SetGatewayMode(PeerTalk_Context *ctx, int enable) {
    if (!ctx) return PT_ERR_INVALID_PARAM;
    return pt_gateway_enable((struct pt_context *)ctx, enable);
}
```

## Acceptance Criteria (When Implemented)

1. Gateway queues messages from both transports
2. Messages relay to peers on opposite transport
3. Pre-allocated pool avoids runtime allocation (allocated once on enable)
4. Relay message includes sender name for attribution
5. Gateway can be enabled/disabled at runtime
6. Queue overflow handled gracefully (drops with warning, logs overflow count)
7. Bitmap-based slot allocation with clear state tracking
8. Pool buffer design avoids stack allocation concerns
9. All error paths logged (transport validation, peer lookup, send failures)
10. **VERIFIED:** POSIX <-> Gateway Mac <-> AppleTalk-only Mac

## Integration Points

When ready to implement, add to:
- `pt_context` structure: `gateway_enabled`, `gateway_queue`
- `pt_context_init()`: call `pt_gateway_init()`
- `pt_context_destroy()`: call `pt_gateway_shutdown()`
- Poll loop: call `pt_gateway_process()` after receiving messages
- Public API: `PeerTalk_SetGatewayMode()`
- Config: `gateway_mode` option (default disabled)
- Error codes: `PT_ERR_GATEWAY_DISABLED`, `PT_ERR_GATEWAY_QUEUE_FULL`

**API Dependency:** `PeerTalk_SendVia()` must be defined in Phase 3.5 or earlier.

> **⚠️ DEPENDENCY GAP (2026-01-29):** Phase 3.5 currently defines `PeerTalk_SendEx()` but NOT
> `PeerTalk_SendVia()`. The `transport` parameter is missing. This must be added to Phase 3.5
> or Phase 6.8-6.10 before gateway can be implemented. Phase 1 declares the signature at line 910.

**Phase 8 Note:** Session 8.3 (Multi-Transport Gateway Chat) is BLOCKED until this
feature is implemented. Consider assigning gateway to Phase 6.11-6.12 if Session 8.3
is required for release.

## Fact-Check Summary

| Fact | Source | Status |
|------|--------|--------|
| Pre-allocated pool is recommended pattern | Inside Macintosh Vol IV | Verified |
| Main loop operations have no restrictions | IM Vol VI Table B-3 | Verified |
| pt_memcpy() safe in main loop | MacTCP Guide, OT v1.3 | Verified |
| DDP packet limit is 572 bytes | Programming With AppleTalk p.6014 | Verified |
| LocalTalk effective rate ~25 KB/s | Programming With AppleTalk p.1039 | Verified |
| ADSP half-open timeout is 2 minutes | Programming With AppleTalk p.5014 | Verified |
| Zone handling required for cross-zone | Programming With AppleTalk Ch.5-6 | Verified |
| OT notifier reentrancy possible | NetworkingOpenTransport Ch.3 p.76 | Verified |

## Logging Requirements

> **IMPORTANT:** `ctx->log` MUST be initialized before calling any gateway functions.
> All gateway functions assume a valid log context. Add validation at top of `pt_gateway_init()`:
> ```c
> if (!ctx || !ctx->log) return PT_ERR_INVALID_PARAM;
> ```

### Log Points Required

| Location | Level | Category | Message |
|----------|-------|----------|---------|
| Disabled gateway early return | DEBUG | PT_LOG_CAT_SEND | "Gateway message dropped: %s" |
| Message truncation | WARN | PT_LOG_CAT_PROTOCOL | "Gateway message truncated: %u -> %u bytes" |
| Successful queue | DEBUG | PT_LOG_CAT_PROTOCOL | "Gateway queued message from peer..." |
| Prefix formatting failure | WARN | PT_LOG_CAT_PROTOCOL | "Gateway prefix formatting failed: len=%d" |
| Enable with overflow recovery | INFO | PT_LOG_CAT_GENERAL | "...recovering from overflow..." |

## Performance Notes (DOD)

### Cache Efficiency

The `pt_gateway_msg` struct is ordered for **cache efficiency**:
- **HOT fields first** (8 bytes): `next`, `from_transport` - accessed during queue traversal
- **WARM fields next** (40 bytes): routing/logging metadata
- **COLD data last**: payload buffer - only touched during copy

This ordering is critical for 68030 (256-byte data cache) where the original layout
(data buffer first) would cause cache misses on every metadata access.

### O(1) Slot Free

The `slot_idx` field enables O(1) slot deallocation instead of O(n) address search.
Set during allocation, used during free.

### Future Optimizations (Implementation Notes)

1. **O(1) free slot search**: Use `__builtin_ffs()` or 68k `BFFFO` instruction
2. **Target peer bitmap**: Pre-build once per `pt_gateway_process()` call
3. **Index-based queue**: Consider index chain instead of pointer chain for better cache locality
