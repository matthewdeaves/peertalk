/**
 * @file platform_ot.c
 * @brief PeerTalk Open Transport Platform Implementation
 *
 * Platform abstraction for System 7.6.1+ and Mac OS 8/9 on PowerPC
 * (and late 68040).
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 2: "Getting Started"
 * - OpenTransport.h, OpenTransportProviders.h (Retro68)
 */

#include "ot_defs.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_OT)

#include <MacMemory.h>
#include <Gestalt.h>
#include <OSUtils.h>

/* ========================================================================== */
/* External Functions (from ot_driver.c)                                       */
/* ========================================================================== */

extern int pt_ot_driver_init(struct pt_context *ctx);
extern void pt_ot_driver_shutdown(struct pt_context *ctx);
extern size_t pt_ot_extra_size(void);

/* Notifier callbacks (defined in udp_ot.c and tcp_ot.c) */
extern pascal void pt_ot_udp_notifier(void *context, OTEventCode code,
                                       OTResult result, void *cookie);
extern pascal void pt_ot_tcp_notifier(void *context, OTEventCode code,
                                       OTResult result, void *cookie);

/* Poll functions (defined in poll_ot.c) */
extern int pt_ot_poll(struct pt_context *ctx);
extern int pt_ot_poll_fast(struct pt_context *ctx);

/* Async send (defined in poll_ot.c) */
extern int pt_ot_tcp_send_async(struct pt_context *ctx, struct pt_peer *peer,
                                  const void *data, uint16_t len, uint8_t flags);

/* ========================================================================== */
/* Open Transport State                                                       */
/* ========================================================================== */

static int g_ot_initialized = 0;

/* ========================================================================== */
/* Platform Operations                                                        */
/* ========================================================================== */

static int ot_init(struct pt_context *ctx)
{
    OSStatus err;

    /*
     * Open Transport Initialization
     *
     * From Networking With Open Transport:
     * "You do not need to call Gestalt to determine whether Open Transport
     * is available. Simply call InitOpenTransport. If it returns noErr,
     * Open Transport is available; otherwise, it is not."
     *
     * Note: InitOpenTransport() is a macro that expands to
     * InitOpenTransportInContext(kInitOTForApplicationMask, NULL)
     * in the Retro68 headers.
     */
    err = InitOpenTransport();
    if (err != noErr) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT,
            "InitOpenTransport failed: %ld", (long)err);
        return -1;
    }

    g_ot_initialized = 1;
    PT_CTX_INFO(ctx, PT_LOG_CAT_INIT, "Open Transport initialized");

    /* Verify TCP/IP availability via Gestalt (informational) */
    {
        long response = 0;
        err = Gestalt(gestaltOpenTpt, &response);
        if (err == noErr) {
            PT_CTX_DEBUG(ctx, PT_LOG_CAT_PLATFORM,
                "OT Gestalt response: 0x%08lX", response);
            if (response & gestaltOpenTptTCPPresentMask) {
                PT_CTX_DEBUG(ctx, PT_LOG_CAT_PLATFORM, "TCP/IP is present");
            }
        }
    }

    /* Initialize driver layer (types, cold data, configs, IP) */
    if (pt_ot_driver_init(ctx) != 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT, "OT driver init failed");
        CloseOpenTransport();
        g_ot_initialized = 0;
        return -1;
    }

    /* Create UPPs for notifier callbacks.
     * CRITICAL: Must be done AFTER driver init (which zeros od).
     * UPPs are disposed in ot_shutdown AFTER all endpoints closed. */
    {
        pt_ot_data *od = pt_ot_get(ctx);

        od->udp_notifier_upp = NewOTNotifyUPP(pt_ot_udp_notifier);
        if (od->udp_notifier_upp == NULL) {
            PT_CTX_ERR(ctx, PT_LOG_CAT_INIT,
                "Failed to create UDP notifier UPP");
            pt_ot_driver_shutdown(ctx);
            CloseOpenTransport();
            g_ot_initialized = 0;
            return -1;
        }

        od->tcp_notifier_upp = NewOTNotifyUPP(pt_ot_tcp_notifier);
        if (od->tcp_notifier_upp == NULL) {
            PT_CTX_ERR(ctx, PT_LOG_CAT_INIT,
                "Failed to create TCP notifier UPP");
            DisposeOTNotifyUPP(od->udp_notifier_upp);
            od->udp_notifier_upp = NULL;
            pt_ot_driver_shutdown(ctx);
            CloseOpenTransport();
            g_ot_initialized = 0;
            return -1;
        }

        PT_CTX_DEBUG(ctx, PT_LOG_CAT_INIT, "OT notifier UPPs created");
    }

    return 0;
}

static void ot_shutdown(struct pt_context *ctx)
{
    if (g_ot_initialized) {
        /* Shut down driver layer (endpoints, configs, cold data) */
        pt_ot_driver_shutdown(ctx);

        /* Dispose UPPs AFTER driver shutdown closed all endpoints */
        {
            pt_ot_data *od = pt_ot_get(ctx);
            if (od->tcp_notifier_upp != NULL) {
                DisposeOTNotifyUPP(od->tcp_notifier_upp);
                od->tcp_notifier_upp = NULL;
            }
            if (od->udp_notifier_upp != NULL) {
                DisposeOTNotifyUPP(od->udp_notifier_upp);
                od->udp_notifier_upp = NULL;
            }
        }

        CloseOpenTransport();
        g_ot_initialized = 0;
        PT_CTX_INFO(ctx, PT_LOG_CAT_INIT, "Open Transport closed");
    }
}

static int ot_poll(struct pt_context *ctx)
{
    return pt_ot_poll(ctx);
}

static int ot_poll_fast(struct pt_context *ctx)
{
    return pt_ot_poll_fast(ctx);
}

static pt_tick_t ot_get_ticks(void)
{
    /*
     * TickCount() works on PPC too. One tick = 1/60th second (~16.67ms).
     *
     * WARNING: TickCount() is NOT safe at interrupt time.
     * For notifier callbacks, use OTGetTimeStamp() /
     * OTElapsedMilliseconds() instead (Table C-1 safe).
     */
    return (pt_tick_t)TickCount();
}

static unsigned long ot_get_free_mem(void)
{
    return (unsigned long)FreeMem();
}

static unsigned long ot_get_max_block(void)
{
    return (unsigned long)MaxBlock();
}

/**
 * OT pipeline init - lightweight version.
 *
 * OT's OTSnd copies data to internal buffers immediately, so we don't
 * need the MacTCP-style pipeline slot buffers. Just set the initialized
 * flag so PeerTalk_SendEx uses the async send path.
 */
static int ot_pipeline_init(struct pt_context *ctx, struct pt_peer *peer)
{
    if (!ctx || !peer)
        return PT_ERR_INVALID_PARAM;

    if (peer->pipeline.initialized)
        return PT_OK;

    peer->pipeline.initialized = 1;
    peer->pipeline.pending_count = 0;

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_MEMORY,
        "OT pipeline init: peer=%u (no slot buffers needed)",
        (unsigned)peer->hot.id);

    return PT_OK;
}

/**
 * OT pipeline cleanup - reset flags.
 */
static void ot_pipeline_cleanup(struct pt_context *ctx, struct pt_peer *peer)
{
    if (!ctx || !peer)
        return;

    peer->pipeline.initialized = 0;
    peer->pipeline.pending_count = 0;
}

/* Platform operations structure */
pt_platform_ops pt_ot_ops = {
    ot_init,
    ot_shutdown,
    ot_poll,
    ot_poll_fast,
    ot_get_ticks,
    ot_get_free_mem,
    ot_get_max_block,
    NULL,                    /* send_udp - implemented in Session 6.2 */
    pt_ot_tcp_send_async,    /* tcp_send_async */
    NULL,                    /* poll_send_completions (OTSnd is synchronous) */
    NULL,                    /* send_slots_available (no slot tracking needed) */
    ot_pipeline_init,        /* pipeline_init */
    ot_pipeline_cleanup      /* pipeline_cleanup */
};

/* ========================================================================== */
/* Platform-Specific Allocation                                               */
/* ========================================================================== */

void *pt_plat_alloc(size_t size)
{
    return (void *)NewPtr((Size)size);
}

void pt_plat_free(void *ptr)
{
    if (ptr != NULL)
        DisposePtr((Ptr)ptr);
}

size_t pt_plat_extra_size(void)
{
    return pt_ot_extra_size();
}

#endif /* PT_PLATFORM_OT */
