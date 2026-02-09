/**
 * @file platform_mactcp.c
 * @brief PeerTalk MacTCP Platform Implementation
 *
 * Platform abstraction for System 6.0.8 - 7.5.5 on 68k Macs.
 *
 * References:
 * - MacTCP Programmer's Guide (1989), Chapter 2: "Opening the Driver"
 * - Inside Macintosh: Devices, Device Manager chapter
 */

#include "pt_internal.h"
#include "pt_compat.h"
#include "mactcp_defs.h"

#if defined(PT_PLATFORM_MACTCP)

#include <Devices.h>
#include <MacTCP.h>
#include <MacMemory.h>
#include <OSUtils.h>

/* Forward declaration for mactcp_driver.c functions */
extern size_t pt_mactcp_extra_size(void);
extern int pt_mactcp_data_init(struct pt_context *ctx);

/* Forward declarations for ASR callbacks (implemented in udp_mactcp.c and tcp_mactcp.c) */
extern pascal void pt_udp_asr(StreamPtr udpStream, unsigned short eventCode,
                              Ptr userDataPtr, struct ICMPReport *icmpMsg);
extern pascal void pt_tcp_asr(StreamPtr tcpStream, unsigned short eventCode,
                              Ptr userDataPtr, unsigned short terminReason,
                              struct ICMPReport *icmpMsg);

/* Note: Completion routines (pt_tcp_*_completion) are NOT used.
 * We poll pb.ioResult directly instead of using callbacks to avoid
 * shutdown crashes. See comments in mactcp_init(). */

/* ========================================================================== */
/* MacTCP Driver and UPPs                                                     */
/* ========================================================================== */

/**
 * MacTCP driver name - Pascal string.
 * The driver is ".IPP" (Internet Protocol Package).
 *
 * From MacTCP Programmer's Guide:
 * "Your application opens the MacTCP driver by calling PBOpen with
 * the driver name '.IPP'"
 */
#define MACTCP_DRIVER_NAME "\p.IPP"

/* MacTCP driver reference number - valid after successful open */
static short g_mactcp_refnum = 0;

/**
 * Universal Procedure Pointers (UPPs) for MacTCP callbacks.
 *
 * MacTCP requires UPPs for callback registration. From MacTCP.h:
 * "For TCPCreatePB Control calls, use NewTCPNotifyProc to set up a
 * TCPNotifyUPP universal procptr to pass in the notifyProc field"
 *
 * UPPs enable the mixed-mode manager to call 68k code from PPC
 * environments, and vice versa. Even on pure 68k, we need these
 * for proper stack frame setup with pascal calling convention.
 *
 * These are created once at init and disposed at shutdown.
 * Individual streams reference these global UPPs.
 */
static TCPNotifyUPP g_tcp_notify_upp = NULL;
static UDPNotifyUPP g_udp_notify_upp = NULL;

/* ASR callbacks are implemented in:
 * - tcp_mactcp.c (pt_tcp_asr) - Session 5.4
 * - udp_mactcp.c (pt_udp_asr) - Session 5.2
 */

/* Forward declaration for buffer allocation (from mactcp_driver.c) */
extern Ptr pt_mactcp_alloc_buffer(unsigned long size);
extern void pt_mactcp_free_buffer(Ptr buffer);

/* ========================================================================== */
/* Early Buffer Pre-allocation                                                */
/* ========================================================================== */

/**
 * Pre-allocate TCP receive buffers BEFORE MacTCP driver opens.
 *
 * CRITICAL: This MUST be called BEFORE PBOpenSync() for the MacTCP driver.
 * MacTCP's initialization allocates significant internal buffers that fragment
 * the heap. By allocating our buffers first, we can get larger contiguous
 * blocks (16-32KB instead of 4KB).
 *
 * The 25% threshold rule (MacTCP Programmer's Guide lines 3070-3091) means:
 * - 4KB buffer = receive completes at 1KB (slow, many poll cycles)
 * - 16KB buffer = receive completes at 4KB (4x better)
 * - 32KB buffer = receive completes at 8KB (optimal for 4096-byte messages)
 *
 * @param ctx  PeerTalk context (pt_mactcp_data is at end of context)
 */
static void pt_mactcp_preallocate_early(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    long max_block;
    unsigned long target_size;
    unsigned long total_needed;
    int i;
    int allocated = 0;

    /* Initialize pre-alloc tracking before any allocations */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        md->prealloced_bufs[i] = NULL;
    }
    md->prealloced_buf_size = 0;

    /* Check memory state BEFORE MacTCP driver opens - should be much larger */
    max_block = MaxBlock();

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_MEMORY,
        "Early pre-allocation: MaxBlock=%ld (before MacTCP driver)",
        max_block);

    /* Determine target buffer size based on available memory
     * We need PT_MAX_PEERS buffers plus headroom for MacTCP + other allocations
     */
    total_needed = (unsigned long)PT_MAX_PEERS * PT_TCP_RCV_BUF_HIGH + 65536;
    if (max_block >= (long)total_needed) {
        target_size = PT_TCP_RCV_BUF_HIGH;  /* 32KB per stream */
        PT_LOG_INFO(ctx->log, PT_LOG_CAT_MEMORY,
            "Pre-allocating %d x 32KB TCP buffers", PT_MAX_PEERS);
    } else {
        total_needed = (unsigned long)PT_MAX_PEERS * PT_TCP_RCV_BUF_BLOCK + 32768;
        if (max_block >= (long)total_needed) {
            target_size = PT_TCP_RCV_BUF_BLOCK;  /* 16KB per stream */
            PT_LOG_INFO(ctx->log, PT_LOG_CAT_MEMORY,
                "Pre-allocating %d x 16KB TCP buffers", PT_MAX_PEERS);
        } else {
            total_needed = (unsigned long)PT_MAX_PEERS * PT_TCP_RCV_BUF_CHAR + 16384;
            if (max_block >= (long)total_needed) {
                target_size = PT_TCP_RCV_BUF_CHAR;  /* 8KB per stream */
                PT_LOG_INFO(ctx->log, PT_LOG_CAT_MEMORY,
                    "Pre-allocating %d x 8KB TCP buffers", PT_MAX_PEERS);
            } else {
                /* Not enough memory - fall back to on-demand allocation */
                PT_LOG_WARN(ctx->log, PT_LOG_CAT_MEMORY,
                    "Insufficient memory for pre-allocation (MaxBlock=%ld, need=%lu for %d peers)",
                    max_block, total_needed, PT_MAX_PEERS);
                return;
            }
        }
    }

    /* Allocate all buffers */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        md->prealloced_bufs[i] = pt_mactcp_alloc_buffer(target_size);
        if (md->prealloced_bufs[i] != NULL) {
            allocated++;
        } else {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_MEMORY,
                "Pre-allocation failed at buffer %d/%d", i + 1, PT_MAX_PEERS);
            break;
        }
    }

    if (allocated == PT_MAX_PEERS) {
        md->prealloced_buf_size = target_size;
        PT_LOG_INFO(ctx->log, PT_LOG_CAT_MEMORY,
            "Pre-allocated %d TCP buffers of %luKB (total %luKB, 25%% threshold=%luB)",
            PT_MAX_PEERS, target_size / 1024,
            (target_size * PT_MAX_PEERS) / 1024, target_size / 4);
    } else {
        /* Partial allocation - free all and fall back to on-demand */
        for (i = 0; i < PT_MAX_PEERS; i++) {
            if (md->prealloced_bufs[i] != NULL) {
                pt_mactcp_free_buffer(md->prealloced_bufs[i]);
                md->prealloced_bufs[i] = NULL;
            }
        }
        md->prealloced_buf_size = 0;
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_MEMORY,
            "Pre-allocation incomplete (%d/%d), using on-demand allocation",
            allocated, PT_MAX_PEERS);
    }
}

/* ========================================================================== */
/* Platform Operations                                                        */
/* ========================================================================== */

static int mactcp_init(struct pt_context *ctx) {
    ParamBlockRec pb;
    OSErr err;

    /**
     * PERFORMANCE OPTIMIZATION: Pre-allocate TCP receive buffers BEFORE
     * opening the MacTCP driver. MacTCP's internal buffer allocation
     * fragments heap memory significantly, reducing MaxBlock from ~300KB
     * to ~10KB. By allocating our buffers first, we can get larger
     * contiguous blocks (16-32KB) which improves receive throughput
     * via the 25% threshold rule.
     */
    pt_mactcp_preallocate_early(ctx);

    /**
     * Open MacTCP driver using PBOpenSync.
     * This works on both System 6 and System 7.
     *
     * Note: The driver stays open even after we "close" it because
     * it's a shared system resource. We just need the refnum.
     */
    pt_memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioNamePtr = (StringPtr)MACTCP_DRIVER_NAME;
    pb.ioParam.ioPermssn = fsCurPerm;

    err = PBOpenSync(&pb);
    if (err != noErr) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_PLATFORM,
            "Failed to open MacTCP driver (.IPP): %d", (int)err);
        /**
         * Common errors:
         * -23 (fnOpnErr): Driver not found - MacTCP not installed
         * Other errors may come from Resource/Device/Slot Manager
         */
        return -1;
    }

    g_mactcp_refnum = pb.ioParam.ioRefNum;
    PT_LOG_INFO(ctx->log, PT_LOG_CAT_INIT,
        "MacTCP driver opened, refnum=%d", (int)g_mactcp_refnum);

    /**
     * Create Universal Procedure Pointers for ASR callbacks.
     * These must be created before any TCP/UDP streams are opened.
     * UPPs wrap the callback function with proper calling convention handling.
     */
    g_tcp_notify_upp = NewTCPNotifyUPP(pt_tcp_asr);
    if (g_tcp_notify_upp == NULL) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_MEMORY,
            "Failed to create TCP notify UPP");
        return -1;
    }

    g_udp_notify_upp = NewUDPNotifyUPP(pt_udp_asr);
    if (g_udp_notify_upp == NULL) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_MEMORY,
            "Failed to create UDP notify UPP");
        DisposeTCPNotifyUPP(g_tcp_notify_upp);
        g_tcp_notify_upp = NULL;
        return -1;
    }

    PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_INIT, "MacTCP UPPs created");

    /* Initialize MacTCP platform data (streams, limits, local IP) */
    if (pt_mactcp_data_init(ctx) < 0) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_INIT,
            "Failed to initialize MacTCP platform data");
        /* Clean up UPPs on failure */
        DisposeTCPNotifyUPP(g_tcp_notify_upp);
        g_tcp_notify_upp = NULL;
        DisposeUDPNotifyUPP(g_udp_notify_upp);
        g_udp_notify_upp = NULL;
        return -1;
    }

    /* Store ASR UPPs in platform data for stream creation.
     *
     * NOTE: We do NOT create completion routine UPPs (tcp_*_completion_upp).
     * Using ioCompletion callbacks causes shutdown crashes because:
     * - TCPAbort cancels pending async operations
     * - Completion routines fire at interrupt time during abort
     * - UPP calls into corrupted or freed memory
     *
     * Instead, we poll pb.ioResult directly in the main loop (csend pattern).
     * This is safer and matches the proven approach from the csend project.
     */
    {
        pt_mactcp_data *md = pt_mactcp_get(ctx);
        md->tcp_notify_upp = g_tcp_notify_upp;
        md->udp_notify_upp = g_udp_notify_upp;

        /* Completion routine UPPs intentionally not created - poll ioResult instead */
        md->tcp_listen_completion_upp = NULL;
        md->tcp_connect_completion_upp = NULL;
        md->tcp_close_completion_upp = NULL;
    }

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_INIT, "MacTCP platform initialized");

    return 0;
}

/* Forward declarations for cleanup functions */
extern void pt_mactcp_discovery_stop(struct pt_context *ctx);
extern void pt_mactcp_tcp_release_all(struct pt_context *ctx);
extern void pt_mactcp_free_preallocated_buffers(struct pt_context *ctx);

static void mactcp_shutdown(struct pt_context *ctx) {
    pt_mactcp_data *md = pt_mactcp_get(ctx);

    /**
     * CRITICAL: Release network streams BEFORE disposing UPPs.
     * This ensures ports are unbound and MacTCP buffers are returned.
     * Order matters: streams use the UPPs, so release streams first.
     *
     * pt_mactcp_tcp_release_all() properly:
     * 1. Aborts all streams (including listener)
     * 2. Waits for pending async operations to complete
     * 3. Releases all streams
     *
     * This follows the LaunchAPPL cleanup pattern from Retro68.
     */
    if (ctx->discovery_active) {
        pt_mactcp_discovery_stop(ctx);
        ctx->discovery_active = 0;
    }

    /* Release ALL TCP streams (listener + peer connections) */
    pt_mactcp_tcp_release_all(ctx);

    /* Free any pre-allocated buffers that are still in the pool */
    pt_mactcp_free_preallocated_buffers(ctx);

    /* Note: Completion routine UPPs were intentionally not created.
     * We poll pb.ioResult directly instead of using callbacks.
     * Nothing to dispose here.
     */
    (void)md;

    /**
     * Dispose of ASR Universal Procedure Pointers.
     * This must be done AFTER all streams using these UPPs are closed.
     */
    if (g_tcp_notify_upp != NULL) {
        DisposeTCPNotifyUPP(g_tcp_notify_upp);
        g_tcp_notify_upp = NULL;
    }
    if (g_udp_notify_upp != NULL) {
        DisposeUDPNotifyUPP(g_udp_notify_upp);
        g_udp_notify_upp = NULL;
    }

    /**
     * We don't actually close the MacTCP driver - it's a shared
     * system resource. Just clear our refnum and log shutdown.
     */
    PT_LOG_INFO(ctx->log, PT_LOG_CAT_INIT, "MacTCP platform shutdown");
    g_mactcp_refnum = 0;
}

/* Forward declarations for poll functions (implemented in poll_mactcp.c) */
extern int pt_mactcp_poll(struct pt_context *ctx);
extern int pt_mactcp_poll_fast(struct pt_context *ctx);

static int mactcp_poll(struct pt_context *ctx) {
    return pt_mactcp_poll(ctx);
}

static int mactcp_poll_fast(struct pt_context *ctx) {
    return pt_mactcp_poll_fast(ctx);
}

static pt_tick_t mactcp_get_ticks(void) {
    /**
     * TickCount() returns ticks since system startup.
     * One tick = 1/60th second (~16.67ms).
     * For timing, we use ticks directly rather than converting to ms.
     *
     * WARNING: TickCount() is NOT listed in Inside Macintosh Volume VI
     * Table B-3 ("Routines That May Be Called at Interrupt Time").
     * This function must ONLY be called from the main event loop
     * (e.g., from pt_platform_ops.poll or PeerTalk_Poll), NEVER from
     * ASR callbacks or completion routines.
     *
     * For ISR timing, use pre-set timestamps or set timestamp=0 and
     * fill in later from the main loop.
     */
    return (pt_tick_t)TickCount();
}

static unsigned long mactcp_get_free_mem(void) {
    return (unsigned long)FreeMem();
}

static unsigned long mactcp_get_max_block(void) {
    return (unsigned long)MaxBlock();
}

/* ========================================================================== */
/* Async Send Pipeline (Session 3 & 4)                                        */
/* ========================================================================== */

/* Forward declarations for core pipeline functions */
extern int pt_pipeline_init(struct pt_context *ctx, struct pt_peer *peer);
extern void pt_pipeline_cleanup(struct pt_context *ctx, struct pt_peer *peer);
extern pt_send_slot *pt_pipeline_get_slot(struct pt_context *ctx, struct pt_peer *peer);

/* Forward declaration for async send (implemented in tcp_io.c) */
extern int pt_mactcp_tcp_send_async(struct pt_context *ctx, struct pt_peer *peer,
                                    const void *data, uint16_t len);

/**
 * Poll for send completions - check ioResult of pending async sends.
 *
 * Per MacTCP Guide (Lines 712-713):
 *   ioResult > 0: still in progress (typically 1)
 *   ioResult == 0: success (noErr)
 *   ioResult < 0: error code
 */
static int mactcp_poll_send_completions(struct pt_context *ctx, struct pt_peer *peer) {
    int i;
    int completions = 0;

    if (!peer || !peer->pipeline.initialized || peer->pipeline.pending_count == 0) {
        return 0;
    }

    for (i = 0; i < PT_SEND_PIPELINE_DEPTH; i++) {
        pt_send_slot *slot = &peer->pipeline.slots[i];
        TCPiopb *pb;

        if (!slot->in_use) continue;

        pb = (TCPiopb *)slot->platform_data;
        if (!pb) continue;

        /* Cache ioResult for polling efficiency */
        slot->ioResult = pb->ioResult;

        if (slot->ioResult <= 0) {
            /* Completed (success or error) */
            slot->in_use = 0;
            slot->completed = 1;
            if (peer->pipeline.pending_count > 0) {
                peer->pipeline.pending_count--;
            }
            completions++;

            if (slot->ioResult != noErr) {
                PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                    "Async send slot %d error: %d", i, (int)slot->ioResult);
            }
        }
    }

    return completions;
}

static int mactcp_send_slots_available(struct pt_context *ctx, struct pt_peer *peer) {
    (void)ctx;
    if (!peer || !peer->pipeline.initialized) {
        return 0;
    }
    return PT_SEND_PIPELINE_DEPTH - peer->pipeline.pending_count;
}

static int mactcp_pipeline_init(struct pt_context *ctx, struct pt_peer *peer) {
    int i;
    int err;

    /* Allocate core buffers first */
    err = pt_pipeline_init(ctx, peer);
    if (err != PT_OK) {
        return err;
    }

    /* Allocate TCPiopb for each slot */
    for (i = 0; i < PT_SEND_PIPELINE_DEPTH; i++) {
        pt_send_slot *slot = &peer->pipeline.slots[i];

        slot->platform_data = (void *)NewPtrClear(sizeof(TCPiopb));
        if (slot->platform_data == NULL) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_MEMORY,
                "Failed to allocate TCPiopb for pipeline slot %d", i);
            /* Cleanup already allocated slots */
            while (--i >= 0) {
                if (peer->pipeline.slots[i].platform_data) {
                    DisposePtr((Ptr)peer->pipeline.slots[i].platform_data);
                    peer->pipeline.slots[i].platform_data = NULL;
                }
            }
            pt_pipeline_cleanup(ctx, peer);
            return PT_ERR_NO_MEMORY;
        }
    }

    PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_MEMORY,
        "Pipeline TCPiopb allocated: peer=%u depth=%d",
        peer->hot.id, PT_SEND_PIPELINE_DEPTH);

    return PT_OK;
}

static void mactcp_pipeline_cleanup(struct pt_context *ctx, struct pt_peer *peer) {
    int i;

    if (!peer) return;

    /* Free TCPiopb for each slot */
    for (i = 0; i < PT_SEND_PIPELINE_DEPTH; i++) {
        pt_send_slot *slot = &peer->pipeline.slots[i];
        if (slot->platform_data) {
            DisposePtr((Ptr)slot->platform_data);
            slot->platform_data = NULL;
        }
    }

    /* Free core buffers */
    pt_pipeline_cleanup(ctx, peer);
}

/* Platform operations structure */
pt_platform_ops pt_mactcp_ops = {
    mactcp_init,
    mactcp_shutdown,
    mactcp_poll,
    mactcp_poll_fast,
    mactcp_get_ticks,
    mactcp_get_free_mem,
    mactcp_get_max_block,
    NULL,  /* send_udp - set by Phase 5 to pt_mactcp_send_udp */
    /* Async send pipeline ops */
    pt_mactcp_tcp_send_async,  /* Implemented in tcp_io.c */
    mactcp_poll_send_completions,
    mactcp_send_slots_available,
    mactcp_pipeline_init,
    mactcp_pipeline_cleanup
};

/* ========================================================================== */
/* Accessor Functions                                                         */
/* ========================================================================== */

/**
 * Get MacTCP driver reference number.
 * Used by TCP/UDP implementation in Phase 5.
 */
short pt_mactcp_get_refnum(void) {
    return g_mactcp_refnum;
}

/**
 * Get TCP notify UPP.
 * Used when creating TCP streams in Phase 5.
 */
TCPNotifyUPP pt_mactcp_get_tcp_upp(void) {
    return g_tcp_notify_upp;
}

/**
 * Get UDP notify UPP.
 * Used when creating UDP streams in Phase 5.
 */
UDPNotifyUPP pt_mactcp_get_udp_upp(void) {
    return g_udp_notify_upp;
}

/* ========================================================================== */
/* Platform-Specific Allocation                                               */
/* ========================================================================== */

void *pt_plat_alloc(size_t size) {
    return (void *)NewPtr((Size)size);
}

void pt_plat_free(void *ptr) {
    if (ptr != NULL) {
        DisposePtr((Ptr)ptr);
    }
}

size_t pt_plat_extra_size(void) {
    /**
     * MacTCP platform needs extra space in the context for:
     * - TCP/UDP stream handles and state
     * - Local IP and network info
     * - Buffer management
     *
     * The pt_mactcp_data struct is defined in mactcp_defs.h
     */
    return pt_mactcp_extra_size();
}

#endif /* PT_PLATFORM_MACTCP */
