/**
 * @file adsp_io.c
 * @brief ADSP Data I/O
 *
 * Send and receive data over ADSP connections.
 * Uses dspWrite, dspRead, dspStatus, and dspAttention.
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

#define IO_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define IO_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/* ========================================================================== */
/* Send Data (dspWrite)                                                        */
/*                                                                             */
/* eom = 1 to mark end of message (for message framing)                        */
/* ========================================================================== */

/**
 * Start async write to an ADSP connection.
 *
 * @param ctx   AppleTalk context
 * @param conn  Hot connection struct
 * @param data  Data to send
 * @param len   Data length
 * @param eom   End-of-message flag (1 = end of logical message)
 * @return      noErr on success, 1 if busy, error code on failure
 */
int pt_adsp_write(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                  const void *data, unsigned short len, Boolean eom)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_connection_cold *cold;

    if (!ctx || !conn || !data || len == 0 || !ctx->cold) return -1;

    if (conn->state != PT_ADSP_CONNECTED) {
        return -1;
    }

    /* Check if previous async still pending */
    if (conn->flags & PT_AT_FLAG_ASYNC_COMPLETE) {
        /* Previous op complete but not checked - caller should check first */
    }

    cold = PT_AT_CONN_COLD(ctx, conn);
    pb = &cold->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspWrite;
    pb->ccbRefNum = cold->ccb_refnum;
    pb->u.ioParams.reqCount = len;
    pb->u.ioParams.dataPtr = (Ptr)data;
    pb->u.ioParams.eom = eom;
    pb->ioCompletion = ctx->completion_upp;

    conn->flags = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        IO_LOG_ERR(ctx, "dspWrite failed: %d (slot=%d, len=%u)",
                   (int)err, (int)conn->slot_index, (unsigned)len);
        return err;
    }

    IO_LOG_DEBUG(ctx, "dspWrite started: slot=%d len=%u eom=%d",
                 (int)conn->slot_index, (unsigned)len, (int)eom);
    return noErr;
}

/* ========================================================================== */
/* Check Send Status                                                           */
/* ========================================================================== */

/**
 * Check if an async write has completed.
 *
 * @param ctx         AppleTalk context
 * @param conn        Hot connection struct
 * @param bytes_sent  Output: actual bytes sent (may be NULL)
 * @return            noErr if complete, 1 if pending, error code on failure
 */
int pt_adsp_write_check(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                        unsigned short *bytes_sent)
{
    pt_adsp_connection_cold *cold;

    if (!conn || !ctx || !ctx->cold) return -1;

    if (!(conn->flags & PT_AT_FLAG_ASYNC_COMPLETE)) {
        return 1;  /* Still pending */
    }

    conn->flags &= ~PT_AT_FLAG_ASYNC_COMPLETE;

    cold = PT_AT_CONN_COLD(ctx, conn);

    if (bytes_sent) {
        *bytes_sent = cold->epb.pb.u.ioParams.actCount;
    }

    if (conn->async_result != noErr) {
        IO_LOG_ERR(ctx, "dspWrite failed: %d (slot=%d)",
                   (int)conn->async_result, (int)conn->slot_index);
    } else {
        IO_LOG_DEBUG(ctx, "dspWrite complete: slot=%d sent=%u",
                     (int)conn->slot_index,
                     (unsigned)cold->epb.pb.u.ioParams.actCount);
    }

    return conn->async_result;
}

/* ========================================================================== */
/* Receive Data (dspRead)                                                      */
/* ========================================================================== */

/**
 * Start async read from an ADSP connection.
 *
 * @param ctx       AppleTalk context
 * @param conn      Hot connection struct
 * @param buffer    Buffer to receive data into
 * @param buf_size  Buffer size
 * @return          noErr on success, error code on failure
 */
int pt_adsp_read(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                 void *buffer, unsigned short buf_size)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_connection_cold *cold;

    if (!ctx || !conn || !buffer || buf_size == 0 || !ctx->cold) return -1;

    if (conn->state != PT_ADSP_CONNECTED) {
        return -1;
    }

    cold = PT_AT_CONN_COLD(ctx, conn);
    pb = &cold->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspRead;
    pb->ccbRefNum = cold->ccb_refnum;
    pb->u.ioParams.reqCount = buf_size;
    pb->u.ioParams.dataPtr = (Ptr)buffer;
    pb->ioCompletion = ctx->completion_upp;

    conn->flags = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        IO_LOG_ERR(ctx, "dspRead failed: %d (slot=%d, buf=%u)",
                   (int)err, (int)conn->slot_index, (unsigned)buf_size);
        return err;
    }

    IO_LOG_DEBUG(ctx, "dspRead started: slot=%d buf=%u",
                 (int)conn->slot_index, (unsigned)buf_size);
    return noErr;
}

/* ========================================================================== */
/* Check Receive Status                                                        */
/* ========================================================================== */

/**
 * Check if an async read has completed.
 *
 * @param ctx             AppleTalk context
 * @param conn            Hot connection struct
 * @param bytes_received  Output: actual bytes received (may be NULL)
 * @param eom             Output: end-of-message flag (may be NULL)
 * @return                noErr if complete, 1 if pending, error code on failure
 */
int pt_adsp_read_check(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                       unsigned short *bytes_received, Boolean *eom)
{
    pt_adsp_connection_cold *cold;

    if (!conn || !ctx || !ctx->cold) return -1;

    if (!(conn->flags & PT_AT_FLAG_ASYNC_COMPLETE)) {
        return 1;  /* Still pending */
    }

    conn->flags &= ~PT_AT_FLAG_ASYNC_COMPLETE;

    cold = PT_AT_CONN_COLD(ctx, conn);

    if (bytes_received) {
        *bytes_received = cold->epb.pb.u.ioParams.actCount;
    }
    if (eom) {
        *eom = cold->epb.pb.u.ioParams.eom;
    }

    if (conn->async_result != noErr) {
        IO_LOG_ERR(ctx, "dspRead failed: %d (slot=%d)",
                   (int)conn->async_result, (int)conn->slot_index);
    } else {
        IO_LOG_DEBUG(ctx, "dspRead complete: slot=%d recv=%u eom=%d",
                     (int)conn->slot_index,
                     (unsigned)cold->epb.pb.u.ioParams.actCount,
                     (int)cold->epb.pb.u.ioParams.eom);
    }

    return conn->async_result;
}

/* ========================================================================== */
/* Check Buffer Space (dspStatus)                                              */
/* ========================================================================== */

/**
 * Query connection buffer status.
 *
 * @param ctx           AppleTalk context
 * @param conn          Hot connection struct
 * @param send_free     Output: free bytes in send queue (may be NULL)
 * @param recv_pending  Output: pending bytes in recv queue (may be NULL)
 * @return              noErr on success, error code on failure
 */
int pt_adsp_get_status(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                       unsigned short *send_free, unsigned short *recv_pending)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_connection_cold *cold;

    if (!ctx || !conn || !ctx->cold) return -1;

    cold = PT_AT_CONN_COLD(ctx, conn);
    pb = &cold->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspStatus;
    pb->ccbRefNum = cold->ccb_refnum;

    err = PBControlSync((ParmBlkPtr)pb);
    if (err != noErr) {
        IO_LOG_ERR(ctx, "dspStatus failed: %d (slot=%d)",
                   (int)err, (int)conn->slot_index);
        return err;
    }

    if (send_free) {
        *send_free = pb->u.statusParams.sendQFree;
    }
    if (recv_pending) {
        *recv_pending = pb->u.statusParams.recvQPending;
    }

    return noErr;
}

/* ========================================================================== */
/* Send Attention Message (out-of-band)                                        */
/* ========================================================================== */

/**
 * Send an attention (out-of-band) message.
 *
 * @param ctx   AppleTalk context
 * @param conn  Hot connection struct
 * @param code  Attention code (application-defined)
 * @param data  Attention data (up to 570 bytes, may be NULL if len == 0)
 * @param len   Data length (max PT_ADSP_ATTN_BUF_SIZE)
 * @return      noErr on success, error code on failure
 */
int pt_adsp_attention(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                      unsigned short code, const void *data,
                      unsigned short len)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_connection_cold *cold;

    if (!ctx || !conn || !ctx->cold) return -1;
    if (len > PT_ADSP_ATTN_BUF_SIZE) return -1;

    if (conn->state != PT_ADSP_CONNECTED) {
        return -1;
    }

    cold = PT_AT_CONN_COLD(ctx, conn);
    pb = &cold->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspAttention;
    pb->ccbRefNum = cold->ccb_refnum;
    pb->u.attnParams.attnCode = code;
    pb->u.attnParams.attnSize = len;
    pb->u.attnParams.attnData = (Ptr)data;
    pb->ioCompletion = ctx->completion_upp;

    conn->flags = 0;

    err = PBControlAsync((ParmBlkPtr)pb);
    if (err != noErr) {
        IO_LOG_ERR(ctx, "dspAttention failed: %d (slot=%d)",
                   (int)err, (int)conn->slot_index);
        return err;
    }

    IO_LOG_DEBUG(ctx, "dspAttention sent: slot=%d code=%u len=%u",
                 (int)conn->slot_index, (unsigned)code, (unsigned)len);
    return noErr;
}

#endif /* PT_PLATFORM_APPLETALK */
