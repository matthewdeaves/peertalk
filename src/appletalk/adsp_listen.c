/**
 * @file adsp_listen.c
 * @brief ADSP Connection Listener
 *
 * Implements the connection listener pattern for ADSP:
 * 1. Initialize connection listener (dspCLInit)
 * 2. Listen for incoming (dspCLListen) - async
 * 3. On connection request: accept (dspOpen+ocAccept) or deny (dspCLDeny)
 * 4. Re-arm listener for next connection
 *
 * Note: dspCLInit does NOT require send/recv queues, attention buffer,
 * or userRoutine. Only ccbPtr and localSocket are used.
 *
 * References:
 * - Programming With AppleTalk (1996), Chapter 5: ADSP
 */

#include "at_defs.h"

#if defined(PT_PLATFORM_APPLETALK)

#include <Devices.h>
#include <MacMemory.h>
#include <string.h>

/* ========================================================================== */
/* Logging Macros                                                              */
/* ========================================================================== */

#define LISTEN_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define LISTEN_LOG_INFO(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_INFO((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define LISTEN_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/* ========================================================================== */
/* Initialize Connection Listener (dspCLInit)                                  */
/* ========================================================================== */

/**
 * Initialize the connection listener.
 *
 * dspCLInit only needs ccbPtr and localSocket - no send/recv queues,
 * no attention buffer, no userRoutine.
 *
 * @param ctx     AppleTalk context
 * @param socket  Socket to listen on (0 = auto-assign)
 * @return        noErr on success, error code on failure
 */
int pt_adsp_listener_init(pt_at_context *ctx, short socket)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_listener_hot *listener_hot;
    pt_adsp_listener_cold *listener_cold;

    if (!ctx || !ctx->cold) return -1;

    LISTEN_LOG_DEBUG(ctx, "Init listener (socket=%d)", (int)socket);

    listener_hot = &ctx->listener;
    listener_cold = PT_AT_LISTENER_COLD(ctx);

    /* Clear hot struct */
    memset(listener_hot, 0, sizeof(pt_adsp_listener_hot));

    /* Clear cold struct (preserve hot pointer) */
    {
        pt_adsp_listener_hot *hot_ptr = listener_cold->hot;
        memset(listener_cold, 0, sizeof(pt_adsp_listener_cold));
        listener_cold->hot = hot_ptr;
    }

    /* Set up extended param block context */
    listener_cold->epb.context = listener_cold;
    pb = &listener_cold->epb.pb;

    /* dspCLInit - connection listener init */
    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspCLInit;
    pb->u.initParams.ccbPtr = (TPCCB)&listener_cold->ccb;
    pb->u.initParams.localSocket = socket;

    err = PBControlSync((ParmBlkPtr)pb);
    if (err != noErr) {
        LISTEN_LOG_ERR(ctx, "dspCLInit failed: %d", (int)err);
        return err;
    }

    listener_cold->ccb_refnum = pb->ccbRefNum;
    listener_hot->state = PT_ADSP_IDLE;

    LISTEN_LOG_INFO(ctx, "Listener init: refnum=%d socket=%d",
                    (int)listener_cold->ccb_refnum,
                    (int)pb->u.initParams.localSocket);
    return noErr;
}

/* ========================================================================== */
/* Start Listening (dspCLListen) - Async                                       */
/* ========================================================================== */

/**
 * Start async listen for incoming connections.
 *
 * @param ctx  AppleTalk context
 * @return     noErr on success, error code on failure
 */
int pt_adsp_listener_listen(pt_at_context *ctx)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_listener_hot *listener_hot;
    pt_adsp_listener_cold *listener_cold;

    if (!ctx || !ctx->cold) return -1;

    listener_hot = &ctx->listener;
    listener_cold = PT_AT_LISTENER_COLD(ctx);

    if (listener_hot->state != PT_ADSP_IDLE) {
        LISTEN_LOG_ERR(ctx, "Cannot listen: state %d",
                       (int)listener_hot->state);
        return -1;
    }

    LISTEN_LOG_DEBUG(ctx, "Starting async listen");

    pb = &listener_cold->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspCLListen;
    pb->ccbRefNum = listener_cold->ccb_refnum;
    pb->ioCompletion = ctx->listener_completion_upp;

    listener_hot->connection_pending = false;
    listener_hot->flags = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        LISTEN_LOG_ERR(ctx, "dspCLListen failed: %d", (int)err);
        return err;
    }

    listener_hot->state = PT_ADSP_LISTENING;
    LISTEN_LOG_INFO(ctx, "Listening for connections");
    return noErr;
}

/* ========================================================================== */
/* Accept Pending Connection                                                   */
/* ========================================================================== */

/**
 * Accept a pending incoming connection.
 *
 * Creates a new CCB for the connection via pt_adsp_init_ccb(),
 * then uses dspOpen with ocAccept to accept. The listener remains
 * valid for re-arming.
 *
 * @param ctx   AppleTalk context
 * @param conn  Hot connection struct (from pt_adsp_alloc)
 * @return      noErr on success, error code on failure
 */
int pt_adsp_listener_accept(pt_at_context *ctx,
                            pt_adsp_connection_hot *conn)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_listener_hot *listener_hot;
    pt_adsp_listener_cold *listener_cold;
    pt_adsp_connection_cold *conn_cold;

    if (!ctx || !conn || !ctx->cold) return -1;

    listener_hot = &ctx->listener;
    listener_cold = PT_AT_LISTENER_COLD(ctx);

    if (!listener_hot->connection_pending) {
        LISTEN_LOG_ERR(ctx, "No connection pending");
        return -1;
    }

    LISTEN_LOG_INFO(ctx, "Accepting from %d.%d:%d",
                    (int)listener_hot->remote_addr.aNet,
                    (int)listener_hot->remote_addr.aNode,
                    (int)listener_hot->remote_addr.aSocket);

    /* Initialize the accepting connection's CCB */
    err = pt_adsp_init_ccb(ctx, conn, 0);
    if (err != noErr) {
        LISTEN_LOG_ERR(ctx, "CCB init for accept failed: %d", (int)err);
        return err;
    }

    conn_cold = PT_AT_CONN_COLD(ctx, conn);
    pb = &conn_cold->epb.pb;

    /* Accept with dspOpen + ocAccept.
     * Must copy sync fields from listener cold to accept PB. */
    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspOpen;
    pb->ccbRefNum = conn_cold->ccb_refnum;
    pb->u.openParams.ocMode = ocAccept;
    pb->u.openParams.remoteCID = listener_cold->remote_cid;
    pb->u.openParams.remoteAddress = listener_hot->remote_addr;
    pb->u.openParams.sendSeq = listener_cold->send_seq;
    pb->u.openParams.sendWindow = listener_cold->send_window;
    pb->u.openParams.attnSendSeq = listener_cold->attn_send_seq;
    pb->ioCompletion = ctx->completion_upp;

    conn->flags = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        LISTEN_LOG_ERR(ctx, "dspOpen (ocAccept) failed: %d", (int)err);
        pt_adsp_remove_ccb(ctx, conn);
        return err;
    }

    conn->remote_addr = listener_hot->remote_addr;
    conn->state = PT_ADSP_CONNECTING;

    /* Clear pending - connection handed off */
    listener_hot->connection_pending = false;
    listener_hot->state = PT_ADSP_IDLE;

    LISTEN_LOG_DEBUG(ctx, "Accept initiated, awaiting completion");
    return noErr;
}

/* ========================================================================== */
/* Deny Pending Connection                                                     */
/* ========================================================================== */

/**
 * Deny a pending incoming connection.
 *
 * @param ctx  AppleTalk context
 * @return     noErr on success, error code on failure
 */
int pt_adsp_listener_deny(pt_at_context *ctx)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_listener_hot *listener_hot;
    pt_adsp_listener_cold *listener_cold;

    if (!ctx || !ctx->cold) return -1;

    listener_hot = &ctx->listener;
    listener_cold = PT_AT_LISTENER_COLD(ctx);

    if (!listener_hot->connection_pending) return noErr;

    LISTEN_LOG_INFO(ctx, "Denying from %d.%d:%d",
                    (int)listener_hot->remote_addr.aNet,
                    (int)listener_hot->remote_addr.aNode,
                    (int)listener_hot->remote_addr.aSocket);

    pb = &listener_cold->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspCLDeny;
    pb->ccbRefNum = listener_cold->ccb_refnum;
    pb->u.openParams.remoteCID = listener_cold->remote_cid;
    pb->u.openParams.remoteAddress = listener_hot->remote_addr;

    err = PBControlSync((ParmBlkPtr)pb);
    if (err != noErr) {
        LISTEN_LOG_ERR(ctx, "dspCLDeny failed: %d", (int)err);
    }

    listener_hot->connection_pending = false;
    listener_hot->state = PT_ADSP_IDLE;

    return err;
}

/* ========================================================================== */
/* Remove Listener (dspCLRemove)                                               */
/* ========================================================================== */

/**
 * Remove the connection listener.
 *
 * @param ctx  AppleTalk context
 * @return     noErr on success, error code on failure
 */
int pt_adsp_listener_remove(pt_at_context *ctx)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_listener_hot *listener_hot;
    pt_adsp_listener_cold *listener_cold;

    if (!ctx || !ctx->cold) return -1;

    listener_hot = &ctx->listener;
    listener_cold = PT_AT_LISTENER_COLD(ctx);

    if (listener_hot->state == PT_ADSP_UNUSED) return noErr;

    LISTEN_LOG_DEBUG(ctx, "Removing listener");

    pb = &listener_cold->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspCLRemove;
    pb->ccbRefNum = listener_cold->ccb_refnum;
    pb->u.closeParams.abort = 1;

    err = PBControlSync((ParmBlkPtr)pb);
    if (err != noErr) {
        LISTEN_LOG_ERR(ctx, "dspCLRemove failed: %d", (int)err);
    } else {
        LISTEN_LOG_INFO(ctx, "Listener removed");
    }

    listener_hot->state = PT_ADSP_UNUSED;
    return err;
}

#endif /* PT_PLATFORM_APPLETALK */
