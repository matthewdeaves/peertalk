/**
 * @file tcp_connect.c
 * @brief MacTCP TCP Connection Implementation
 *
 * TCPActiveOpen for outgoing connections and TCPClose for disconnect.
 * Uses hot/cold struct split for 68k cache efficiency.
 *
 * References:
 * - MacTCP Programmer's Guide (1989), Chapter 3: "TCP"
 */

#include "mactcp_defs.h"
#include "peer.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_MACTCP)

#include <Devices.h>
#include <OSUtils.h>  /* For TickCount() - only in main loop! */

/* ========================================================================== */
/* External Accessors                                                         */
/* ========================================================================== */

extern short pt_mactcp_get_refnum(void);

/* From tcp_mactcp.c */
extern int pt_mactcp_tcp_create(struct pt_context *ctx, int idx);
extern int pt_mactcp_tcp_release(struct pt_context *ctx, int idx);

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define PT_CONNECT_TIMEOUT_TICKS  (30 * 60)  /* 30 seconds at 60 ticks/sec */
#define PT_CLOSE_TIMEOUT_TICKS    (10 * 60)  /* 10 seconds */

/* ========================================================================== */
/* Completion Routines                                                        */
/* ========================================================================== */

/**
 * Completion routine for async TCPActiveOpen.
 *
 * CRITICAL: Called at INTERRUPT LEVEL.
 * DOD: userDataPtr points to hot struct - only hot fields modified.
 *
 * @param pb  Parameter block from completed operation
 */
pascal void pt_tcp_connect_completion(TCPiopb *pb)
{
    pt_tcp_stream_hot *hot = (pt_tcp_stream_hot *)pb->csParam.open.userDataPtr;

    hot->async_result = pb->ioResult;
    hot->async_pending = 0;

    /* Set log event for deferred logging */
    hot->log_events |= PT_LOG_EVT_CONNECT_COMPLETE;
    if (pb->ioResult != noErr) {
        hot->log_error_code = pb->ioResult;
        hot->log_events |= PT_LOG_EVT_ERROR;
    }

    /* Connection info (local_ip, local_port) stored in pb
     * Main loop will copy to cold struct */
}

/**
 * Completion routine for async TCPClose.
 *
 * CRITICAL: Called at INTERRUPT LEVEL.
 * DOD: userDataPtr points to hot struct - only hot fields modified.
 *
 * @param pb  Parameter block from completed operation
 */
pascal void pt_tcp_close_completion(TCPiopb *pb)
{
    pt_tcp_stream_hot *hot = (pt_tcp_stream_hot *)pb->csParam.close.userDataPtr;

    hot->async_result = pb->ioResult;
    hot->async_pending = 0;

    /* Set log event for deferred logging */
    hot->log_events |= PT_LOG_EVT_CLOSE_COMPLETE;
    if (pb->ioResult != noErr) {
        hot->log_error_code = pb->ioResult;
        hot->log_events |= PT_LOG_EVT_ERROR;
    }
}

/* ========================================================================== */
/* Connect                                                                    */
/* ========================================================================== */

/**
 * Get peer index from peer pointer.
 */
static int pt_peer_index(struct pt_context *ctx, struct pt_peer *peer)
{
    if (peer == NULL || ctx->peers == NULL)
        return -1;
    return (int)(peer - ctx->peers);
}

/**
 * Get stream index from peer's connection handle.
 * Connection stores idx+1 so that stream 0 doesn't become NULL.
 */
static int pt_peer_stream_idx(struct pt_peer *peer)
{
    if (peer == NULL || peer->hot.connection == NULL)
        return -1;
    return (int)(intptr_t)peer->hot.connection - 1;
}

/**
 * Initiate connection to peer.
 *
 * DOD: Uses hot/cold struct split. Stores stream index in peer.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to connect to (must be in DISCOVERED state)
 * @return      0 on success, negative error code on failure
 */
int pt_mactcp_connect(struct pt_context *ctx, struct pt_peer *peer)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot;
    pt_tcp_stream_cold *cold;
    int idx;
    int i;
    OSErr err;

    if (peer == NULL || peer->hot.magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    if (peer->hot.state != PT_PEER_STATE_DISCOVERED)
        return PT_ERR_INVALID_STATE;

    /* Find free stream slot */
    idx = -1;
    for (i = 0; i < PT_MAX_PEERS; i++) {
        if (md->tcp_hot[i].state == PT_STREAM_UNUSED) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
            "No free TCP stream for connect");
        return PT_ERR_RESOURCE;
    }

    hot = &md->tcp_hot[idx];
    cold = &md->tcp_cold[idx];

    /* Create stream */
    if (pt_mactcp_tcp_create(ctx, idx) < 0)
        return PT_ERR_RESOURCE;

    /* Setup async active open (pb in cold struct) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPActiveOpen;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;

    /* CRITICAL: Do NOT use completion routine - poll ioResult instead.
     * Using ioCompletion UPPs causes crashes on shutdown when TCPAbort
     * cancels pending operations and the completion fires at interrupt time.
     * This matches the proven csend pattern.
     */
    cold->pb.ioCompletion = NULL;

    cold->pb.csParam.open.ulpTimeoutValue = 30;  /* 30 second timeout */
    cold->pb.csParam.open.ulpTimeoutAction = 1;  /* Abort on timeout */
    cold->pb.csParam.open.validityFlags = 0xC0;  /* ULP timeout fields valid */

    cold->pb.csParam.open.remoteHost = peer->cold.info.address;
    cold->pb.csParam.open.remotePort = peer->cold.info.port;
    cold->pb.csParam.open.localHost = 0;
    cold->pb.csParam.open.localPort = 0;  /* Let MacTCP assign */

    cold->pb.csParam.open.tosFlags = 0x2;  /* High throughput (not low delay) */
    cold->pb.csParam.open.userDataPtr = (Ptr)hot;  /* Completion gets hot struct */

    cold->remote_ip = peer->cold.info.address;
    cold->remote_port = peer->cold.info.port;
    hot->peer_idx = (int8_t)pt_peer_index(ctx, peer);
    hot->async_pending = 1;
    hot->async_result = 1;
    hot->log_events = 0;
    hot->state = PT_STREAM_CONNECTING;

    /* Link peer to stream */
    peer->hot.connection = (void *)(intptr_t)idx;
    pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTING);

    /* Store connect start time for timeout monitoring */
    cold->close_start = (unsigned long)TickCount();  /* Reuse close_start for connect timing */

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT,
        "Connecting to peer %u (\"%s\") at %lu.%lu.%lu.%lu:%u",
        (unsigned)peer->hot.id, peer->cold.name,
        (peer->cold.info.address >> 24) & 0xFF,
        (peer->cold.info.address >> 16) & 0xFF,
        (peer->cold.info.address >> 8) & 0xFF,
        peer->cold.info.address & 0xFF,
        (unsigned)peer->cold.info.port);

    /* Issue async call */
    err = PBControlAsync((ParmBlkPtr)&cold->pb);

    if (err != noErr) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_CONNECT,
            "TCPActiveOpen failed: %d", (int)err);
        hot->state = PT_STREAM_IDLE;
        hot->async_pending = 0;
        peer->hot.connection = NULL;
        pt_peer_set_state(ctx, peer, PT_PEER_STATE_FAILED);
        pt_mactcp_tcp_release(ctx, idx);
        return PT_ERR_NETWORK;
    }

    return 0;
}

/* ========================================================================== */
/* Connect Poll                                                               */
/* ========================================================================== */

/**
 * Poll connecting streams for completion.
 *
 * CRITICAL: Must check for connection timeout. TCPActiveOpen
 * can hang indefinitely if remote host is unreachable.
 *
 * DOD: Uses hot/cold struct split. Single-pass iteration over hot array.
 *
 * @param ctx  PeerTalk context
 * @return     Number of connections completed this poll
 */
int pt_mactcp_connect_poll(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    int i;
    int processed = 0;
    unsigned long now = (unsigned long)TickCount();
    TCPiopb abort_pb;

    for (i = 0; i < PT_MAX_PEERS; i++) {
        pt_tcp_stream_hot *hot = &md->tcp_hot[i];
        pt_tcp_stream_cold *cold = &md->tcp_cold[i];
        struct pt_peer *peer;

        if (hot->state != PT_STREAM_CONNECTING)
            continue;

        peer = PT_PEER_FROM_IDX(ctx, hot->peer_idx);
        if (peer == NULL)
            continue;

        /* Check for connection timeout BEFORE checking ioResult.
         * Use signed comparison to handle Ticks wrap-around correctly */
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

        /* Check if async operation completed by polling ioResult directly.
         * ioResult > 0 means still in progress, <= 0 means complete.
         */
        if (cold->pb.ioResult > 0)
            continue;  /* Still waiting */

        /* Operation complete - capture result */
        hot->async_pending = 0;
        hot->async_result = cold->pb.ioResult;

        /* Connection attempt completed */
        if (hot->async_result == noErr) {
            /* Success! Copy local address from pb to cold struct */
            cold->local_ip = cold->pb.csParam.open.localHost;
            cold->local_port = cold->pb.csParam.open.localPort;

            PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT,
                "Connected to peer %u (\"%s\")",
                (unsigned)peer->hot.id, peer->cold.name);

            hot->state = PT_STREAM_CONNECTED;
            pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTED);
            peer->hot.last_seen = (pt_tick_t)TickCount();

            if (ctx->callbacks.on_peer_connected != NULL) {
                ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                                 peer->hot.id,
                                                 ctx->callbacks.user_data);
            }
        } else {
            /* Failed */
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
                "Connect to peer %u failed: %d",
                (unsigned)peer->hot.id, (int)hot->async_result);

            pt_peer_set_state(ctx, peer, PT_PEER_STATE_FAILED);
            peer->hot.connection = NULL;

            pt_mactcp_tcp_release(ctx, i);
        }

        processed++;
    }

    return processed;
}

/* ========================================================================== */
/* Disconnect                                                                 */
/* ========================================================================== */

/**
 * Disconnect from peer.
 *
 * Uses async TCPClose to avoid blocking for 30+ seconds waiting for
 * FIN-ACK from remote. Timeout monitored in poll loop.
 *
 * DOD: Uses hot/cold struct split. Looks up stream by index stored in peer.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to disconnect from
 * @return      0 on success, negative error code on failure
 */
int pt_mactcp_disconnect(struct pt_context *ctx, struct pt_peer *peer)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot;
    pt_tcp_stream_cold *cold;
    int idx;
    OSErr err;

    if (peer == NULL || peer->hot.magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    idx = pt_peer_stream_idx(peer);
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return PT_ERR_INVALID_STATE;

    hot = &md->tcp_hot[idx];
    cold = &md->tcp_cold[idx];

    if (hot->state == PT_STREAM_CONNECTED) {
        /*
         * Graceful close - USE ASYNC to avoid 30+ second blocking!
         * From MacTCP Programmer's Guide: TCPClose can block waiting
         * for FIN-ACK from remote.
         */
        pt_memset(&cold->pb, 0, sizeof(cold->pb));
        cold->pb.csCode = TCPClose;
        cold->pb.ioCRefNum = md->driver_refnum;
        cold->pb.tcpStream = hot->stream;

        /* CRITICAL: Do NOT use completion routine - poll ioResult instead.
         * Using ioCompletion UPPs causes crashes on shutdown.
         */
        cold->pb.ioCompletion = NULL;

        cold->pb.csParam.close.ulpTimeoutValue = 10;
        cold->pb.csParam.close.ulpTimeoutAction = 1;  /* Abort on timeout */
        cold->pb.csParam.close.validityFlags = 0xC0;
        cold->pb.csParam.close.userDataPtr = (Ptr)hot;

        hot->state = PT_STREAM_CLOSING;
        hot->async_pending = 1;
        hot->log_events = 0;
        cold->close_start = (unsigned long)TickCount();

        PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT,
            "Closing connection to peer %u", (unsigned)peer->hot.id);

        /* ASYNC close - returns immediately, completion routine called later */
        err = PBControlAsync((ParmBlkPtr)&cold->pb);

        if (err != noErr && err != connectionClosing) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
                "TCPClose returned: %d", (int)err);
        }

        /* Main loop will monitor close_start and force abort if too slow */
        /* DO NOT release stream here - poll loop handles completion */
        return 0;
    }

    /* Stream not connected - release immediately */
    pt_mactcp_tcp_release(ctx, idx);
    peer->hot.connection = NULL;

    /* Update peer state */
    if (ctx->callbacks.on_peer_disconnected != NULL) {
        ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                            peer->hot.id, 0,
                                            ctx->callbacks.user_data);
    }

    pt_peer_set_state(ctx, peer, PT_PEER_STATE_UNUSED);

    return 0;
}

/* ========================================================================== */
/* Close Poll                                                                 */
/* ========================================================================== */

/**
 * Poll closing streams for completion.
 *
 * CRITICAL: Must be called from main poll loop to handle async TCPClose.
 * Monitors close timeout and forces abort if remote is unresponsive.
 *
 * DOD: Uses hot/cold struct split. Single-pass iteration over hot array.
 *
 * @param ctx  PeerTalk context
 * @return     Number of closes completed this poll
 */
int pt_mactcp_close_poll(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    int i;
    int processed = 0;
    unsigned long now = (unsigned long)TickCount();
    TCPiopb abort_pb;

    for (i = 0; i < PT_MAX_PEERS; i++) {
        pt_tcp_stream_hot *hot = &md->tcp_hot[i];
        pt_tcp_stream_cold *cold = &md->tcp_cold[i];
        struct pt_peer *peer;

        if (hot->state != PT_STREAM_CLOSING)
            continue;

        /* Check for close timeout (signed comparison for Ticks wrap) */
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

        /* Check if async operation completed by polling ioResult directly.
         * ioResult > 0 means still in progress, <= 0 means complete.
         */
        if (cold->pb.ioResult > 0)
            continue;

        /* Operation complete - capture result */
        hot->async_pending = 0;
        hot->async_result = cold->pb.ioResult;

        /* Close completed - get peer before releasing stream */
        peer = PT_PEER_FROM_IDX(ctx, hot->peer_idx);

        /* Log result */
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_CONNECT,
            "TCPClose completed: result=%d", (int)hot->async_result);

        /* Release stream */
        pt_mactcp_tcp_release(ctx, i);

        if (peer != NULL) {
            peer->hot.connection = NULL;

            if (ctx->callbacks.on_peer_disconnected != NULL) {
                ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                                    peer->hot.id, 0,
                                                    ctx->callbacks.user_data);
            }

            pt_peer_set_state(ctx, peer, PT_PEER_STATE_UNUSED);
        }

        processed++;
    }

    return processed;
}

#endif /* PT_PLATFORM_MACTCP */
