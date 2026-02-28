/**
 * @file tcp_connect_ot.c
 * @brief Open Transport TCP Connection Implementation
 *
 * Outgoing TCP connections and connection poll loop.
 * Uses hot/cold struct split and O(1) bitmap pool allocation.
 *
 * Key differences from MacTCP:
 * - OT uses OTConnect (async) + OTRcvConnect, not TCPActiveOpen
 * - Do NOT bind before connect (OT docs p.109 - provider auto-binds)
 * - Connection completion via T_CONNECT notifier flag, not ioResult polling
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 4: "Endpoints"
 * - OpenTransport.h, OpenTransportProviders.h (Retro68)
 */

#include "ot_defs.h"
#include "peer.h"
#include "queue.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_OT)

#include <OSUtils.h>  /* TickCount() - main loop only! */

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define PT_CONNECT_TIMEOUT_TICKS  (30 * 60)  /* 30 seconds at 60 ticks/sec */

/* ========================================================================== */
/* External Functions                                                         */
/* ========================================================================== */

/* From tcp_ot.c */
extern int pt_ot_tcp_create(struct pt_context *ctx);
extern int pt_ot_tcp_bind(struct pt_context *ctx, int idx,
                            InetPort port, OTQLen qlen);
extern int pt_ot_tcp_set_options(struct pt_context *ctx, int idx);
extern void pt_ot_tcp_close(struct pt_context *ctx, int idx);
extern void pt_ot_tcp_cleanup(struct pt_context *ctx, int idx);
extern void pt_ot_tcp_process_log_events(struct pt_context *ctx,
                                          pt_tcp_endpoint_hot *hot,
                                          int idx);

/* ========================================================================== */
/* Connect                                                                    */
/* ========================================================================== */

/**
 * Initiate outgoing TCP connection to peer.
 *
 * Allocates endpoint from bitmap pool, opens it, sets up TCall
 * with remote address, and calls OTConnect asynchronously.
 *
 * IMPORTANT: Does NOT bind before connect. Per OT docs p.109:
 * "If you do not need to connect from a specific port, you can skip
 * the bind step and let the endpoint provider bind the endpoint
 * automatically when you call OTConnect."
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to connect to (must be in DISCOVERED state)
 * @return      0 on success, negative error code on failure
 */
int pt_ot_tcp_connect(struct pt_context *ctx, struct pt_peer *peer)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot;
    pt_tcp_endpoint_cold *cold;
    int idx;
    OSStatus err;
    char ip_str[PT_IP_STR_LEN];

    if (peer == NULL || peer->hot.magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    if (peer->hot.state != PT_PEER_STATE_DISCOVERED)
        return PT_ERR_INVALID_STATE;

    /* Create endpoint (handles pool alloc, open, notifier, async mode) */
    idx = pt_ot_tcp_create(ctx);
    if (idx < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No free endpoint for connect to peer %u (pool exhausted)",
            (unsigned)peer->hot.id);
        return PT_ERR_RESOURCE;
    }

    hot = pt_ot_get_tcp_hot(od, idx);
    cold = pt_ot_get_tcp_cold(od, idx);
    if (hot == NULL || cold == NULL) {
        pt_ot_tcp_cleanup(ctx, idx);
        return PT_ERR_RESOURCE;
    }

    /* Explicit bind to any port before async OTConnect.
     * OT auto-bind from T_UNBND state may not work in async mode
     * on all OT versions. Set sync mode for bind, then restore async. */
    OTSetSynchronous(hot->ref);
    if (pt_ot_tcp_bind(ctx, idx, 0, 0) < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "Failed to bind endpoint for connect to peer %u",
            (unsigned)peer->hot.id);
        pt_ot_tcp_cleanup(ctx, idx);
        return PT_ERR_NETWORK;
    }

    /* Set TCP options now that endpoint is bound (T_IDLE state).
     * Must be done before OTSetAsynchronous since OTOptionManagement
     * requires synchronous mode. */
    pt_ot_tcp_set_options(ctx, idx);

    OTSetAsynchronous(hot->ref);

    /* Setup TCall with remote address (in cold data) */
    OTInitInetAddress(&cold->remote_addr,
                      peer->cold.info.port,
                      peer->cold.info.address);

    pt_memset(&cold->call, 0, sizeof(TCall));
    cold->call.addr.buf = (UInt8 *)&cold->remote_addr;
    cold->call.addr.len = sizeof(InetAddress);
    cold->call.addr.maxlen = sizeof(InetAddress);
    /* opt and udata left as zero (no options, no user data) */

    /* Link peer ↔ endpoint */
    hot->peer = peer;
    hot->state = PT_EP_OUTGOING;
    hot->close_start = (unsigned long)TickCount();  /* Connect start time */

    /* Store endpoint idx+1 in peer (so endpoint 0 doesn't become NULL) */
    peer->hot.connection = (void *)(intptr_t)(idx + 1);
    pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTING);

    OTInetHostToString(peer->cold.info.address, ip_str);
    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Connecting to peer %u (\"%s\") at %s:%u [ep=%d]",
        (unsigned)peer->hot.id,
        pt_get_peer_name(ctx, peer->hot.name_idx),
        ip_str, (unsigned)peer->cold.info.port, idx);

    /* Issue async OTConnect.
     * For async mode, OTConnect returns kOTNoDataErr to indicate
     * the connection is in progress. T_CONNECT event fires when done. */
    err = OTConnect(hot->ref, &cold->call, NULL);

    if (err != kOTNoDataErr && err != kOTNoError) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTConnect failed: %ld (peer %u)",
            (long)err, (unsigned)peer->hot.id);

        /* Rollback */
        peer->hot.connection = NULL;
        pt_peer_set_state(ctx, peer, PT_PEER_STATE_FAILED);
        hot->peer = NULL;
        pt_ot_tcp_cleanup(ctx, idx);
        return PT_ERR_NETWORK;
    }

    return 0;
}

/* ========================================================================== */
/* Connect Poll                                                               */
/* ========================================================================== */

/**
 * Poll connecting endpoints for completion.
 *
 * Uses bitmap-optimized iteration: only visits active (in-use) slots.
 * For each endpoint in PT_EP_OUTGOING state:
 * - Process deferred log events from notifier
 * - Check for connection timeout (30 seconds)
 * - Check T_CONNECT flag → OTRcvConnect → success
 * - Check T_DISCONNECT flag → OTRcvDisconnect → failure
 *
 * @param ctx  PeerTalk context
 * @return     Number of connections completed (success or failure)
 */
int pt_ot_connect_poll(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    uint32_t active;
    int processed = 0;
    unsigned long now = (unsigned long)TickCount();

    /* Bitmap iteration: only active (in-use) slots */
    active = ~od->tcp_pool.free_bitmap
           & ((1UL << od->tcp_pool.capacity) - 1);

    while (active) {
        pt_tcp_endpoint_hot *hot;
        struct pt_peer *peer;
        int i;

        /* Find first set bit (lowest active slot) */
#if defined(__GNUC__)
        i = __builtin_ffs((int)active) - 1;
#else
        {
            uint32_t tmp = active;
            i = 0;
            while ((tmp & 1) == 0) { tmp >>= 1; i++; }
        }
#endif
        /* Clear this bit to advance iteration */
        active &= ~(1UL << i);

        hot = pt_ot_get_tcp_hot(od, i);
        if (hot == NULL || hot->state != PT_EP_OUTGOING)
            continue;

        /* Process deferred log events from notifier */
        pt_ot_tcp_process_log_events(ctx, hot, i);

        peer = hot->peer;

        /* --- Timeout check (30 seconds) --- */
        {
            unsigned long elapsed = now - hot->close_start;

            if (elapsed > PT_CONNECT_TIMEOUT_TICKS) {
                PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
                    "TCP[%d] connect timeout (%lu ticks) for peer %u",
                    i, elapsed,
                    peer ? (unsigned)peer->hot.id : 0);

                /* Force abortive disconnect */
                OTSndDisconnect(hot->ref, NULL);

                if (peer != NULL) {
                    pt_peer_set_state(ctx, peer,
                                      PT_PEER_STATE_FAILED);
                    peer->hot.connection = NULL;
                }

                hot->peer = NULL;
                pt_ot_tcp_cleanup(ctx, i);
                processed++;
                continue;
            }
        }

        /* --- T_CONNECT: connection completed successfully --- */
        if (PT_FLAG_TEST(hot->flags, PT_OT_FLAG_CONNECT_COMPLETE)) {
            TCall ret;
            InetAddress ret_addr;
            OSStatus err;
            char ip_str[PT_IP_STR_LEN];

            PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_CONNECT_COMPLETE);

            /* Setup return TCall to receive remote address info */
            pt_memset(&ret, 0, sizeof(TCall));
            ret.addr.buf = (UInt8 *)&ret_addr;
            ret.addr.maxlen = sizeof(InetAddress);

            err = OTRcvConnect(hot->ref, &ret);

            if (err == kOTNoError) {
                hot->state = PT_EP_DATAXFER;

                if (peer != NULL) {
                    /* Allocate send/recv queues for data exchange */
                    {
                        pt_queue *sq, *rq;
                        sq = (pt_queue *)pt_alloc(sizeof(pt_queue));
                        rq = (pt_queue *)pt_alloc(sizeof(pt_queue));
                        if (sq && rq &&
                            pt_queue_init(ctx, sq, 16) == 0 &&
                            pt_queue_init(ctx, rq, 16) == 0) {
                            peer->send_queue = sq;
                            peer->recv_queue = rq;
                        } else {
                            PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
                                "Failed to allocate queues for "
                                "peer %u",
                                (unsigned)peer->hot.id);
                            if (sq) pt_free(sq);
                            if (rq) pt_free(rq);
                            peer->send_queue = NULL;
                            peer->recv_queue = NULL;
                        }
                    }

                    /* Init async send pipeline (lightweight for OT) */
                    if (ctx->plat && ctx->plat->pipeline_init) {
                        ctx->plat->pipeline_init(ctx, peer);
                    }

                    pt_peer_set_state(ctx, peer,
                                      PT_PEER_STATE_CONNECTED);
                    peer->hot.last_seen = (pt_tick_t)TickCount();

                    OTInetHostToString(peer->cold.info.address,
                                       ip_str);
                    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
                        "Connected to peer %u (\"%s\") at %s:%u",
                        (unsigned)peer->hot.id,
                        pt_get_peer_name(ctx,
                                         peer->hot.name_idx),
                        ip_str,
                        (unsigned)peer->cold.info.port);

                    /* Fire callback AFTER state transition */
                    if (ctx->callbacks.on_peer_connected != NULL) {
                        ctx->callbacks.on_peer_connected(
                            (PeerTalk_Context *)ctx,
                            peer->hot.id,
                            ctx->callbacks.user_data);
                    }
                }
            } else {
                PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
                    "TCP[%d] OTRcvConnect failed: %ld",
                    i, (long)err);

                if (peer != NULL) {
                    pt_peer_set_state(ctx, peer,
                                      PT_PEER_STATE_FAILED);
                    peer->hot.connection = NULL;
                }

                hot->peer = NULL;
                pt_ot_tcp_cleanup(ctx, i);
            }

            processed++;
            continue;
        }

        /* --- T_DISCONNECT during connect: connection rejected --- */
        if (PT_FLAG_TEST(hot->flags, PT_OT_FLAG_DISCONNECT)) {
            PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_DISCONNECT);

            /* Must call OTRcvDisconnect to acknowledge and clear state */
            OTRcvDisconnect(hot->ref, NULL);

            PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
                "TCP[%d] connection rejected (peer %u)",
                i, peer ? (unsigned)peer->hot.id : 0);

            if (peer != NULL) {
                pt_peer_set_state(ctx, peer,
                                  PT_PEER_STATE_FAILED);
                peer->hot.connection = NULL;
            }

            hot->peer = NULL;
            pt_ot_tcp_cleanup(ctx, i);
            processed++;
            continue;
        }
    }

    return processed;
}

/* ========================================================================== */
/* Disconnect                                                                  */
/* ========================================================================== */

/**
 * Disconnect a peer's TCP endpoint.
 *
 * Extracts the endpoint index from the peer's connection field
 * (stored as idx+1 to distinguish from NULL) and initiates close.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to disconnect
 * @return      0 on success, negative error code on failure
 */
int pt_ot_disconnect(struct pt_context *ctx, struct pt_peer *peer)
{
    pt_ot_data *od;
    pt_tcp_endpoint_hot *hot;
    int idx;

    if (peer == NULL || peer->hot.magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    if (peer->hot.connection == NULL)
        return PT_ERR_INVALID_STATE;

    /* Extract endpoint index (stored as idx+1) */
    idx = (int)(intptr_t)peer->hot.connection - 1;

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Disconnecting peer %u [ep=%d]",
        (unsigned)peer->hot.id, idx);

    /* Clear endpoint→peer link BEFORE tcp_close so the PT_EP_CLOSING
     * handler in the poll loop won't fire a duplicate callback. */
    od = pt_ot_get(ctx);
    hot = pt_ot_get_tcp_hot(od, idx);
    if (hot != NULL)
        hot->peer = NULL;

    /* Initiate TCP close (async - endpoint enters PT_EP_CLOSING) */
    pt_ot_tcp_close(ctx, idx);

    /* CRITICAL: Full peer cleanup so the peer can reconnect immediately.
     * The TCP endpoint will finish closing asynchronously in the poll loop
     * (PT_EP_CLOSING → cleanup), but the peer is freed now.
     * Matches the pattern used in all remote-initiated disconnect paths. */
    if (ctx->plat && ctx->plat->pipeline_cleanup) {
        ctx->plat->pipeline_cleanup(ctx, peer);
    }
    if (peer->send_queue) {
        pt_queue_free(peer->send_queue);
        pt_free(peer->send_queue);
        peer->send_queue = NULL;
    }
    if (peer->recv_queue) {
        pt_queue_free(peer->recv_queue);
        pt_free(peer->recv_queue);
        peer->recv_queue = NULL;
    }
    peer->hot.connection = NULL;
    pt_peer_set_state(ctx, peer, PT_PEER_STATE_DISCOVERED);

    return 0;
}

#endif /* PT_PLATFORM_OT */
