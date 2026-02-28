/**
 * @file adsp_connect.c
 * @brief ADSP Active Connection
 *
 * Initiates outgoing connections to remote ADSP sockets.
 * Uses dspOpen with ocRequest for active connections,
 * dspClose for graceful close, abort for immediate close.
 *
 * References:
 * - Programming With AppleTalk (1996), Chapter 5: ADSP
 */

#include "at_defs.h"

#if defined(PT_PLATFORM_APPLETALK)

#include <Devices.h>
#include <string.h>

/* ========================================================================== */
/* Logging Macros                                                              */
/* ========================================================================== */

#define CONN_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define CONN_LOG_INFO(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_INFO((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define CONN_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/* ========================================================================== */
/* Connect to Remote Peer (dspOpen with ocRequest)                             */
/* ========================================================================== */

/**
 * Initiate connection to a remote ADSP endpoint.
 *
 * Initializes CCB if needed, then starts async dspOpen with ocRequest.
 *
 * @param ctx          AppleTalk context
 * @param conn         Hot connection struct (from pt_adsp_alloc)
 * @param remote_addr  Target address (network, node, socket)
 * @return             noErr on success, error code on failure
 */
int pt_adsp_connect(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                    AddrBlock *remote_addr)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_connection_cold *cold;

    if (!ctx || !conn || !remote_addr || !ctx->cold) return -1;

    CONN_LOG_INFO(ctx, "Connecting to %d.%d:%d",
                  (int)remote_addr->aNet, (int)remote_addr->aNode,
                  (int)remote_addr->aSocket);

    /* Initialize CCB if not already done */
    if (conn->state == PT_ADSP_UNUSED ||
        conn->state == PT_ADSP_INITIALIZING) {
        err = pt_adsp_init_ccb(ctx, conn, 0);
        if (err != noErr) {
            CONN_LOG_ERR(ctx, "CCB init for connect failed: %d", (int)err);
            return err;
        }
    }

    if (conn->state != PT_ADSP_IDLE) {
        CONN_LOG_ERR(ctx, "Cannot connect: state %d", (int)conn->state);
        return -1;
    }

    cold = PT_AT_CONN_COLD(ctx, conn);
    pb = &cold->epb.pb;

    /* Initiate connection request */
    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspOpen;
    pb->ccbRefNum = cold->ccb_refnum;
    pb->u.openParams.ocMode = ocRequest;
    pb->u.openParams.remoteAddress = *remote_addr;
    pb->u.openParams.filterAddress.aNet = 0;
    pb->u.openParams.filterAddress.aNode = 0;
    pb->u.openParams.filterAddress.aSocket = 0;
    pb->ioCompletion = ctx->completion_upp;

    conn->flags = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        CONN_LOG_ERR(ctx, "dspOpen (ocRequest) failed: %d", (int)err);
        return err;
    }

    conn->remote_addr = *remote_addr;
    conn->state = PT_ADSP_CONNECTING;

    CONN_LOG_DEBUG(ctx, "Connect initiated, awaiting completion");
    return noErr;
}

/* ========================================================================== */
/* Check Connection Status                                                     */
/* ========================================================================== */

/**
 * Check if an async connect/accept has completed.
 *
 * @param ctx   AppleTalk context
 * @param conn  Hot connection struct
 * @return      noErr if connected, 1 if pending, error code on failure
 */
int pt_adsp_check_connect(pt_at_context *ctx,
                          pt_adsp_connection_hot *conn)
{
    if (!conn) return -1;

    if (conn->state != PT_ADSP_CONNECTING)
        return -1;

    /* Check async completion flag */
    if (!(conn->flags & PT_AT_FLAG_ASYNC_COMPLETE))
        return 1;  /* Still pending */

    conn->flags &= ~PT_AT_FLAG_ASYNC_COMPLETE;

    if (conn->async_result == noErr) {
        conn->state = PT_ADSP_CONNECTED;
        CONN_LOG_INFO(ctx, "Connected: slot=%d remote=%d.%d:%d",
                      (int)conn->slot_index,
                      (int)conn->remote_addr.aNet,
                      (int)conn->remote_addr.aNode,
                      (int)conn->remote_addr.aSocket);
        return noErr;
    }

    /* Connection failed */
    conn->state = PT_ADSP_ERROR;
    CONN_LOG_ERR(ctx, "Connect failed: slot=%d err=%d",
                 (int)conn->slot_index, (int)conn->async_result);
    return conn->async_result;
}

/* ========================================================================== */
/* Close Connection (dspClose) - Graceful                                      */
/* ========================================================================== */

/**
 * Initiate graceful close of an ADSP connection.
 *
 * @param ctx   AppleTalk context
 * @param conn  Hot connection struct
 * @return      noErr on success, error code on failure
 */
int pt_adsp_close(pt_at_context *ctx, pt_adsp_connection_hot *conn)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_connection_cold *cold;

    if (!ctx || !conn || !ctx->cold) return -1;

    if (conn->state != PT_ADSP_CONNECTED) {
        CONN_LOG_ERR(ctx, "Cannot close: state %d", (int)conn->state);
        return -1;
    }

    CONN_LOG_INFO(ctx, "Closing connection to %d.%d:%d",
                  (int)conn->remote_addr.aNet,
                  (int)conn->remote_addr.aNode,
                  (int)conn->remote_addr.aSocket);

    cold = PT_AT_CONN_COLD(ctx, conn);
    pb = &cold->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspClose;
    pb->ccbRefNum = cold->ccb_refnum;
    pb->u.closeParams.abort = 0;  /* Graceful */
    pb->ioCompletion = ctx->completion_upp;

    conn->flags = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        CONN_LOG_ERR(ctx, "dspClose failed: %d", (int)err);
    }

    conn->state = PT_ADSP_CLOSING;
    CONN_LOG_DEBUG(ctx, "Graceful close initiated");
    return noErr;
}

/* ========================================================================== */
/* Abort Connection (immediate close)                                          */
/* ========================================================================== */

/**
 * Immediately abort an ADSP connection.
 *
 * @param ctx   AppleTalk context
 * @param conn  Hot connection struct
 * @return      noErr on success, error code on failure
 */
int pt_adsp_abort(pt_at_context *ctx, pt_adsp_connection_hot *conn)
{
    DSPParamBlock *pb;
    pt_adsp_connection_cold *cold;

    if (!ctx || !conn || !ctx->cold) return -1;

    CONN_LOG_INFO(ctx, "Aborting connection to %d.%d:%d",
                  (int)conn->remote_addr.aNet,
                  (int)conn->remote_addr.aNode,
                  (int)conn->remote_addr.aSocket);

    cold = PT_AT_CONN_COLD(ctx, conn);
    pb = &cold->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspClose;
    pb->ccbRefNum = cold->ccb_refnum;
    pb->u.closeParams.abort = 1;

    PBControlSync((ParmBlkPtr)pb);

    conn->state = PT_ADSP_IDLE;
    CONN_LOG_DEBUG(ctx, "Connection aborted");
    return noErr;
}

#endif /* PT_PLATFORM_APPLETALK */
