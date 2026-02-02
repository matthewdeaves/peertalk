/**
 * @file pt_init.c
 * @brief PeerTalk Initialization and Shutdown
 *
 * Lifecycle management for PeerTalk context, including PT_Log integration.
 */

#include "pt_internal.h"
#include "pt_compat.h"
#include "peer.h"
#include <string.h>

/* ========================================================================== */
/* PeerTalk_Init - Initialize PeerTalk Context                                */
/* ========================================================================== */

PeerTalk_Context *PeerTalk_Init(const PeerTalk_Config *config) {
    struct pt_context *ctx;
    size_t ctx_size;
    size_t extra_size;

    /* Validate configuration */
    if (!config) {
        return NULL;
    }

    /* Validate local name is not empty */
    if (!config->local_name[0]) {
        return NULL;
    }

    /* Calculate allocation size: context + platform-specific data */
    extra_size = pt_plat_extra_size();
    ctx_size = sizeof(struct pt_context) + extra_size;

    /* Allocate context using platform-specific allocator */
    ctx = (struct pt_context *)pt_plat_alloc(ctx_size);
    if (!ctx) {
        return NULL;
    }

    /* Zero the entire context */
    pt_memset(ctx, 0, ctx_size);

    /* Set magic number for validation */
    ctx->magic = PT_CONTEXT_MAGIC;

    /* Copy configuration */
    pt_memcpy(&ctx->config, config, sizeof(PeerTalk_Config));

    /* Apply defaults for zero values */
    if (ctx->config.transports == 0) {
        ctx->config.transports = PT_TRANSPORT_ALL;
    }
    if (ctx->config.discovery_port == 0) {
        ctx->config.discovery_port = PT_DEFAULT_DISCOVERY_PORT;
    }
    if (ctx->config.tcp_port == 0) {
        ctx->config.tcp_port = PT_DEFAULT_TCP_PORT;
    }
    if (ctx->config.udp_port == 0) {
        ctx->config.udp_port = PT_DEFAULT_UDP_PORT;
    }
    if (ctx->config.max_peers == 0) {
        ctx->config.max_peers = PT_MAX_PEERS;
    }
    if (ctx->config.discovery_interval == 0) {
        ctx->config.discovery_interval = 5000;  /* 5 seconds */
    }
    if (ctx->config.peer_timeout == 0) {
        ctx->config.peer_timeout = 15000;  /* 15 seconds */
    }

    /* Initialize PT_Log from Phase 0 */
    ctx->log = PT_LogCreate();
    if (ctx->log) {
        /* Configure logging based on user settings */
        if (config->log_level > 0) {
            PT_LogSetLevel(ctx->log, (PT_LogLevel)config->log_level);
        }
        PT_LogSetCategories(ctx->log, PT_LOG_CAT_ALL);
        PT_LogSetOutput(ctx->log, PT_LOG_OUT_CONSOLE);
    }

    /* Initialize local peer info */
    ctx->local_info.id = 0;  /* Self is always ID 0 */
    ctx->local_info.address = 0;  /* Filled in by platform layer */
    ctx->local_info.port = ctx->config.tcp_port;
    ctx->local_info.transports_available = ctx->config.transports;
    ctx->local_info.transport_connected = 0;
    ctx->local_info.name_idx = 0xFF;  /* No name index yet */

    /* Initialize peer management */
    ctx->max_peers = ctx->config.max_peers;
    ctx->peer_count = 0;
    ctx->next_peer_id = 1;  /* Start from 1 (0 is reserved for self) */

    /* Initialize peer ID lookup table (0xFF = invalid) */
    pt_memset(ctx->peer_id_to_index, 0xFF,
              sizeof(ctx->peer_id_to_index));

    /* Allocate peer list */
    if (pt_peer_list_init(ctx, ctx->max_peers) != 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT,
                  "Failed to initialize peer list");
        if (ctx->log) {
            PT_LogDestroy(ctx->log);
        }
        pt_plat_free(ctx);
        return NULL;
    }

    /* Select platform operations based on compile-time platform */
#if defined(PT_PLATFORM_POSIX)
    ctx->plat = &pt_posix_ops;
#elif defined(PT_PLATFORM_MACTCP)
    ctx->plat = &pt_mactcp_ops;
#elif defined(PT_PLATFORM_OT)
    ctx->plat = &pt_ot_ops;
#else
    #error "Unknown platform"
#endif

    /* Initialize platform-specific layer */
    if (ctx->plat->init) {
        if (ctx->plat->init(ctx) != 0) {
            /* Platform init failed */
            if (ctx->log) {
                PT_LogDestroy(ctx->log);
            }
            pt_plat_free(ctx);
            return NULL;
        }
    }

    /* Log successful initialization */
    PT_CTX_INFO(ctx, PT_LOG_CAT_INIT,
        "PeerTalk v%s initialized: name='%s' transports=0x%04X",
        PeerTalk_Version(), ctx->config.local_name, ctx->config.transports);

    ctx->initialized = 1;

    /* Return opaque handle */
    return (PeerTalk_Context *)ctx;
}

/* ========================================================================== */
/* PeerTalk_Shutdown - Clean Up and Free Resources                           */
/* ========================================================================== */

void PeerTalk_Shutdown(PeerTalk_Context *ctx_handle) {
    struct pt_context *ctx = (struct pt_context *)ctx_handle;

    /* Validate context */
    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return;
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_INIT, "PeerTalk shutting down");

    /* Mark as not initialized */
    ctx->initialized = 0;

    /* Shutdown platform-specific layer */
    if (ctx->plat && ctx->plat->shutdown) {
        ctx->plat->shutdown(ctx);
    }

    /* Destroy PT_Log context last (need it for shutdown logging) */
    if (ctx->log) {
        PT_LogFlush(ctx->log);
        PT_LogDestroy(ctx->log);
        ctx->log = NULL;
    }

    /* Clear magic number */
    ctx->magic = 0;

    /* Free context memory */
    pt_plat_free(ctx);
}

/* ========================================================================== */
/* PeerTalk_Poll - Main Event Loop (Stub for Phase 4+)                       */
/* ========================================================================== */

PeerTalk_Error PeerTalk_Poll(PeerTalk_Context *ctx_handle) {
    struct pt_context *ctx = (struct pt_context *)ctx_handle;

    /* Validate context */
    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Call platform-specific poll */
    if (ctx->plat && ctx->plat->poll) {
        if (ctx->plat->poll(ctx) != 0) {
            return PT_ERR_NETWORK;
        }
    }

    return PT_OK;
}

/* ========================================================================== */
/* PeerTalk_SetCallbacks - Register Callbacks (Stub for Phase 4+)            */
/* ========================================================================== */

PeerTalk_Error PeerTalk_SetCallbacks(PeerTalk_Context *ctx_handle,
                                     const PeerTalk_Callbacks *callbacks) {
    struct pt_context *ctx = (struct pt_context *)ctx_handle;

    /* Validate context */
    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

    if (!callbacks) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Copy callbacks */
    pt_memcpy(&ctx->callbacks, callbacks, sizeof(PeerTalk_Callbacks));

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_INIT, "Callbacks registered");

    return PT_OK;
}


/* ========================================================================== */
/* Peer Name Table Access (Stub for Phase 2+)                                */
/* ========================================================================== */

const char *pt_get_peer_name(struct pt_context *ctx, uint8_t name_idx) {
    /* Stub - full implementation in Phase 2+ */
    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return "";
    }

    if (name_idx >= PT_MAX_PEERS) {
        return "";
    }

    return ctx->peer_names[name_idx];
}

/* ========================================================================== */
/* Discovery Control (Phase 4)                                               */
/* ========================================================================== */

#if defined(PT_PLATFORM_POSIX)
/* Forward declarations from net_posix.h */
extern int pt_posix_discovery_start(struct pt_context *ctx);
extern void pt_posix_discovery_stop(struct pt_context *ctx);
#endif

PeerTalk_Error PeerTalk_StartDiscovery(PeerTalk_Context *ctx_public) {
    struct pt_context *ctx = (struct pt_context *)ctx_public;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

#if defined(PT_PLATFORM_POSIX)
    if (pt_posix_discovery_start(ctx) < 0) {
        return PT_ERR_NETWORK;
    }
#else
    /* TODO: Mac platform discovery */
    (void)ctx;
    return PT_ERR_NOT_SUPPORTED;
#endif

    PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY, "Discovery started");
    return PT_OK;
}

PeerTalk_Error PeerTalk_StopDiscovery(PeerTalk_Context *ctx_public) {
    struct pt_context *ctx = (struct pt_context *)ctx_public;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

#if defined(PT_PLATFORM_POSIX)
    pt_posix_discovery_stop(ctx);
#else
    /* TODO: Mac platform discovery */
    (void)ctx;
#endif

    PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY, "Discovery stopped");
    return PT_OK;
}
