/**
 * @file at_driver.c
 * @brief AppleTalk Driver Interface
 *
 * Opens the .MPP and .DSP drivers required for NBP and ADSP.
 * Creates UPPs for ADSP callbacks and manages init/shutdown lifecycle.
 *
 * Contains interrupt-level callbacks:
 * - pt_adsp_completion: ioCompletion for async ADSP operations
 * - pt_adsp_event: userRoutine for unsolicited connection events
 * - pt_listener_completion: ioCompletion for listener (dspCLListen)
 *
 * References:
 * - Programming With AppleTalk (1996), Chapter 5: ADSP
 * - Inside Macintosh Volume VI, Table B-3 (interrupt-safe routines)
 */

#include "at_defs.h"

#if defined(PT_PLATFORM_APPLETALK)

#include <Devices.h>
#include <MacMemory.h>
#include <string.h>

/* ========================================================================== */
/* Logging Macros                                                              */
/*                                                                             */
/* Safe wrappers that check for NULL log context.                              */
/* WARNING: NOT ISR-safe! Only use from main thread code.                      */
/* ========================================================================== */

#define AT_LOG_ERR(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_ERR((ctx)->log, PT_LOG_CAT_PLATFORM, __VA_ARGS__); } while(0)
#define AT_LOG_WARN(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_WARN((ctx)->log, PT_LOG_CAT_PLATFORM, __VA_ARGS__); } while(0)
#define AT_LOG_INFO(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_INFO((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)
#define AT_LOG_DEBUG(ctx, ...) \
    do { if ((ctx) && (ctx)->log) \
        PT_LOG_DEBUG((ctx)->log, PT_LOG_CAT_NETWORK, __VA_ARGS__); } while(0)

/* ========================================================================== */
/* ioCompletion Callback (runs at interrupt level!)                            */
/*                                                                             */
/* Called when async operations (dspRead, dspWrite, dspOpen, etc) complete.    */
/* Receives DSPPBPtr in A0.                                                    */
/*                                                                             */
/* ISR RULES:                                                                  */
/*   - NO memory allocation                                                    */
/*   - NO synchronous calls                                                    */
/*   - Only set volatile flags                                                 */
/* ========================================================================== */

static pascal void pt_adsp_completion(DSPPBPtr pb)
{
    pt_adsp_connection_cold *cold;
    pt_adsp_connection_hot *hot;

    /* Recover context from extended param block */
    cold = (pt_adsp_connection_cold *)PT_ADSP_GET_CONTEXT(pb);
    if (!cold) return;

    hot = cold->hot;
    if (!hot) return;

    /* Store result and mark complete in HOT struct */
    hot->async_result = pb->ioResult;
    hot->flags |= PT_AT_FLAG_ASYNC_COMPLETE;
}

/* ========================================================================== */
/* userRoutine Callback (runs at interrupt level!)                             */
/*                                                                             */
/* Called for unsolicited connection events (close, attention, etc).            */
/* Receives TPCCB (CCB pointer) in A1 - NOT the param block!                  */
/*                                                                             */
/* Since TRCCB is the first member of pt_adsp_connection_cold, we can cast     */
/* directly to recover our context.                                            */
/*                                                                             */
/* Per Programming With AppleTalk p.112: userRoutine "is called under          */
/* the same conditions as a completion routine (at interrupt level)             */
/* and must follow the same rules."                                            */
/*                                                                             */
/* CRITICAL: Clear userFlags after reading to allow future events.             */
/* ========================================================================== */

static pascal void pt_adsp_event(TPCCB ccb)
{
    pt_adsp_connection_cold *cold;
    pt_adsp_connection_hot *hot;
    UInt8 user_flags;

    if (!ccb) return;

    /* CCB is first member of pt_adsp_connection_cold */
    cold = (pt_adsp_connection_cold *)ccb;

    hot = cold->hot;
    if (!hot) return;

    /* Read userFlags */
    user_flags = ccb->userFlags;

    /* Map userFlags to our packed flags */
    if (user_flags & eClosed) {
        hot->flags |= PT_AT_FLAG_CONNECTION_CLOSED;
    }
    if (user_flags & eTearDown) {
        hot->flags |= PT_AT_FLAG_CONNECTION_CLOSED;
    }
    if (user_flags & eAttention) {
        hot->flags |= PT_AT_FLAG_ATTENTION;
    }
    if (user_flags & eFwdReset) {
        hot->flags |= PT_AT_FLAG_FWD_RESET;
    }

    /* CRITICAL: Clear userFlags after reading.
     * "failure to clear will hang connection" */
    ccb->userFlags = 0;
}

/* ========================================================================== */
/* Listener ioCompletion Callback (runs at interrupt level!)                   */
/*                                                                             */
/* Called when dspCLListen completes (incoming connection request arrived).     */
/* Receives DSPPBPtr in A0.                                                    */
/* ========================================================================== */

static pascal void pt_listener_completion(DSPPBPtr pb)
{
    pt_adsp_listener_cold *cold;
    pt_adsp_listener_hot *hot;

    cold = (pt_adsp_listener_cold *)PT_ADSP_GET_CONTEXT(pb);
    if (!cold) return;

    hot = cold->hot;
    if (!hot) return;

    hot->async_result = pb->ioResult;

    if (pb->ioResult == noErr) {
        /* Extract synchronization info to cold struct */
        cold->remote_cid = pb->u.openParams.remoteCID;
        cold->send_seq = pb->u.openParams.sendSeq;
        cold->send_window = pb->u.openParams.sendWindow;
        cold->attn_send_seq = pb->u.openParams.attnSendSeq;

        /* Remote address to hot struct for quick access */
        hot->remote_addr = pb->u.openParams.remoteAddress;
        hot->connection_pending = true;
    }

    hot->flags |= PT_AT_FLAG_ASYNC_COMPLETE;
}

/* ========================================================================== */
/* Driver Management                                                           */
/* ========================================================================== */

/**
 * Initialize AppleTalk drivers and context.
 *
 * Opens .MPP (for NBP) and .DSP (for ADSP) drivers, creates
 * callback UPPs, and allocates cold data block.
 *
 * @param ctx  AppleTalk context (caller-allocated)
 * @param log  PT_Log instance for logging (may be NULL)
 * @return     noErr on success, Mac OS error code on failure
 */
int pt_at_init(pt_at_context *ctx, PT_Log *log)
{
    OSErr err;
    ParamBlockRec pb;
    int i;

    if (!ctx) return -1;

    /* Clear context */
    memset(ctx, 0, sizeof(pt_at_context));
    ctx->log = log;

    AT_LOG_DEBUG(ctx, "Initializing AppleTalk drivers");

    /* Allocate cold data block */
    ctx->cold = (pt_at_context_cold *)NewPtrClear(
        sizeof(pt_at_context_cold));
    if (!ctx->cold) {
        AT_LOG_ERR(ctx, "Failed to allocate cold data (%ld bytes)",
                   (long)sizeof(pt_at_context_cold));
        return memFullErr;
    }

    /* Initialize hot/cold linkage for connections */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        ctx->connections[i].slot_index = (uint8_t)i;
        ctx->connections[i].state = PT_ADSP_UNUSED;
        ctx->cold->connections[i].hot = &ctx->connections[i];
    }

    /* Initialize NBP cold state pointers */
    ctx->cold->nbp.lookup_buf = ctx->cold->nbp_lookup_buf;
    ctx->cold->nbp.entries = ctx->cold->nbp_entries;
    ctx->cold->nbp.entry_names = ctx->cold->nbp_entry_names;

    /* Initialize listener hot/cold linkage */
    ctx->cold->listener.hot = &ctx->listener;

    /* Open .MPP driver (required for NBP) */
    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioNamePtr = "\p.MPP";
    pb.ioParam.ioPermssn = fsCurPerm;

    err = PBOpenSync(&pb);
    if (err != noErr) {
        AT_LOG_ERR(ctx, ".MPP open failed: %d", (int)err);
        DisposePtr((Ptr)ctx->cold);
        ctx->cold = NULL;
        return err;
    }
    ctx->mpp_refnum = pb.ioParam.ioRefNum;
    AT_LOG_DEBUG(ctx, ".MPP opened (refnum=%d)", (int)ctx->mpp_refnum);

    /* Open .DSP driver (required for ADSP) */
    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioNamePtr = "\p.DSP";
    pb.ioParam.ioPermssn = fsCurPerm;

    err = PBOpenSync(&pb);
    if (err != noErr) {
        AT_LOG_ERR(ctx, ".DSP open failed: %d", (int)err);
        /* Close .MPP on failure */
        memset(&pb, 0, sizeof(pb));
        pb.ioParam.ioRefNum = ctx->mpp_refnum;
        PBCloseSync(&pb);
        DisposePtr((Ptr)ctx->cold);
        ctx->cold = NULL;
        return err;
    }
    ctx->dsp_refnum = pb.ioParam.ioRefNum;
    AT_LOG_DEBUG(ctx, ".DSP opened (refnum=%d)", (int)ctx->dsp_refnum);

    /* Create completion routine UPP (for ioCompletion) */
    ctx->completion_upp = NewADSPCompletionUPP(pt_adsp_completion);
    if (!ctx->completion_upp) {
        AT_LOG_ERR(ctx, "Failed to create completion UPP");
        goto fail_upps;
    }

    /* Create event routine UPP (for userRoutine) */
    ctx->event_upp = NewADSPConnectionEventUPP(pt_adsp_event);
    if (!ctx->event_upp) {
        AT_LOG_ERR(ctx, "Failed to create event UPP");
        DisposeADSPCompletionUPP(ctx->completion_upp);
        ctx->completion_upp = NULL;
        goto fail_upps;
    }

    /* Create listener completion UPP */
    ctx->listener_completion_upp =
        NewADSPCompletionUPP(pt_listener_completion);
    if (!ctx->listener_completion_upp) {
        AT_LOG_ERR(ctx, "Failed to create listener UPP");
        DisposeADSPConnectionEventUPP(ctx->event_upp);
        ctx->event_upp = NULL;
        DisposeADSPCompletionUPP(ctx->completion_upp);
        ctx->completion_upp = NULL;
        goto fail_upps;
    }

    ctx->drivers_open = true;
    AT_LOG_INFO(ctx, "AppleTalk drivers initialized");
    return noErr;

fail_upps:
    /* Clean up drivers */
    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioRefNum = ctx->dsp_refnum;
    PBCloseSync(&pb);
    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioRefNum = ctx->mpp_refnum;
    PBCloseSync(&pb);
    DisposePtr((Ptr)ctx->cold);
    ctx->cold = NULL;
    return memFullErr;
}

/**
 * Shut down AppleTalk drivers and free resources.
 *
 * Disposes UPPs and frees cold data block. Logs MaxBlock
 * delta for memory leak detection on real hardware.
 *
 * @param ctx  AppleTalk context
 */
void pt_at_shutdown(pt_at_context *ctx)
{
    long maxblock_before, maxblock_after;

    if (!ctx || !ctx->drivers_open) return;

    AT_LOG_DEBUG(ctx, "Shutting down AppleTalk drivers");

    /* LEAK DETECTION: Record MaxBlock for debugging */
    maxblock_before = MaxBlock();
    AT_LOG_DEBUG(ctx, "MaxBlock before shutdown: %ld", maxblock_before);

    /* Dispose all UPPs */
    if (ctx->listener_completion_upp) {
        DisposeADSPCompletionUPP(ctx->listener_completion_upp);
        ctx->listener_completion_upp = NULL;
    }
    if (ctx->event_upp) {
        DisposeADSPConnectionEventUPP(ctx->event_upp);
        ctx->event_upp = NULL;
    }
    if (ctx->completion_upp) {
        DisposeADSPCompletionUPP(ctx->completion_upp);
        ctx->completion_upp = NULL;
    }

    /* Free cold data block */
    if (ctx->cold) {
        DisposePtr((Ptr)ctx->cold);
        ctx->cold = NULL;
    }

    ctx->drivers_open = false;

    /* LEAK DETECTION: Compare MaxBlock after cleanup */
    maxblock_after = MaxBlock();
    AT_LOG_DEBUG(ctx, "MaxBlock after shutdown: %ld (delta: %ld)",
                 maxblock_after, maxblock_after - maxblock_before);
    if (maxblock_after < maxblock_before - 1024) {
        AT_LOG_WARN(ctx, "Potential leak: %ld bytes not freed",
                    maxblock_before - maxblock_after);
    }

    AT_LOG_INFO(ctx, "AppleTalk drivers shut down");
}

/* ========================================================================== */
/* Get Local AppleTalk Address                                                 */
/* ========================================================================== */

/**
 * Get the local AppleTalk network address.
 *
 * Uses GetNodeAddress() to retrieve local network and node numbers.
 * Socket number is endpoint-specific and set during dspInit/dspCLInit.
 *
 * @param ctx   AppleTalk context
 * @param addr  Output address (socket will be 0)
 * @return      noErr on success, Mac OS error code on failure
 */
int pt_at_get_local_addr(pt_at_context *ctx, AddrBlock *addr)
{
    OSErr err;
    short node;
    short network;

    if (!ctx || !addr) return -1;

    err = GetNodeAddress(&node, &network);
    if (err != noErr) {
        return err;
    }

    addr->aNet = network;
    addr->aNode = (unsigned char)node;
    addr->aSocket = 0;  /* Socket is endpoint-specific */

    return noErr;
}

#endif /* PT_PLATFORM_APPLETALK */
