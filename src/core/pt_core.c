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

static unsigned long pt_get_time(void)
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
/* IP formatting                                                       */
/* ------------------------------------------------------------------ */

void pt_format_ip(unsigned long ip, char *buf)
{
    const unsigned char *b = (const unsigned char *)&ip;
    int i;
    int pos = 0;

    for (i = 0; i < 4; i++) {
        unsigned char val = b[i];
        if (val >= 100) {
            buf[pos++] = (char)('0' + val / 100);
            buf[pos++] = (char)('0' + (val / 10) % 10);
            buf[pos++] = (char)('0' + val % 10);
        } else if (val >= 10) {
            buf[pos++] = (char)('0' + val / 10);
            buf[pos++] = (char)('0' + val % 10);
        } else {
            buf[pos++] = (char)('0' + val);
        }
        if (i < 3) {
            buf[pos++] = '.';
        }
    }
    buf[pos] = '\0';
}

/* ------------------------------------------------------------------ */
/* Error firing                                                        */
/* ------------------------------------------------------------------ */

void pt_fire_error(PT_Context_Internal *ctx,
                   PT_Peer_Internal *peer,
                   PT_Status err, const char *desc)
{
    CLOG_WARN("Error %d: %s", (int)err, desc ? desc : "");
    if (ctx->callbacks.on_error) {
        ctx->callbacks.on_error((PT_Peer *)peer, err, desc,
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
            ctx->peers[i].addr_str[0] = '\0';
            ctx->peers[i].last_seen = 0;
            ctx->peers[i].last_tcp_activity = 0;
            ctx->peers[i].last_tcp_send = 0;
            ctx->peers[i].connect_start = 0;
            ctx->peers[i].inbound = 0;
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
                                   const PT_PlatformPeer *ppeer)
{
    PT_Peer_Internal *peer;
    int swap = 0;   /* replacing a live transport -- do not re-fire callbacks */

    peer = pt_find_peer_by_ip(ctx, peer_ip);

    /* T020: peer already connected. */
    if (peer && peer->state == PT_PEER_CONNECTED) {
        /* Deterministic simultaneous-connect rule: the connection dialed
           BY THE LOWER-IP PEER wins.  The connect_start tiebreaker below
           only fires while OUR outbound is still pending; if our own
           outbound completed before this incoming arrived we land here
           instead.  If our live connection is that outbound and the
           remote has a lower IP, the remote keeps its outbound and drops
           ours -- so blindly rejecting here leaves each side holding a
           DIFFERENT connection and BOTH abort (~200ms later, reason 2),
           looping forever on slow links.  Adopt the incoming instead. */
        if (!peer->inbound && ctx->local_ip > peer_ip) {
            CLOG_INFO("Tiebreaker: replacing our outbound with incoming "
                      "(lower IP wins)");
            ctx->platform_ops->tcp_disconnect(ctx, peer);
            swap = 1;
        } else {
            CLOG_INFO("Rejecting duplicate incoming from %s "
                      "(already connected)", peer->name);
            return;
        }
    }

    /* T021/T022: IP tiebreaker -- our outbound still in progress */
    if (!swap && peer && peer->connect_start > 0) {
        if (ctx->local_ip == peer_ip) {
            /* T022: Same IP (loopback) -- accept incoming */
            CLOG_INFO("Same-IP peer, accepting incoming");
            ctx->platform_ops->tcp_disconnect(ctx, peer);
            peer->connect_start = 0;
        } else if (ctx->local_ip > peer_ip) {
            /* We should NOT initiate (higher IP) -- cancel outgoing,
               accept incoming */
            CLOG_INFO("Tiebreaker: cancelling outgoing, accepting incoming");
            ctx->platform_ops->tcp_disconnect(ctx, peer);
            peer->connect_start = 0;
        } else {
            /* We ARE the initiator (lower IP) -- reject incoming,
               let our outgoing complete */
            CLOG_INFO("Tiebreaker: rejecting incoming, keeping outgoing");
            return;
        }
    }

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
            pt_fire_error(ctx, NULL, PT_ERR_NO_ROOM,
                          "No peer slots for incoming connection");
            return;
        }
        peer->ip_addr = peer_ip;
        pt_format_ip(peer_ip, peer->addr_str);
        peer->name[0] = '\0';
        ctx->peer_count++;
    }

    /* Accept (or, on a swap, adopt) the connection.  Reset the receive
       and reassembly state so an adopted transport starts as clean as a
       freshly allocated one -- no bytes from the replaced connection can
       bleed into the new one. */
    peer->platform_peer = *ppeer;
    peer->state = PT_PEER_CONNECTED;
    peer->inbound = 1;
    peer->last_tcp_activity = ctx->current_time;
    peer->last_tcp_send = ctx->current_time;
    peer->tcp_recv_len = 0;
    peer->reassembly_total = 0;
    peer->reassembly_received = 0;
    peer->connect_start = 0;

    if (swap) {
        /* Transport swapped underneath an already-connected peer; the app
           already saw on_connected, so stay silent. */
        CLOG_INFO("Incoming TCP connection adopted (replaced our outbound)");
    } else {
        CLOG_INFO("Incoming TCP connection accepted");
        if (ctx->callbacks.on_connected) {
            ctx->callbacks.on_connected((PT_Peer *)peer,
                                        ctx->callbacks.on_connected_data);
        }
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
/* Core-owned platform-event transitions                               */
/*                                                                      */
/* Backends used to inline these state changes inside their poll()      */
/* loops (one copy per platform).  Now the backend only reports an      */
/* event and core owns the transition in exactly one place.            */
/* ------------------------------------------------------------------ */

void pt_complete_connect(PT_Context_Internal *ctx,
                         PT_Peer_Internal *peer, int ok)
{
    if (!peer) return;

    peer->connect_start = 0;

    if (ok) {
        peer->state = PT_PEER_CONNECTED;
        peer->inbound = 0;
        peer->last_tcp_activity = ctx->current_time;
        peer->last_tcp_send = ctx->current_time;
        CLOG_INFO("TCP connected to %s", peer->name);
        if (ctx->callbacks.on_connected) {
            ctx->callbacks.on_connected(
                (PT_Peer *)peer,
                ctx->callbacks.on_connected_data);
        }
    } else {
        /* tcp_disconnect closes the handle and clears it */
        ctx->platform_ops->tcp_disconnect(ctx, peer);
        peer->state = PT_PEER_DISCONNECTED;
        pt_fire_error(ctx, peer, PT_ERR_SEND_FAILED,
                      "TCP connect failed");
    }
}

void pt_drain_disconnect(PT_Context_Internal *ctx,
                         PT_Peer_Internal *peer)
{
    if (!peer) return;

    /* The adapter has already read any final bytes into tcp_recv_buf
       before reporting CLOSED (a buffered goodbye frame may still be
       in there).  Parse it first; a goodbye transitions the peer to
       DISCONNECTED with PT_QUIT and we must not also fire an error. */
    if (peer->tcp_recv_len > 0 && peer->state == PT_PEER_CONNECTED) {
        pt_messaging_process_tcp_data(ctx, peer);
    }
    if (peer->state == PT_PEER_CONNECTED) {
        pt_handle_peer_disconnect(ctx, peer, PT_DISCONNECT_ERROR);
    }
}

/* Apply one event reported by a backend's next_event(). */
static void pt_apply_platform_event(PT_Context_Internal *ctx,
                                    const PT_Event *ev)
{
    switch (ev->type) {
    case PT_EVT_CONNECTED:
        pt_complete_connect(ctx, ev->peer, ev->ok);
        break;
    case PT_EVT_DATA:
        if (ev->peer) {
            ev->peer->last_tcp_activity = ctx->current_time;
            pt_messaging_process_tcp_data(ctx, ev->peer);
        }
        break;
    case PT_EVT_CLOSED:
        pt_drain_disconnect(ctx, ev->peer);
        break;
    case PT_EVT_NONE:
    default:
        break;
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
    pt_discovery_build_packet(ctx);

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

    /* Broadcast leave on the discovery channel so lobby peers
     * remove us immediately instead of waiting for the 15s timeout.
     * Must happen before callbacks are cleared and UDP is torn down. */
    pt_discovery_broadcast_leave(ctx);

    /* FR-012: Clear all callbacks before teardown so no callbacks
       fire during shutdown (T024) */
    memset(&ctx->callbacks, 0, sizeof(ctx->callbacks));

    /* Disconnect all connected peers.  Do NOT send goodbye frames here:
     * on MacTCP the send (mactcp_tcp_send) is SYNCHRONOUS with a 60s ULP
     * timeout, so a goodbye to a dead or simultaneously-quitting peer would
     * hang the whole machine for up to a minute.  tcp_disconnect therefore
     * closes gracelessly on every backend (OT: OTSndDisconnect / MacTCP:
     * TCPAbort -> RST; POSIX: close() -> FIN) with no goodbye in flight.
     *
     * Consequence (by design, uniform across all three platforms): a peer
     * still connected when we shut down observes on_disconnected with reason
     * PT_DISCONNECT_ERROR, NOT PT_QUIT -- PT_QUIT requires a buffered goodbye
     * frame, which only PT_Disconnect / PT_DisconnectAll send (mid-run, when
     * the peer is known live).  The CLEAN-QUIT signal at shutdown is instead
     * the UDP leave broadcast above (PT_DISCOVERY_FLAG_LEAVE): it fires the
     * partner's on_peer_lost.  On the partner, handling our RST sets its slot
     * for us to DISCONNECTED but leaves in_use set (pt_handle_peer_disconnect),
     * so its leave handler still finds us regardless of TCP/UDP ordering.
     * Reporting a graceless close as
     * ERROR is truthful; delivering QUIT here would need either a per-platform
     * orderly release (OT-only -> breaks Principle VI cross-platform parity)
     * or a safe MacTCP goodbye (reintroduces the 60s freeze).  See the memory
     * note ot-shutdown-goodbye-abortive-close for the full analysis. */
    for (i = 0; i < ctx->max_peers; i++) {
        if (ctx->peers[i].in_use &&
            ctx->peers[i].state == PT_PEER_CONNECTED) {
            ctx->platform_ops->tcp_disconnect(ctx, &ctx->peers[i]);
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

    /* If rediscovering after a previous session, release old streams
     * and create fresh ones.  MacTCP requires explicit TCPRelease/
     * UDPRelease before reusing stream slots -- aborted-but-not-released
     * streams corrupt the driver's internal state on PPC.
     * Ref: MacTCP Programmer's Guide (1989) line 1853:
     * "UDP Release must be called to release memory held by the driver.
     *  Failure to do so may produce unpredictable results." */
    if (ctx->discovery_listening && ctx->platform_ops->cleanup_streams) {
        ctx->platform_ops->cleanup_streams(ctx);
    }

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

void PT_DisconnectAll(PT_Context *pub_ctx)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    int i;
    int count = 0;

    if (!ctx) return;

    for (i = 0; i < ctx->max_peers; i++) {
        if (ctx->peers[i].in_use &&
            ctx->peers[i].state == PT_PEER_CONNECTED) {
            send_goodbye(ctx, &ctx->peers[i]);
            pt_handle_peer_disconnect(ctx, &ctx->peers[i], PT_QUIT);
            count++;
        }
    }

    if (count > 0) {
        CLOG_INFO("Disconnected all peers (%d)", count);
    }
}

/* ------------------------------------------------------------------ */
/* Public API: Messaging (stubs -- implemented in pt_messaging.c)      */
/* ------------------------------------------------------------------ */

void PT_RegisterMessage(PT_Context *pub_ctx, unsigned char type,
                        PT_Transport transport)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx) return;
    if (type == PT_MSG_TYPE_GOODBYE || type == PT_MSG_TYPE_KEEPALIVE) {
        CLOG_WARN("Cannot register reserved message type %d", (int)type);
        return;
    }
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
        /* cppcheck-suppress unsignedLessThanZero ; false positive: comparing two unsigned longs */
        ctx->current_time >= ctx->discovery_timer) {
        pt_discovery_broadcast(ctx);
        ctx->discovery_timer = ctx->current_time +
                               PT_DISCOVERY_INTERVAL;
    }

    /* Platform poll.  Event-driven backends drain one event at a time
       and core applies each transition; legacy backends still own the
       loop via poll(). */
    if (ctx->platform_ops->next_event) {
        PT_Event ev;
        int guard = 0;
        while (guard++ < PT_MAX_EVENTS_PER_POLL &&
               ctx->platform_ops->next_event(ctx, &ev)) {
            pt_apply_platform_event(ctx, &ev);
        }
    } else {
        ctx->platform_ops->poll(ctx);
    }

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
            pt_fire_error(ctx, &ctx->peers[i], PT_ERR_SEND_FAILED,
                          "Connection timeout");
        }

        /* TCP keepalive — send if no TCP data sent recently */
        if (ctx->peers[i].state == PT_PEER_CONNECTED &&
            ctx->peers[i].last_tcp_send > 0 &&
            ctx->current_time - ctx->peers[i].last_tcp_send >=
                PT_KEEPALIVE_INTERVAL) {
            pt_messaging_send_keepalive(ctx, &ctx->peers[i]);
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

int PT_GetPeerCount(const PT_Context *pub_ctx)
{
    const PT_Context_Internal *ctx = (const PT_Context_Internal *)pub_ctx;
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

const char *PT_PeerName(const PT_Peer *pub_peer)
{
    const PT_Peer_Internal *peer = (const PT_Peer_Internal *)pub_peer;
    if (!peer) return "";
    return peer->name;
}

const char *PT_PeerAddress(const PT_Peer *pub_peer)
{
    const PT_Peer_Internal *peer = (const PT_Peer_Internal *)pub_peer;
    if (!peer) return "";
    return peer->addr_str;
}

const char *PT_LocalAddress(const PT_Context *pub_ctx)
{
    static char local_addr_str[16];
    const PT_Context_Internal *ctx = (const PT_Context_Internal *)pub_ctx;
    if (!ctx || ctx->local_ip == 0) return "";
    pt_format_ip(ctx->local_ip, local_addr_str);
    return local_addr_str;
}

PT_Status PT_SendUDPBroadcast(PT_Context *pub_ctx, unsigned short port,
                              const void *data, size_t len)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx) return PT_ERR_INVALID_ARG;
    if (!data && len > 0) return PT_ERR_INVALID_ARG;
    return ctx->platform_ops->udp_broadcast(ctx, port, data, len);
}

PT_PeerState PT_GetPeerState(const PT_Peer *pub_peer)
{
    const PT_Peer_Internal *peer = (const PT_Peer_Internal *)pub_peer;
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
    pt_discovery_build_packet(ctx);

    /* Rebuild debug prefix if debug broadcast is active */
    if (ctx->debug_port != 0) {
        PT_EnableDebugBroadcast((PT_Context *)ctx, ctx->debug_port);
    }
    return PT_OK;
}

/* ------------------------------------------------------------------ */
/* Public API: Peer Ranking                                            */
/* ------------------------------------------------------------------ */

int PT_GetPeerRank(const PT_Context *pub_ctx, const PT_Peer *pub_peer)
{
    const PT_Context_Internal *ctx = (const PT_Context_Internal *)pub_ctx;
    const PT_Peer_Internal *peer = (const PT_Peer_Internal *)pub_peer;
    unsigned long target_ip;
    int rank = 0;
    int i;

    if (!ctx) return -1;

    if (peer) {
        if (peer->state != PT_PEER_CONNECTED) return -1;
        target_ip = peer->ip_addr;
    } else {
        target_ip = ctx->local_ip;
    }

    /* Count connected peers + self with IP below target */
    if (ctx->local_ip < target_ip) rank++;

    for (i = 0; i < ctx->max_peers; i++) {
        if (!ctx->peers[i].in_use) continue;
        if (ctx->peers[i].state != PT_PEER_CONNECTED) continue;
        if (peer && ctx->peers[i].ip_addr == target_ip) continue;
        if (ctx->peers[i].ip_addr < target_ip) rank++;
    }

    return rank;
}

/* ------------------------------------------------------------------ */
/* Public API: Debug Broadcast                                         */
/* ------------------------------------------------------------------ */

static void pt_build_debug_prefix(PT_Context_Internal *ctx)
{
    char ip_buf[16];
    int pos = 0;
    const char *s;

    pt_format_ip(ctx->local_ip, ip_buf);

    ctx->debug_prefix[pos++] = '[';
    s = ctx->name;
    while (*s && pos < 33) {
        ctx->debug_prefix[pos++] = *s++;
    }
    ctx->debug_prefix[pos++] = '@';
    s = ip_buf;
    while (*s && pos < 49) {
        ctx->debug_prefix[pos++] = *s++;
    }
    ctx->debug_prefix[pos++] = ']';
    ctx->debug_prefix[pos++] = ' ';
    ctx->debug_prefix[pos] = '\0';
    ctx->debug_prefix_len = pos;
}

PT_Status PT_EnableDebugBroadcast(PT_Context *pub_ctx, unsigned short port)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx) return PT_ERR_INVALID_ARG;

    ctx->debug_port = port ? port : PT_DEBUG_PORT;
    pt_build_debug_prefix(ctx);

    CLOG_INFO("Debug broadcast enabled on port %u", ctx->debug_port);
    return PT_OK;
}

void PT_DebugSend(PT_Context *pub_ctx, const char *msg, size_t len)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    static char debug_buf[256];
    int total;
    int avail;

    if (!ctx || !ctx->debug_port || !msg || len == 0) return;

    /* Build: prefix + message + newline */
    memcpy(debug_buf, ctx->debug_prefix, (size_t)ctx->debug_prefix_len);
    total = ctx->debug_prefix_len;

    avail = (int)sizeof(debug_buf) - total - 2; /* room for msg + newline */
    if ((int)len > avail) len = (size_t)avail;
    memcpy(debug_buf + total, msg, len);
    total += (int)len;
    debug_buf[total++] = '\n';

    ctx->platform_ops->udp_broadcast(ctx, ctx->debug_port,
                                     debug_buf, (size_t)total);
}

void PT_DisableDebugBroadcast(PT_Context *pub_ctx)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    if (!ctx) return;

    if (ctx->debug_port != 0) {
        CLOG_INFO("Debug broadcast disabled");
    }
    ctx->debug_port = 0;
}
