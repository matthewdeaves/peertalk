/**
 * @file tcp_server_ot.c
 * @brief Open Transport TCP Server (tilisten Pattern)
 *
 * Accepts incoming TCP connections using OT's tilisten module.
 * The listener endpoint stays in T_IDLE state permanently while
 * new endpoints receive accepted connections via OTAccept handoff.
 *
 * Key differences from MacTCP:
 * - OT listener stays active (MacTCP listener stream becomes the connection)
 * - OT uses OTListen + OTAccept to hand off to new endpoint from pool
 * - tilisten module prevents kOTLookErr from concurrent T_LISTEN events
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 5: "Connection-Oriented"
 * - tilisten module: pp.9450-9528
 */

#include "ot_defs.h"
#include "peer.h"
#include "queue.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_OT)

#include <Gestalt.h>
#include <OSUtils.h>  /* TickCount() - main loop only! */

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

#define LISTEN_QLEN  4   /* Queue up to 4 pending connections */

/* ========================================================================== */
/* External Functions                                                         */
/* ========================================================================== */

/* From tcp_ot.c */
extern int pt_ot_tcp_create(struct pt_context *ctx);
extern int pt_ot_tcp_set_options(struct pt_context *ctx, int idx);
extern void pt_ot_tcp_close(struct pt_context *ctx, int idx);
extern void pt_ot_tcp_cleanup(struct pt_context *ctx, int idx);

/* Forward declaration (listen_stop called from listen_start on error) */
void pt_ot_listen_stop(struct pt_context *ctx);

/* ========================================================================== */
/* Listener Endpoint Creation                                                 */
/* ========================================================================== */

/**
 * Create the listener endpoint.
 *
 * Uses "tilisten,tcp" configuration for reliable multi-connection accept.
 * Falls back to plain "tcp" if tilisten is not available.
 *
 * The listener endpoint uses od->listener_hot/listener_cold, which are
 * separate from the per-peer tcp_pool.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success, -1 on failure
 */
static int pt_ot_listener_create(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = &od->listener_hot;
    OTConfigurationRef config;
    EndpointRef ref;
    OSStatus err;

    if (hot->state != PT_EP_UNUSED) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "Listener already created (state=%s)",
            pt_ep_state_name(hot->state));
        return -1;
    }

    /* Try tilisten,tcp first (requires OT 1.1.1+, prevents kOTLookErr).
     * Falls back to plain tcp if tilisten not available. */
    config = OTCreateConfiguration("tilisten,tcp");
    if (config == kOTInvalidConfigurationPtr ||
        config == kOTNoMemoryConfigurationPtr) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_INIT,
            "tilisten not available, using plain tcp for listener");
        config = OTCloneConfiguration(od->tcp_config);
        if (config == NULL) {
            PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
                "Failed to clone TCP config for listener");
            return -1;
        }
    }

    /* Open endpoint synchronously */
    ref = OTOpenEndpoint(config, 0, NULL, &err);
    /* config is now disposed by OT */

    if (err != kOTNoError || ref == kOTInvalidEndpointRef) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTOpenEndpoint(listener) failed: %ld", (long)err);
        return -1;
    }

    hot->ref = ref;
    hot->state = PT_EP_UNBOUND;
    PT_FLAGS_CLEAR_ALL(hot->flags);
    hot->async_result = 0;
    hot->peer = NULL;
    hot->close_start = 0;
    hot->log_error_code = 0;
    hot->log_events = 0;

    /* Install notifier using pre-created UPP */
    if (od->tcp_notifier_upp == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT,
            "TCP notifier UPP not created");
        OTCloseProvider(ref);
        hot->ref = kOTInvalidEndpointRef;
        hot->state = PT_EP_UNUSED;
        return -1;
    }

    err = OTInstallNotifier(ref, od->tcp_notifier_upp, hot);
    if (err != kOTNoError) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTInstallNotifier(listener) failed: %ld", (long)err);
        OTCloseProvider(ref);
        hot->ref = kOTInvalidEndpointRef;
        hot->state = PT_EP_UNUSED;
        return -1;
    }

    /* Set async mode for notifier-driven events */
    OTSetAsynchronous(ref);

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
        "Listener endpoint created: ref=0x%08lX",
        (unsigned long)ref);

    return 0;
}

/**
 * Bind listener with port and listen queue.
 *
 * @param ctx   PeerTalk context
 * @param port  Port to listen on
 * @param qlen  Listen queue length (max pending connections)
 * @return      0 on success, -1 on failure
 */
static int pt_ot_listener_bind(struct pt_context *ctx,
                                InetPort port, OTQLen qlen)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = &od->listener_hot;
    pt_tcp_endpoint_cold *cold = od->listener_cold;
    TBind bind_req, bind_ret;
    OSStatus err;

    if (hot->state != PT_EP_UNBOUND) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "Listener bind: wrong state %s",
            pt_ep_state_name(hot->state));
        return -1;
    }

    if (cold == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Listener cold data not allocated");
        return -1;
    }

    /* Setup local address in cold data */
    OTInitInetAddress(&cold->local_addr, port, kOTAnyInetAddress);

    bind_req.addr.buf = (UInt8 *)&cold->local_addr;
    bind_req.addr.len = sizeof(InetAddress);
    bind_req.addr.maxlen = sizeof(InetAddress);
    bind_req.qlen = qlen;  /* CRITICAL: qlen > 0 enables listening */

    bind_ret.addr.buf = (UInt8 *)&cold->local_addr;
    bind_ret.addr.len = 0;
    bind_ret.addr.maxlen = sizeof(InetAddress);
    bind_ret.qlen = 0;

    err = OTBind(hot->ref, &bind_req, &bind_ret);
    if (err != kOTNoError) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTBind(listener, port=%u, qlen=%u) failed: %ld",
            (unsigned)port, (unsigned)qlen, (long)err);
        return -1;
    }

    hot->state = PT_EP_IDLE;

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
        "Listener bound: port=%u qlen=%u",
        (unsigned)cold->local_addr.fPort, (unsigned)qlen);

    return 0;
}

/* ========================================================================== */
/* Listen Start / Stop                                                        */
/* ========================================================================== */

/**
 * Start TCP listener.
 *
 * Creates listener endpoint with tilisten module, binds to configured
 * port with listen queue. After this, T_LISTEN events will fire in the
 * notifier when clients connect.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success, -1 on failure
 */
int pt_ot_listen_start(struct pt_context *ctx)
{
    InetPort port;

    /* Create listener endpoint */
    if (pt_ot_listener_create(ctx) < 0)
        return -1;

    /* Bind with listen queue */
    port = ctx->config.tcp_port > 0
         ? ctx->config.tcp_port
         : PT_DEFAULT_TCP_PORT;

    if (pt_ot_listener_bind(ctx, port, LISTEN_QLEN) < 0) {
        pt_ot_listen_stop(ctx);
        return -1;
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "TCP listener started on port %u (qlen=%d)",
        (unsigned)port, LISTEN_QLEN);

    return 0;
}

/**
 * Stop TCP listener.
 *
 * Unbinds and closes the listener endpoint.
 *
 * @param ctx  PeerTalk context
 */
void pt_ot_listen_stop(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = &od->listener_hot;

    if (hot->ref == kOTInvalidEndpointRef)
        return;

    /* Try orderly unbind if endpoint is bound */
    if (hot->state >= PT_EP_IDLE) {
        OTResult ep_state = OTGetEndpointState(hot->ref);
        if (ep_state == T_IDLE)
            OTUnbind(hot->ref);
    }

    OTCloseProvider(hot->ref);
    hot->ref = kOTInvalidEndpointRef;
    hot->state = PT_EP_UNUSED;
    PT_FLAGS_CLEAR_ALL(hot->flags);

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT, "TCP listener stopped");
}

/* ========================================================================== */
/* Listen Poll                                                                */
/* ========================================================================== */

/**
 * Poll for incoming TCP connections.
 *
 * Implements the tilisten accept pattern:
 * 1. Check T_LISTEN flag (set by notifier)
 * 2. Call OTListen to get pending connection info
 * 3. Allocate new endpoint from pool via pt_ot_tcp_create
 * 4. Call OTAccept to hand off connection to new endpoint
 * 5. Find or create peer, set states, fire callback
 *
 * The listener stays in T_IDLE and continues accepting more connections.
 *
 * @param ctx  PeerTalk context
 * @return     1 if connection accepted, 0 if nothing happened, -1 on error
 */
int pt_ot_listen_poll(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *listener = &od->listener_hot;
    pt_tcp_endpoint_hot *client_hot;
    pt_tcp_endpoint_cold *client_cold;
    struct pt_peer *peer;
    TCall call;
    InetAddress from_addr;
    OSStatus err;
    int client_idx;
    char ip_str[PT_IP_STR_LEN];

    /* Listener must be bound and idle */
    if (listener->ref == kOTInvalidEndpointRef ||
        listener->state != PT_EP_IDLE)
        return 0;

    /* Check for T_LISTEN event using atomic test */
    if (!PT_FLAG_TEST(listener->flags, PT_OT_FLAG_LISTEN_PENDING))
        return 0;

    PT_FLAG_CLEAR(listener->flags, PT_OT_FLAG_LISTEN_PENDING);

    /* Process listener deferred log events */
    if (listener->log_events != 0) {
        listener->log_events = 0;
    }

    /* Setup call structure to receive pending connection info */
    pt_memset(&call, 0, sizeof(TCall));
    call.addr.buf = (UInt8 *)&from_addr;
    call.addr.maxlen = sizeof(InetAddress);

    /* Get pending connection info */
    err = OTListen(listener->ref, &call);

    if (err == kOTNoDataErr) {
        /* No pending connection (race with flag clear) */
        return 0;
    }

    if (err != kOTNoError) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "OTListen failed: %ld", (long)err);
        return -1;
    }

    OTInetHostToString(from_addr.fHost, ip_str);
    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Incoming connection from %s:%u", ip_str, from_addr.fPort);

    /* Optional: check if application wants to accept this connection */
    if (ctx->callbacks.on_connection_requested != NULL) {
        PeerTalk_PeerInfo info;
        pt_memset(&info, 0, sizeof(info));
        info.address = from_addr.fHost;
        info.port = from_addr.fPort;

        if (!ctx->callbacks.on_connection_requested(
                (PeerTalk_Context *)ctx, &info,
                ctx->callbacks.user_data)) {
            PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
                "Connection from %s:%u rejected by application",
                ip_str, from_addr.fPort);
            OTSndDisconnect(listener->ref, &call);
            return 0;
        }
    }

    /* Allocate client endpoint from pool (O(1) bitmap) */
    client_idx = pt_ot_tcp_create(ctx);
    if (client_idx < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No free endpoint for incoming connection (pool exhausted)");
        OTSndDisconnect(listener->ref, &call);
        return 0;
    }

    client_hot = pt_ot_get_tcp_hot(od, client_idx);
    client_cold = pt_ot_get_tcp_cold(od, client_idx);
    if (client_hot == NULL || client_cold == NULL) {
        pt_ot_tcp_cleanup(ctx, client_idx);
        OTSndDisconnect(listener->ref, &call);
        return 0;
    }

    /* Store remote address in client cold data */
    client_cold->remote_addr = from_addr;
    client_hot->state = PT_EP_INCOMING;

    /* Accept the connection, handing off to client endpoint.
     *
     * Per OT docs p.112-113: If the accepting endpoint is not bound,
     * the provider automatically binds it to the listener's address.
     *
     * With tilisten module: OTAccept won't get kOTLookErr from
     * concurrent T_LISTEN events (the module queues them).
     *
     * Returns kOTNoDataErr for async (expected), kOTNoError if
     * completed immediately. */
    err = OTAccept(listener->ref, client_hot->ref, &call);

    if (err != kOTNoError && err != kOTNoDataErr) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTAccept failed: %ld", (long)err);
        pt_ot_tcp_cleanup(ctx, client_idx);
        return -1;
    }

    /* Set TCP options on accepted endpoint (T_DATAXFER state).
     * Per OT docs: TCP options negotiable in all states except T_UNBND/T_UNINIT.
     * Must temporarily switch to sync mode for OTOptionManagement. */
    OTSetSynchronous(client_hot->ref);
    pt_ot_tcp_set_options(ctx, client_idx);
    OTSetAsynchronous(client_hot->ref);

    /* Set client to DATAXFER immediately.
     *
     * The handoff is virtually instant on local networks.
     * T_PASSCON will fire on the client endpoint and
     * T_ACCEPTCOMPLETE on the listener when OT finishes,
     * but our flag-based approach handles this naturally:
     * - T_DATA arriving before T_PASSCON just sets DATA_AVAILABLE flag
     * - Main loop will see DATAXFER state + DATA_AVAILABLE and read data
     */
    client_hot->state = PT_EP_DATAXFER;

    /* Find existing peer or create new one */
    peer = pt_peer_find_by_addr(ctx, from_addr.fHost, 0);
    if (peer == NULL) {
        peer = pt_peer_create(ctx, "",
                              from_addr.fHost, from_addr.fPort);
    }

    if (peer == NULL) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No peer slot for incoming connection from %s", ip_str);
        pt_ot_tcp_close(ctx, client_idx);
        return 0;
    }

    /* Link peer ↔ endpoint */
    client_hot->peer = peer;
    peer->hot.connection = (void *)(intptr_t)(client_idx + 1);
    peer->hot.last_seen = (pt_tick_t)TickCount();

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
                "Failed to allocate queues for peer %u",
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

    pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTED);

    /* Reset receive buffer state for new connection */
    peer->cold.ibuflen = 0;

    /* Fire callback AFTER state transition */
    if (ctx->callbacks.on_peer_connected != NULL) {
        ctx->callbacks.on_peer_connected(
            (PeerTalk_Context *)ctx,
            peer->hot.id,
            ctx->callbacks.user_data);
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Accepted connection from peer %u (\"%s\") at %s:%u [ep=%d]",
        (unsigned)peer->hot.id,
        pt_get_peer_name(ctx, peer->hot.name_idx),
        ip_str, (unsigned)from_addr.fPort, client_idx);

    return 1;
}

#endif /* PT_PLATFORM_OT */
