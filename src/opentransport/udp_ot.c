/**
 * @file udp_ot.c
 * @brief Open Transport UDP Endpoint Implementation
 *
 * UDP endpoint creation, send, receive, and close for discovery.
 * Uses hot/cold struct split for PPC cache efficiency.
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 7: "Connectionless"
 * - OpenTransport.h, OpenTransportProviders.h (Retro68)
 */

#include "ot_defs.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_OT)

/* ========================================================================== */
/* UDP Notifier Callback                                                      */
/* ========================================================================== */

/**
 * UDP Asynchronous Notification Routine.
 *
 * CRITICAL: Called at deferred task time (NOT system task time).
 * From Networking With Open Transport:
 * - MUST NOT move or purge memory
 * - MUST NOT make synchronous OT calls
 * - MUST NOT perform time-consuming tasks
 * - May be called reentrantly (use atomic operations)
 *
 * Strategy: Set flags only using OTAtomicSetBit, let main loop process.
 *
 * @param context  Pointer to pt_udp_endpoint_hot struct
 * @param code     OT event code (T_DATA, T_UDERR, T_GODATA, etc.)
 * @param result   Result code for completion events
 * @param cookie   Event-specific data (unused for UDP events)
 */
pascal void pt_ot_udp_notifier(void *context, OTEventCode code,
                                OTResult result, void *cookie)
{
    pt_udp_endpoint_hot *hot = (pt_udp_endpoint_hot *)context;

    (void)result;
    (void)cookie;

    switch (code) {
    case T_DATA:
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_DATA_AVAILABLE);
        break;

    case T_UDERR:
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_UDERR_PENDING);
        break;

    case T_GODATA:
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_GODATA);
        break;

    case T_BINDCOMPLETE:
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_BIND_COMPLETE);
        break;

    default:
        break;
    }
}

/* ========================================================================== */
/* Broadcast Option                                                           */
/* ========================================================================== */

/**
 * Enable IP broadcast option on endpoint.
 *
 * Uses OTOptionManagement with T_NEGOTIATE to set IP_BROADCAST.
 * Required before OTSndUData to a broadcast address.
 *
 * @param ctx  PeerTalk context
 * @param ref  Endpoint reference
 * @return     0 on success, -1 on error
 */
static int pt_ot_udp_enable_broadcast(struct pt_context *ctx, EndpointRef ref)
{
    UInt8 opt_buf[kOTFourByteOptionSize];
    TOption *opt = (TOption *)opt_buf;
    TOptMgmt req, ret;
    OSStatus err;

    opt->len = kOTFourByteOptionSize;
    opt->level = INET_IP;
    opt->name = kIP_BROADCAST;
    opt->status = 0;
    opt->value[0] = 1;  /* Enable */

    req.opt.buf = opt_buf;
    req.opt.len = kOTFourByteOptionSize;
    req.opt.maxlen = kOTFourByteOptionSize;
    req.flags = T_NEGOTIATE;

    ret.opt.buf = opt_buf;
    ret.opt.len = 0;
    ret.opt.maxlen = kOTFourByteOptionSize;
    ret.flags = 0;

    err = OTOptionManagement(ref, &req, &ret);
    if (err != kOTNoError) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
            "OTOptionManagement(IP_BROADCAST) failed: %ld", (long)err);
        return -1;
    }

    /* Check result status */
    opt = (TOption *)ret.opt.buf;
    if (opt->status != T_SUCCESS) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
            "IP_BROADCAST option status: 0x%lX", (unsigned long)opt->status);
    }

    return 0;
}

/* ========================================================================== */
/* UDP Endpoint Creation                                                      */
/* ========================================================================== */

/**
 * Create UDP endpoint for discovery.
 *
 * From Networking With Open Transport:
 * 1. Clone master UDP configuration
 * 2. Open endpoint (disposes the cloned config)
 * 3. Install notifier with UPP
 * 4. Set non-blocking mode
 * 5. Enable broadcast option
 * 6. Bind to specified port
 *
 * Uses hot/cold struct split for cache efficiency:
 * - Hot: ~12 bytes, polled every iteration
 * - Cold: ~2KB, accessed during I/O
 *
 * @param ctx   PeerTalk context
 * @param port  Port to bind (0 for system-assigned)
 * @return      0 on success, -1 on error
 */
int pt_ot_udp_create(struct pt_context *ctx, InetPort port)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_udp_endpoint_hot *hot = &od->udp_hot;
    pt_udp_endpoint_cold *cold = od->udp_cold;
    OTConfigurationRef config;
    EndpointRef ref;
    OSStatus err;
    TBind bind_req, bind_ret;

    if (hot->state != PT_EP_UNUSED) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
            "UDP endpoint already exists (state=%s)",
            pt_ep_state_name(hot->state));
        return -1;
    }

    if (cold == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "UDP cold data not allocated");
        return -1;
    }

    /* Clone master UDP configuration.
     * CRITICAL: OTOpenEndpoint disposes the config it receives,
     * so we must clone before each use. */
    config = OTCloneConfiguration(od->udp_config);
    if (config == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "OTCloneConfiguration(udp) failed");
        return -1;
    }

    /* Clear hot flags */
    PT_FLAGS_CLEAR_ALL(hot->flags);

    /* Open endpoint synchronously */
    ref = OTOpenEndpoint(config, 0, NULL, &err);
    /* config is now disposed by OT regardless of success/failure */

    if (err != kOTNoError || ref == kOTInvalidEndpointRef) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
            "OTOpenEndpoint(udp) failed: %ld", (long)err);
        return -1;
    }

    hot->ref = ref;
    hot->state = PT_EP_UNBOUND;

    /* Install notifier using pre-created UPP.
     * Pass hot struct as context - notifier only touches hot data. */
    if (od->udp_notifier_upp == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT,
            "UDP notifier UPP not created");
        OTCloseProvider(ref);
        hot->ref = kOTInvalidEndpointRef;
        hot->state = PT_EP_UNUSED;
        return -1;
    }

    err = OTInstallNotifier(ref, od->udp_notifier_upp, hot);
    if (err != kOTNoError) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
            "OTInstallNotifier(udp) failed: %ld", (long)err);
        OTCloseProvider(ref);
        hot->ref = kOTInvalidEndpointRef;
        hot->state = PT_EP_UNUSED;
        return -1;
    }

    /* Set non-blocking mode for all I/O operations */
    err = OTSetNonBlocking(ref);
    if (err != kOTNoError) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
            "OTSetNonBlocking(udp) failed: %ld (continuing)", (long)err);
    }

    /* Enable broadcast option for discovery sends */
    pt_ot_udp_enable_broadcast(ctx, ref);

    /* Bind to specified port.
     * Use cold data for address storage. */
    OTInitInetAddress(&cold->local_addr, port, kOTAnyInetAddress);

    bind_req.addr.buf = (UInt8 *)&cold->local_addr;
    bind_req.addr.len = sizeof(InetAddress);
    bind_req.addr.maxlen = sizeof(InetAddress);
    bind_req.qlen = 0;  /* UDP: no listen queue */

    bind_ret.addr.buf = (UInt8 *)&cold->local_addr;
    bind_ret.addr.len = 0;
    bind_ret.addr.maxlen = sizeof(InetAddress);
    bind_ret.qlen = 0;

    err = OTBind(ref, &bind_req, &bind_ret);
    if (err != kOTNoError) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
            "OTBind(udp, port=%u) failed: %ld",
            (unsigned)port, (long)err);
        OTCloseProvider(ref);
        hot->ref = kOTInvalidEndpointRef;
        hot->state = PT_EP_UNUSED;
        return -1;
    }

    hot->state = PT_EP_IDLE;

    PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK,
        "UDP endpoint created: ref=0x%08lX port=%u",
        (unsigned long)ref, (unsigned)port);

    return 0;
}

/* ========================================================================== */
/* UDP Send                                                                   */
/* ========================================================================== */

/**
 * Send UDP datagram.
 *
 * Uses OTSndUData for datagram transmission.
 *
 * @param ctx        PeerTalk context
 * @param dest_ip    Destination IP (network byte order)
 * @param dest_port  Destination port
 * @param data       Data to send
 * @param len        Length of data
 * @return           0 on success, -1 on error
 */
int pt_ot_udp_send(struct pt_context *ctx,
                    InetHost dest_ip, InetPort dest_port,
                    const void *data, size_t len)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_udp_endpoint_hot *hot = &od->udp_hot;
    InetAddress dest_addr;
    TUnitData udata;
    OSStatus err;

    if (hot->state != PT_EP_IDLE) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
            "UDP endpoint not idle: state=%s",
            pt_ep_state_name(hot->state));
        return -1;
    }

    /* Setup destination address */
    OTInitInetAddress(&dest_addr, dest_port, dest_ip);

    /* Setup TUnitData for send */
    udata.addr.buf = (UInt8 *)&dest_addr;
    udata.addr.len = sizeof(InetAddress);
    udata.addr.maxlen = sizeof(InetAddress);

    udata.opt.buf = NULL;
    udata.opt.len = 0;
    udata.opt.maxlen = 0;

    udata.udata.buf = (UInt8 *)data;
    udata.udata.len = (ByteCount)len;
    udata.udata.maxlen = (ByteCount)len;

    err = OTSndUData(hot->ref, &udata);

    if (err == kOTFlowErr) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
            "UDP send flow control (kOTFlowErr)");
        return 0;
    }

    if (err == kOTLookErr) {
        /* Async event pending - likely T_UDERR */
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_UDERR_PENDING);
        return 0;
    }

    if (err != kOTNoError) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
            "OTSndUData failed: %ld", (long)err);
        return -1;
    }

    return 0;
}

/* ========================================================================== */
/* UDP Receive                                                                */
/* ========================================================================== */

/**
 * Receive UDP datagram (non-blocking).
 *
 * Returns one datagram at a time. Caller must loop until this returns 0
 * to fully drain the receive queue and clear T_DATA event.
 *
 * Uses cold data for receive buffer and address storage.
 *
 * @param ctx        PeerTalk context
 * @param from_ip    [out] Source IP address
 * @param from_port  [out] Source port
 * @param data       [out] Buffer to receive data
 * @param len        [in/out] Buffer size in, bytes received out
 * @return           1 if data received, 0 if no data, -1 on error
 */
int pt_ot_udp_recv(struct pt_context *ctx,
                    InetHost *from_ip, InetPort *from_port,
                    void *data, size_t *len)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_udp_endpoint_hot *hot = &od->udp_hot;
    pt_udp_endpoint_cold *cold = od->udp_cold;
    TUnitData udata;
    OTFlags flags = 0;
    OSStatus err;
    size_t copy_len;

    if (hot->state != PT_EP_IDLE)
        return 0;

    /* Quick check: notifier flag indicates data is available */
    if (!PT_FLAG_TEST(hot->flags, PT_OT_FLAG_DATA_AVAILABLE))
        return 0;

    /* Setup receive into cold buffer */
    pt_memset(&cold->recv_addr, 0, sizeof(cold->recv_addr));

    udata.addr.buf = (UInt8 *)&cold->recv_addr;
    udata.addr.len = 0;
    udata.addr.maxlen = sizeof(InetAddress);

    udata.opt.buf = NULL;
    udata.opt.len = 0;
    udata.opt.maxlen = 0;

    udata.udata.buf = cold->recv_buf;
    udata.udata.len = 0;
    udata.udata.maxlen = sizeof(cold->recv_buf);

    err = OTRcvUData(hot->ref, &udata, &flags);

    if (err == kOTNoDataErr) {
        /* No more data - clear flag so we don't keep polling */
        PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_DATA_AVAILABLE);
        return 0;
    }

    if (err == kOTLookErr) {
        /* Async event pending (T_UDERR) - clear data flag */
        PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_DATA_AVAILABLE);
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_UDERR_PENDING);
        return 0;
    }

    if (err != kOTNoError) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
            "OTRcvUData failed: %ld", (long)err);
        PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_DATA_AVAILABLE);
        return -1;
    }

    /* Extract source address */
    *from_ip = cold->recv_addr.fHost;
    *from_port = cold->recv_addr.fPort;

    /* Copy data to caller buffer */
    copy_len = (size_t)udata.udata.len;
    if (copy_len > *len)
        copy_len = *len;

    pt_memcpy(data, cold->recv_buf, copy_len);
    *len = copy_len;

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
        "UDP recv %u bytes from %lu.%lu.%lu.%lu:%u",
        (unsigned)copy_len,
        ((unsigned long)*from_ip >> 24) & 0xFF,
        ((unsigned long)*from_ip >> 16) & 0xFF,
        ((unsigned long)*from_ip >> 8) & 0xFF,
        (unsigned long)*from_ip & 0xFF,
        (unsigned)*from_port);

    /* Note: Do NOT clear PT_OT_FLAG_DATA_AVAILABLE here.
     * Caller should loop until we return 0 (kOTNoDataErr)
     * to fully drain the receive queue. */

    return 1;  /* Got data */
}

/* ========================================================================== */
/* UDP Error Clearing                                                         */
/* ========================================================================== */

/**
 * Clear pending UDP error indication.
 *
 * MUST be called from main loop ONLY (NOT from notifier).
 * OTRcvUDErr is NOT in Table C-1 (not interrupt-safe).
 *
 * @param ctx  PeerTalk context
 */
void pt_ot_udp_clear_error(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_udp_endpoint_hot *hot = &od->udp_hot;
    TUDErr uderr;
    InetAddress err_addr;
    OSStatus err;

    if (!PT_FLAG_TEST(hot->flags, PT_OT_FLAG_UDERR_PENDING))
        return;

    PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_UDERR_PENDING);

    uderr.addr.buf = (UInt8 *)&err_addr;
    uderr.addr.len = 0;
    uderr.addr.maxlen = sizeof(InetAddress);
    uderr.opt.buf = NULL;
    uderr.opt.len = 0;
    uderr.opt.maxlen = 0;
    uderr.error = 0;

    err = OTRcvUDErr(hot->ref, &uderr);

    if (err == kOTNoError) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
            "UDP error cleared: error=%ld dest=%lu.%lu.%lu.%lu:%u",
            (long)uderr.error,
            ((unsigned long)err_addr.fHost >> 24) & 0xFF,
            ((unsigned long)err_addr.fHost >> 16) & 0xFF,
            ((unsigned long)err_addr.fHost >> 8) & 0xFF,
            (unsigned long)err_addr.fHost & 0xFF,
            (unsigned)err_addr.fPort);
    } else if (err != kOTNoUDErrErr) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
            "OTRcvUDErr failed: %ld", (long)err);
    }
}

/* ========================================================================== */
/* UDP Close                                                                  */
/* ========================================================================== */

/**
 * Close UDP endpoint.
 *
 * Unbinds and closes the provider. Sets state to PT_EP_UNUSED.
 *
 * @param ctx  PeerTalk context
 */
void pt_ot_udp_close(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_udp_endpoint_hot *hot = &od->udp_hot;

    if (hot->state == PT_EP_UNUSED)
        return;

    if (hot->ref != kOTInvalidEndpointRef) {
        /* Try orderly unbind if bound */
        if (hot->state >= PT_EP_IDLE) {
            OTResult ep_state = OTGetEndpointState(hot->ref);
            if (ep_state == T_IDLE)
                OTUnbind(hot->ref);
        }

        OTCloseProvider(hot->ref);
        hot->ref = kOTInvalidEndpointRef;
    }

    PT_FLAGS_CLEAR_ALL(hot->flags);
    hot->state = PT_EP_UNUSED;

    PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK, "UDP endpoint closed");
}

#endif /* PT_PLATFORM_OT */
