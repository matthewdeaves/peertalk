/**
 * @file adsp_appletalk.c
 * @brief ADSP Connection Management
 *
 * Manages ADSP Connection Control Blocks (CCBs) and buffers.
 * Uses hot/cold separation: hot structs are polled every frame,
 * cold structs contain CCBs, buffers, and param blocks.
 *
 * References:
 * - Programming With AppleTalk (1996), Chapter 5: ADSP
 * - Inside Macintosh: Networking (ADSP chapter)
 */

#include "at_defs.h"

#if defined(PT_PLATFORM_APPLETALK)

#include <Devices.h>
#include <MacMemory.h>
#include <string.h>

/* ========================================================================== */
/* Logging Macros                                                              */
/* ========================================================================== */

#define ADSP_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_ERR((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define ADSP_LOG_INFO(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_INFO((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define ADSP_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/* ========================================================================== */
/* Buffer Allocation (cold struct)                                             */
/* ========================================================================== */

/**
 * Allocate ADSP connection buffers in cold struct.
 *
 * All buffers must be locked (non-relocatable, non-purgeable).
 * NewPtrClear guarantees this.
 *
 * @param cold           Cold connection struct
 * @param failed_buffer  Output: which buffer failed (1=send, 2=recv, 3=attn)
 * @return               noErr on success, memFullErr on failure
 */
static int pt_adsp_alloc_buffers(pt_adsp_connection_cold *cold,
                                 int *failed_buffer)
{
    *failed_buffer = 0;

    cold->send_queue = NewPtrClear(PT_ADSP_SEND_QUEUE_SIZE);
    if (!cold->send_queue) {
        *failed_buffer = 1;
        return memFullErr;
    }

    cold->recv_queue = NewPtrClear(PT_ADSP_RECV_QUEUE_SIZE);
    if (!cold->recv_queue) {
        DisposePtr(cold->send_queue);
        cold->send_queue = NULL;
        *failed_buffer = 2;
        return memFullErr;
    }

    cold->attn_buffer = NewPtrClear(PT_ADSP_ATTN_BUF_SIZE);
    if (!cold->attn_buffer) {
        DisposePtr(cold->recv_queue);
        DisposePtr(cold->send_queue);
        cold->recv_queue = NULL;
        cold->send_queue = NULL;
        *failed_buffer = 3;
        return memFullErr;
    }

    return noErr;
}

/**
 * Free ADSP connection buffers.
 */
static void pt_adsp_free_buffers(pt_adsp_connection_cold *cold)
{
    if (cold->attn_buffer) {
        DisposePtr(cold->attn_buffer);
        cold->attn_buffer = NULL;
    }
    if (cold->recv_queue) {
        DisposePtr(cold->recv_queue);
        cold->recv_queue = NULL;
    }
    if (cold->send_queue) {
        DisposePtr(cold->send_queue);
        cold->send_queue = NULL;
    }
}

/* ========================================================================== */
/* Initialize CCB (dspInit)                                                    */
/* ========================================================================== */

/**
 * Initialize a connection's CCB using hot/cold pattern.
 *
 * @param ctx     AppleTalk context
 * @param conn    Hot connection struct
 * @param socket  Socket number (0 = auto-assign)
 * @return        noErr on success, error code on failure
 */
int pt_adsp_init_ccb(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                     short socket)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_connection_cold *cold;

    if (!ctx || !conn) return -1;

    ADSP_LOG_DEBUG(ctx, "Init CCB slot %d socket=%d",
                   (int)conn->slot_index, (int)socket);

    cold = PT_AT_CONN_COLD(ctx, conn);

    /* Clear hot struct (preserve slot_index) */
    {
        uint8_t slot = conn->slot_index;
        memset(conn, 0, sizeof(pt_adsp_connection_hot));
        conn->slot_index = slot;
    }

    /* Clear cold struct (preserve hot pointer) */
    {
        pt_adsp_connection_hot *hot_ptr = cold->hot;
        memset(cold, 0, sizeof(pt_adsp_connection_cold));
        cold->hot = hot_ptr;
    }

    /* Allocate buffers */
    {
        int failed_buffer;
        err = pt_adsp_alloc_buffers(cold, &failed_buffer);
        if (err != noErr) {
            ADSP_LOG_ERR(ctx, "Buffer alloc failed: buf=%d MaxBlock=%ld",
                         failed_buffer, MaxBlock());
            return err;
        }
    }

    /* Set up extended param block context */
    cold->epb.context = cold;
    pb = &cold->epb.pb;

    /* Initialize CCB with dspInit */
    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspInit;
    pb->u.initParams.ccbPtr = (TPCCB)&cold->ccb;
    pb->u.initParams.userRoutine = ctx->event_upp;
    pb->u.initParams.sendQSize = PT_ADSP_SEND_QUEUE_SIZE;
    pb->u.initParams.sendQueue = cold->send_queue;
    pb->u.initParams.recvQSize = PT_ADSP_RECV_QUEUE_SIZE;
    pb->u.initParams.recvQueue = cold->recv_queue;
    pb->u.initParams.attnPtr = cold->attn_buffer;
    pb->u.initParams.localSocket = socket;

    err = PBControlSync((ParmBlkPtr)pb);
    if (err != noErr) {
        ADSP_LOG_ERR(ctx, "dspInit failed: %d", (int)err);
        pt_adsp_free_buffers(cold);
        return err;
    }

    /* Save assigned socket and refnum */
    cold->ccb_refnum = pb->ccbRefNum;
    cold->local_addr.aSocket = (unsigned char)pb->u.initParams.localSocket;

    conn->state = PT_ADSP_IDLE;
    ADSP_LOG_DEBUG(ctx, "CCB init: refnum=%d socket=%d",
                   (int)cold->ccb_refnum,
                   (int)cold->local_addr.aSocket);
    return noErr;
}

/* ========================================================================== */
/* Remove CCB (dspRemove)                                                      */
/* ========================================================================== */

/**
 * Remove a connection's CCB and free buffers.
 *
 * @param ctx   AppleTalk context
 * @param conn  Hot connection struct
 * @return      noErr on success, error code on failure
 */
int pt_adsp_remove_ccb(pt_at_context *ctx, pt_adsp_connection_hot *conn)
{
    OSErr err;
    DSPParamBlock *pb;
    pt_adsp_connection_cold *cold;

    if (!ctx || !conn) return -1;
    if (conn->state == PT_ADSP_UNUSED) return noErr;

    cold = PT_AT_CONN_COLD(ctx, conn);
    ADSP_LOG_DEBUG(ctx, "Removing CCB refnum=%d", (int)cold->ccb_refnum);

    pb = &cold->epb.pb;

    memset(pb, 0, sizeof(DSPParamBlock));
    pb->ioCRefNum = ctx->dsp_refnum;
    pb->csCode = dspRemove;
    pb->ccbRefNum = cold->ccb_refnum;
    pb->u.closeParams.abort = 1;

    err = PBControlSync((ParmBlkPtr)pb);
    if (err != noErr) {
        ADSP_LOG_ERR(ctx, "dspRemove failed: %d", (int)err);
    }

    pt_adsp_free_buffers(cold);
    conn->state = PT_ADSP_UNUSED;
    ADSP_LOG_DEBUG(ctx, "CCB removed");
    return err;
}

/* ========================================================================== */
/* Connection Pool                                                             */
/* ========================================================================== */

/**
 * Allocate a connection slot from pool.
 *
 * @param ctx  AppleTalk context
 * @return     Hot connection pointer, or NULL if pool exhausted
 */
pt_adsp_connection_hot *pt_adsp_alloc(pt_at_context *ctx)
{
    int i;

    if (!ctx) return NULL;

    for (i = 0; i < PT_MAX_PEERS; i++) {
        if (ctx->connections[i].state == PT_ADSP_UNUSED) {
            ctx->connections[i].state = PT_ADSP_INITIALIZING;

#if PT_MAX_PEERS <= 32
            ctx->active_mask |= (1UL << i);
            ADSP_LOG_DEBUG(ctx, "Alloc slot %d (active=%d)",
                           i, pt_popcount(ctx->active_mask));
#else
            ctx->active_connections[ctx->active_count] = (uint8_t)i;
            ctx->active_count++;
            ADSP_LOG_DEBUG(ctx, "Alloc slot %d (active=%d)",
                           i, (int)ctx->active_count);
#endif
            return &ctx->connections[i];
        }
    }

    ADSP_LOG_ERR(ctx, "Connection pool exhausted");
    return NULL;
}

/**
 * Release a connection slot back to pool.
 *
 * @param ctx   AppleTalk context
 * @param conn  Hot connection struct to release
 */
void pt_adsp_release(pt_at_context *ctx, pt_adsp_connection_hot *conn)
{
    int slot;
#if PT_MAX_PEERS > 32
    int i;
#endif

    if (!ctx || !conn) return;

    slot = conn->slot_index;

    /* Close if connected */
    if (conn->state >= PT_ADSP_CONNECTED) {
        pt_adsp_remove_ccb(ctx, conn);
    }

    conn->state = PT_ADSP_UNUSED;

#if PT_MAX_PEERS <= 32
    ctx->active_mask &= ~(1UL << slot);
    ADSP_LOG_DEBUG(ctx, "Release slot %d (active=%d)",
                   slot, pt_popcount(ctx->active_mask));
#else
    for (i = 0; i < ctx->active_count; i++) {
        if (ctx->active_connections[i] == slot) {
            ctx->active_connections[i] =
                ctx->active_connections[ctx->active_count - 1];
            ctx->active_count--;
            ADSP_LOG_DEBUG(ctx, "Release slot %d (active=%d)",
                           slot, (int)ctx->active_count);
            break;
        }
    }
#endif
}

#endif /* PT_PLATFORM_APPLETALK */
