/**
 * @file ot_adsp.c
 * @brief Open Transport ADSP Endpoint Implementation
 *
 * ADSP endpoint creation, binding, connecting, sending, close, and cleanup.
 * Uses hot/cold struct split and O(1) bitmap pool allocation.
 *
 * Key insight: OT provides a unified API, so ADSP uses the same functions
 * as TCP (OTOpenEndpoint, OTBind, OTConnect, OTSnd, OTRcv, etc.) with
 * different configuration strings and address types.
 *
 * Differences from TCP:
 * 1. Configuration string: "adsp(EnableEOM=1)" instead of "tcp"
 * 2. Address type: DDPAddress instead of InetAddress
 * 3. EOM flag: T_MORE for partial sends, 0 for complete messages
 * 4. Connect by NBP name: OTConnect with NBPAddress
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 9: AppleTalk Services
 * - OpenTransport.h, OpenTransportProviders.h (Retro68)
 */

#include "ot_multi.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_OT)

#include <OSUtils.h>  /* TickCount() */

/* Close timeout: 30 seconds in ticks (~1800 at 60Hz) */
#define PT_OT_ADSP_CLOSE_TIMEOUT_TICKS  1800

/* Forward declaration - cleanup is called from close */
void pt_ot_adsp_cleanup(struct pt_context *ctx, int idx);

/* ========================================================================== */
/* ADSP Notifier Callback                                                     */
/* ========================================================================== */

/**
 * ADSP Asynchronous Notification Routine.
 *
 * Same events as TCP - OT's transport independence!
 *
 * CRITICAL: Called at deferred task time.
 * From Networking With Open Transport:
 * - MUST NOT move or purge memory
 * - MUST NOT make synchronous OT calls
 * - May be called reentrantly (use atomic operations)
 *
 * Strategy: Set flags and store result codes. Main loop processes.
 *
 * Uses PT_OT_FLAG_* constants and PT_FLAG_SET() macro for reentrancy safety.
 * Per NetworkingOpenTransport.txt Ch.3 p.75: "Open Transport might call a
 * notification routine reentrantly."
 *
 * @param context  Pointer to pt_adsp_endpoint_hot struct
 * @param code     OT event code
 * @param result   Result code for completion events
 * @param cookie   Event-specific data
 */
pascal void pt_ot_adsp_notifier(void *context, OTEventCode code,
                                  OTResult result, void *cookie)
{
    pt_adsp_endpoint_hot *hot = (pt_adsp_endpoint_hot *)context;

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
 * Process deferred log events from ADSP notifier.
 *
 * Same pattern as TCP: notifier sets bits, main loop logs and clears.
 *
 * @param ctx  PeerTalk context
 * @param hot  ADSP endpoint hot data
 * @param idx  Endpoint index (for log messages)
 */
void pt_ot_adsp_process_log_events(struct pt_context *ctx,
                                     pt_adsp_endpoint_hot *hot,
                                     int idx)
{
    uint8_t events = hot->log_events;
    if (events == 0)
        return;

    hot->log_events = 0;  /* Clear all pending log events */

    if (events & PT_OT_LOG_EVT_CONNECT_DONE) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
            "ADSP[%d] connect complete: result=%ld",
            idx, (long)hot->async_result);
    }

    if (events & PT_OT_LOG_EVT_LISTEN_DONE) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
            "ADSP[%d] listen event", idx);
    }

    if (events & PT_OT_LOG_EVT_ACCEPT_DONE) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
            "ADSP[%d] accept complete: result=%ld",
            idx, (long)hot->async_result);
    }

    if (events & PT_OT_LOG_EVT_CLOSE_DONE) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
            "ADSP[%d] orderly release received", idx);
    }

    if (events & PT_OT_LOG_EVT_ERROR) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "ADSP[%d] error: %ld",
            idx, (long)hot->log_error_code);
    }

    if (events & PT_OT_LOG_EVT_DATA_IN) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
            "ADSP[%d] data available", idx);
    }

    if (events & PT_OT_LOG_EVT_BIND_DONE) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
            "ADSP[%d] bind complete: result=%ld",
            idx, (long)hot->async_result);
    }
}

/* ========================================================================== */
/* ADSP Endpoint Creation                                                     */
/* ========================================================================== */

/**
 * Create an ADSP endpoint from the pool.
 *
 * Allocates a slot using O(1) bitmap, opens endpoint with cloned ADSP
 * configuration ("adsp(EnableEOM=1)"), installs notifier, and sets
 * async mode.
 *
 * @param ctx  PeerTalk context
 * @return     Endpoint index (0..PT_MAX_PEERS-1) on success, -1 on failure
 */
int pt_ot_adsp_create(struct pt_context *ctx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    pt_adsp_endpoint_hot *hot;
    pt_adsp_endpoint_cold *cold;
    OTConfigurationRef config;
    EndpointRef ref;
    OSStatus err;
    int idx;

    /* Allocate slot from bitmap pool (O(1)) */
    idx = pt_endpoint_pool_alloc(&md->adsp_pool);
    if (idx < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "ADSP endpoint pool exhausted (count=%d/%d)",
            (int)md->adsp_pool.count, (int)md->adsp_pool.capacity);
        return -1;
    }

    hot = pt_ot_get_adsp_hot(md, idx);
    cold = pt_ot_get_adsp_cold(md, idx);
    if (hot == NULL || cold == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "ADSP endpoint data NULL for idx=%d", idx);
        pt_endpoint_pool_free(&md->adsp_pool, idx);
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

    pt_memset(cold, 0, sizeof(pt_adsp_endpoint_cold));

    /* Clone master ADSP configuration */
    config = OTCloneConfiguration(md->adsp_config);
    if (config == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "OTCloneConfiguration(adsp) failed");
        pt_endpoint_pool_free(&md->adsp_pool, idx);
        return -1;
    }

    /* Open endpoint synchronously */
    ref = OTOpenEndpoint(config, 0, NULL, &err);
    /* config is now disposed by OT */

    if (err != kOTNoError || ref == kOTInvalidEndpointRef) {
        if (err == kENOMEMErr) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_MEMORY,
                "OT out of memory creating ADSP endpoint (kOTENOMEMErr)");
        }
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTOpenEndpoint(adsp[%d]) failed: %ld", idx, (long)err);
        pt_endpoint_pool_free(&md->adsp_pool, idx);
        return -1;
    }

    hot->ref = ref;
    hot->state = PT_EP_UNBOUND;

    /* Install notifier using pre-created UPP */
    if (md->adsp_notifier_upp == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT,
            "ADSP notifier UPP not created");
        OTCloseProvider(ref);
        hot->ref = kOTInvalidEndpointRef;
        hot->state = PT_EP_UNUSED;
        pt_endpoint_pool_free(&md->adsp_pool, idx);
        return -1;
    }

    err = OTInstallNotifier(ref, md->adsp_notifier_upp, hot);
    if (err != kOTNoError) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTInstallNotifier(adsp[%d]) failed: %ld", idx, (long)err);
        OTCloseProvider(ref);
        hot->ref = kOTInvalidEndpointRef;
        hot->state = PT_EP_UNUSED;
        pt_endpoint_pool_free(&md->adsp_pool, idx);
        return -1;
    }

    /* Set async mode for all subsequent operations */
    err = OTSetAsynchronous(ref);
    if (err != kOTNoError) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "OTSetAsynchronous(adsp[%d]) failed: %ld (continuing)",
            idx, (long)err);
    }

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
        "ADSP[%d] endpoint created: ref=0x%08lX (pool %d/%d)",
        idx, (unsigned long)ref,
        (int)md->adsp_pool.count, (int)md->adsp_pool.capacity);

    return idx;
}

/* ========================================================================== */
/* ADSP Bind                                                                  */
/* ========================================================================== */

/**
 * Bind ADSP endpoint to a DDP socket.
 *
 * Unlike TCP ports, ADSP uses DDP sockets (0-255).
 * Socket 0 means "let ADSP assign one".
 *
 * @param ctx     PeerTalk context
 * @param idx     Endpoint index from pt_ot_adsp_create
 * @param socket  DDP socket to bind (0 for auto-assign)
 * @param qlen    Listen queue length (0 for non-listening endpoints)
 * @return        0 on success, -1 on failure
 */
int pt_ot_adsp_bind(struct pt_context *ctx, int idx,
                      uint8_t socket, int qlen)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    pt_adsp_endpoint_hot *hot = pt_ot_get_adsp_hot(md, idx);
    pt_adsp_endpoint_cold *cold = pt_ot_get_adsp_cold(md, idx);
    TBind bind_req, bind_ret;
    OSStatus err;

    if (hot == NULL || cold == NULL)
        return -1;

    if (hot->state != PT_EP_UNBOUND) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "ADSP[%d] bind: wrong state %s (expected UNBOUND)",
            idx, pt_ep_state_name(hot->state));
        return -1;
    }

    /* Setup DDP address in cold data.
     * DDPAddress fields:
     *   fAddressType = AF_ATALK_DDP
     *   fNetwork     = 0 (filled by AARP)
     *   fNodeID      = 0 (filled by AARP)
     *   fSocket      = socket (0 = auto-assign)
     *   fDDPType     = 7 (ADSP = DDP protocol type 7)
     */
    cold->local_addr.fAddressType = AF_ATALK_DDP;
    cold->local_addr.fNetwork = 0;
    cold->local_addr.fNodeID = 0;
    cold->local_addr.fSocket = socket;
    cold->local_addr.fDDPType = 7;  /* ADSP */
    cold->local_addr.fPad = 0;

    bind_req.addr.buf = (UInt8 *)&cold->local_addr;
    bind_req.addr.len = sizeof(DDPAddress);
    bind_req.addr.maxlen = sizeof(DDPAddress);
    bind_req.qlen = qlen;

    bind_ret.addr.buf = (UInt8 *)&cold->local_addr;
    bind_ret.addr.len = 0;
    bind_ret.addr.maxlen = sizeof(DDPAddress);
    bind_ret.qlen = 0;

    err = OTBind(hot->ref, &bind_req, &bind_ret);
    if (err != kOTNoError) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "OTBind(adsp[%d], socket=%u, qlen=%d) failed: %ld",
            idx, (unsigned)socket, qlen, (long)err);
        return -1;
    }

    hot->state = PT_EP_IDLE;

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "ADSP[%d] bound: net=%u node=%u socket=%u qlen=%d",
        idx,
        (unsigned)cold->local_addr.fNetwork,
        (unsigned)cold->local_addr.fNodeID,
        (unsigned)cold->local_addr.fSocket,
        qlen);

    return 0;
}

/* ========================================================================== */
/* ADSP Connect by NBP Name                                                   */
/* ========================================================================== */

/**
 * Connect ADSP endpoint to a peer by NBP name.
 *
 * OT supports connecting directly by NBP name - it resolves automatically.
 * The endpoint must be unbound or idle (auto-binds if unbound).
 *
 * @param ctx   PeerTalk context
 * @param idx   Endpoint index (or -1 to auto-create)
 * @param name  NBP object name (e.g., "Alice's Mac")
 * @param type  NBP type (e.g., "PeerTalk")
 * @param zone  NBP zone (NULL or "*" for all zones)
 * @return      Endpoint index on success, -1 on failure
 */
int pt_ot_adsp_connect_by_name(struct pt_context *ctx,
                                  int idx,
                                  const char *name,
                                  const char *type,
                                  const char *zone)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    pt_adsp_endpoint_hot *hot;
    TCall call;
    NBPAddress nbp_addr;
    char entity_str[128];
    OTByteCount addr_len;
    OSStatus err;

    /* Auto-create endpoint if idx == -1 */
    if (idx < 0) {
        idx = pt_ot_adsp_create(ctx);
        if (idx < 0)
            return -1;
    }

    hot = pt_ot_get_adsp_hot(md, idx);
    if (hot == NULL)
        return -1;

    if (hot->state != PT_EP_UNBOUND && hot->state != PT_EP_IDLE) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "ADSP[%d] connect: wrong state %s",
            idx, pt_ep_state_name(hot->state));
        return -1;
    }

    /* Auto-bind if unbound */
    if (hot->state == PT_EP_UNBOUND) {
        if (pt_ot_adsp_bind(ctx, idx, 0, 0) != 0)
            return -1;
    }

    /* Build NBP entity string: "name:type@zone" */
    pt_snprintf(entity_str, sizeof(entity_str), "%s:%s@%s",
                name, type, zone ? zone : "*");

    /* Setup NBP address for OTConnect */
    nbp_addr.fAddressType = AF_ATALK_NBP;
    addr_len = OTSetAddressFromNBPString(nbp_addr.fNBPNameBuffer,
                                           entity_str, -1);

    /* Setup call structure */
    pt_memset(&call, 0, sizeof(TCall));
    call.addr.buf = (UInt8 *)&nbp_addr;
    call.addr.len = sizeof(OTAddressType) + addr_len;
    call.addr.maxlen = sizeof(NBPAddress);
    call.opt.buf = NULL;
    call.opt.len = 0;
    call.udata.buf = NULL;
    call.udata.len = 0;
    call.sequence = 0;

    hot->state = PT_EP_OUTGOING;

    err = OTConnect(hot->ref, &call, NULL);

    /* kOTNoDataErr means connect is in progress (async mode) */
    if (err != kOTNoError && err != kOTNoDataErr) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "ADSP[%d] OTConnect to '%s' failed: %ld",
            idx, entity_str, (long)err);
        hot->state = PT_EP_IDLE;
        return -1;
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "ADSP[%d] connecting to '%s'...", idx, entity_str);

    return idx;
}

/* ========================================================================== */
/* ADSP Send                                                                  */
/* ========================================================================== */

/**
 * Send data on a connected ADSP endpoint.
 *
 * ADSP's EOM flag provides message framing - much simpler than TCP!
 * When eom is true, sends with no flags (complete message).
 * When eom is false, sends with T_MORE (partial message).
 *
 * OTSnd returns OTResult (byte count on success), NOT OSStatus.
 *
 * @param ctx   PeerTalk context
 * @param idx   Endpoint index
 * @param data  Data to send
 * @param len   Length of data
 * @param eom   End-of-message flag
 * @return      Bytes sent (>0), 0 on flow control, -1 on error
 */
int pt_ot_adsp_send(struct pt_context *ctx, int idx,
                      const void *data, uint16_t len, Boolean eom)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    pt_adsp_endpoint_hot *hot = pt_ot_get_adsp_hot(md, idx);
    OTFlags flags;
    OTResult result;

    if (hot == NULL)
        return -1;

    if (hot->state != PT_EP_DATAXFER) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
            "ADSP[%d] send: wrong state %s",
            idx, pt_ep_state_name(hot->state));
        return -1;
    }

    /* T_MORE = more data coming (partial message)
     * 0 = end of message (complete) */
    flags = eom ? 0 : T_MORE;

    result = OTSnd(hot->ref, (void *)data, (OTByteCount)len, flags);

    if (result == kOTFlowErr) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_SEND,
            "ADSP[%d] send flow control", idx);
        return 0;  /* Would block - wait for T_GODATA */
    }

    if (result == kOTLookErr) {
        /* Async event pending - caller should check flags */
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_SEND,
            "ADSP[%d] send: look error (async event pending)", idx);
        return 0;
    }

    if (result < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_SEND,
            "ADSP[%d] OTSnd failed: %ld", idx, (long)result);
        return -1;
    }

    return (int)result;  /* Returns byte count sent */
}

/* ========================================================================== */
/* ADSP Receive                                                               */
/* ========================================================================== */

/**
 * Receive data from a connected ADSP endpoint.
 *
 * Non-blocking. Returns data if available, 0 if no data.
 * Sets *eom to true if the received data completes a message.
 *
 * OTRcv returns OTResult (byte count on success), NOT OSStatus.
 *
 * @param ctx      PeerTalk context
 * @param idx      Endpoint index
 * @param buffer   Buffer to receive into
 * @param max_len  Buffer size
 * @param eom      [out] Set to true if end-of-message received
 * @return         Bytes received (>0), 0 if no data, -1 on error
 */
int pt_ot_adsp_recv(struct pt_context *ctx, int idx,
                      void *buffer, uint16_t max_len, Boolean *eom)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    pt_adsp_endpoint_hot *hot = pt_ot_get_adsp_hot(md, idx);
    OTFlags flags = 0;
    OTResult result;

    if (hot == NULL)
        return -1;

    if (hot->state != PT_EP_DATAXFER)
        return 0;

    result = OTRcv(hot->ref, buffer, (OTByteCount)max_len, &flags);

    if (result == kOTNoDataErr) {
        /* No more data - clear the flag */
        PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_DATA_AVAILABLE);
        return 0;
    }

    if (result == kOTLookErr) {
        /* Async event pending (T_DISCONNECT, T_ORDREL) */
        PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_DATA_AVAILABLE);
        return 0;
    }

    if (result < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_RECV,
            "ADSP[%d] OTRcv failed: %ld", idx, (long)result);
        return -1;
    }

    if (eom != NULL) {
        /* EOM: no T_MORE flag means complete message */
        *eom = !(flags & T_MORE);
    }

    /* More data may be available - don't clear flag yet */
    return (int)result;  /* Returns byte count received */
}

/* ========================================================================== */
/* ADSP Close                                                                 */
/* ========================================================================== */

/**
 * Initiate orderly close of an ADSP endpoint.
 *
 * Same pattern as TCP close:
 * - For connected endpoints: sends orderly disconnect, sets CLOSING state
 * - For non-connected: goes directly to cleanup
 * - Main poll loop checks for close timeout (30s)
 *
 * @param ctx  PeerTalk context
 * @param idx  Endpoint index
 */
void pt_ot_adsp_close(struct pt_context *ctx, int idx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    pt_adsp_endpoint_hot *hot = pt_ot_get_adsp_hot(md, idx);
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
                "ADSP[%d] orderly disconnect initiated", idx);
            return;
        }

        if (err == kOTFlowErr) {
            hot->state = PT_EP_CLOSING;
            hot->close_start = (unsigned long)TickCount();
            PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
                "ADSP[%d] close deferred (flow control)", idx);
            return;
        }

        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "ADSP[%d] OTSndOrderlyDisconnect failed: %ld (forcing cleanup)",
            idx, (long)err);
    }

    /* Non-connected or failed close: go directly to cleanup */
    pt_ot_adsp_cleanup(ctx, idx);
}

/**
 * Final cleanup of an ADSP endpoint.
 *
 * Unbinds if bound, closes provider, clears all state, returns
 * slot to pool. Called after orderly disconnect completes or times out.
 *
 * @param ctx  PeerTalk context
 * @param idx  Endpoint index
 */
void pt_ot_adsp_cleanup(struct pt_context *ctx, int idx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    pt_adsp_endpoint_hot *hot = pt_ot_get_adsp_hot(md, idx);

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
    pt_endpoint_pool_free(&md->adsp_pool, idx);

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
        "ADSP[%d] endpoint cleaned up (pool %d/%d)",
        idx, (int)md->adsp_pool.count, (int)md->adsp_pool.capacity);
}

/**
 * Check for ADSP close timeout and force cleanup.
 *
 * Called from main poll loop for endpoints in PT_EP_CLOSING state.
 * If more than 30 seconds have elapsed since close was initiated,
 * force an abortive disconnect and cleanup.
 *
 * @param ctx  PeerTalk context
 * @param idx  Endpoint index
 * @return     1 if timeout forced cleanup, 0 if still waiting
 */
int pt_ot_adsp_check_close_timeout(struct pt_context *ctx, int idx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    pt_adsp_endpoint_hot *hot = pt_ot_get_adsp_hot(md, idx);
    unsigned long now;
    unsigned long elapsed;

    if (hot == NULL || hot->state != PT_EP_CLOSING)
        return 0;

    now = (unsigned long)TickCount();
    elapsed = now - hot->close_start;

    if (elapsed < PT_OT_ADSP_CLOSE_TIMEOUT_TICKS)
        return 0;

    PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
        "ADSP[%d] close timeout (%lu ticks) - forcing disconnect",
        idx, elapsed);

    /* Force abortive disconnect */
    if (hot->ref != kOTInvalidEndpointRef) {
        OTSndDisconnect(hot->ref, NULL);
    }

    pt_ot_adsp_cleanup(ctx, idx);
    return 1;
}

#endif /* PT_PLATFORM_OT */
