# Phase: Async Send Pipelining

**Status:** PLANNING
**Priority:** High
**Estimated Improvement:** 200-400% throughput increase (87 KB/s → 200-350 KB/s)

## Overview

Replace synchronous `PBControlSync()` TCP sends with async `PBControlAsync()` to enable multiple messages in flight simultaneously. This removes the per-message ACK wait bottleneck.

### Current State
```
Send(msg1) ──[50ms wait for ACK]──> Send(msg2) ──[50ms wait]──> ...
Result: ~21 messages/sec at 4096 bytes = 87 KB/s
```

### Target State
```
Send(msg1) ─> Send(msg2) ─> Send(msg3) ─> Send(msg4) ─> Poll completions...
Result: ~80-100 messages/sec at 4096 bytes = 300+ KB/s
```

---

## Memory Budget Analysis

### Standard Build (8MB+ RAM machines)

| Component | Per-Slot | × 4 Slots | Total |
|-----------|----------|-----------|-------|
| Send buffer | 4,112 bytes | 16,448 bytes | |
| TCPiopb | 108 bytes | 432 bytes | |
| Slot metadata | 8 bytes | 32 bytes | |
| **Per-peer total** | | | **~17 KB** |
| **8 peers max** | | | **~136 KB** |

### Low-Memory Build (Mac SE, 4MB RAM)

**Constraints:**
- Total RAM: 4,096 KB
- System + MacTCP: ~1,500 KB
- Application heap: ~384-512 KB (our lowmem builds)
- Available for networking: ~200 KB

**Low-memory configuration:**

| Setting | Standard | Low-Memory |
|---------|----------|------------|
| `PT_SEND_PIPELINE_DEPTH` | 4 | 2 |
| `PT_MAX_PEERS` | 8 | 4 |
| `PT_MESSAGE_MAX_PAYLOAD` | 4096 | 1024 |
| Max send buffer | 4,112 | 1,040 |
| **Per-peer memory** | 17 KB | 2.5 KB |
| **Total (all peers)** | 136 KB | 10 KB |

**Compile-time selection:**
```c
#ifdef PT_LOWMEM
    #define PT_SEND_PIPELINE_DEPTH  2
    #define PT_MAX_PEERS            4
    #define PT_MESSAGE_MAX_PAYLOAD  1024
#else
    #define PT_SEND_PIPELINE_DEPTH  4
    #define PT_MAX_PEERS            8
    #define PT_MESSAGE_MAX_PAYLOAD  4096
#endif
```

---

## Platform Compatibility

| Platform | Async Model | Pipeline Needed | Notes |
|----------|-------------|-----------------|-------|
| **MacTCP** | PBControlAsync + polling | Yes | Primary target |
| **Open Transport** | OTSnd + notifier | Yes | T_MEMORYRELEASED callback |
| **POSIX** | Non-blocking send() | No | Kernel handles buffering |
| **AppleTalk/ADSP** | PBControlAsync | Maybe | Lower priority |

### Shared Abstraction

```c
/* Platform ops extension */
struct pt_platform_ops {
    /* Existing sync send */
    int (*tcp_send)(struct pt_context *, struct pt_peer *,
                    const void *, uint16_t);

    /* New async send - NULL if not supported */
    int (*tcp_send_async)(struct pt_context *, struct pt_peer *,
                          const void *, uint16_t);

    /* Poll for send completions - NULL if not needed */
    int (*poll_send_completions)(struct pt_context *, struct pt_peer *);

    /* Query available send slots */
    int (*send_slots_available)(struct pt_context *, struct pt_peer *);
};
```

---

## Session Plan

### Session 1: Core Data Structures

**Goal:** Add pipeline structures to pt_internal.h with memory-conscious design.

**Files:**
- `src/core/pt_internal.h` - Add pt_send_slot, pt_send_pipeline structs
- `include/peertalk.h` - Add PT_SEND_PIPELINE_DEPTH config

**Deliverables:**
```c
/* Send slot - holds one in-flight message */
typedef struct {
    uint8_t        *buffer;         /* Message buffer (header + payload + CRC) */
    uint16_t        buffer_size;    /* Allocated size */
    uint16_t        message_len;    /* Actual message length */
    uint8_t         in_use;         /* 1 if send pending */
    uint8_t         completed;      /* 1 if send finished (success or error) */
    int16_t         result;         /* Completion result code */
    void           *platform_data;  /* Platform-specific (TCPiopb*, etc.) */
} pt_send_slot;

typedef struct {
    pt_send_slot    slots[PT_SEND_PIPELINE_DEPTH];
    uint8_t         next_slot;      /* Round-robin allocation index */
    uint8_t         pending_count;  /* Number of in-flight sends */
    uint8_t         initialized;    /* 1 if buffers allocated */
    uint8_t         reserved;
} pt_send_pipeline;
```

**Acceptance Criteria:**
- [ ] Structures compile on all platforms
- [ ] `#ifdef PT_LOWMEM` controls sizing
- [ ] No impact on existing sync send path

---

### Session 2: Platform Ops Extension

**Goal:** Extend pt_platform_ops with async send interface.

**Files:**
- `src/core/pt_internal.h` - Add ops to pt_platform_ops
- `src/posix/platform_posix.c` - Set async ops to NULL (not needed)
- `src/mactcp/platform_mactcp.c` - Declare stubs for now

**Deliverables:**
```c
/* POSIX - no async needed, kernel buffers */
static struct pt_platform_ops posix_ops = {
    .tcp_send = pt_posix_tcp_send,
    .tcp_send_async = NULL,           /* Not needed */
    .poll_send_completions = NULL,    /* Not needed */
    .send_slots_available = NULL,     /* Not needed */
    /* ... existing ops ... */
};

/* MacTCP - async send support */
static struct pt_platform_ops mactcp_ops = {
    .tcp_send = pt_mactcp_tcp_send,
    .tcp_send_async = pt_mactcp_tcp_send_async,
    .poll_send_completions = pt_mactcp_poll_send_completions,
    .send_slots_available = pt_mactcp_send_slots_available,
    /* ... existing ops ... */
};
```

**Acceptance Criteria:**
- [ ] POSIX tests still pass
- [ ] MacTCP builds (stubs return PT_ERR_NOT_SUPPORTED)
- [ ] No runtime behavior change yet

---

### Session 3: Pipeline Initialization & Teardown

**Goal:** Implement buffer allocation for send pipeline.

**Files:**
- `src/core/peer.c` - Add pipeline init/cleanup to peer lifecycle
- `src/mactcp/tcp_mactcp.c` - MacTCP-specific buffer allocation

**Deliverables:**
```c
/* Called when peer connects */
int pt_pipeline_init(struct pt_context *ctx, struct pt_peer *peer)
{
    int i;
    size_t buf_size = PT_MESSAGE_MAX_PAYLOAD + PT_MESSAGE_HEADER_SIZE + 4;

    for (i = 0; i < PT_SEND_PIPELINE_DEPTH; i++) {
        peer->pipeline.slots[i].buffer = pt_alloc(ctx, buf_size);
        if (!peer->pipeline.slots[i].buffer) {
            pt_pipeline_cleanup(ctx, peer);
            return PT_ERR_NO_MEMORY;
        }
        peer->pipeline.slots[i].buffer_size = buf_size;
        peer->pipeline.slots[i].in_use = 0;
    }
    peer->pipeline.initialized = 1;
    return PT_OK;
}

/* Called when peer disconnects */
void pt_pipeline_cleanup(struct pt_context *ctx, struct pt_peer *peer)
{
    int i;
    /* Wait for pending sends to complete or timeout */
    /* Then free buffers */
    for (i = 0; i < PT_SEND_PIPELINE_DEPTH; i++) {
        if (peer->pipeline.slots[i].buffer) {
            pt_free(ctx, peer->pipeline.slots[i].buffer);
            peer->pipeline.slots[i].buffer = NULL;
        }
    }
    peer->pipeline.initialized = 0;
}
```

**Memory optimization for Mac SE:**
```c
#ifdef PT_LOWMEM
/* Lazy allocation - only allocate slots as needed */
static pt_send_slot *pt_pipeline_get_slot(struct pt_peer *peer)
{
    int i;
    for (i = 0; i < PT_SEND_PIPELINE_DEPTH; i++) {
        if (!peer->pipeline.slots[i].in_use) {
            if (!peer->pipeline.slots[i].buffer) {
                /* Allocate on first use */
                peer->pipeline.slots[i].buffer = pt_alloc(...);
            }
            return &peer->pipeline.slots[i];
        }
    }
    return NULL;  /* All slots busy */
}
#endif
```

**Acceptance Criteria:**
- [ ] Buffers allocated on peer connect
- [ ] Buffers freed on peer disconnect
- [ ] No memory leaks (test with stress test)
- [ ] Lowmem build uses lazy allocation

---

### Session 4: MacTCP Async Send Implementation

**Goal:** Implement the core async send for MacTCP.

**Files:**
- `src/mactcp/tcp_io.c` - Add pt_mactcp_tcp_send_async()

**Deliverables:**
```c
int pt_mactcp_tcp_send_async(struct pt_context *ctx, struct pt_peer *peer,
                              const void *data, uint16_t len)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_send_slot *slot;
    TCPiopb *pb;
    int slot_idx;

    /* Find free slot */
    slot_idx = pt_pipeline_find_free_slot(peer);
    if (slot_idx < 0) {
        return PT_ERR_WOULD_BLOCK;
    }

    slot = &peer->pipeline.slots[slot_idx];
    pb = (TCPiopb *)slot->platform_data;

    /* Build complete message in slot buffer */
    pt_build_message(slot->buffer, data, len, peer->hot.send_seq++);
    slot->message_len = PT_MESSAGE_HEADER_SIZE + len + 2;  /* +CRC */

    /* Setup WDS pointing to slot buffer */
    slot->wds[0].length = slot->message_len;
    slot->wds[0].ptr = (Ptr)slot->buffer;
    slot->wds[1].length = 0;
    slot->wds[1].ptr = NULL;

    /* Setup async send */
    pt_memset(pb, 0, sizeof(*pb));
    pb->csCode = TCPSend;
    pb->ioCRefNum = md->driver_refnum;
    pb->tcpStream = hot->stream;
    pb->csParam.send.ulpTimeoutValue = 30;
    pb->csParam.send.ulpTimeoutAction = 1;
    pb->csParam.send.validityFlags = 0xC0;
    pb->csParam.send.pushFlag = 0;  /* Don't push - allow batching */
    pb->csParam.send.wdsPtr = (Ptr)slot->wds;
    pb->ioCompletion = NULL;  /* We poll instead of using completion routine */

    slot->in_use = 1;
    slot->completed = 0;
    peer->pipeline.pending_count++;

    /* Issue async send */
    PBControlAsync((ParmBlkPtr)pb);

    return PT_OK;
}
```

**Acceptance Criteria:**
- [ ] Async send compiles and links
- [ ] Basic send works (verified via packet capture)
- [ ] Multiple sends can be issued without waiting

---

### Session 5: Completion Polling

**Goal:** Poll for send completions in main loop.

**Files:**
- `src/mactcp/poll_mactcp.c` - Add completion polling
- `src/mactcp/tcp_io.c` - Add pt_mactcp_poll_send_completions()

**Deliverables:**
```c
int pt_mactcp_poll_send_completions(struct pt_context *ctx, struct pt_peer *peer)
{
    int i;
    int completions = 0;

    if (!peer->pipeline.initialized || peer->pipeline.pending_count == 0) {
        return 0;
    }

    for (i = 0; i < PT_SEND_PIPELINE_DEPTH; i++) {
        pt_send_slot *slot = &peer->pipeline.slots[i];
        TCPiopb *pb = (TCPiopb *)slot->platform_data;

        if (!slot->in_use) continue;

        /* Check if async operation completed */
        /* ioResult: 1 = in progress, 0 = success, <0 = error */
        if (pb->ioResult <= 0) {
            slot->result = pb->ioResult;
            slot->in_use = 0;
            slot->completed = 1;
            peer->pipeline.pending_count--;
            completions++;

            if (pb->ioResult != noErr) {
                PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                    "Async send error: %d", (int)pb->ioResult);
                /* Handle error - may need to disconnect */
            }
        }
    }

    return completions;
}

/* In poll_mactcp.c - add to main poll loop */
static void pt_mactcp_poll_connected(struct pt_context *ctx, struct pt_peer *peer)
{
    /* Existing receive handling... */
    pt_mactcp_tcp_recv(ctx, peer);

    /* NEW: Poll send completions */
    pt_mactcp_poll_send_completions(ctx, peer);

    /* Existing stream handling... */
    pt_stream_poll(ctx, peer, pt_mactcp_stream_send);
}
```

**Acceptance Criteria:**
- [ ] Completions detected correctly
- [ ] Errors logged and handled
- [ ] Slot freed for reuse after completion

---

### Session 6: Core Send Integration

**Goal:** Integrate async send into PeerTalk_Send() with fallback.

**Files:**
- `src/core/send.c` - Update PeerTalk_Send to use async when available

**Deliverables:**
```c
PeerTalk_Error PeerTalk_Send(PeerTalk_Context *ctx_pub, PeerTalk_PeerID peer_id,
                              const void *data, uint16_t len)
{
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    struct pt_peer *peer;

    /* Validation... */

    peer = pt_peer_find_by_id(ctx, peer_id);
    if (!peer) return PT_ERR_PEER_NOT_FOUND;

    /* Try async send if available and slots free */
    if (ctx->plat->tcp_send_async &&
        peer->pipeline.initialized &&
        peer->pipeline.pending_count < PT_SEND_PIPELINE_DEPTH) {

        int err = ctx->plat->tcp_send_async(ctx, peer, data, len);
        if (err != PT_ERR_WOULD_BLOCK) {
            return err;  /* Success or fatal error */
        }
        /* Fall through to sync if would block */
    }

    /* Fallback to sync send */
    return ctx->plat->tcp_send(ctx, peer, data, len);
}
```

**Acceptance Criteria:**
- [ ] Async used when slots available
- [ ] Graceful fallback to sync when slots exhausted
- [ ] No API change for callers

---

### Session 7: Throughput Testing

**Goal:** Verify performance improvement with async pipelining.

**Files:**
- `tests/mac/test_throughput.c` - May need updates for new behavior
- New log analysis

**Test Matrix:**

| Configuration | Pipeline Depth | Expected Rate |
|---------------|----------------|---------------|
| Sync baseline | 0 (disabled) | 87 KB/s |
| Async depth=2 | 2 | 150-200 KB/s |
| Async depth=4 | 4 | 250-350 KB/s |
| Lowmem depth=2 | 2 | 150-200 KB/s |

**Acceptance Criteria:**
- [ ] 2x+ improvement over baseline
- [ ] No packet loss increase
- [ ] Stable under sustained load
- [ ] Mac SE lowmem build works

---

### Session 8: Stress Testing & Edge Cases

**Goal:** Ensure robustness under adverse conditions.

**Test Cases:**
1. Rapid connect/disconnect during async sends
2. Peer disconnect while sends pending
3. Network timeout during async send
4. Memory pressure (Mac SE)
5. Full pipeline backpressure

**Files:**
- `tests/mac/test_stress.c` - Update for async scenarios

**Acceptance Criteria:**
- [ ] No crashes on disconnect during sends
- [ ] Pending sends properly cancelled on disconnect
- [ ] Memory fully reclaimed after disconnect
- [ ] Backpressure handled gracefully

---

## Open Transport Alignment

This design aligns well with future OT support:

| Aspect | MacTCP (this phase) | Open Transport (future) |
|--------|---------------------|-------------------------|
| Async model | PBControlAsync + poll ioResult | OTSnd + T_MEMORYRELEASED notifier |
| Buffer ownership | Copy to slot before send | OT manages or copy to slot |
| Completion | Poll in main loop | Notifier sets flag, poll in main loop |
| Backpressure | PT_ERR_WOULD_BLOCK | kOTFlowErr |

**Shared code:**
- Pipeline data structures (pt_send_slot, pt_send_pipeline)
- Slot allocation logic
- Fallback to sync behavior
- Memory budgeting

**OT-specific:**
- Notifier callback implementation
- Endpoint state handling
- OTAllocMem vs application heap

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Buffer lifetime bugs | All data copied to slot before async call |
| Interrupt-time complexity | No completion routine - just poll ioResult |
| Memory exhaustion | Configurable depth, lazy allocation for lowmem |
| Deadlock on disconnect | Timeout waiting for pending sends |
| Regression in sync path | Feature flag to disable async entirely |

---

## Configuration Options

```c
/* In peertalk.h or compile-time */

/* Master enable/disable */
#define PT_ASYNC_SEND_ENABLED    1

/* Pipeline depth (memory vs throughput tradeoff) */
#ifndef PT_SEND_PIPELINE_DEPTH
    #ifdef PT_LOWMEM
        #define PT_SEND_PIPELINE_DEPTH  2
    #else
        #define PT_SEND_PIPELINE_DEPTH  4
    #endif
#endif

/* Disable push flag for better batching (0 = allow batching) */
#define PT_ASYNC_SEND_PUSH_FLAG  0

/* Timeout for pending sends on disconnect (ticks) */
#define PT_ASYNC_SEND_DRAIN_TIMEOUT  (5 * 60)  /* 5 seconds */
```

---

## Success Metrics

| Metric | Baseline | Target | Stretch |
|--------|----------|--------|---------|
| Throughput (4KB msg) | 87 KB/s | 200 KB/s | 350 KB/s |
| Throughput (1KB msg) | 32 KB/s | 100 KB/s | 150 KB/s |
| Memory (standard) | 0 KB | <150 KB | <100 KB |
| Memory (lowmem) | 0 KB | <15 KB | <10 KB |
| Latency impact | 0 | <10% | 0% |

---

## Dependencies

- **Requires:** MacTCP streaming fix (completed)
- **Requires:** Log streaming fix (completed)
- **Enables:** Open Transport async send (future phase)
- **Enables:** AppleTalk ADSP pipelining (future phase)
