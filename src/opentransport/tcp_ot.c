/**
 * @file tcp_ot.c
 * @brief Open Transport TCP Endpoint Implementation
 *
 * TCP endpoint creation, binding, sending, close, and cleanup.
 * Uses hot/cold struct split and O(1) bitmap pool allocation.
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 6: "TCP/IP"
 * - OpenTransport.h, OpenTransportProviders.h (Retro68)
 */

#include "ot_defs.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_OT)

#include <OSUtils.h>  /* TickCount() */

/* Close timeout: 30 seconds in ticks (~1800 at 60Hz) */
#define PT_OT_CLOSE_TIMEOUT_TICKS  1800

/* Forward declaration - cleanup is called from close */
void pt_ot_tcp_cleanup(struct pt_context *ctx, int idx);

/* ========================================================================== */
/* TCP Notifier Callback                                                      */
/* ========================================================================== */

/**
 * TCP Asynchronous Notification Routine.
 *
 * CRITICAL: Called at deferred task time.
 * From Networking With Open Transport:
 * - MUST NOT move or purge memory
 * - MUST NOT make synchronous OT calls
 * - May be called reentrantly (use atomic operations)
 *
 * Strategy: Set flags and store result codes. Main loop processes.
 *
 * @param context  Pointer to pt_tcp_endpoint_hot struct
 * @param code     OT event code
 * @param result   Result code for completion events
 * @param cookie   Event-specific data
 */
pascal void pt_ot_tcp_notifier(void *context, OTEventCode code,
                                OTResult result, void *cookie)
{
    pt_tcp_endpoint_hot *hot = (pt_tcp_endpoint_hot *)context;

    (void)cookie;

    switch (code) {
    /* ---- Asynchronous events ---- */

    case T_LISTEN:
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_LISTEN_PENDING);
        hot->log_events |= PT_OT_LOG_EVT_LISTEN_DONE;
        break;

    case T_CONNECT:
        hot->async_result = result;
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_CONNECT_COMPLETE);
        hot->log_events |= PT_OT_LOG_EVT_CONNECT_DONE;
        break;

    case T_DATA:
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_DATA_AVAILABLE);
        hot->log_events |= PT_OT_LOG_EVT_DATA_IN;
        break;

    case T_DISCONNECT:
        hot->async_result = result;
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_DISCONNECT);
        if (result != kOTNoError) {
            hot->log_error_code = result;
            hot->log_events |= PT_OT_LOG_EVT_ERROR;
        }
        break;

    case T_ORDREL:
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_ORDERLY_RELEASE);
        hot->log_events |= PT_OT_LOG_EVT_CLOSE_DONE;
        break;

    case T_GODATA:
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_GODATA);
        break;

    case T_PASSCON:
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_PASSCON);
        break;

    /* ---- Completion events ---- */

    case T_ACCEPTCOMPLETE:
        hot->async_result = result;
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_ACCEPT_COMPLETE);
        hot->log_events |= PT_OT_LOG_EVT_ACCEPT_DONE;
        break;

    case T_BINDCOMPLETE:
        hot->async_result = result;
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_BIND_COMPLETE);
        hot->log_events |= PT_OT_LOG_EVT_BIND_DONE;
        break;

    case T_MEMORYRELEASED:
        /* WARNING: May be called reentrantly (OT docs p.21353-21355) */
        PT_FLAG_SET(hot->flags, PT_OT_FLAG_SEND_COMPLETE);
        break;

    case T_OPENCOMPLETE:
        hot->async_result = result;
        break;

    /* Ignore other events */
    default:
        break;
    }
}

/* ========================================================================== */
/* Deferred Log Processing                                                    */
/* ========================================================================== */

/**
 * Process deferred log events from notifier.
 *
 * Notifier sets PT_OT_LOG_EVT_* bits and stores error codes in hot struct.
 * This function checks bits, logs, and clears them. Called from main loop.
 *
 * @param ctx  PeerTalk context
 * @param hot  TCP endpoint hot data
 * @param idx  Endpoint index (for log messages)
 */
void pt_ot_tcp_process_log_events(struct pt_context *ctx,
                                   pt_tcp_endpoint_hot *hot,
                                   int idx)
{
    uint8_t events = hot->log_events;
    if (events == 0)
        return;

    hot->log_events = 0;  /* Clear all pending log events */

    if (events & PT_OT_LOG_EVT_CONNECT_DONE) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] connect complete: result=%ld",
            idx, (long)hot->async_result);
    }

    if (events & PT_OT_LOG_EVT_LISTEN_DONE) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] listen event", idx);
    }

    if (events & PT_OT_LOG_EVT_ACCEPT_DONE) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] accept complete: result=%ld",
            idx, (long)hot->async_result);
    }

    if (events & PT_OT_LOG_EVT_CLOSE_DONE) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] orderly release received", idx);
    }

    if (events & PT_OT_LOG_EVT_ERROR) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] error: %ld",
            idx, (long)hot->log_error_code);
    }

    if (events & PT_OT_LOG_EVT_DATA_IN) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
            "TCP[%d] data available", idx);
    }

    if (events & PT_OT_LOG_EVT_BIND_DONE) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] bind complete: result=%ld",
            idx, (long)hot->async_result);
    }
}

/* ========================================================================== */
/* TCP Endpoint Creation                                                      */
/* ========================================================================== */

/**
 * Create a TCP endpoint from the pool.
 *
 * Allocates a slot using O(1) bitmap, opens endpoint with cloned TCP
 * configuration, installs notifier, and sets async mode.
 *
 * @param ctx  PeerTalk context
 * @return     Endpoint index (0..PT_MAX_PEERS-1) on success, -1 on failure
 */
int pt_ot_tcp_create(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot;
    pt_tcp_endpoint_cold *cold;
    OTConfigurationRef config;
    EndpointRef ref;
    OSStatus err;
    int idx;

    /* Allocate slot from bitmap pool (O(1)) */
    idx = pt_endpoint_pool_alloc(&od->tcp_pool);
    if (idx < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "TCP endpoint pool exhausted (count=%d/%d)",
            (int)od->tcp_pool.count, (int)od->tcp_pool.capacity);
        return -1;
    }

    hot = pt_ot_get_tcp_hot(od, idx);
    cold = pt_ot_get_tcp_cold(od, idx);
    if (hot == NULL || cold == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "TCP endpoint data NULL for idx=%d", idx);
        pt_endpoint_pool_free(&od->tcp_pool, idx);
        return -1;
    }

    /* Clear hot and cold data */
    PT_FLAGS_CLEAR_ALL(hot->flags);
    hot->async_result = 0;
    hot->peer = NULL;
    hot->close_start = 0;
    hot->log_error_code = 0;
    hot->log_events = 0;
    hot->endpoint_idx = (uint8_t)idx;

    pt_memset(cold, 0, sizeof(pt_tcp_endpoint_cold));

    /* Clone master TCP configuration */
    config = OTCloneConfiguration(od->tcp_config);
    if (config == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "OTCloneConfiguration(tcp) failed");
        pt_endpoint_pool_free(&od->tcp_pool, idx);
        return -1;
    }

    /* Open endpoint synchronously */
    ref = OTOpenEndpoint(config, 0, NULL, &err);
    /* config is now disposed by OT */

    if (err != kOTNoError || ref == kOTInvalidEndpointRef) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTOpenEndpoint(tcp[%d]) failed: %ld", idx, (long)err);
        pt_endpoint_pool_free(&od->tcp_pool, idx);
        return -1;
    }

    hot->ref = ref;
    hot->state = PT_EP_UNBOUND;

    /* Install notifier using pre-created UPP */
    if (od->tcp_notifier_upp == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT,
            "TCP notifier UPP not created");
        OTCloseProvider(ref);
        hot->ref = kOTInvalidEndpointRef;
        hot->state = PT_EP_UNUSED;
        pt_endpoint_pool_free(&od->tcp_pool, idx);
        return -1;
    }

    err = OTInstallNotifier(ref, od->tcp_notifier_upp, hot);
    if (err != kOTNoError) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTInstallNotifier(tcp[%d]) failed: %ld", idx, (long)err);
        OTCloseProvider(ref);
        hot->ref = kOTInvalidEndpointRef;
        hot->state = PT_EP_UNUSED;
        pt_endpoint_pool_free(&od->tcp_pool, idx);
        return -1;
    }

    /* TCP options (TCP_NODELAY, XTI_SNDBUF) are set AFTER bind via
     * pt_ot_tcp_set_options(). Per OT docs: "TCP options may be negotiated
     * in all endpoint states except T_UNBND and T_UNINIT." */

    /* Set async + non-blocking mode for all subsequent operations.
     * Async: completion events delivered via notifier.
     * Non-blocking: OTSnd returns kOTFlowErr immediately instead of
     * blocking. Critical for OTSnd loop to work correctly — if OTSnd
     * blocks waiting for buffer space, the STREAMS service procedure
     * can't drain the TCP send buffer, causing a deadlock. */
    err = OTSetAsynchronous(ref);
    if (err != kOTNoError) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "OTSetAsynchronous(tcp[%d]) failed: %ld (continuing)",
            idx, (long)err);
    }

    err = OTSetNonBlocking(ref);
    if (err != kOTNoError) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "OTSetNonBlocking(tcp[%d]) failed: %ld (continuing)",
            idx, (long)err);
    }

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
        "TCP[%d] endpoint created: ref=0x%08lX (pool %d/%d)",
        idx, (unsigned long)ref,
        (int)od->tcp_pool.count, (int)od->tcp_pool.capacity);

    return idx;
}

/* ========================================================================== */
/* TCP Bind                                                                   */
/* ========================================================================== */

/**
 * Bind TCP endpoint to a port.
 *
 * @param ctx   PeerTalk context
 * @param idx   Endpoint index from pt_ot_tcp_create
 * @param port  Port to bind (0 for system-assigned)
 * @param qlen  Listen queue length (0 for non-listening endpoints)
 * @return      0 on success, -1 on failure
 */
int pt_ot_tcp_bind(struct pt_context *ctx, int idx,
                    InetPort port, OTQLen qlen)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = pt_ot_get_tcp_hot(od, idx);
    pt_tcp_endpoint_cold *cold = pt_ot_get_tcp_cold(od, idx);
    TBind bind_req, bind_ret;
    OSStatus err;

    if (hot == NULL || cold == NULL)
        return -1;

    if (hot->state != PT_EP_UNBOUND) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] bind: wrong state %s (expected UNBOUND)",
            idx, pt_ep_state_name(hot->state));
        return -1;
    }

    /* Setup local address in cold data */
    OTInitInetAddress(&cold->local_addr, port, kOTAnyInetAddress);

    bind_req.addr.buf = (UInt8 *)&cold->local_addr;
    bind_req.addr.len = sizeof(InetAddress);
    bind_req.addr.maxlen = sizeof(InetAddress);
    bind_req.qlen = qlen;

    bind_ret.addr.buf = (UInt8 *)&cold->local_addr;
    bind_ret.addr.len = 0;
    bind_ret.addr.maxlen = sizeof(InetAddress);
    bind_ret.qlen = 0;

    err = OTBind(hot->ref, &bind_req, &bind_ret);
    if (err != kOTNoError) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTBind(tcp[%d], port=%u, qlen=%u) failed: %ld",
            idx, (unsigned)port, (unsigned)qlen, (long)err);
        return -1;
    }

    hot->state = PT_EP_IDLE;

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
        "TCP[%d] bound: port=%u qlen=%u",
        idx, (unsigned)cold->local_addr.fPort, (unsigned)qlen);

    return 0;
}

/* ========================================================================== */
/* TCP Options                                                                */
/* ========================================================================== */

/**
 * Set TCP performance options on a bound endpoint.
 *
 * Per OT docs: "TCP options may be negotiated in all endpoint states
 * except T_UNBND and T_UNINIT." Must be called AFTER bind.
 *
 * MUST be called in synchronous mode (caller ensures this).
 *
 * Sets:
 * - TCP_NODELAY: Disable Nagle's algorithm (don't batch small sends)
 * - XTI_SNDBUF: Increase TCP send buffer for more data in-flight
 *
 * @param ctx  PeerTalk context
 * @param idx  Endpoint index
 * @return     0 on success, -1 on failure (non-fatal, logged as warning)
 */
int pt_ot_tcp_set_options(struct pt_context *ctx, int idx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = pt_ot_get_tcp_hot(od, idx);
    UInt8 opt_buf[kOTFourByteOptionSize];
    TOption *opt = (TOption *)opt_buf;
    TOptMgmt req, ret;
    OSStatus opt_err;
    OTResult ep_state;

    if (hot == NULL)
        return -1;

    /* Log endpoint state for debugging */
    ep_state = OTGetEndpointState(hot->ref);
    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "TCP[%d] setting options (ep_state=%ld, kOTFourByteOptionSize=%lu)",
        idx, (long)ep_state, (unsigned long)kOTFourByteOptionSize);

    /* Disable Nagle (TCP_NODELAY=YES).
     * Testing shows Nagle has no effect on OT's TCP segment sizes
     * (~62 bytes regardless). With TCP_NODELAY, segments go to wire
     * immediately rather than waiting for ACKs, and app-level
     * coalescing (8KB threshold) handles batching. */
    opt->len = kOTFourByteOptionSize;
    opt->level = INET_TCP;
    opt->name = TCP_NODELAY;
    opt->status = 0;
    opt->value[0] = T_YES;

    req.opt.buf = opt_buf;
    req.opt.len = kOTFourByteOptionSize;
    req.opt.maxlen = kOTFourByteOptionSize;
    req.flags = T_NEGOTIATE;

    ret.opt.buf = opt_buf;
    ret.opt.len = 0;
    ret.opt.maxlen = sizeof(opt_buf);
    ret.flags = 0;

    opt_err = OTOptionManagement(hot->ref, &req, &ret);
    if (opt_err != kOTNoError) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] TCP_NODELAY failed: %ld (continuing)",
            idx, (long)opt_err);
    } else {
        PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] TCP_NODELAY=YES (status=%ld, ret_flags=%ld)",
            idx, (long)opt->status, (long)ret.flags);
    }

    /* Set XTI_SNDBUF individually */
    opt->len = kOTFourByteOptionSize;
    opt->level = XTI_GENERIC;
    opt->name = XTI_SNDBUF;
    opt->status = 0;
    opt->value[0] = 65536;

    req.opt.len = kOTFourByteOptionSize;
    ret.opt.len = 0;

    opt_err = OTOptionManagement(hot->ref, &req, &ret);
    if (opt_err != kOTNoError) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] XTI_SNDBUF failed: %ld (continuing)",
            idx, (long)opt_err);
    } else {
        PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] XTI_SNDBUF=%lu (status=%ld, negotiated=%lu)",
            idx, (unsigned long)65536, (long)opt->status,
            (unsigned long)opt->value[0]);
    }

    /* XTI_SNDLOWAT: Tested 512 (R27) - WORSE for small messages.
     * OT delays sending until 512 bytes accumulate, but 256B messages
     * don't fill that threshold. Leave at OT default. */

    /* IP_TOS: TOS constants not in Retro68 headers, skip for now. */

    /* TCP_MAXSEG: read-only in OT (confirmed: SET returns T_NOTSUPPORT).
     * OT uses MSS=536 (RFC non-local default). Read current value. */
    opt->len = kOTFourByteOptionSize;
    opt->level = INET_TCP;
    opt->name = TCP_MAXSEG;
    opt->status = 0;
    opt->value[0] = 0;

    req.opt.len = kOTFourByteOptionSize;
    req.flags = T_CURRENT;
    ret.opt.len = 0;

    opt_err = OTOptionManagement(hot->ref, &req, &ret);
    if (opt_err != kOTNoError) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] TCP_MAXSEG read failed: %ld", idx, (long)opt_err);
    } else {
        PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] TCP_MAXSEG=%lu (status=%ld) %s",
            idx, (unsigned long)opt->value[0], (long)opt->status,
            opt->value[0] <= 536 ? "*** MSS=536 IS THE BOTTLENECK ***" : "OK");
    }

    /* Read current XTI_SNDBUF and XTI_RCVBUF to confirm negotiated values */
    opt->len = kOTFourByteOptionSize;
    opt->level = XTI_GENERIC;
    opt->name = XTI_SNDBUF;
    opt->status = 0;
    opt->value[0] = 0;

    req.opt.len = kOTFourByteOptionSize;
    req.flags = T_CURRENT;
    ret.opt.len = 0;

    opt_err = OTOptionManagement(hot->ref, &req, &ret);
    if (opt_err == kOTNoError) {
        PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] Current XTI_SNDBUF=%lu",
            idx, (unsigned long)opt->value[0]);
    }

    /* Set XTI_RCVBUF=65536 to match SNDBUF. Default 16616 limits
     * TCP window and ACK generation. Larger recv buffer allows more
     * data in flight from peer, improving bidirectional throughput. */
    opt->len = kOTFourByteOptionSize;
    opt->level = XTI_GENERIC;
    opt->name = XTI_RCVBUF;
    opt->status = 0;
    opt->value[0] = 65536;

    req.opt.len = kOTFourByteOptionSize;
    req.flags = T_NEGOTIATE;
    ret.opt.len = 0;

    opt_err = OTOptionManagement(hot->ref, &req, &ret);
    if (opt_err != kOTNoError) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] XTI_RCVBUF set failed: %ld (continuing)",
            idx, (long)opt_err);
    } else {
        PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] XTI_RCVBUF=%lu (status=%ld, negotiated=%lu)",
            idx, (unsigned long)65536, (long)opt->status,
            (unsigned long)opt->value[0]);
    }

    return 0;
}

/* ========================================================================== */
/* TCP Send                                                                   */
/* ========================================================================== */

/**
 * Send data on a connected TCP endpoint.
 *
 * Loops OTSnd calls to drain as much data as possible per invocation.
 * OT's STREAMS TCP stack allocates minimum 64-byte mblk_t buffers
 * (confirmed: OT book line 41254), accepting only ~62 bytes per
 * non-blocking OTSnd call. This is a fundamental OT STREAMS limitation:
 *
 * Tested and confirmed ineffective (R20-R28):
 * - TCP_NODELAY on/off: no effect on segment size
 * - XTI_SNDBUF=65536: accepted but doesn't change mblk_t allocation
 * - XTI_SNDLOWAT=512: WORSE (delays small messages)
 * - Blocking synchronous OTSnd (R28): WORSE (stalls event loop,
 *   same 62-byte segments, 2-6x longer test phases)
 * - T_MORE flag: TCP ignores it (OT book line 15003)
 * - OT 1.1.2 → 1.3.1 upgrade: identical segments
 *
 * The non-blocking loop remains the best approach — it fills OT's
 * send buffer without stalling the event loop.
 *
 * @param ctx   PeerTalk context
 * @param idx   Endpoint index
 * @param data  Data to send
 * @param len   Length of data
 * @return      Bytes sent (>0), 0 on flow control, -1 on error
 */
#define PT_OT_SND_MAX_LOOPS 256

int pt_ot_tcp_send(struct pt_context *ctx, int idx,
                    const void *data, size_t len)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = pt_ot_get_tcp_hot(od, idx);
    const uint8_t *ptr = (const uint8_t *)data;
    size_t total_sent = 0;
    OTResult result;
    int loops = 0;

    if (hot == NULL)
        return -1;

    if (hot->state != PT_EP_DATAXFER) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
            "TCP[%d] send: wrong state %s",
            idx, pt_ep_state_name(hot->state));
        return -1;
    }

    /* Loop OTSnd to drain buffer aggressively.
     * OT accepts only ~62 bytes per call (64-byte mblk_t minimum).
     * Loop until all data queued, flow control hit, or max iterations. */
    while (total_sent < len && loops < PT_OT_SND_MAX_LOOPS) {
        result = OTSnd(hot->ref, (void *)ptr,
                       (OTByteCount)(len - total_sent), 0);

        if (result == kOTFlowErr || result == kOTLookErr) {
            /* Flow control or async event - return what we have */
            break;
        }

        if (result < 0) {
            if (total_sent > 0)
                return (int)total_sent;  /* Return partial success */
            PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
                "TCP[%d] OTSnd failed: %ld", idx, (long)result);
            return -1;
        }

        ptr += result;
        total_sent += (size_t)result;
        loops++;
    }

    return (int)total_sent;
}

/* ========================================================================== */
/* TCP Receive                                                                */
/* ========================================================================== */

/**
 * Receive data from a connected TCP endpoint.
 *
 * Non-blocking. Returns data if available, 0 if no data.
 * Called unconditionally each poll cycle; returns 0 cheaply if no data.
 *
 * @param ctx   PeerTalk context
 * @param idx   Endpoint index
 * @param data  Buffer to receive into
 * @param len   Buffer size
 * @param flags [out] OT flags from OTRcv
 * @return      Bytes received (>0), 0 if no data, -1 on error
 */
int pt_ot_tcp_recv(struct pt_context *ctx, int idx,
                    void *data, size_t len, OTFlags *out_flags)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = pt_ot_get_tcp_hot(od, idx);
    OTFlags flags = 0;
    OTResult result;

    if (hot == NULL)
        return -1;

    if (hot->state != PT_EP_DATAXFER) {
        return 0;
    }

    result = OTRcv(hot->ref, data, (OTByteCount)len, &flags);

    if (out_flags != NULL)
        *out_flags = flags;

    if (result == kOTNoDataErr) {
        PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_DATA_AVAILABLE);
        return 0;
    }

    if (result == kOTLookErr) {
        /* Async event pending */
        return 0;
    }

    if (result < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
            "TCP[%d] OTRcv failed: %ld", idx, (long)result);
        return -1;
    }

    return (int)result;
}

/* ========================================================================== */
/* TCP Close                                                                  */
/* ========================================================================== */

/**
 * Initiate orderly close of a TCP endpoint.
 *
 * For connected endpoints: sends orderly disconnect, sets state to CLOSING,
 * and records close_start for timeout monitoring.
 *
 * For non-connected endpoints: goes directly to cleanup.
 *
 * Main poll loop should call pt_ot_tcp_check_close_timeout() to
 * force-close endpoints that don't complete within 30 seconds.
 *
 * @param ctx  PeerTalk context
 * @param idx  Endpoint index
 */
void pt_ot_tcp_close(struct pt_context *ctx, int idx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = pt_ot_get_tcp_hot(od, idx);
    OSStatus err;

    if (hot == NULL)
        return;

    if (hot->state == PT_EP_UNUSED)
        return;

    /* For connected endpoints, try orderly disconnect */
    if (hot->state == PT_EP_DATAXFER) {
        err = OTSndOrderlyDisconnect(hot->ref);

        if (err == kOTNoError) {
            hot->state = PT_EP_CLOSING;
            hot->close_start = (unsigned long)TickCount();
            PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
                "TCP[%d] orderly disconnect initiated", idx);
            return;
        }

        if (err == kOTFlowErr) {
            /* Queue full - set closing state, will complete later */
            hot->state = PT_EP_CLOSING;
            hot->close_start = (unsigned long)TickCount();
            PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
                "TCP[%d] close deferred (flow control)", idx);
            return;
        }

        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] OTSndOrderlyDisconnect failed: %ld (forcing cleanup)",
            idx, (long)err);
    }

    /* Non-connected or failed close: go directly to cleanup */
    pt_ot_tcp_cleanup(ctx, idx);
}

/**
 * Final cleanup of a TCP endpoint.
 *
 * Unbinds if bound, closes provider, clears all state, returns
 * slot to pool. Called after orderly disconnect completes or times out.
 *
 * @param ctx  PeerTalk context
 * @param idx  Endpoint index
 */
void pt_ot_tcp_cleanup(struct pt_context *ctx, int idx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = pt_ot_get_tcp_hot(od, idx);

    if (hot == NULL)
        return;

    if (hot->ref != kOTInvalidEndpointRef) {
        /* Try orderly unbind if endpoint is bound */
        if (hot->state >= PT_EP_IDLE) {
            OTResult ep_state = OTGetEndpointState(hot->ref);
            if (ep_state == T_IDLE)
                OTUnbind(hot->ref);
        }

        OTCloseProvider(hot->ref);
        hot->ref = kOTInvalidEndpointRef;
    }

    /* Clear all state */
    hot->peer = NULL;
    hot->state = PT_EP_UNUSED;
    hot->async_result = 0;
    hot->close_start = 0;
    hot->log_error_code = 0;
    hot->log_events = 0;
    PT_FLAGS_CLEAR_ALL(hot->flags);

    /* Return slot to pool */
    pt_endpoint_pool_free(&od->tcp_pool, idx);

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
        "TCP[%d] endpoint cleaned up (pool %d/%d)",
        idx, (int)od->tcp_pool.count, (int)od->tcp_pool.capacity);
}

/**
 * Check for close timeout and force cleanup.
 *
 * Called from main poll loop for endpoints in PT_EP_CLOSING state.
 * If more than 30 seconds have elapsed since close was initiated,
 * force an abortive disconnect and cleanup.
 *
 * @param ctx  PeerTalk context
 * @param idx  Endpoint index
 * @return     1 if timeout forced cleanup, 0 if still waiting
 */
int pt_ot_tcp_check_close_timeout(struct pt_context *ctx, int idx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = pt_ot_get_tcp_hot(od, idx);
    unsigned long now;
    unsigned long elapsed;

    if (hot == NULL || hot->state != PT_EP_CLOSING)
        return 0;

    now = (unsigned long)TickCount();
    elapsed = now - hot->close_start;

    if (elapsed < PT_OT_CLOSE_TIMEOUT_TICKS)
        return 0;

    PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
        "TCP[%d] close timeout (%lu ticks) - forcing disconnect",
        idx, elapsed);

    /* Force abortive disconnect */
    if (hot->ref != kOTInvalidEndpointRef) {
        OTSndDisconnect(hot->ref, NULL);
    }

    pt_ot_tcp_cleanup(ctx, idx);
    return 1;
}

#endif /* PT_PLATFORM_OT */
