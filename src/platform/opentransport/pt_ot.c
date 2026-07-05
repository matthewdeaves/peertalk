/*
 * pt_ot.c -- Open Transport platform backend
 *
 * Notifier sets volatile flags only; processing happens in poll.
 * Uses tilisten,tcp for concurrent accept handling.
 *
 * Targets PPC Macs with System 7.6.1+ / Mac OS 8/9.
 */

#include "pt_internal.h"

#ifdef PT_PLATFORM_OT

#include <ConditionalMacros.h>   /* TARGET_API_MAC_CARBON */

/*
 * Carbon (OS X / CarbonLib) OT: with OTCARBONAPPLICATION the header keeps
 * the *InContext macros but supplies the client context for us, so the
 * call sites below stay identical to the classic ones and we do NOT undo
 * the redirects.  Must be defined before including <OpenTransport.h>.
 */
#if TARGET_API_MAC_CARBON
#define OTCARBONAPPLICATION 1
#endif

#include <OpenTransport.h>
#include <OpenTransportProviders.h>

#if !TARGET_API_MAC_CARBON
/*
 * Classic 68k/PPC OT: Retro68 import libraries export the non-InContext
 * symbols (OTOpenEndpoint, InitOpenTransport, CloseOpenTransport) but the
 * header #defines those names as macros that expand to the InContext
 * variants, which don't exist in the import libs.  Undo the redirects
 * so we can call the real library symbols directly.
 */
#undef OTOpenEndpoint
#undef InitOpenTransport
#undef CloseOpenTransport
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define OT_TCP_QLEN         4
#define OT_RECV_BUF_SIZE    4096
#define OT_MAX_ENDPOINTS    32

/* Endpoint states */
#define EP_FREE             0
#define EP_LISTENING        1
#define EP_CONNECTING       2
#define EP_CONNECTED        3

/* Event flag bit indices for OTAtomicSetBit/ClearBit (0-7 per byte).
   OTAtomic* functions are in Table C-1: safe at hardware interrupt time. */
#define EVT_BIT_DATA        0
#define EVT_BIT_DISCONNECT  1
#define EVT_BIT_ORDREL      2
#define EVT_BIT_CONNECT     3
#define EVT_BIT_LISTEN      4
#define EVT_BIT_PASSCON     5
#define EVT_BIT_GODATA      6

/* UDP uses same bit index */
#define EVT_BIT_UDP_DATA    0

/* ------------------------------------------------------------------ */
/* Per-endpoint state                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    EndpointRef     ep;
    volatile UInt8  flags;
    int             state;
    volatile int    flow_off;   /* flow control active (set by notifier) */
    struct PT_Peer_Internal *owner; /* back-pointer to owning peer, NULL if free */
} OTEndpointSlot;

typedef struct {
    EndpointRef     ep;
    volatile UInt8  flags;
} OTUDPSlot;

/* ------------------------------------------------------------------ */
/* Platform state                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    InetHost        local_ip;

    /* Listener endpoint (tilisten,tcp) */
    EndpointRef     listener_ep;
    volatile UInt8  listener_flags;

    /* Per-peer TCP endpoints for data transfer */
    OTEndpointSlot  tcp_eps[OT_MAX_ENDPOINTS];

    /* UDP endpoints */
    OTUDPSlot       discovery_udp;
    OTUDPSlot       message_udp;

    /* UPP handles (must be disposed on shutdown) */
    OTNotifyUPP     listener_upp;
    OTNotifyUPP     tcp_upp;
    OTNotifyUPP     udp_upp;

    /* Event-iterator state (next_event) */
    int             ev_started;  /* round's one-time work done? */
    int             ev_cursor;   /* next TCP endpoint index to examine */
} OTState;

static OTState g_ot;

/* ------------------------------------------------------------------ */
/* Notifier callbacks (deferred task time -- set flags only)            */
/* ------------------------------------------------------------------ */

static pascal void listener_notifier(void *context, OTEventCode code,
                                     OTResult result, void *cookie)
{
    OTState *st = (OTState *)context;
    (void)result; (void)cookie;

    if (code == T_LISTEN) {
        OTAtomicSetBit((UInt8 *)&st->listener_flags, EVT_BIT_LISTEN);
    } else if (code == T_PASSCON) {
        OTAtomicSetBit((UInt8 *)&st->listener_flags, EVT_BIT_PASSCON);
    } else if (code == T_DISCONNECT) {
        /* A queued connection was aborted before we accepted it.
         * Must track this so ot_poll can consume it via OTRcvDisconnect --
         * OT won't deliver new T_LISTEN events while T_DISCONNECT is
         * pending on the listener (per XTI state machine rules). */
        OTAtomicSetBit((UInt8 *)&st->listener_flags, EVT_BIT_DISCONNECT);
    }
}

static pascal void tcp_notifier(void *context, OTEventCode code,
                                OTResult result, void *cookie)
{
    OTEndpointSlot *slot = (OTEndpointSlot *)context;
    (void)result; (void)cookie;

    switch (code) {
    case T_DATA:
        OTAtomicSetBit((UInt8 *)&slot->flags, EVT_BIT_DATA);
        break;
    case T_DISCONNECT:
        OTAtomicSetBit((UInt8 *)&slot->flags, EVT_BIT_DISCONNECT);
        break;
    case T_ORDREL:
        OTAtomicSetBit((UInt8 *)&slot->flags, EVT_BIT_ORDREL);
        break;
    case T_CONNECT:
        OTAtomicSetBit((UInt8 *)&slot->flags, EVT_BIT_CONNECT);
        break;
    case T_GODATA:
        OTAtomicSetBit((UInt8 *)&slot->flags, EVT_BIT_GODATA);
        slot->flow_off = 0;
        break;
    case T_PASSCON:
        OTAtomicSetBit((UInt8 *)&slot->flags, EVT_BIT_PASSCON);
        break;
    }
}

static pascal void udp_notifier(void *context, OTEventCode code,
                                OTResult result, void *cookie)
{
    OTUDPSlot *slot = (OTUDPSlot *)context;
    (void)result; (void)cookie;

    if (code == T_DATA) {
        OTAtomicSetBit((UInt8 *)&slot->flags, EVT_BIT_UDP_DATA);
    }
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Drain all pending events from an endpoint so it's clean for reuse.
   OT refuses OTConnect/OTSndDisconnect with kOTLookErr (-3155) if
   events are pending.  Must consume them before reusing the endpoint. */
static void drain_endpoint_events(EndpointRef ep)
{
    OTResult look;
    int safety = 10;

    while (safety-- > 0) {
        look = OTLook(ep);
        switch (look) {
        case T_DISCONNECT:
            OTRcvDisconnect(ep, NULL);
            CLOG_DEBUG("Drained T_DISCONNECT from endpoint");
            break;
        case T_ORDREL:
            OTRcvOrderlyDisconnect(ep);
            CLOG_DEBUG("Drained T_ORDREL from endpoint");
            break;
        case T_CONNECT:
            OTRcvConnect(ep, NULL);
            CLOG_DEBUG("Drained T_CONNECT from endpoint");
            break;
        case T_DATA:
        case T_EXDATA:
            {
                unsigned char junk[256];
                OTFlags fl = 0;
                OTRcv(ep, junk, sizeof(junk), &fl);
            }
            CLOG_DEBUG("Drained pending data from endpoint");
            break;
        default:
            return;
        }
    }
    CLOG_DEBUG("Endpoint drain hit safety limit, events may remain (last=%ld)",
               (long)OTLook(ep));
}

/* Reset an endpoint to T_IDLE for reuse: unbind then rebind with
   ephemeral port.  If rebind fails, close the endpoint entirely so
   find_free_ep never hands out an unusable slot. */
static void reset_endpoint(OTEndpointSlot *slot)
{
    InetAddress addr;
    TBind req;
    OSStatus err;

    OTSetSynchronous(slot->ep);
    OTSetBlocking(slot->ep);
    OTUnbind(slot->ep);

    OTInitInetAddress(&addr, 0, 0);
    req.addr.maxlen = sizeof(addr);
    req.addr.len = sizeof(addr);
    req.addr.buf = (unsigned char *)&addr;
    req.qlen = 0;

    err = OTBind(slot->ep, &req, NULL);
    if (err != kOTNoError) {
        CLOG_WARN("OTBind failed on reset (%d), closing endpoint",
                  (int)err);
        OTCloseProvider(slot->ep);
        slot->ep = kOTInvalidEndpointRef;
    } else {
        OTSetAsynchronous(slot->ep);
        OTSetNonBlocking(slot->ep);
    }

    slot->state = EP_FREE;
    slot->flags = 0;
    slot->flow_off = 0;
    slot->owner = NULL;
}

static int find_free_ep(void)
{
    int i;
    for (i = 0; i < OT_MAX_ENDPOINTS; i++) {
        if (g_ot.tcp_eps[i].ep != kOTInvalidEndpointRef &&
            g_ot.tcp_eps[i].state == EP_FREE) {
            return i;
        }
    }
    return -1;
}

static EndpointRef create_tcp_endpoint(OTNotifyUPP notifyUPP,
                                       void *context)
{
    EndpointRef ep;
    OSStatus err;
    InetAddress addr;
    TBind req;
    OTConfigurationRef config;

    config = OTCreateConfiguration("tcp");
    if (!config) return kOTInvalidEndpointRef;

    ep = OTOpenEndpoint(config, 0, NULL, &err);
    if (err != kOTNoError || ep == kOTInvalidEndpointRef) {
        return kOTInvalidEndpointRef;
    }

    /* Bind with ephemeral port while still synchronous.
       OTBind must complete before switching to async mode. */
    OTInitInetAddress(&addr, 0, 0);
    req.addr.maxlen = sizeof(addr);
    req.addr.len = sizeof(addr);
    req.addr.buf = (unsigned char *)&addr;
    req.qlen = 0;

    err = OTBind(ep, &req, NULL);
    if (err != kOTNoError) {
        OTCloseProvider(ep);
        return kOTInvalidEndpointRef;
    }

    /* Now install notifier and switch to async for runtime use */
    err = OTInstallNotifier(ep, notifyUPP, context);
    if (err != kOTNoError) {
        OTCloseProvider(ep);
        return kOTInvalidEndpointRef;
    }
    OTSetAsynchronous(ep);
    OTSetNonBlocking(ep);

    return ep;
}

static EndpointRef create_udp_endpoint(OTNotifyUPP notifyUPP,
                                       void *context,
                                       InetPort port)
{
    EndpointRef ep;
    OSStatus err;
    InetAddress addr;
    TBind req;
    OTConfigurationRef config;

    config = OTCreateConfiguration("udp");
    if (!config) return kOTInvalidEndpointRef;

    ep = OTOpenEndpoint(config, 0, NULL, &err);
    if (err != kOTNoError || ep == kOTInvalidEndpointRef) {
        return kOTInvalidEndpointRef;
    }

    /* Bind while still synchronous so it completes before use */
    OTInitInetAddress(&addr, port, 0);
    req.addr.maxlen = sizeof(addr);
    req.addr.len = sizeof(addr);
    req.addr.buf = (unsigned char *)&addr;
    req.qlen = 0;

    err = OTBind(ep, &req, NULL);
    if (err != kOTNoError) {
        OTCloseProvider(ep);
        return kOTInvalidEndpointRef;
    }

    /* Now install notifier and switch to async for runtime use */
    err = OTInstallNotifier(ep, notifyUPP, context);
    if (err != kOTNoError) {
        OTCloseProvider(ep);
        return kOTInvalidEndpointRef;
    }
    OTSetAsynchronous(ep);
    OTSetNonBlocking(ep);

    return ep;
}

/* ------------------------------------------------------------------ */
/* PT_PlatformOps implementation                                       */
/* ------------------------------------------------------------------ */

static PT_Status ot_init(PT_Context_Internal *ctx)
{
    OSStatus err;
    int i;
    int count;
    InetInterfaceInfo info;
    InetAddress addr;
    TBind req;
    OTNotifyUPP listener_upp;
    OTNotifyUPP tcp_upp;
    OTNotifyUPP udp_upp;

    memset(&g_ot, 0, sizeof(g_ot));

    /* Mark all endpoint slots as invalid */
    for (i = 0; i < OT_MAX_ENDPOINTS; i++) {
        g_ot.tcp_eps[i].ep = kOTInvalidEndpointRef;
    }
    g_ot.listener_ep = kOTInvalidEndpointRef;
    g_ot.discovery_udp.ep = kOTInvalidEndpointRef;
    g_ot.message_udp.ep = kOTInvalidEndpointRef;

    /* Initialize Open Transport */
    err = InitOpenTransport();
    if (err != kOTNoError) {
        CLOG_ERR("Failed to init Open Transport: %d", (int)err);
        return PT_ERR_INIT;
    }

    /* Get local IP */
    err = OTInetGetInterfaceInfo(&info, kDefaultInetInterface);
    if (err != kOTNoError) {
        CLOG_ERR("Failed to get interface info: %d", (int)err);
        CloseOpenTransport();
        return PT_ERR_INIT;
    }
    g_ot.local_ip = info.fAddress;
    ctx->local_ip = (unsigned long)g_ot.local_ip;

    /* Create UPPs and store for later disposal */
    listener_upp = NewOTNotifyUPP(listener_notifier);
    tcp_upp = NewOTNotifyUPP(tcp_notifier);
    udp_upp = NewOTNotifyUPP(udp_notifier);
    if (!listener_upp || !tcp_upp || !udp_upp) {
        CLOG_ERR("Failed to create OT UPPs (OOM)");
        goto fail_upps;
    }
    g_ot.listener_upp = listener_upp;
    g_ot.tcp_upp = tcp_upp;
    g_ot.udp_upp = udp_upp;

    /* Create listener endpoint with tilisten,tcp */
    {
        OTConfigurationRef lconfig = OTCreateConfiguration("tilisten,tcp");
        if (!lconfig) {
            CLOG_ERR("Failed to create listener config");
            goto fail_upps;
        }
        g_ot.listener_ep = OTOpenEndpoint(lconfig, 0, NULL, &err);
    }
    if (err != kOTNoError || g_ot.listener_ep == kOTInvalidEndpointRef) {
        CLOG_ERR("Failed to create listener endpoint: %d", (int)err);
        goto fail_upps;
    }

    /* Bind listener synchronously before switching to async mode.
       OTBind must complete before the endpoint can accept connections. */
    OTInitInetAddress(&addr, PT_TCP_PORT, 0);
    req.addr.maxlen = sizeof(addr);
    req.addr.len = sizeof(addr);
    req.addr.buf = (unsigned char *)&addr;
    req.qlen = OT_TCP_QLEN;

    err = OTBind(g_ot.listener_ep, &req, NULL);
    if (err != kOTNoError) {
        CLOG_ERR("Failed to bind listener: %d", (int)err);
        OTCloseProvider(g_ot.listener_ep);
        g_ot.listener_ep = kOTInvalidEndpointRef;
        goto fail_upps;
    }

    /* Now switch to async for runtime event handling */
    err = OTInstallNotifier(g_ot.listener_ep, listener_upp, &g_ot);
    if (err != kOTNoError) {
        CLOG_ERR("Failed to install listener notifier: %d", (int)err);
        OTCloseProvider(g_ot.listener_ep);
        g_ot.listener_ep = kOTInvalidEndpointRef;
        goto fail_upps;
    }
    OTSetAsynchronous(g_ot.listener_ep);
    OTSetNonBlocking(g_ot.listener_ep);

    /* Create TCP endpoint pool for data transfer.
       Each endpoint is bound synchronously inside create_tcp_endpoint. */
    count = ctx->max_peers;
    if (count > OT_MAX_ENDPOINTS) count = OT_MAX_ENDPOINTS;
    for (i = 0; i < count; i++) {
        g_ot.tcp_eps[i].ep = create_tcp_endpoint(
            tcp_upp, &g_ot.tcp_eps[i]);
        if (g_ot.tcp_eps[i].ep == kOTInvalidEndpointRef) break;

        g_ot.tcp_eps[i].state = EP_FREE;
        g_ot.tcp_eps[i].owner = NULL;
    }

    /* Create UDP discovery endpoint (port 7353) */
    g_ot.discovery_udp.ep = create_udp_endpoint(
        udp_upp, &g_ot.discovery_udp, PT_DISCOVERY_PORT);
    if (g_ot.discovery_udp.ep == kOTInvalidEndpointRef) {
        CLOG_ERR("Failed to create discovery UDP endpoint");
        goto fail_tcp;
    }

    /* Create UDP message endpoint (port 7355) */
    g_ot.message_udp.ep = create_udp_endpoint(
        udp_upp, &g_ot.message_udp, PT_UDP_MSG_PORT);
    if (g_ot.message_udp.ep == kOTInvalidEndpointRef) {
        CLOG_ERR("Failed to create message UDP endpoint");
        goto fail_disc_udp;
    }

    ctx->platform_state = &g_ot;

    CLOG_INFO("Open Transport initialized (IP: %lu.%lu.%lu.%lu)",
              (g_ot.local_ip >> 24) & 0xFF,
              (g_ot.local_ip >> 16) & 0xFF,
              (g_ot.local_ip >> 8) & 0xFF,
              g_ot.local_ip & 0xFF);

    return PT_OK;

fail_disc_udp:
    OTCloseProvider(g_ot.discovery_udp.ep);
    g_ot.discovery_udp.ep = kOTInvalidEndpointRef;
fail_tcp:
    for (i = 0; i < OT_MAX_ENDPOINTS; i++) {
        if (g_ot.tcp_eps[i].ep != kOTInvalidEndpointRef) {
            OTCloseProvider(g_ot.tcp_eps[i].ep);
            g_ot.tcp_eps[i].ep = kOTInvalidEndpointRef;
        }
    }
    if (g_ot.listener_ep != kOTInvalidEndpointRef) {
        OTCloseProvider(g_ot.listener_ep);
        g_ot.listener_ep = kOTInvalidEndpointRef;
    }
fail_upps:
    if (g_ot.listener_upp) {
        DisposeOTNotifyUPP(g_ot.listener_upp);
        g_ot.listener_upp = NULL;
    }
    if (g_ot.tcp_upp) {
        DisposeOTNotifyUPP(g_ot.tcp_upp);
        g_ot.tcp_upp = NULL;
    }
    if (g_ot.udp_upp) {
        DisposeOTNotifyUPP(g_ot.udp_upp);
        g_ot.udp_upp = NULL;
    }
    CloseOpenTransport();
    return PT_ERR_INIT;
}

static void ot_shutdown(PT_Context_Internal *ctx)
{
    int i;

    /* Close all TCP data endpoints */
    for (i = 0; i < OT_MAX_ENDPOINTS; i++) {
        if (g_ot.tcp_eps[i].ep != kOTInvalidEndpointRef) {
            OTSndDisconnect(g_ot.tcp_eps[i].ep, NULL);
            OTCloseProvider(g_ot.tcp_eps[i].ep);
            g_ot.tcp_eps[i].ep = kOTInvalidEndpointRef;
        }
    }

    /* Close listener */
    if (g_ot.listener_ep != kOTInvalidEndpointRef) {
        OTCloseProvider(g_ot.listener_ep);
        g_ot.listener_ep = kOTInvalidEndpointRef;
    }

    /* Close UDP endpoints */
    if (g_ot.discovery_udp.ep != kOTInvalidEndpointRef) {
        OTCloseProvider(g_ot.discovery_udp.ep);
        g_ot.discovery_udp.ep = kOTInvalidEndpointRef;
    }
    if (g_ot.message_udp.ep != kOTInvalidEndpointRef) {
        OTCloseProvider(g_ot.message_udp.ep);
        g_ot.message_udp.ep = kOTInvalidEndpointRef;
    }

    /* Dispose UPP handles */
    if (g_ot.listener_upp) {
        DisposeOTNotifyUPP(g_ot.listener_upp);
        g_ot.listener_upp = NULL;
    }
    if (g_ot.tcp_upp) {
        DisposeOTNotifyUPP(g_ot.tcp_upp);
        g_ot.tcp_upp = NULL;
    }
    if (g_ot.udp_upp) {
        DisposeOTNotifyUPP(g_ot.udp_upp);
        g_ot.udp_upp = NULL;
    }

    CloseOpenTransport();
    ctx->platform_state = NULL;
}

static PT_Status ot_udp_broadcast(PT_Context_Internal *ctx,
                                  unsigned short port,
                                  const void *data, size_t len)
{
    TUnitData udata;
    InetAddress dest;
    OTResult res;
    OTUDPSlot *us;

    (void)ctx;

    us = (port == PT_DISCOVERY_PORT) ?
         &g_ot.discovery_udp : &g_ot.message_udp;

    OTInitInetAddress(&dest, port, 0xFFFFFFFF);

    udata.addr.maxlen = sizeof(dest);
    udata.addr.len = sizeof(dest);
    udata.addr.buf = (unsigned char *)&dest;
    udata.opt.maxlen = 0;
    udata.opt.len = 0;
    udata.opt.buf = NULL;
    udata.udata.maxlen = (OTByteCount)len;
    udata.udata.len = (OTByteCount)len;
    udata.udata.buf = (unsigned char *)data;

    res = OTSndUData(us->ep, &udata);
    return (res == kOTNoError) ? PT_OK : PT_ERR_SEND_FAILED;
}

static PT_Status ot_udp_send(PT_Context_Internal *ctx,
                             const PT_Peer_Internal *peer,
                             unsigned short port,
                             const void *data, size_t len)
{
    TUnitData udata;
    InetAddress dest;
    OTResult res;

    (void)ctx;

    OTInitInetAddress(&dest, port, (InetHost)peer->ip_addr);

    udata.addr.maxlen = sizeof(dest);
    udata.addr.len = sizeof(dest);
    udata.addr.buf = (unsigned char *)&dest;
    udata.opt.maxlen = 0;
    udata.opt.len = 0;
    udata.opt.buf = NULL;
    udata.udata.maxlen = (OTByteCount)len;
    udata.udata.len = (OTByteCount)len;
    udata.udata.buf = (unsigned char *)data;

    res = OTSndUData(g_ot.message_udp.ep, &udata);
    return (res == kOTNoError) ? PT_OK : PT_ERR_SEND_FAILED;
}

static PT_Status ot_udp_listen(PT_Context_Internal *ctx,
                               unsigned short port)
{
    /* UDP endpoints are already bound and async -- notifier
       will set flags when data arrives. Nothing to do here. */
    (void)ctx; (void)port;
    return PT_OK;
}

static PT_Status ot_tcp_listen(PT_Context_Internal *ctx)
{
    /* Listener is already bound with qlen > 0 in ot_init.
       OTListen will be called from poll when T_LISTEN fires. */
    (void)ctx;
    CLOG_INFO("TCP listener active on port %d", PT_TCP_PORT);
    return PT_OK;
}

static PT_Status ot_tcp_connect(PT_Context_Internal *ctx,
                                PT_Peer_Internal *peer)
{
    int idx;
    OTEndpointSlot *slot;
    TCall sndcall;
    InetAddress dest;
    OTResult res;

    (void)ctx;

    idx = find_free_ep();
    if (idx < 0) return PT_ERR_NO_ROOM;

    slot = &g_ot.tcp_eps[idx];

    OTInitInetAddress(&dest, PT_TCP_PORT, (InetHost)peer->ip_addr);

    memset(&sndcall, 0, sizeof(sndcall));
    sndcall.addr.maxlen = sizeof(dest);
    sndcall.addr.len = sizeof(dest);
    sndcall.addr.buf = (unsigned char *)&dest;

    /* Drain stale events from previous use of this endpoint.
       Without this, OTConnect returns kOTLookErr (-3155) if a
       T_DISCONNECT from a prior session is still pending. */
    drain_endpoint_events(slot->ep);
    slot->flags = 0;

    res = OTConnect(slot->ep, &sndcall, NULL);
    /* Async connect returns kOTNoDataErr (not yet complete) */
    if (res != kOTNoError && res != kOTNoDataErr) {
        CLOG_WARN("OTConnect failed: %d", (int)res);
        return PT_ERR_SEND_FAILED;
    }

    slot->state = EP_CONNECTING;
    slot->flags = 0;
    slot->flow_off = 0;

    peer->platform_peer.endpoint = slot;
    peer->platform_peer.events = 0;
    slot->owner = peer;

    return PT_OK;
}

static PT_Status ot_tcp_send(PT_Context_Internal *ctx,
                             PT_Peer_Internal *peer,
                             const void *data, size_t len)
{
    OTEndpointSlot *slot;
    OTResult res;

    (void)ctx;

    slot = (OTEndpointSlot *)peer->platform_peer.endpoint;
    if (!slot || slot->state != EP_CONNECTED) return PT_ERR_NOT_CONNECTED;

    if (slot->flow_off) return PT_ERR_SEND_FAILED;

    /* Copy data to send buffer (skip if already there) */
    if (len > peer->tcp_send_size) len = peer->tcp_send_size;
    if ((const unsigned char *)data != peer->tcp_send_buf) {
        memcpy(peer->tcp_send_buf, data, len);
    }

    {
        unsigned char *buf = peer->tcp_send_buf;
        size_t remaining = len;
        while (remaining > 0) {
            res = OTSnd(slot->ep, buf, (OTByteCount)remaining, 0);
            if (res == kOTFlowErr) {
                slot->flow_off = 1;
                return PT_ERR_SEND_FAILED;
            }
            if (res < 0) return PT_ERR_SEND_FAILED;
            buf += res;
            remaining -= (size_t)res;
        }
    }

    return PT_OK;
}

static void ot_tcp_disconnect(PT_Context_Internal *ctx,
                              PT_Peer_Internal *peer)
{
    OTEndpointSlot *slot;

    (void)ctx;

    slot = (OTEndpointSlot *)peer->platform_peer.endpoint;
    if (!slot) return;

    /* Drain pending events before disconnect — OTSndDisconnect also
       returns kOTLookErr if a T_DISCONNECT is already pending. */
    drain_endpoint_events(slot->ep);
    OTSndDisconnect(slot->ep, NULL);

    reset_endpoint(slot);

    peer->platform_peer.endpoint = NULL;
    peer->platform_peer.events = 0;
}

static void poll_udp(PT_Context_Internal *ctx, OTUDPSlot *us,
                     int is_discovery)
{
    if (!OTAtomicClearBit((UInt8 *)&us->flags, EVT_BIT_UDP_DATA)) return;

    /* Read all available datagrams */
    for (;;) {
        unsigned char buf[1500];
        TUnitData udata;
        InetAddress src;
        OTFlags flags;
        OTResult res;

        memset(&udata, 0, sizeof(udata));
        udata.addr.maxlen = sizeof(src);
        udata.addr.buf = (unsigned char *)&src;
        udata.opt.maxlen = 0;
        udata.opt.buf = NULL;
        udata.udata.maxlen = sizeof(buf);
        udata.udata.buf = buf;

        flags = 0;
        res = OTRcvUData(us->ep, &udata, &flags);
        if (res != kOTNoError) break;

        if (is_discovery) {
            pt_discovery_receive(ctx, buf, udata.udata.len,
                                 (unsigned long)src.fHost);
        } else {
            pt_messaging_process_udp_data(ctx, buf, udata.udata.len,
                                          (unsigned long)src.fHost);
        }
    }
}

/* Read available bytes into the peer's TCP receive buffer.  Shared by
   the DATA path and the final-byte drain before a CLOSED event (a
   buffered goodbye frame may still be readable after T_DISCONNECT /
   T_ORDREL — R23).  Core parses the buffer; the adapter only fills it. */
static void ot_recv_into_peer(OTEndpointSlot *slot, PT_Peer_Internal *peer)
{
    size_t space;
    OTResult nread;
    OTFlags rflags;

    space = peer->tcp_recv_size - peer->tcp_recv_len;
    if (space == 0) return;

    rflags = 0;
    nread = OTRcv(slot->ep, peer->tcp_recv_buf + peer->tcp_recv_len,
                  (OTByteCount)space, &rflags);
    if (nread < 0) {
        if (nread != kOTNoDataErr) {
            CLOG_DEBUG("OTRcv error %ld", (long)nread);
        }
    } else if (nread > 0) {
        peer->tcp_recv_len += (size_t)nread;
    }
}

/* One-time work at the start of each drain round: service the listener
   (accept incoming connections inline, as MacTCP does) and the UDP
   endpoints.  These produce no PT_Event — incoming connections are wired
   up via pt_handle_incoming_connection, which fires on_connected. */
static void ot_round_start(PT_Context_Internal *ctx)
{
    /* ---- Handle listener T_DISCONNECT (queued connection aborted) ---- */
    if (OTAtomicClearBit((UInt8 *)&g_ot.listener_flags, EVT_BIT_DISCONNECT)) {
        /* A connection queued by tilisten was aborted before we accepted it.
         * Must consume via OTRcvDisconnect or OT blocks future T_LISTEN
         * events (XTI state machine requires all events be acknowledged). */
        drain_endpoint_events(g_ot.listener_ep);
    }

    /* ---- Handle listener events (T_LISTEN) ---- */
    if (OTAtomicClearBit((UInt8 *)&g_ot.listener_flags, EVT_BIT_LISTEN)) {
        /* Also clear PASSCON on listener (processed below on data ep) */
        OTAtomicClearBit((UInt8 *)&g_ot.listener_flags, EVT_BIT_PASSCON);

        /* Process all pending listen indications */
        {
        int look_retries = 3; /* safety limit for kOTLookErr drain-retry */
        for (;;) {
            TCall call;
            InetAddress remote_addr;
            OTResult res;
            int idx;
            OTEndpointSlot *slot;

            memset(&call, 0, sizeof(call));
            call.addr.maxlen = sizeof(remote_addr);
            call.addr.buf = (unsigned char *)&remote_addr;

            res = OTListen(g_ot.listener_ep, &call);
            if (res != kOTNoError) {
                /* OTListen can fail with kOTLookErr if T_DISCONNECT is
                 * pending on the listener (connection aborted between
                 * T_LISTEN and our OTListen call).  Drain it and retry. */
                if (res == kOTLookErr && look_retries-- > 0) {
                    drain_endpoint_events(g_ot.listener_ep);
                    continue;
                }
                break;
            }

            /* Find a free endpoint to accept onto */
            idx = find_free_ep();
            if (idx < 0) {
                /* No room -- reject */
                OTSndDisconnect(g_ot.listener_ep, &call);
                pt_fire_error(ctx, NULL, PT_ERR_NO_ROOM,
                              "No endpoint slots for incoming");
                continue;
            }

            slot = &g_ot.tcp_eps[idx];
            slot->state = EP_CONNECTING; /* will become CONNECTED on T_PASSCON */
            slot->flags = 0;
            slot->flow_off = 0;

            res = OTAccept(g_ot.listener_ep, slot->ep, &call);
            if (res != kOTNoError) {
                slot->state = EP_FREE;
                /* kOTLookErr means T_DISCONNECT arrived for this
                 * connection while we were accepting.  Drain it so
                 * the listener can process future connections. */
                if (res == kOTLookErr) {
                    drain_endpoint_events(g_ot.listener_ep);
                }
                continue;
            }

            /* Store remote IP temporarily so we can match on T_PASSCON.
               Use the peer->platform_peer.events field to stash the IP
               until passcon fires. We'll handle the actual connection
               setup when T_PASSCON arrives on the data endpoint. */
            OTAtomicSetBit((UInt8 *)&slot->flags, EVT_BIT_PASSCON); /* trigger immediate processing */

            /* Set up the connection. tilisten serializes incoming
               connections; kOTLookErr retry above handles the edge
               case where T_DISCONNECT arrives between listen/accept */
            {
                PT_PlatformPeer ppeer;
                memset(&ppeer, 0, sizeof(ppeer));
                ppeer.endpoint = slot;
                ppeer.events = 0;

                slot->state = EP_CONNECTED;

                pt_handle_incoming_connection(
                    ctx, (unsigned long)remote_addr.fHost, &ppeer);

                /* Find which peer (if any) now owns this endpoint */
                {
                    int j;
                    for (j = 0; j < ctx->max_peers; j++) {
                        if (ctx->peers[j].in_use &&
                            ctx->peers[j].platform_peer.endpoint == slot) {
                            slot->owner = &ctx->peers[j];
                            break;
                        }
                    }
                }

                if (!slot->owner) {
                    /* No room in peer table -- disconnect */
                    OTSndDisconnect(slot->ep, NULL);
                    reset_endpoint(slot);
                }
            }
        }
        } /* look_retries scope */
    }

    /* ---- UDP sockets ---- */
    poll_udp(ctx, &g_ot.discovery_udp, 1);
    poll_udp(ctx, &g_ot.message_udp, 0);
}

/* Event-driven seam: hand core one platform event per call, draining the
   round when it returns 0.  Core owns every state transition via
   pt_apply_platform_event (CONNECTED/DATA/CLOSED); the adapter only reports
   what the OT notifier observed.  T_DISCONNECT and T_ORDREL collapse into a
   single CLOSED after the final bytes (a buffered goodbye) are drained, so
   core can choose QUIT vs ERROR in exactly one place. */
static int ot_next_event(PT_Context_Internal *ctx, PT_Event *out)
{
    int i;

    if (!g_ot.ev_started) {
        ot_round_start(ctx);
        g_ot.ev_started = 1;
        g_ot.ev_cursor = 0;
    }

    for (i = g_ot.ev_cursor; i < OT_MAX_ENDPOINTS; i++) {
        OTEndpointSlot *slot = &g_ot.tcp_eps[i];
        unsigned long flags;
        int had_data, had_discon, had_ordrel, had_connect;
        int had_passcon, had_godata;

        if (slot->ep == kOTInvalidEndpointRef) continue;

        /* Atomic test-and-clear each flag individually (R27).
           OTAtomicClearBit returns previous state; notifier flags
           set after clear are preserved for the next round. */
        had_data    = OTAtomicClearBit((UInt8 *)&slot->flags, EVT_BIT_DATA);
        had_discon  = OTAtomicClearBit((UInt8 *)&slot->flags, EVT_BIT_DISCONNECT);
        had_ordrel  = OTAtomicClearBit((UInt8 *)&slot->flags, EVT_BIT_ORDREL);
        had_connect = OTAtomicClearBit((UInt8 *)&slot->flags, EVT_BIT_CONNECT);
        had_passcon = OTAtomicClearBit((UInt8 *)&slot->flags, EVT_BIT_PASSCON);
        had_godata  = OTAtomicClearBit((UInt8 *)&slot->flags, EVT_BIT_GODATA);
        /* PASSCON/GODATA need no event: incoming accept is wired up in
           ot_round_start, and flow-control resume is handled by the
           notifier clearing slot->flow_off on T_GODATA. */
        (void)had_passcon;
        (void)had_godata;

        flags = 0;
        if (had_data)    flags |= (1UL << EVT_BIT_DATA);
        if (had_discon)  flags |= (1UL << EVT_BIT_DISCONNECT);
        if (had_ordrel)  flags |= (1UL << EVT_BIT_ORDREL);
        if (had_connect) flags |= (1UL << EVT_BIT_CONNECT);

        if (!flags) continue;

        /* ---- Active connect completion -> CONNECTED event ---- */
        if (slot->state == EP_CONNECTING && (flags & (1UL << EVT_BIT_CONNECT))) {
            OTResult res = OTRcvConnect(slot->ep, NULL);

            g_ot.ev_cursor = i + 1;
            out->type = PT_EVT_CONNECTED;
            out->peer = slot->owner;
            if (res == kOTNoError && slot->owner) {
                slot->state = EP_CONNECTED;
                out->ok = 1;
            } else {
                /* core's pt_complete_connect(ok=0) calls ot_tcp_disconnect,
                   which drains + OTSndDisconnect + resets the endpoint. */
                out->ok = 0;
            }
            return 1;
        }

        /* ---- Disconnect / orderly release -> single CLOSED event ----
           Drain final bytes (incl. a buffered goodbye) into the peer
           buffer, consume the OT indication, then report CLOSED.  Core's
           pt_drain_disconnect parses the buffer (QUIT) or fires ERROR. */
        if (flags & ((1UL << EVT_BIT_DISCONNECT) | (1UL << EVT_BIT_ORDREL))) {
            PT_Peer_Internal *peer = slot->owner;

            if (peer) {
                ot_recv_into_peer(slot, peer);
            }
            if (flags & (1UL << EVT_BIT_DISCONNECT)) {
                OTRcvDisconnect(slot->ep, NULL);
            } else {
                OTRcvOrderlyDisconnect(slot->ep);
                OTSndOrderlyDisconnect(slot->ep);
            }
            g_ot.ev_cursor = i + 1;
            out->type = PT_EVT_CLOSED;
            out->peer = peer;
            return 1;
        }

        if (slot->state != EP_CONNECTED) continue;

        /* ---- Data available -> DATA event ---- */
        if (flags & (1UL << EVT_BIT_DATA)) {
            if (slot->owner) {
                ot_recv_into_peer(slot, slot->owner);
            }
            g_ot.ev_cursor = i + 1;
            out->type = PT_EVT_DATA;
            out->peer = slot->owner;
            return 1;
        }
    }

    /* Round drained. */
    g_ot.ev_started = 0;
    out->type = PT_EVT_NONE;
    out->peer = NULL;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Stream cleanup for rediscovery (feature parity with MacTCP backend) */
/* ------------------------------------------------------------------ */

static void ot_cleanup_streams(PT_Context_Internal *ctx)
{
    int i;
    (void)ctx;

    CLOG_INFO("Cleaning up OT endpoints for rediscovery");

    /* Drain stale events from the listener endpoint.  If a queued
     * connection was aborted (T_DISCONNECT) during the previous game's
     * disconnect sequence, the event stays pending because the notifier
     * only tracks it as a flag bit.  OT won't deliver new T_LISTEN
     * events while T_DISCONNECT is unconsumed (XTI state machine).
     * Draining here ensures the listener is clean for the next session. */
    if (g_ot.listener_ep != kOTInvalidEndpointRef) {
        drain_endpoint_events(g_ot.listener_ep);
        g_ot.listener_flags = 0;
    }

    for (i = 0; i < OT_MAX_ENDPOINTS; i++) {
        OTEndpointSlot *slot = &g_ot.tcp_eps[i];
        if (slot->ep == kOTInvalidEndpointRef) continue;

        /* Drain any stale events left by the notifier after disconnect */
        drain_endpoint_events(slot->ep);
        slot->flags = 0;

        /* If endpoint is still connected/connecting, force cleanup */
        if (slot->state != EP_FREE && slot->state != EP_LISTENING) {
            OTSndDisconnect(slot->ep, NULL);
            reset_endpoint(slot);
        }
    }

    CLOG_INFO("OT endpoint cleanup complete");
}

/* ------------------------------------------------------------------ */
/* Ops table                                                           */
/* ------------------------------------------------------------------ */

static PT_PlatformOps ot_ops = {
    ot_init,
    ot_shutdown,
    ot_udp_broadcast,
    ot_udp_send,
    ot_udp_listen,
    ot_tcp_listen,
    ot_tcp_connect,
    ot_tcp_send,
    ot_tcp_disconnect,
    ot_cleanup_streams,
    ot_next_event
};

PT_PlatformOps *ot_get_ops(void)
{
    return &ot_ops;
}

#endif /* PT_PLATFORM_OT */
