/*
 * pt_core.c -- PeerTalk core: init, shutdown, callbacks, peer management
 */

#include "pt_internal.h"

#include <stdlib.h> /* malloc, free */
#include <string.h>

#ifdef PT_PLATFORM_POSIX
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>
#endif

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
#include <Events.h>
#include <Memory.h>
#include <Gestalt.h>
#endif

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */

unsigned long pt_get_time(void)
{
#if defined(PT_PLATFORM_POSIX)
    return (unsigned long)time(NULL);
#elif defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
    /* TickCount() returns ticks at ~60Hz, safe at main loop time */
    return (unsigned long)(TickCount() / 60);
#else
    return 0;
#endif
}

/* ------------------------------------------------------------------ */
/* Platform identification (R24)                                       */
/* ------------------------------------------------------------------ */

static void pt_log_platform_info(void)
{
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
    /* Gestalt with predefined selectors is Table B-3 safe */
    long machine_type = 0;
    long proc_type = 0;
    long sys_ver = 0;

    if (Gestalt(gestaltMachineType, &machine_type) != noErr)
        machine_type = -1;
    if (Gestalt(gestaltProcessorType, &proc_type) != noErr)
        proc_type = -1;
    if (Gestalt(gestaltSystemVersion, &sys_ver) != noErr)
        sys_ver = 0;

    CLOG_INFO("Platform: machine=%ld, cpu=%ld, system=%lx",
              machine_type, proc_type, sys_ver);
#if defined(PT_PLATFORM_MACTCP)
    CLOG_INFO("Backend: MacTCP");
#else
    CLOG_INFO("Backend: Open Transport");
#endif

#elif defined(PT_PLATFORM_POSIX)
    {
        struct utsname u;
        if (uname(&u) == 0) {
            CLOG_INFO("Platform: %s %s %s", u.sysname, u.release,
                      u.machine);
        }
        CLOG_INFO("Backend: POSIX");
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Error firing                                                        */
/* ------------------------------------------------------------------ */

void pt_fire_error(PT_Context_Internal *ctx, PT_Status err,
                   const char *desc)
{
    CLOG_WARN("Error %d: %s", (int)err, desc ? desc : "");
    if (ctx->callbacks.on_error) {
        ctx->callbacks.on_error(err, desc,
                                ctx->callbacks.on_error_data);
    }
}

/* ------------------------------------------------------------------ */
/* Peer management                                                     */
/* ------------------------------------------------------------------ */

PT_Peer_Internal *pt_find_peer_by_ip(PT_Context_Internal *ctx,
                                     unsigned long ip)
{
    int i;
    for (i = 0; i < ctx->max_peers; i++) {
        if (ctx->peers[i].in_use && ctx->peers[i].ip_addr == ip) {
            return &ctx->peers[i];
        }
    }
    return NULL;
}

PT_Peer_Internal *pt_alloc_peer(PT_Context_Internal *ctx)
{
    int i;
    for (i = 0; i < ctx->max_peers; i++) {
        if (!ctx->peers[i].in_use) {
            /* Clear non-buffer fields individually to preserve
               buffer pointers set during pt_memory_allocate */
            ctx->peers[i].name[0] = '\0';
            ctx->peers[i].state = PT_PEER_DISCOVERED;
            ctx->peers[i].ip_addr = 0;
            ctx->peers[i].last_seen = 0;
            ctx->peers[i].last_tcp_activity = 0;
            ctx->peers[i].connect_start = 0;
            ctx->peers[i].in_use = 1;
            ctx->peers[i].tcp_recv_len = 0;
            ctx->peers[i].reassembly_type = 0;
            ctx->peers[i].reassembly_received = 0;
            ctx->peers[i].reassembly_total = 0;
            ctx->peers[i].reassembly_timer = 0;
#if defined(PT_PLATFORM_POSIX)
            ctx->peers[i].platform_peer.tcp_fd = -1;
#elif defined(PT_PLATFORM_MACTCP)
            ctx->peers[i].platform_peer.tcp_stream = NULL;
#elif defined(PT_PLATFORM_OT)
            ctx->peers[i].platform_peer.endpoint = NULL;
            ctx->peers[i].platform_peer.events = 0;
#endif
            return &ctx->peers[i];
        }
    }
    return NULL;
}

void pt_handle_incoming_connection(PT_Context_Internal *ctx,
                                   unsigned long peer_ip,
                                   PT_PlatformPeer *ppeer)
{
    PT_Peer_Internal *peer;

    peer = pt_find_peer_by_ip(ctx, peer_ip);
    if (!peer) {
        /* Unknown peer connecting -- allocate new slot */
        peer = pt_alloc_peer(ctx);
        if (!peer) {
            /* No room -- close connection */
#if defined(PT_PLATFORM_POSIX)
            if (ppeer->tcp_fd >= 0) {
                close(ppeer->tcp_fd);
            }
#endif
            pt_fire_error(ctx, PT_ERR_NO_ROOM,
                          "No peer slots for incoming connection");
            return;
        }
        peer->ip_addr = peer_ip;
        peer->name[0] = '\0';
        ctx->peer_count++;
    }

    /* Accept the connection */
    peer->platform_peer = *ppeer;
    peer->state = PT_PEER_CONNECTED;
    peer->last_tcp_activity = ctx->current_time;
    peer->tcp_recv_len = 0;
    peer->connect_start = 0;

    CLOG_INFO("Incoming TCP connection accepted");

    if (ctx->callbacks.on_connected) {
        ctx->callbacks.on_connected((PT_Peer *)peer,
                                    ctx->callbacks.on_connected_data);
    }
}

void pt_handle_peer_disconnect(PT_Context_Internal *ctx,
                               PT_Peer_Internal *peer,
                               PT_DisconnectReason reason)
{
    if (peer->state != PT_PEER_CONNECTED) return;

    ctx->platform_ops->tcp_disconnect(ctx, peer);

    peer->state = PT_PEER_DISCONNECTED;
    peer->tcp_recv_len = 0;
    peer->reassembly_total = 0;
    peer->reassembly_received = 0;
    peer->connect_start = 0;

    CLOG_INFO("Peer %s disconnected (reason: %d)",
              peer->name, (int)reason);

    if (ctx->callbacks.on_disconnected) {
        ctx->callbacks.on_disconnected((PT_Peer *)peer, reason,
                                       ctx->callbacks.on_disconnected_data);
    }
}

/* ------------------------------------------------------------------ */
/* Internal: send goodbye frame to a connected peer                    */
/* ------------------------------------------------------------------ */

static void send_goodbye(PT_Context_Internal *ctx,
                          PT_Peer_Internal *peer)
{
    unsigned char goodbye[PT_TCP_HEADER_SIZE];
    goodbye[0] = 0;
    goodbye[1] = 0;
    goodbye[2] = PT_MSG_TYPE_GOODBYE;
    goodbye[3] = 0;
    ctx->platform_ops->tcp_send(ctx, peer, goodbye, PT_TCP_HEADER_SIZE);
}

/* ------------------------------------------------------------------ */
/* Public API: Lifecycle                                               */
/* ------------------------------------------------------------------ */

PT_Status PT_Init(PT_Context **out_ctx, const char *name)
{
    PT_Context_Internal *ctx;
    size_t namelen;
    PT_Status status;

    if (!out_ctx || !name) return PT_ERR_INVALID_ARG;

    namelen = strlen(name);
    if (namelen > PT_NAME_MAX) return PT_ERR_INVALID_ARG;

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
    /* Extend heap BEFORE any allocation (R17).
     * Must be the first Memory Manager call in the application.
     * Without this, the heap stays at SIZE resource minimum and
     * NewPtrClear below will fail or leave no room for buffers. */
    MaxApplZone();
    MoreMasters();
    MoreMasters();
    MoreMasters();
    MoreMasters();

    ctx = (PT_Context_Internal *)NewPtrClear(
        (Size)sizeof(PT_Context_Internal));
#else
    ctx = (PT_Context_Internal *)malloc(sizeof(PT_Context_Internal));
    if (ctx) memset(ctx, 0, sizeof(PT_Context_Internal));
#endif
    if (!ctx) return PT_ERR_INIT;

    memcpy(ctx->name, name, namelen + 1);

    /* Default all message types to PT_RELIABLE */
    {
        int i;
        for (i = 0; i < 256; i++) {
            ctx->message_types[i] = PT_RELIABLE;
        }
    }

    /* Allocate memory (Classic Mac overrides sizes via FreeMem) */
    if (pt_memory_allocate(ctx, PT_DEFAULT_MAX_PEERS,
                           PT_DEFAULT_TCP_RECV, PT_DEFAULT_TCP_SEND,
                           PT_DEFAULT_UDP_BUF,
                           PT_DEFAULT_REASSEMBLY) != 0) {
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
        DisposePtr((Ptr)ctx);
#else
        free(ctx);
#endif
        return PT_ERR_INIT;
    }

    /* Get platform ops */
#if defined(PT_PLATFORM_POSIX)
    ctx->platform_ops = posix_get_ops();
#elif defined(PT_PLATFORM_MACTCP)
    ctx->platform_ops = mactcp_get_ops();
#elif defined(PT_PLATFORM_OT)
    ctx->platform_ops = ot_get_ops();
#endif

    status = ctx->platform_ops->init(ctx);
    if (status != PT_OK) {
        pt_memory_free(ctx);
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
        DisposePtr((Ptr)ctx);
#else
        free(ctx);
#endif
        return status;
    }

    ctx->current_time = pt_get_time();

    pt_log_platform_info();
    CLOG_INFO("PeerTalk initialized: name='%s', max_peers=%d",
              ctx->name, ctx->max_peers);

    *out_ctx = (PT_Context *)ctx;
    return PT_OK;
}

void PT_Shutdown(PT_Context *pub_ctx)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    int i;

    if (!ctx) return;

    /* Send goodbye to all connected peers */
    for (i = 0; i < ctx->max_peers; i++) {
        if (ctx->peers[i].in_use &&
            ctx->peers[i].state == PT_PEER_CONNECTED) {
            send_goodbye(ctx, &ctx->peers[i]);
            ctx->platform_ops->tcp_disconnect(ctx, &ctx->peers[i]);

            if (ctx->callbacks.on_disconnected) {
                ctx->callbacks.on_disconnected(
                    (PT_Peer *)&ctx->peers[i], PT_QUIT,
                    ctx->callbacks.on_disconnected_data);
            }
        }
    }

    ctx->platform_ops->shutdown(ctx);
    pt_memory_free(ctx);

    CLOG_INFO("PeerTalk shutdown complete");

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
    DisposePtr((Ptr)ctx);
#else
    free(ctx);
#endif
}

/* ------------------------------------------------------------------ */
/* Public API: Discovery                                               */
/* ------------------------------------------------------------------ */

PT_Status PT_StartDiscovery(PT_Context *pub_ctx)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    PT_Status status;

    if (!ctx) return PT_ERR_INVALID_ARG;

    ctx->discovery_active = 1;
    ctx->discovery_listening = 1;
    ctx->discovery_timer = 0;

    status = ctx->platform_ops->udp_listen(ctx, PT_DISCOVERY_PORT);
    if (status != PT_OK) return status;

    status = ctx->platform_ops->udp_listen(ctx, PT_UDP_MSG_PORT);
    if (status != PT_OK) return status;

    status = ctx->platform_ops->tcp_listen(ctx);
    if (status != PT_OK) return status;

    CLOG_INFO("Discovery started");
    return PT_OK;
}

void PT_StopDiscovery(PT_Context *pub_ctx)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx) return;

    ctx->discovery_active = 0;
    CLOG_INFO("Discovery broadcasting stopped (still listening)");
}

/* ------------------------------------------------------------------ */
/* Public API: Connections                                             */
/* ------------------------------------------------------------------ */

PT_Status PT_Connect(PT_Context *pub_ctx, PT_Peer *pub_peer)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    PT_Peer_Internal *peer = (PT_Peer_Internal *)pub_peer;
    PT_Status status;

    if (!ctx || !peer) return PT_ERR_INVALID_ARG;

    if (peer->state != PT_PEER_DISCOVERED &&
        peer->state != PT_PEER_DISCONNECTED) {
        return PT_ERR_NOT_CONNECTED;
    }

    peer->tcp_recv_len = 0;
    peer->reassembly_total = 0;
    peer->reassembly_received = 0;

    status = ctx->platform_ops->tcp_connect(ctx, peer);
    if (status != PT_OK) return status;

    peer->connect_start = ctx->current_time;
    CLOG_INFO("Connecting to %s...", peer->name);
    return PT_OK;
}

void PT_Disconnect(PT_Context *pub_ctx, PT_Peer *pub_peer)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    PT_Peer_Internal *peer = (PT_Peer_Internal *)pub_peer;

    if (!ctx || !peer) return;
    if (peer->state != PT_PEER_CONNECTED) return;

    send_goodbye(ctx, peer);

    pt_handle_peer_disconnect(ctx, peer, PT_QUIT);
}

/* ------------------------------------------------------------------ */
/* Public API: Messaging (stubs -- implemented in pt_messaging.c)      */
/* ------------------------------------------------------------------ */

void PT_RegisterMessage(PT_Context *pub_ctx, unsigned char type,
                        PT_Transport transport)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx || type == PT_MSG_TYPE_GOODBYE) return;
    ctx->message_types[type] = transport;
}

/* PT_Send and PT_Broadcast are defined in pt_messaging.c */

/* ------------------------------------------------------------------ */
/* Public API: Event Loop                                              */
/* ------------------------------------------------------------------ */

void PT_Poll(PT_Context *pub_ctx)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    int i;

    if (!ctx) return;

    ctx->current_time = pt_get_time();

    /* Discovery broadcast timer */
    if (ctx->discovery_active &&
        ctx->current_time >= ctx->discovery_timer) {
        pt_discovery_broadcast(ctx);
        ctx->discovery_timer = ctx->current_time +
                               PT_DISCOVERY_INTERVAL;
    }

    /* Platform poll */
    ctx->platform_ops->poll(ctx);

    /* Discovery timeouts */
    pt_discovery_check_timeouts(ctx);

    /* Connection timeouts */
    for (i = 0; i < ctx->max_peers; i++) {
        if (!ctx->peers[i].in_use) continue;

        /* TCP connect timeout */
        if (ctx->peers[i].connect_start > 0 &&
            ctx->current_time - ctx->peers[i].connect_start >=
                PT_CONNECT_TIMEOUT) {
            CLOG_WARN("Connect timeout for %s", ctx->peers[i].name);
            ctx->platform_ops->tcp_disconnect(ctx, &ctx->peers[i]);
            ctx->peers[i].connect_start = 0;
            ctx->peers[i].state = PT_PEER_DISCONNECTED;
            pt_fire_error(ctx, PT_ERR_SEND_FAILED,
                          "Connection timeout");
        }

        /* TCP inactivity timeout */
        if (ctx->peers[i].state == PT_PEER_CONNECTED &&
            ctx->peers[i].last_tcp_activity > 0 &&
            ctx->current_time - ctx->peers[i].last_tcp_activity >=
                PT_TCP_TIMEOUT) {
            CLOG_WARN("TCP timeout for %s", ctx->peers[i].name);
            pt_handle_peer_disconnect(ctx, &ctx->peers[i],
                                      PT_TIMEOUT);
        }
    }

    /* Reassembly timeouts */
    pt_messaging_check_reassembly_timeouts(ctx);
}

/* ------------------------------------------------------------------ */
/* Public API: Callback Registration                                   */
/* ------------------------------------------------------------------ */

void PT_OnPeerDiscovered(PT_Context *pub_ctx, PT_PeerCallback cb,
                         void *user_data)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx) return;
    ctx->callbacks.on_peer_discovered = cb;
    ctx->callbacks.on_peer_discovered_data = user_data;
}

void PT_OnPeerLost(PT_Context *pub_ctx, PT_PeerCallback cb,
                   void *user_data)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx) return;
    ctx->callbacks.on_peer_lost = cb;
    ctx->callbacks.on_peer_lost_data = user_data;
}

void PT_OnConnected(PT_Context *pub_ctx, PT_PeerCallback cb,
                    void *user_data)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx) return;
    ctx->callbacks.on_connected = cb;
    ctx->callbacks.on_connected_data = user_data;
}

void PT_OnDisconnected(PT_Context *pub_ctx, PT_DisconnectCallback cb,
                       void *user_data)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx) return;
    ctx->callbacks.on_disconnected = cb;
    ctx->callbacks.on_disconnected_data = user_data;
}

void PT_OnMessage(PT_Context *pub_ctx, unsigned char type,
                  PT_MessageCallback cb, void *user_data)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx) return;
    ctx->callbacks.on_message[type] = cb;
    ctx->callbacks.on_message_data[type] = user_data;
}

void PT_OnError(PT_Context *pub_ctx, PT_ErrorCallback cb,
                void *user_data)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx) return;
    ctx->callbacks.on_error = cb;
    ctx->callbacks.on_error_data = user_data;
}

/* ------------------------------------------------------------------ */
/* Public API: Peer Info                                               */
/* ------------------------------------------------------------------ */

int PT_GetPeerCount(PT_Context *pub_ctx)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx) return 0;
    return ctx->peer_count;
}

PT_Peer *PT_GetPeer(PT_Context *pub_ctx, int index)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    int count = 0;
    int i;

    if (!ctx || index < 0) return NULL;

    for (i = 0; i < ctx->max_peers; i++) {
        if (ctx->peers[i].in_use) {
            if (count == index) {
                return (PT_Peer *)&ctx->peers[i];
            }
            count++;
        }
    }
    return NULL;
}

const char *PT_PeerName(PT_Peer *pub_peer)
{
    PT_Peer_Internal *peer = (PT_Peer_Internal *)pub_peer;
    if (!peer) return "";
    return peer->name;
}

PT_PeerState PT_GetPeerState(PT_Peer *pub_peer)
{
    PT_Peer_Internal *peer = (PT_Peer_Internal *)pub_peer;
    if (!peer) return PT_PEER_DISCONNECTED;
    return peer->state;
}

PT_Status PT_SetName(PT_Context *pub_ctx, const char *name)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    size_t namelen;
    if (!ctx || !name) return PT_ERR_INVALID_ARG;
    namelen = strlen(name);
    if (namelen > PT_NAME_MAX) return PT_ERR_INVALID_ARG;
    memcpy(ctx->name, name, namelen + 1);
    return PT_OK;
}
