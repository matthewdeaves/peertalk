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

#include <OpenTransport.h>
#include <OpenTransportProviders.h>

/*
 * Retro68 import libraries export the non-InContext symbols
 * (OTOpenEndpoint, InitOpenTransport, CloseOpenTransport) but the
 * header #defines those names as macros that expand to the InContext
 * variants, which don't exist in the import libs.  Undo the redirects
 * so we can call the real library symbols directly.
 */
#undef OTOpenEndpoint
#undef InitOpenTransport
#undef CloseOpenTransport

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

/* Event flag bits for notifier */
#define EVT_DATA            0x0001UL
#define EVT_DISCONNECT      0x0002UL
#define EVT_ORDREL          0x0004UL
#define EVT_CONNECT         0x0008UL
#define EVT_LISTEN          0x0010UL
#define EVT_PASSCON         0x0020UL
#define EVT_GODATA          0x0040UL

/* UDP event flag bits */
#define EVT_UDP_DATA        0x0001UL

/* ------------------------------------------------------------------ */
/* Per-endpoint state                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    EndpointRef     ep;
    volatile unsigned long flags;
    int             state;
    volatile int    flow_off;   /* flow control active (set by notifier) */
} OTEndpointSlot;

typedef struct {
    EndpointRef     ep;
    volatile unsigned long flags;
} OTUDPSlot;

/* ------------------------------------------------------------------ */
/* Platform state                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    InetHost        local_ip;

    /* Listener endpoint (tilisten,tcp) */
    EndpointRef     listener_ep;
    volatile unsigned long listener_flags;

    /* Per-peer TCP endpoints for data transfer */
    OTEndpointSlot  tcp_eps[OT_MAX_ENDPOINTS];

    /* UDP endpoints */
    OTUDPSlot       discovery_udp;
    OTUDPSlot       message_udp;

    /* UPP handles (must be disposed on shutdown) */
    OTNotifyUPP     listener_upp;
    OTNotifyUPP     tcp_upp;
    OTNotifyUPP     udp_upp;
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
        st->listener_flags |= EVT_LISTEN;
    } else if (code == T_PASSCON) {
        st->listener_flags |= EVT_PASSCON;
    }
}

static pascal void tcp_notifier(void *context, OTEventCode code,
                                OTResult result, void *cookie)
{
    OTEndpointSlot *slot = (OTEndpointSlot *)context;
    (void)result; (void)cookie;

    switch (code) {
    case T_DATA:
        slot->flags |= EVT_DATA;
        break;
    case T_DISCONNECT:
        slot->flags |= EVT_DISCONNECT;
        break;
    case T_ORDREL:
        slot->flags |= EVT_ORDREL;
        break;
    case T_CONNECT:
        slot->flags |= EVT_CONNECT;
        break;
    case T_GODATA:
        slot->flags |= EVT_GODATA;
        slot->flow_off = 0;
        break;
    case T_PASSCON:
        slot->flags |= EVT_PASSCON;
        break;
    }
}

static pascal void udp_notifier(void *context, OTEventCode code,
                                OTResult result, void *cookie)
{
    OTUDPSlot *slot = (OTUDPSlot *)context;
    (void)result; (void)cookie;

    if (code == T_DATA) {
        slot->flags |= EVT_UDP_DATA;
    }
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

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

static PT_Peer_Internal *find_peer_for_ep(PT_Context_Internal *ctx,
                                          OTEndpointSlot *slot)
{
    int j;
    for (j = 0; j < ctx->max_peers; j++) {
        if (ctx->peers[j].in_use &&
            ctx->peers[j].platform_peer.endpoint == slot) {
            return &ctx->peers[j];
        }
    }
    return NULL;
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
        CloseOpenTransport();
        return PT_ERR_INIT;
    }
    g_ot.listener_upp = listener_upp;
    g_ot.tcp_upp = tcp_upp;
    g_ot.udp_upp = udp_upp;

    /* Create listener endpoint with tilisten,tcp */
    {
        OTConfigurationRef lconfig = OTCreateConfiguration("tilisten,tcp");
        if (!lconfig) {
            CLOG_ERR("Failed to create listener config");
            CloseOpenTransport();
            return PT_ERR_INIT;
        }
        g_ot.listener_ep = OTOpenEndpoint(lconfig, 0, NULL, &err);
    }
    if (err != kOTNoError || g_ot.listener_ep == kOTInvalidEndpointRef) {
        CLOG_ERR("Failed to create listener endpoint: %d", (int)err);
        CloseOpenTransport();
        return PT_ERR_INIT;
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
        CloseOpenTransport();
        return PT_ERR_INIT;
    }

    /* Now switch to async for runtime event handling */
    err = OTInstallNotifier(g_ot.listener_ep, listener_upp, &g_ot);
    if (err != kOTNoError) {
        CLOG_ERR("Failed to install listener notifier: %d", (int)err);
        OTCloseProvider(g_ot.listener_ep);
        CloseOpenTransport();
        return PT_ERR_INIT;
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
    }

    /* Create UDP discovery endpoint (port 7353) */
    g_ot.discovery_udp.ep = create_udp_endpoint(
        udp_upp, &g_ot.discovery_udp, PT_DISCOVERY_PORT);
    if (g_ot.discovery_udp.ep == kOTInvalidEndpointRef) {
        CLOG_ERR("Failed to create discovery UDP endpoint");
        CloseOpenTransport();
        return PT_ERR_INIT;
    }

    /* Create UDP message endpoint (port 7355) */
    g_ot.message_udp.ep = create_udp_endpoint(
        udp_upp, &g_ot.message_udp, PT_UDP_MSG_PORT);
    if (g_ot.message_udp.ep == kOTInvalidEndpointRef) {
        CLOG_ERR("Failed to create message UDP endpoint");
        CloseOpenTransport();
        return PT_ERR_INIT;
    }

    ctx->platform_state = &g_ot;

    CLOG_INFO("Open Transport initialized (IP: %lu.%lu.%lu.%lu)",
              (g_ot.local_ip >> 24) & 0xFF,
              (g_ot.local_ip >> 16) & 0xFF,
              (g_ot.local_ip >> 8) & 0xFF,
              g_ot.local_ip & 0xFF);

    return PT_OK;
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
                             PT_Peer_Internal *peer,
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

    OTSndDisconnect(slot->ep, NULL);

    /* Unbind and rebind synchronously to reset endpoint for reuse (R29).
       Async unbind/rebind races: completion may not arrive before next use. */
    OTSetSynchronous(slot->ep);
    OTSetBlocking(slot->ep);
    OTUnbind(slot->ep);
    {
        InetAddress addr;
        TBind req;
        OTInitInetAddress(&addr, 0, 0);
        req.addr.maxlen = sizeof(addr);
        req.addr.len = sizeof(addr);
        req.addr.buf = (unsigned char *)&addr;
        req.qlen = 0;
        OTBind(slot->ep, &req, NULL);
    }
    OTSetAsynchronous(slot->ep);
    OTSetNonBlocking(slot->ep);

    slot->state = EP_FREE;
    slot->flags = 0;
    slot->flow_off = 0;

    peer->platform_peer.endpoint = NULL;
    peer->platform_peer.events = 0;
}

static void poll_udp(PT_Context_Internal *ctx, OTUDPSlot *us,
                     int is_discovery)
{
    if (!(us->flags & EVT_UDP_DATA)) return;
    /* Snapshot-and-clear: single long write is atomic on PPC (R27) */
    us->flags = 0;

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

static void ot_poll(PT_Context_Internal *ctx)
{
    int i;
    unsigned long flags;

    /* ---- Handle listener events (T_LISTEN) ---- */
    if (g_ot.listener_flags & EVT_LISTEN) {
        /* Snapshot-and-clear: single long write is atomic on PPC (R27) */
        {
            unsigned long lf = g_ot.listener_flags;
            g_ot.listener_flags = 0;
            (void)lf;
        }

        /* Process all pending listen indications */
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
            if (res != kOTNoError) break;

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
                continue;
            }

            /* Store remote IP temporarily so we can match on T_PASSCON.
               Use the peer->platform_peer.events field to stash the IP
               until passcon fires. We'll handle the actual connection
               setup when T_PASSCON arrives on the data endpoint. */
            slot->flags |= EVT_PASSCON; /* trigger immediate processing */

            /* Directly set up the connection now since tilisten
               guarantees OTAccept won't fail with kOTLookErr */
            {
                PT_PlatformPeer ppeer;
                memset(&ppeer, 0, sizeof(ppeer));
                ppeer.endpoint = slot;
                ppeer.events = 0;

                slot->state = EP_CONNECTED;

                pt_handle_incoming_connection(
                    ctx, (unsigned long)remote_addr.fHost, &ppeer);

                /* Check if peer accepted */
                if (!find_peer_for_ep(ctx, slot)) {
                    /* No room in peer table -- disconnect */
                    OTSndDisconnect(slot->ep, NULL);
                    OTSetSynchronous(slot->ep);
                    OTSetBlocking(slot->ep);
                    OTUnbind(slot->ep);
                    {
                        InetAddress addr;
                        TBind req;
                        OTInitInetAddress(&addr, 0, 0);
                        req.addr.maxlen = sizeof(addr);
                        req.addr.len = sizeof(addr);
                        req.addr.buf = (unsigned char *)&addr;
                        req.qlen = 0;
                        OTBind(slot->ep, &req, NULL);
                    }
                    OTSetAsynchronous(slot->ep);
                    OTSetNonBlocking(slot->ep);
                    slot->state = EP_FREE;
                    slot->flags = 0;
                }
            }
        }
    }

    /* ---- Process TCP data endpoints ---- */
    for (i = 0; i < OT_MAX_ENDPOINTS; i++) {
        OTEndpointSlot *slot = &g_ot.tcp_eps[i];
        if (slot->ep == kOTInvalidEndpointRef) continue;

        /* Snapshot-and-clear: single long write is atomic on PPC (R27).
           Notifier flags set after this clear are preserved for next poll. */
        flags = slot->flags;
        slot->flags = 0;
        if (!flags && slot->state == EP_FREE) continue;

        /* ---- Active connect completion ---- */
        if (slot->state == EP_CONNECTING && (flags & EVT_CONNECT)) {
            PT_Peer_Internal *peer;
            OTResult res;

            res = OTRcvConnect(slot->ep, NULL);

            peer = find_peer_for_ep(ctx, slot);
            if (res == kOTNoError && peer) {
                slot->state = EP_CONNECTED;
                peer->state = PT_PEER_CONNECTED;
                peer->last_tcp_activity = ctx->current_time;
                peer->connect_start = 0;

                CLOG_INFO("TCP connected to %s", peer->name);
                if (ctx->callbacks.on_connected) {
                    ctx->callbacks.on_connected(
                        (PT_Peer *)peer,
                        ctx->callbacks.on_connected_data);
                }
            } else {
                /* Connection failed */
                OTSndDisconnect(slot->ep, NULL);
                OTSetSynchronous(slot->ep);
                OTSetBlocking(slot->ep);
                OTUnbind(slot->ep);
                {
                    InetAddress addr;
                    TBind req;
                    OTInitInetAddress(&addr, 0, 0);
                    req.addr.maxlen = sizeof(addr);
                    req.addr.len = sizeof(addr);
                    req.addr.buf = (unsigned char *)&addr;
                    req.qlen = 0;
                    OTBind(slot->ep, &req, NULL);
                }
                OTSetAsynchronous(slot->ep);
                OTSetNonBlocking(slot->ep);
                slot->state = EP_FREE;
                slot->flags = 0;
                if (peer) {
                    peer->connect_start = 0;
                    peer->state = PT_PEER_DISCONNECTED;
                    peer->platform_peer.endpoint = NULL;
                    pt_fire_error(ctx, peer, PT_ERR_SEND_FAILED,
                                  "OT connect failed");
                }
            }
            continue;
        }

        /* ---- Disconnect event ---- */
        if (flags & EVT_DISCONNECT) {
            PT_Peer_Internal *peer;

            /* Drain remaining data before disconnect — goodbye frame
               may be readable even after T_DISCONNECT (R23). */
            peer = find_peer_for_ep(ctx, slot);
            if (peer) {
                size_t space = peer->tcp_recv_size -
                               peer->tcp_recv_len;
                if (space > 0) {
                    OTResult nread;
                    OTFlags rflags = 0;
                    nread = OTRcv(slot->ep,
                                  peer->tcp_recv_buf +
                                      peer->tcp_recv_len,
                                  (OTByteCount)space, &rflags);
                    if (nread > 0) {
                        peer->tcp_recv_len += (size_t)nread;
                    }
                }
                if (peer->tcp_recv_len > 0) {
                    pt_messaging_process_tcp_data(ctx, peer);
                }
            }

            OTRcvDisconnect(slot->ep, NULL);

            if (peer && peer->state == PT_PEER_CONNECTED) {
                pt_handle_peer_disconnect(ctx, peer,
                                          PT_DISCONNECT_ERROR);
            }
            continue;
        }

        /* ---- Orderly release ---- */
        if (flags & EVT_ORDREL) {
            PT_Peer_Internal *peer;

            /* Drain remaining data before orderly release — goodbye
               frame may still be in the receive buffer (R23). */
            peer = find_peer_for_ep(ctx, slot);
            if (peer) {
                size_t space = peer->tcp_recv_size -
                               peer->tcp_recv_len;
                if (space > 0) {
                    OTResult nread;
                    OTFlags rflags = 0;
                    nread = OTRcv(slot->ep,
                                  peer->tcp_recv_buf +
                                      peer->tcp_recv_len,
                                  (OTByteCount)space, &rflags);
                    if (nread > 0) {
                        peer->tcp_recv_len += (size_t)nread;
                    }
                }
                if (peer->tcp_recv_len > 0) {
                    pt_messaging_process_tcp_data(ctx, peer);
                }
            }

            OTRcvOrderlyDisconnect(slot->ep);
            OTSndOrderlyDisconnect(slot->ep);

            if (peer && peer->state == PT_PEER_CONNECTED) {
                pt_handle_peer_disconnect(ctx, peer,
                                          PT_DISCONNECT_ERROR);
            }
            continue;
        }

        if (slot->state != EP_CONNECTED) continue;

        /* ---- Data available ---- */
        if (flags & EVT_DATA) {
            PT_Peer_Internal *peer;

            peer = find_peer_for_ep(ctx, slot);
            if (peer) {
                size_t space = peer->tcp_recv_size - peer->tcp_recv_len;
                if (space > 0) {
                    OTResult nread;
                    OTFlags rflags;

                    rflags = 0;
                    nread = OTRcv(slot->ep,
                                  peer->tcp_recv_buf + peer->tcp_recv_len,
                                  (OTByteCount)space, &rflags);
                    if (nread > 0) {
                        peer->tcp_recv_len += (size_t)nread;
                        peer->last_tcp_activity = ctx->current_time;
                        pt_messaging_process_tcp_data(ctx, peer);
                    }
                }
            }
        }
    }

    /* ---- UDP sockets ---- */
    poll_udp(ctx, &g_ot.discovery_udp, 1);
    poll_udp(ctx, &g_ot.message_udp, 0);
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
    ot_poll
};

PT_PlatformOps *ot_get_ops(void)
{
    return &ot_ops;
}

#endif /* PT_PLATFORM_OT */
