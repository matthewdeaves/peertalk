/**
 * @file poll_mactcp.c
 * @brief MacTCP Main Poll Function
 *
 * Integrates all MacTCP components into a unified polling loop.
 * Uses single-pass polling with state dispatch for cache efficiency.
 *
 * DOD: Instead of multiple loops filtering by state, we iterate once
 * and dispatch based on state. This reduces from 3× array traversals
 * to 1×, and only accesses hot struct data during the scan.
 *
 * References:
 * - MacTCP Programmer's Guide (1989)
 */

#include "mactcp_defs.h"
#include "peer.h"
#include "pt_internal.h"
#include "pt_compat.h"
#include "protocol.h"
#include "queue.h"
#include "direct_buffer.h"

#if defined(PT_PLATFORM_MACTCP)

#include <Devices.h>
#include <OSUtils.h>

/* ========================================================================== */
/* Queue Allocation Helpers                                                   */
/* ========================================================================== */

/**
 * Allocate and initialize a peer queue.
 * DOD: Uses pt_alloc for consistent memory management.
 */
static pt_queue *pt_mactcp_alloc_peer_queue(struct pt_context *ctx)
{
    pt_queue *q;
    int result;

    q = (pt_queue *)pt_alloc(sizeof(pt_queue));
    if (!q) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to allocate queue: out of memory");
        return NULL;
    }

    result = pt_queue_init(ctx, q, 16);
    if (result != 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to initialize queue: error %d", result);
        pt_free(q);
        return NULL;
    }

    return q;
}

/**
 * Free a peer queue.
 */
static void pt_mactcp_free_peer_queue(pt_queue *q)
{
    if (q) {
        pt_queue_free(q);
        pt_free(q);
    }
}

/* ========================================================================== */
/* External Functions                                                         */
/* ========================================================================== */

/* From discovery_mactcp.c */
extern int pt_mactcp_discovery_poll(struct pt_context *ctx);
extern int pt_mactcp_discovery_send(struct pt_context *ctx, uint8_t disc_type);

/* From tcp_listen.c */
extern int pt_mactcp_listen_poll(struct pt_context *ctx);

/* From tcp_io.c */
extern int pt_mactcp_tcp_recv(struct pt_context *ctx, struct pt_peer *peer);
extern int pt_mactcp_tcp_send(struct pt_context *ctx, struct pt_peer *peer,
                               const void *data, uint16_t len);
extern int pt_mactcp_tcp_send_with_flags(struct pt_context *ctx, struct pt_peer *peer,
                                          const void *data, uint16_t len, uint8_t flags);
extern int pt_mactcp_send_capability(struct pt_context *ctx, struct pt_peer *peer);

/* From tcp_mactcp.c */
extern int pt_mactcp_tcp_release(struct pt_context *ctx, int idx);

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define PT_CONNECT_TIMEOUT_TICKS  (30 * 60)  /* 30 seconds at 60 ticks/sec */
#define PT_CLOSE_TIMEOUT_TICKS    (10 * 60)  /* 10 seconds */
#define PT_PEER_TIMEOUT_TICKS     (30 * 60)  /* 30 seconds peer timeout */
#define PT_DEFAULT_ANNOUNCE_TICKS (15 * 60)  /* 15 seconds default announce */

/* ========================================================================== */
/* State-Specific Poll Handlers                                               */
/* ========================================================================== */

/**
 * Poll a connecting stream.
 *
 * DOD: md passed as parameter to avoid re-fetching.
 */
static void pt_mactcp_poll_connecting(struct pt_context *ctx,
                                       pt_mactcp_data *md,
                                       int idx,
                                       pt_tcp_stream_hot *hot,
                                       pt_tcp_stream_cold *cold,
                                       struct pt_peer *peer,
                                       unsigned long now)
{
    TCPiopb abort_pb;

    if (peer == NULL)
        return;

    /* Check for connection timeout BEFORE checking ioResult.
     * Use signed comparison to handle Ticks wrap-around correctly.
     * ioResult > 0 means still in progress (typically 1 = inProgress). */
    if (cold->pb.ioResult > 0 &&
        (long)(now - cold->close_start) > (long)PT_CONNECT_TIMEOUT_TICKS) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
            "Connect to peer %u timed out after 30s, aborting",
            (unsigned)peer->hot.id);

        /* Abort the pending connection */
        pt_memset(&abort_pb, 0, sizeof(abort_pb));
        abort_pb.csCode = TCPAbort;
        abort_pb.ioCRefNum = md->driver_refnum;
        abort_pb.tcpStream = hot->stream;
        PBControlSync((ParmBlkPtr)&abort_pb);
    }

    /* Poll ioResult directly - no completion routine is used.
     * ioResult > 0 means still in progress, <= 0 means complete. */
    if (cold->pb.ioResult > 0)
        return;  /* Still waiting */

    /* Operation complete - capture result */
    hot->async_pending = 0;
    hot->async_result = cold->pb.ioResult;

    /* Connection attempt completed */
    if (hot->async_result == noErr) {
        /* Success! Copy local address from pb to cold struct */
        cold->local_ip = cold->pb.csParam.open.localHost;
        cold->local_port = cold->pb.csParam.open.localPort;

        /* Allocate send and receive queues for the peer */
        peer->send_queue = pt_mactcp_alloc_peer_queue(ctx);
        peer->recv_queue = pt_mactcp_alloc_peer_queue(ctx);

        if (!peer->send_queue || !peer->recv_queue) {
            PT_LOG_ERR(ctx->log, PT_LOG_CAT_MEMORY,
                "Failed to allocate queues for peer %u",
                (unsigned)peer->hot.id);
            pt_mactcp_free_peer_queue(peer->send_queue);
            pt_mactcp_free_peer_queue(peer->recv_queue);
            peer->send_queue = NULL;
            peer->recv_queue = NULL;
            pt_peer_set_state(ctx, peer, PT_PEER_STATE_FAILED);
            peer->hot.connection = NULL;
            pt_mactcp_tcp_release(ctx, idx);
            return;
        }

        PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT,
            "Connected to peer %u (\"%s\")",
            (unsigned)peer->hot.id, peer->cold.name);

        hot->state = PT_STREAM_CONNECTED;
        pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTED);
        peer->hot.last_seen = (pt_tick_t)now;

        /* CRITICAL: Reset receive buffer state for new connection.
         * If peer struct is reused, ibuflen may have stale data. */
        peer->cold.ibuflen = 0;

        /* CRITICAL: Store stream index so pt_mactcp_tcp_send can find it.
         * Use idx+1 so that stream 0 doesn't become NULL pointer. */
        peer->hot.connection = (void *)(intptr_t)(idx + 1);

        if (ctx->callbacks.on_peer_connected != NULL) {
            ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                             peer->hot.id,
                                             ctx->callbacks.user_data);
        }

        /* Send capability message to negotiate constraints */
        pt_mactcp_send_capability(ctx, peer);
    } else {
        /* Failed */
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
            "Connect to peer %u failed: %d",
            (unsigned)peer->hot.id, (int)hot->async_result);

        pt_peer_set_state(ctx, peer, PT_PEER_STATE_FAILED);
        peer->hot.connection = NULL;

        pt_mactcp_tcp_release(ctx, idx);
    }
}

/**
 * Poll a connected stream for send/receive.
 * DOD: Drains send queue first, then receives data.
 */
static void pt_mactcp_poll_connected(struct pt_context *ctx,
                                      int idx,
                                      pt_tcp_stream_hot *hot,
                                      pt_tcp_stream_cold *cold,
                                      struct pt_peer *peer)
{
    int result;
    const void *data;
    uint16_t len;

    if (peer == NULL)
        return;

    /* Process deferred log events from ASR */
    if (hot->log_events & PT_LOG_EVT_DATA_ARRIVED) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK, "Data arrived on stream %d", idx);
        hot->log_events &= ~PT_LOG_EVT_DATA_ARRIVED;
    }

    if (hot->log_events & PT_LOG_EVT_CONN_CLOSED) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK, "Connection closing on stream %d", idx);
        hot->log_events &= ~PT_LOG_EVT_CONN_CLOSED;
    }

    if (hot->log_events & PT_LOG_EVT_TERMINATED) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
            "Connection terminated on stream %d: reason=%d",
            idx, (int)hot->log_error_code);
        hot->log_events &= ~(PT_LOG_EVT_TERMINATED | PT_LOG_EVT_ERROR);
    }

    /* Tier 2: Send large message from direct buffer first (priority) */
    if (pt_direct_buffer_ready(&peer->send_direct)) {
        pt_direct_buffer *buf = &peer->send_direct;

        /* Mark as sending before the actual send */
        pt_direct_buffer_mark_sending(buf);

        result = pt_mactcp_tcp_send_with_flags(ctx, peer, buf->data, buf->length, buf->msg_flags);

        /* Complete the buffer (returns to IDLE state) */
        pt_direct_buffer_complete(buf);

        if (result == 0) {
            PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
                "Sent %u bytes (Tier 2) to peer %u",
                (unsigned)buf->length, (unsigned)peer->hot.id);
        } else {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                "Tier 2 send to peer %u failed: %d",
                (unsigned)peer->hot.id, result);
        }
    }

    /* Tier 1: Drain send queue - send one message per poll iteration */
    if (peer->send_queue) {
        pt_queue *q = peer->send_queue;
        if (pt_queue_pop_priority_direct(q, &data, &len) == 0) {
            uint8_t slot_flags = q->slots[q->pending_pop_slot].flags;

            /* Check if this is a fragment - needs PT_MSG_FLAG_FRAGMENT */
            if (slot_flags & PT_SLOT_FRAGMENT) {
                result = pt_mactcp_tcp_send_with_flags(ctx, peer, data, len,
                                                        PT_MSG_FLAG_FRAGMENT);
            } else {
                result = pt_mactcp_tcp_send(ctx, peer, data, len);
            }

            if (result == 0) {
                pt_queue_pop_priority_commit(q);
                PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
                    "Sent %u bytes (Tier 1%s) to peer %u",
                    (unsigned)len,
                    (slot_flags & PT_SLOT_FRAGMENT) ? " frag" : "",
                    (unsigned)peer->hot.id);
            } else {
                PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                    "Tier 1 send to peer %u failed: %d", (unsigned)peer->hot.id, result);
            }
        }
    }

    /* Receive data */
    result = pt_mactcp_tcp_recv(ctx, peer);
    if (result < 0) {
        /* Connection lost */
        PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT,
            "Connection lost to peer %u", (unsigned)peer->hot.id);

        if (ctx->callbacks.on_peer_disconnected != NULL) {
            ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                                peer->hot.id, PT_ERR_NETWORK,
                                                ctx->callbacks.user_data);
        }

        peer->hot.connection = NULL;
        pt_peer_destroy(ctx, peer);
        pt_mactcp_tcp_release(ctx, idx);
        return;  /* Peer destroyed, can't continue */
    }

    /* Flow control: Check for pressure updates to send
     *
     * When our recv queue pressure crosses a threshold (25%, 50%, 75%),
     * inform the peer so they can throttle their sends.
     * This is critical on constrained Mac hardware.
     */
    if (peer->cold.caps.pressure_update_pending ||
        pt_peer_check_pressure_update(ctx, peer)) {
        /* Send updated capabilities with new pressure value */
        pt_mactcp_send_capability(ctx, peer);
    }

    (void)cold;  /* Unused in this handler */
}

/**
 * Poll a closing stream for completion.
 *
 * DOD: md passed as parameter to avoid re-fetching.
 */
static void pt_mactcp_poll_closing(struct pt_context *ctx,
                                    pt_mactcp_data *md,
                                    int idx,
                                    pt_tcp_stream_hot *hot,
                                    pt_tcp_stream_cold *cold,
                                    struct pt_peer *peer,
                                    unsigned long now)
{
    TCPiopb abort_pb;

    /* Check for close timeout (signed comparison for Ticks wrap).
     * ioResult > 0 means still in progress. */
    if (cold->pb.ioResult > 0 &&
        (long)(now - cold->close_start) > (long)PT_CLOSE_TIMEOUT_TICKS) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
            "TCPClose timeout, forcing abort");

        pt_memset(&abort_pb, 0, sizeof(abort_pb));
        abort_pb.csCode = TCPAbort;
        abort_pb.ioCRefNum = md->driver_refnum;
        abort_pb.tcpStream = hot->stream;
        PBControlSync((ParmBlkPtr)&abort_pb);
    }

    /* Poll ioResult directly - no completion routine is used.
     * ioResult > 0 means still in progress, <= 0 means complete. */
    if (cold->pb.ioResult > 0)
        return;

    /* Operation complete - capture result */
    hot->async_pending = 0;
    hot->async_result = cold->pb.ioResult;

    /* Close completed - log result */
    PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_CONNECT,
        "TCPClose completed: result=%d", (int)hot->async_result);

    /* Release stream */
    pt_mactcp_tcp_release(ctx, idx);

    if (peer != NULL) {
        peer->hot.connection = NULL;

        if (ctx->callbacks.on_peer_disconnected != NULL) {
            ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                                peer->hot.id, 0,
                                                ctx->callbacks.user_data);
        }

        pt_peer_set_state(ctx, peer, PT_PEER_STATE_UNUSED);
    }
}

/* ========================================================================== */
/* Main Poll Function                                                         */
/* ========================================================================== */

/**
 * Main MacTCP poll function.
 *
 * Integrates discovery, listener, and all TCP streams into a single
 * polling loop. Called from PeerTalk_Poll().
 *
 * DOD: Single-pass polling with state dispatch.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success
 */
int pt_mactcp_poll(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    unsigned long now = (unsigned long)TickCount();
    unsigned long announce_interval;
    int i;

    /* Process discovery - drain all pending packets */
    while (pt_mactcp_discovery_poll(ctx) > 0)
        ;

    /* Process listener for incoming connections */
    pt_mactcp_listen_poll(ctx);

    /*
     * DOD: Single-pass polling of all TCP streams.
     * Combines what was connect_poll, close_poll, and connected I/O poll
     * into one iteration over the hot struct array.
     */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        pt_tcp_stream_hot *hot = &md->tcp_hot[i];
        pt_tcp_stream_cold *cold = &md->tcp_cold[i];
        struct pt_peer *peer = PT_PEER_FROM_IDX(ctx, hot->peer_idx);

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

    /*
     * Periodic discovery announce.
     * Mac SE 4MB: Use 15 seconds to reduce CPU/network load.
     * Performa 6200+: Can use 10 seconds.
     * Configurable via ctx->config but we use a reasonable default.
     */
    announce_interval = PT_DEFAULT_ANNOUNCE_TICKS;

    /* Signed comparison for Ticks wrap */
    if (ctx->discovery_active &&
        (long)(now - md->last_announce_tick) > (long)announce_interval) {
        pt_mactcp_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);
        md->last_announce_tick = now;
    }

    /* Check for peer timeouts */
    for (i = 0; i < (int)ctx->max_peers; i++) {
        struct pt_peer *peer = &ctx->peers[i];

        /* Only check DISCOVERED peers (not connected/connecting) */
        if (peer->hot.state != PT_PEER_STATE_DISCOVERED)
            continue;

        /* Signed comparison for Ticks wrap */
        if ((long)(now - peer->hot.last_seen) > (long)PT_PEER_TIMEOUT_TICKS) {
            PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_CONNECT,
                "Peer %u timed out", (unsigned)peer->hot.id);

            if (ctx->callbacks.on_peer_lost != NULL) {
                ctx->callbacks.on_peer_lost((PeerTalk_Context *)ctx,
                                            peer->hot.id,
                                            ctx->callbacks.user_data);
            }

            pt_peer_destroy(ctx, peer);
        }
    }

    return 0;
}

/* ========================================================================== */
/* Memory Leak Test (for hardware verification)                               */
/* ========================================================================== */

/**
 * Memory leak test for hardware verification.
 *
 * Run this on real hardware after extensive operations to verify
 * no memory leaks. MaxBlock should remain stable.
 *
 * @param ctx  PeerTalk context
 */
void pt_mactcp_leak_test(struct pt_context *ctx)
{
    long block_before = (long)MaxBlock();
    long free_before = (long)FreeMem();

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_MEMORY,
        "Leak test: MaxBlock=%ld FreeMem=%ld", block_before, free_before);

    /* Caller should perform 50+ connect/disconnect cycles, then: */

    long block_after = (long)MaxBlock();
    long free_after = (long)FreeMem();

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_MEMORY,
        "Leak test end: MaxBlock=%ld (delta=%ld) FreeMem=%ld (delta=%ld)",
        block_after, block_after - block_before,
        free_after, free_after - free_before);

    /* MaxBlock should be same or higher (fragmentation reduces it) */
    /* FreeMem delta should be small (< 1KB acceptable for heap overhead) */
    if (block_after < block_before - 1024) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_MEMORY, "WARNING: Possible memory leak!");
    }
}

#endif /* PT_PLATFORM_MACTCP */
