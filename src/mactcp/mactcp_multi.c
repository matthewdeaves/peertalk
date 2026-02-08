/**
 * @file mactcp_multi.c
 * @brief Multi-Transport Implementation for MacTCP + AppleTalk
 *
 * Implements unified transport layer for Macs with both TCP/IP and AppleTalk.
 * Works in MacTCP-only mode when AppleTalk (Phase 7) is not linked.
 *
 * Session 5.9 of Phase 5 (MacTCP).
 */

#include "mactcp_multi.h"
#include "mactcp_defs.h"
#include "peer.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_MACTCP)

#include <OSUtils.h>

/* ========================================================================== */
/* External Functions                                                          */
/* ========================================================================== */

/* From mactcp_driver.c */
extern int pt_mactcp_init(struct pt_context *ctx);
extern void pt_mactcp_shutdown(struct pt_context *ctx);

/* From poll_mactcp.c */
extern int pt_mactcp_poll(struct pt_context *ctx);

/* ========================================================================== */
/* AppleTalk Weak Linking                                                      */
/* ========================================================================== */

/**
 * AppleTalk callback pointers.
 * Set by pt_mactcp_multi_register_appletalk() if Phase 7 is linked.
 * Remain NULL if Phase 7 is not linked.
 */
static pt_at_init_fn     g_at_init_fn = NULL;
static pt_at_shutdown_fn g_at_shutdown_fn = NULL;
static pt_at_poll_fn     g_at_poll_fn = NULL;

void pt_mactcp_multi_register_appletalk(
    pt_at_init_fn init_fn,
    pt_at_shutdown_fn shutdown_fn,
    pt_at_poll_fn poll_fn)
{
    g_at_init_fn = init_fn;
    g_at_shutdown_fn = shutdown_fn;
    g_at_poll_fn = poll_fn;
}

/* ========================================================================== */
/* Accessor Functions                                                          */
/* ========================================================================== */

pt_mactcp_multi_data *pt_mactcp_multi_get(struct pt_context *ctx)
{
    if (ctx == NULL)
        return NULL;

    /* Platform data is allocated immediately after pt_context */
    return (pt_mactcp_multi_data *)((char *)ctx + sizeof(struct pt_context));
}

/* ========================================================================== */
/* Initialization / Shutdown                                                   */
/* ========================================================================== */

int pt_mactcp_multi_init(struct pt_context *ctx)
{
    pt_mactcp_multi_data *md;
    int mactcp_ok = 0;
    int appletalk_ok = 0;

    if (ctx == NULL)
        return -1;

    md = pt_mactcp_multi_get(ctx);

    /* Clear multi-transport state */
    md->appletalk = NULL;
    md->transports_available = PT_TRANSPORT_NONE;
    md->transports_active = PT_TRANSPORT_NONE;
    md->preferred_transport = PT_PREFER_TCP;
    md->appletalk_linked = (g_at_init_fn != NULL) ? 1 : 0;

    /* Initialize MacTCP first */
    if (pt_mactcp_init(ctx) == 0) {
        md->transports_available |= (PT_TRANSPORT_TCP | PT_TRANSPORT_UDP);
        mactcp_ok = 1;

        PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK,
            "MacTCP initialized successfully");
    } else {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "MacTCP initialization failed");
    }

    /* Try AppleTalk if Phase 7 is linked */
    if (g_at_init_fn != NULL) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
            "AppleTalk support linked, attempting init...");

        if (g_at_init_fn(ctx, &md->appletalk) == 0) {
            md->transports_available |= PT_TRANSPORT_APPLETALK;
            appletalk_ok = 1;

            PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK,
                "AppleTalk initialized successfully");
        } else {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                "AppleTalk initialization failed (continuing with MacTCP only)");
        }
    } else {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
            "AppleTalk support not linked (MacTCP-only mode)");
    }

    /* At least one transport must succeed */
    if (!mactcp_ok && !appletalk_ok) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_NETWORK,
            "All transports failed to initialize");
        return -1;
    }

    /* Log available transports */
    PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK,
        "Transports available: TCP=%d UDP=%d AppleTalk=%d",
        (md->transports_available & PT_TRANSPORT_TCP) ? 1 : 0,
        (md->transports_available & PT_TRANSPORT_UDP) ? 1 : 0,
        (md->transports_available & PT_TRANSPORT_APPLETALK) ? 1 : 0);

    return 0;
}

void pt_mactcp_multi_shutdown(struct pt_context *ctx)
{
    pt_mactcp_multi_data *md;

    if (ctx == NULL)
        return;

    md = pt_mactcp_multi_get(ctx);

    /* Shutdown AppleTalk first (if active) */
    if (md->appletalk != NULL && g_at_shutdown_fn != NULL) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK, "Shutting down AppleTalk...");
        g_at_shutdown_fn(ctx, md->appletalk);
        md->appletalk = NULL;
    }

    /* Shutdown MacTCP */
    if (md->transports_available & (PT_TRANSPORT_TCP | PT_TRANSPORT_UDP)) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK, "Shutting down MacTCP...");
        pt_mactcp_shutdown(ctx);
    }

    md->transports_available = PT_TRANSPORT_NONE;
    md->transports_active = PT_TRANSPORT_NONE;
}

/* ========================================================================== */
/* Unified Poll                                                                */
/* ========================================================================== */

int pt_mactcp_multi_poll(struct pt_context *ctx)
{
    pt_mactcp_multi_data *md;

    if (ctx == NULL)
        return -1;

    md = pt_mactcp_multi_get(ctx);

    /* Poll MacTCP (TCP/IP and UDP) */
    if (md->transports_available & (PT_TRANSPORT_TCP | PT_TRANSPORT_UDP)) {
        pt_mactcp_poll(ctx);
    }

    /* Poll AppleTalk (if active) */
    if (md->appletalk != NULL && g_at_poll_fn != NULL) {
        g_at_poll_fn(ctx, md->appletalk);
    }

    return 0;
}

/* ========================================================================== */
/* Transport Query                                                             */
/* ========================================================================== */

uint8_t pt_mactcp_multi_get_transports(struct pt_context *ctx)
{
    pt_mactcp_multi_data *md;

    if (ctx == NULL)
        return PT_TRANSPORT_NONE;

    md = pt_mactcp_multi_get(ctx);
    return md->transports_available;
}

int pt_mactcp_multi_has_transport(struct pt_context *ctx, uint8_t transport)
{
    pt_mactcp_multi_data *md;

    if (ctx == NULL)
        return 0;

    md = pt_mactcp_multi_get(ctx);
    return (md->transports_available & transport) != 0;
}

/* ========================================================================== */
/* Peer Deduplication                                                          */
/* ========================================================================== */

struct pt_peer *pt_mactcp_multi_find_or_create_peer(
    struct pt_context *ctx,
    const char *name,
    ip_addr tcp_ip,
    tcp_port tcp_port,
    uint8_t transport_flags)
{
    struct pt_peer *peer;
    unsigned long now;

    if (ctx == NULL || name == NULL)
        return NULL;

    /* Get current time (Ticks is low-memory global, safe to read) */
    now = (unsigned long)TickCount();

    /*
     * Cross-transport deduplication:
     * Look for existing peer by name first.
     */
    peer = pt_peer_find_by_name(ctx, name);

    if (peer != NULL) {
        /* Existing peer - add new transport */
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_CONNECT,
            "Peer \"%s\" already known, adding transport 0x%02X",
            name, transport_flags);

        pt_mactcp_multi_peer_add_transport(peer, transport_flags);

        /* Update TCP address if this is a TCP discovery */
        if ((transport_flags & PT_TRANSPORT_TCP) && tcp_ip != 0) {
            peer->cold.info.address = tcp_ip;
            peer->cold.info.port = tcp_port;
        }

        /* Update last seen */
        peer->hot.last_seen = (pt_tick_t)now;

        return peer;
    }

    /* New peer - create with initial transport */
    if (transport_flags & PT_TRANSPORT_TCP) {
        peer = pt_peer_create(ctx, name, tcp_ip, tcp_port);
    } else {
        /* AppleTalk-only peer - no IP address */
        peer = pt_peer_create(ctx, name, 0, 0);
    }

    if (peer != NULL) {
        /* Set initial transport availability */
        peer->cold.info.transports_available = transport_flags;

        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_CONNECT,
            "Created new peer \"%s\" with transport 0x%02X",
            name, transport_flags);
    }

    return peer;
}

void pt_mactcp_multi_peer_add_transport(struct pt_peer *peer,
                                         uint8_t transport_flag)
{
    if (peer == NULL)
        return;

    peer->cold.info.transports_available |= transport_flag;
}

void pt_mactcp_multi_peer_remove_transport(struct pt_peer *peer,
                                            uint8_t transport_flag)
{
    if (peer == NULL)
        return;

    peer->cold.info.transports_available &= ~transport_flag;
}

uint8_t pt_mactcp_multi_peer_get_transports(struct pt_peer *peer)
{
    if (peer == NULL)
        return PT_TRANSPORT_NONE;

    return peer->cold.info.transports_available;
}

#endif /* PT_PLATFORM_MACTCP */
