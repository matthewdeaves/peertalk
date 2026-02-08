/**
 * @file tcp_listen.c
 * @brief MacTCP TCP Listener Implementation
 *
 * TCPPassiveOpen for accepting incoming connections.
 * Uses hot/cold struct split for 68k cache efficiency.
 *
 * Key Insight from MacTCP Programmer's Guide:
 * "TCPPassiveOpen listens for an incoming connection. The command is
 * completed when a connection is established or when an error occurs."
 *
 * Unlike BSD sockets, you must issue a NEW TCPPassiveOpen after each
 * accepted connection. This is a one-shot operation.
 *
 * References:
 * - MacTCP Programmer's Guide (1989), Chapter 3: "TCP"
 */

#include "mactcp_defs.h"
#include "peer.h"
#include "pt_internal.h"
#include "pt_compat.h"
#include "queue.h"

#if defined(PT_PLATFORM_MACTCP)

#include <Devices.h>
#include <OSUtils.h>  /* For TickCount() - only in main loop! */

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
/* External Accessors                                                         */
/* ========================================================================== */

extern short pt_mactcp_get_refnum(void);

/* From tcp_mactcp.c */
extern int pt_mactcp_tcp_create_listener(struct pt_context *ctx);
extern int pt_mactcp_tcp_release_listener(struct pt_context *ctx);

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define DEFAULT_TCP_PORT 7354

#define TCP_PORT(ctx) \
    ((ctx)->config.tcp_port > 0 ? (ctx)->config.tcp_port : DEFAULT_TCP_PORT)

/* ========================================================================== */
/* Completion Routine                                                         */
/* ========================================================================== */

/**
 * Completion routine for async TCPPassiveOpen.
 * Called when connection arrives or timeout.
 *
 * CRITICAL: Called at INTERRUPT LEVEL.
 * - Cannot allocate or release memory
 * - Cannot make synchronous MacTCP calls
 * - Cannot call PT_Log
 * - Only modify hot struct fields
 *
 * DOD: userDataPtr points to hot struct - only hot fields modified.
 *
 * @param pb  Parameter block from completed operation
 */
pascal void pt_tcp_listen_completion(TCPiopb *pb)
{
    pt_tcp_stream_hot *hot = (pt_tcp_stream_hot *)pb->csParam.open.userDataPtr;

    hot->async_result = pb->ioResult;
    hot->async_pending = 0;

    /* Set log event for deferred logging */
    hot->log_events |= PT_LOG_EVT_LISTEN_COMPLETE;
    if (pb->ioResult != noErr) {
        hot->log_error_code = pb->ioResult;
        hot->log_events |= PT_LOG_EVT_ERROR;
    }

    /* Connection info stored in pb - main loop will copy to cold struct */
}

/* ========================================================================== */
/* Listen Start                                                               */
/* ========================================================================== */

/**
 * Start listening on a port.
 * Issues an async TCPPassiveOpen.
 *
 * DOD: Uses hot/cold struct split for listener.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success, -1 on error
 */
int pt_mactcp_listen_start(struct pt_context *ctx)
{
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
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
            "Listener not idle: state=%s", pt_state_name(hot->state));
        return -1;
    }

    port = TCP_PORT(ctx);

    /* Setup async passive open (pb in cold struct) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPPassiveOpen;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;

    /* CRITICAL: Do NOT use completion routine - poll ioResult instead.
     * Using ioCompletion UPPs causes crashes on shutdown when TCPAbort
     * cancels pending operations and the completion fires at interrupt time.
     * This matches the proven csend pattern.
     */
    cold->pb.ioCompletion = NULL;

    /* From MacTCP Programmer's Guide: validity bits for timeout params */
    cold->pb.csParam.open.ulpTimeoutValue = 0;   /* Use default (2 minutes) */
    cold->pb.csParam.open.ulpTimeoutAction = 1;  /* Abort on timeout */
    cold->pb.csParam.open.validityFlags = 0xC0;  /* ULP timeout fields valid */
    cold->pb.csParam.open.commandTimeoutValue = 0;  /* No command timeout (infinite) */

    /* Accept connections from any remote */
    cold->pb.csParam.open.remoteHost = 0;
    cold->pb.csParam.open.remotePort = 0;
    cold->pb.csParam.open.localHost = 0;
    cold->pb.csParam.open.localPort = port;

    cold->pb.csParam.open.tosFlags = 0x1;  /* Low delay */
    cold->pb.csParam.open.userDataPtr = (Ptr)hot;  /* Completion gets hot struct */

    hot->async_pending = 1;
    hot->async_result = 1;  /* In progress */
    hot->log_events = 0;
    hot->state = PT_STREAM_LISTENING;

    /* Issue async call */
    err = PBControlAsync((ParmBlkPtr)&cold->pb);

    if (err != noErr) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_CONNECT,
            "TCPPassiveOpen failed: %d", (int)err);
        hot->state = PT_STREAM_IDLE;
        hot->async_pending = 0;
        return -1;
    }

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT,
        "Listening on port %u", (unsigned)port);

    return 0;
}

/* ========================================================================== */
/* Listen Poll                                                                */
/* ========================================================================== */

/**
 * Poll for incoming connection.
 *
 * CRITICAL: The listener stream now holds the connection after PassiveOpen
 * completes. We need to:
 * 1. Find or create peer
 * 2. Transfer stream to peer's tcp slot
 * 3. Create new stream for listener
 *
 * DOD: Uses hot/cold struct split. Transfer copies both hot and cold data.
 *
 * @param ctx  PeerTalk context
 * @return     1 if connection accepted, 0 if still waiting, -1 on error
 */
int pt_mactcp_listen_poll(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *listener_hot = &md->listener_hot;
    pt_tcp_stream_cold *listener_cold = &md->listener_cold;
    struct pt_peer *peer;
    int client_idx;
    int i;
    ip_addr remote_ip;
    tcp_port remote_port;
    TCPiopb abort_pb;

    if (listener_hot->state != PT_STREAM_LISTENING)
        return 0;

    /* Check if async operation completed by polling ioResult directly.
     * This is the proven csend pattern - no completion routine needed.
     * ioResult > 0 means still in progress, <= 0 means complete.
     */
    if (listener_cold->pb.ioResult > 0)
        return 0;

    /* Operation complete - capture result in hot struct for fast access.
     * DOD: After this point, use hot->async_result not cold->pb.ioResult
     */
    listener_hot->async_pending = 0;
    listener_hot->async_result = listener_cold->pb.ioResult;

    /* Check result using hot struct value */
    if (listener_hot->async_result == commandTimeout) {
        /* No connection yet - restart listener */
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_CONNECT,
            "Listen timeout, restarting");
        listener_hot->state = PT_STREAM_IDLE;
        pt_mactcp_listen_start(ctx);
        return 0;
    }

    if (listener_hot->async_result != noErr) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_CONNECT,
            "Listen failed: %d", (int)listener_hot->async_result);
        listener_hot->state = PT_STREAM_IDLE;
        pt_mactcp_listen_start(ctx);
        return -1;
    }

    /* Connection accepted! Extract info from pb */
    remote_ip = listener_cold->pb.csParam.open.remoteHost;
    remote_port = listener_cold->pb.csParam.open.remotePort;

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT,
        "Incoming connection from %lu.%lu.%lu.%lu:%u",
        (remote_ip >> 24) & 0xFF,
        (remote_ip >> 16) & 0xFF,
        (remote_ip >> 8) & 0xFF,
        remote_ip & 0xFF,
        (unsigned)remote_port);

    /* Find peer or create one */
    peer = pt_peer_find_by_addr(ctx, remote_ip, 0);
    if (peer == NULL) {
        peer = pt_peer_create(ctx, "", remote_ip, remote_port);
    }

    if (peer == NULL) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
            "No peer slot for incoming connection");
        /* Abort this connection */
        pt_memset(&abort_pb, 0, sizeof(abort_pb));
        abort_pb.csCode = TCPAbort;
        abort_pb.ioCRefNum = md->driver_refnum;
        abort_pb.tcpStream = listener_hot->stream;
        PBControlSync((ParmBlkPtr)&abort_pb);

        listener_hot->state = PT_STREAM_IDLE;
        pt_mactcp_listen_start(ctx);
        return 0;
    }

    /* Find free client stream slot */
    client_idx = -1;
    for (i = 0; i < PT_MAX_PEERS; i++) {
        if (md->tcp_hot[i].state == PT_STREAM_UNUSED) {
            client_idx = i;
            break;
        }
    }

    if (client_idx < 0) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
            "No free TCP stream slot");
        /* Abort and restart */
        pt_memset(&abort_pb, 0, sizeof(abort_pb));
        abort_pb.csCode = TCPAbort;
        abort_pb.ioCRefNum = md->driver_refnum;
        abort_pb.tcpStream = listener_hot->stream;
        PBControlSync((ParmBlkPtr)&abort_pb);

        listener_hot->state = PT_STREAM_IDLE;
        pt_mactcp_listen_start(ctx);
        return 0;
    }

    /*
     * Transfer listener's stream to client slot.
     * This is the MacTCP pattern - the listener's stream
     * becomes the connected stream.
     *
     * DOD: Copy both hot and cold structs to client slot.
     * Buffer pointer transfers (not copied) - now owned by client.
     */
    md->tcp_hot[client_idx] = *listener_hot;
    md->tcp_cold[client_idx] = *listener_cold;

    /* Update client hot struct */
    md->tcp_hot[client_idx].peer_idx = (int8_t)(peer - ctx->peers);
    md->tcp_hot[client_idx].state = PT_STREAM_CONNECTED;
    md->tcp_hot[client_idx].log_events = 0;

    /* Update client cold struct with connection info */
    md->tcp_cold[client_idx].remote_ip = remote_ip;
    md->tcp_cold[client_idx].remote_port = remote_port;
    md->tcp_cold[client_idx].local_port = listener_cold->pb.csParam.open.localPort;

    /* Clear listener for new stream (DO NOT free buffer - it's now owned by client) */
    pt_memset(listener_hot, 0, sizeof(*listener_hot));
    listener_hot->state = PT_STREAM_UNUSED;
    listener_hot->peer_idx = -1;

    pt_memset(listener_cold, 0, sizeof(*listener_cold));
    /* rcv_buffer is now NULL - was transferred to client */

    /* Create new listener stream and start listening */
    if (pt_mactcp_listen_start(ctx) < 0) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
            "Failed to restart listener");
    }

    /* Allocate send and receive queues for the peer */
    peer->send_queue = pt_mactcp_alloc_peer_queue(ctx);
    peer->recv_queue = pt_mactcp_alloc_peer_queue(ctx);

    if (!peer->send_queue || !peer->recv_queue) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_MEMORY,
            "Failed to allocate queues for peer %u, rejecting connection",
            (unsigned)peer->hot.id);
        pt_mactcp_free_peer_queue(peer->send_queue);
        pt_mactcp_free_peer_queue(peer->recv_queue);
        peer->send_queue = NULL;
        peer->recv_queue = NULL;
        /* Clean up - don't accept this connection */
        md->tcp_hot[client_idx].state = PT_STREAM_UNUSED;
        return 0;
    }

    /* Update peer state */
    pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTED);
    peer->hot.last_seen = (pt_tick_t)TickCount();

    /* CRITICAL: Reset receive buffer state for new connection.
     * If peer struct is reused, ibuflen may have stale data. */
    peer->cold.ibuflen = 0;

    /* Store stream index in peer's platform-specific connection handle */
    /* Store idx+1 so stream 0 doesn't become NULL pointer */
    peer->hot.connection = (void *)(intptr_t)(client_idx + 1);

    /* Fire callback */
    if (ctx->callbacks.on_peer_connected != NULL) {
        ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                         peer->hot.id,
                                         ctx->callbacks.user_data);
    }

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT,
        "Accepted connection from peer %u at 0x%08X (assigned to slot %d)",
        (unsigned)peer->hot.id, remote_ip, client_idx);

    return 1;
}

/* ========================================================================== */
/* Listen Stop                                                                */
/* ========================================================================== */

/**
 * Stop listening.
 *
 * DOD: Uses hot/cold struct split for listener.
 *
 * @param ctx  PeerTalk context
 */
void pt_mactcp_listen_stop(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot = &md->listener_hot;
    TCPiopb abort_pb;

    if (hot->state == PT_STREAM_LISTENING) {
        /* Abort pending listen */
        pt_memset(&abort_pb, 0, sizeof(abort_pb));
        abort_pb.csCode = TCPAbort;
        abort_pb.ioCRefNum = md->driver_refnum;
        abort_pb.tcpStream = hot->stream;
        PBControlSync((ParmBlkPtr)&abort_pb);
    }

    pt_mactcp_tcp_release_listener(ctx);

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT, "Listen stopped");
}

#endif /* PT_PLATFORM_MACTCP */
