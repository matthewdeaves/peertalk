/**
 * @file platform_ot.c
 * @brief PeerTalk Open Transport Platform Implementation
 *
 * Platform abstraction for System 7.6.1+ and Mac OS 8/9 on PowerPC
 * (and late 68040).
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 2: "Getting Started"
 * - Open Transport headers: OpenTransport.h, OpenTptInternet.h
 */

#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_OT)

#include <OpenTransport.h>
#include <OpenTptInternet.h>
#include <MacMemory.h>
#include <Gestalt.h>
#include <OSUtils.h>

/* ========================================================================== */
/* Open Transport State                                                       */
/* ========================================================================== */

/* Track OT initialization state */
static int g_ot_initialized = 0;

/* ========================================================================== */
/* Platform Operations                                                        */
/* ========================================================================== */

static int ot_init(struct pt_context *ctx) {
    OSStatus err;

    /**
     * Open Transport Initialization
     *
     * From Networking With Open Transport:
     * "You do not need to call Gestalt to determine whether Open Transport
     * is available. Simply call InitOpenTransport. If it returns noErr,
     * Open Transport is available; otherwise, it is not."
     *
     * Note: InitOpenTransportInContext() is Carbon-only (CarbonLib 1.0+).
     * For classic Mac OS 7.6.1-9 compatibility, use InitOpenTransport().
     */
    err = InitOpenTransport();
    if (err != noErr) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_GENERAL,
            "InitOpenTransport failed: %ld", (long)err);
        /**
         * Common failures:
         * - kOTNotFoundErr: TCP/IP not configured
         * - kENOMEMErr: Out of memory
         * - Various other OT errors
         */
        return -1;
    }

    g_ot_initialized = 1;
    PT_LOG_INFO(ctx->log, PT_LOG_CAT_GENERAL, "Open Transport initialized");

    /**
     * Optionally verify TCP/IP is available via Gestalt.
     * This is informational - we already know OT is present since
     * InitOpenTransport succeeded.
     */
    {
        long response = 0;
        err = Gestalt(gestaltOpenTpt, &response);
        if (err == noErr) {
            PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_GENERAL,
                "OT Gestalt response: 0x%08lX", response);
            if (response & gestaltOpenTptTCPPresentMask) {
                PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_GENERAL, "TCP/IP is present");
            }
        }
    }

    return 0;
}

static void ot_shutdown(struct pt_context *ctx) {
    if (g_ot_initialized) {
        CloseOpenTransport();
        g_ot_initialized = 0;
        PT_LOG_INFO(ctx->log, PT_LOG_CAT_GENERAL, "Open Transport closed");
    }
}

static int ot_poll(struct pt_context *ctx) {
    /* Stub - implemented in Phase 6 (Open Transport Networking) */
    (void)ctx;
    return 0;
}

static pt_tick_t ot_get_ticks(void) {
    /**
     * TickCount() works on PPC too.
     * One tick = 1/60th second (~16.67ms).
     *
     * WARNING: TickCount() is NOT safe at interrupt time.
     * This function must ONLY be called from the main event loop.
     *
     * For notifier callbacks, use OTGetTimeStamp() and
     * OTElapsedMilliseconds() instead - these ARE listed in
     * Table C-1 of Networking With Open Transport as callable
     * from notifiers.
     */
    return (pt_tick_t)TickCount();
}

static unsigned long ot_get_free_mem(void) {
    return (unsigned long)FreeMem();
}

static unsigned long ot_get_max_block(void) {
    return (unsigned long)MaxBlock();
}

/* Platform operations structure */
pt_platform_ops pt_ot_ops = {
    ot_init,
    ot_shutdown,
    ot_poll,
    ot_get_ticks,
    ot_get_free_mem,
    ot_get_max_block,
    NULL  /* send_udp - set by Phase 6 to pt_ot_send_udp */
};

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
     * Open Transport platform needs extra space in the context for:
     * - Endpoint references and state (allocated in Phase 6)
     * For now, return 0 - will be updated when networking is implemented.
     */
    return 0;
}

#endif /* PT_PLATFORM_OT */
