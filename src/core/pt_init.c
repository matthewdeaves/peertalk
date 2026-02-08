/**
 * @file pt_init.c
 * @brief PeerTalk Initialization and Shutdown
 *
 * Lifecycle management for PeerTalk context, including PT_Log integration.
 */

#include "pt_internal.h"
#include "pt_compat.h"
#include "peer.h"
#include "queue.h"
#include "direct_buffer.h"
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
    if (ctx->config.direct_buffer_size == 0) {
        ctx->config.direct_buffer_size = PT_DIRECT_DEFAULT_SIZE;
    }

    /* Apply two-tier queue configuration */
    ctx->direct_threshold = PT_DIRECT_THRESHOLD;
    ctx->direct_buffer_size = ctx->config.direct_buffer_size;

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
#elif defined(PT_PLATFORM_MACTCP)
/* Forward declarations from discovery_mactcp.c */
extern int pt_mactcp_discovery_start(struct pt_context *ctx);
extern void pt_mactcp_discovery_stop(struct pt_context *ctx);
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
#elif defined(PT_PLATFORM_MACTCP)
    if (pt_mactcp_discovery_start(ctx) < 0) {
        return PT_ERR_NETWORK;
    }
#else
    (void)ctx;
    return PT_ERR_NOT_SUPPORTED;
#endif

    ctx->discovery_active = 1;
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
#elif defined(PT_PLATFORM_MACTCP)
    pt_mactcp_discovery_stop(ctx);
#else
    (void)ctx;
#endif

    ctx->discovery_active = 0;
    PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY, "Discovery stopped");
    return PT_OK;
}

/* ========================================================================== */
/* Connection Control (Phase 4 Session 4.2)                                  */
/* ========================================================================== */

#if defined(PT_PLATFORM_POSIX)
/* Forward declarations from net_posix.h */
extern int pt_posix_listen_start(struct pt_context *ctx);
extern void pt_posix_listen_stop(struct pt_context *ctx);
extern int pt_posix_connect(struct pt_context *ctx, struct pt_peer *peer);
extern int pt_posix_disconnect(struct pt_context *ctx, struct pt_peer *peer);
#elif defined(PT_PLATFORM_MACTCP)
/* Forward declarations from tcp_listen.c, tcp_connect.c */
extern int pt_mactcp_listen_start(struct pt_context *ctx);
extern void pt_mactcp_listen_stop(struct pt_context *ctx);
extern int pt_mactcp_connect(struct pt_context *ctx, struct pt_peer *peer);
extern int pt_mactcp_disconnect(struct pt_context *ctx, struct pt_peer *peer);
#endif

PeerTalk_Error PeerTalk_StartListening(PeerTalk_Context *ctx_public) {
    struct pt_context *ctx = (struct pt_context *)ctx_public;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

#if defined(PT_PLATFORM_POSIX)
    if (pt_posix_listen_start(ctx) < 0) {
        return PT_ERR_NETWORK;
    }
#elif defined(PT_PLATFORM_MACTCP)
    if (pt_mactcp_listen_start(ctx) < 0) {
        return PT_ERR_NETWORK;
    }
#else
    (void)ctx;
    return PT_ERR_NOT_SUPPORTED;
#endif

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT, "TCP listening started");
    return PT_OK;
}

PeerTalk_Error PeerTalk_StopListening(PeerTalk_Context *ctx_public) {
    struct pt_context *ctx = (struct pt_context *)ctx_public;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

#if defined(PT_PLATFORM_POSIX)
    pt_posix_listen_stop(ctx);
#elif defined(PT_PLATFORM_MACTCP)
    pt_mactcp_listen_stop(ctx);
#else
    (void)ctx;
#endif

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT, "TCP listening stopped");
    return PT_OK;
}

PeerTalk_Error PeerTalk_Connect(PeerTalk_Context *ctx_public,
                                PeerTalk_PeerID peer_id) {
    struct pt_context *ctx = (struct pt_context *)ctx_public;
    struct pt_peer *peer;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Find peer by ID */
    peer = pt_peer_find_by_id(ctx, peer_id);
    if (!peer) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
                    "Connect failed: Peer %u not found", peer_id);
        return PT_ERR_PEER_NOT_FOUND;
    }

#if defined(PT_PLATFORM_POSIX)
    return pt_posix_connect(ctx, peer);
#elif defined(PT_PLATFORM_MACTCP)
    return pt_mactcp_connect(ctx, peer);
#else
    (void)peer;
    return PT_ERR_NOT_SUPPORTED;
#endif
}

PeerTalk_Error PeerTalk_Disconnect(PeerTalk_Context *ctx_public,
                                   PeerTalk_PeerID peer_id) {
    struct pt_context *ctx = (struct pt_context *)ctx_public;
    struct pt_peer *peer;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Find peer by ID */
    peer = pt_peer_find_by_id(ctx, peer_id);
    if (!peer) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
                    "Disconnect failed: Peer %u not found", peer_id);
        return PT_ERR_PEER_NOT_FOUND;
    }

#if defined(PT_PLATFORM_POSIX)
    return pt_posix_disconnect(ctx, peer);
#elif defined(PT_PLATFORM_MACTCP)
    return pt_mactcp_disconnect(ctx, peer);
#else
    (void)peer;
    return PT_ERR_NOT_SUPPORTED;
#endif
}

/* ========================================================================== */
/* Peer List Functions (Phase 1)                                             */
/* ========================================================================== */

/**
 * Get list of discovered peers
 *
 * Copies peer information for all discovered peers into the provided buffer.
 * Returns the actual count of peers copied.
 *
 * @param ctx Valid PeerTalk context
 * @param peers Buffer to receive peer info (caller-allocated)
 * @param max_peers Size of the peers buffer
 * @param out_count Receives actual number of peers copied
 *
 * @return PT_OK on success, PT_ERR_* on failure
 */
PeerTalk_Error PeerTalk_GetPeers(PeerTalk_Context *ctx_pub,
                                  PeerTalk_PeerInfo *peers,
                                  uint16_t max_peers,
                                  uint16_t *out_count) {
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    uint16_t count = 0;
    uint16_t i;

    /* Validate parameters */
    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_STATE;
    }
    if (!peers || !out_count) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Iterate through all peer slots */
    for (i = 0; i < ctx->max_peers && count < max_peers; i++) {
        struct pt_peer *peer = &ctx->peers[i];

        /* Skip unused slots */
        if (peer->hot.state == PT_PEER_UNUSED) {
            continue;
        }

        /* Validate peer magic */
        if (peer->hot.magic != PT_PEER_MAGIC) {
            continue;
        }

        /* Copy peer info using existing helper */
        pt_peer_get_info(peer, &peers[count]);
        count++;
    }

    *out_count = count;
    return PT_OK;
}

/**
 * Get peer list version (increments when peers added/removed)
 *
 * Allows detecting changes without copying entire peer list.
 *
 * @param ctx Valid PeerTalk context
 * @return Current peers version counter, 0 on error
 */
uint32_t PeerTalk_GetPeersVersion(PeerTalk_Context *ctx_pub) {
    struct pt_context *ctx = (struct pt_context *)ctx_pub;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return 0;
    }

    return ctx->peers_version;
}

/**
 * Get peer info by ID (returns pointer to internal structure)
 *
 * Returns pointer valid until next Poll call. Does not copy data.
 *
 * @param ctx Valid PeerTalk context
 * @param peer_id Peer ID to look up
 * @return Pointer to peer info, or NULL if not found
 */
const PeerTalk_PeerInfo *PeerTalk_GetPeerByID(PeerTalk_Context *ctx_pub,
                                               PeerTalk_PeerID peer_id) {
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    struct pt_peer *peer;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return NULL;
    }

    peer = pt_peer_find_by_id(ctx, peer_id);
    if (!peer) {
        return NULL;
    }

    /* Return pointer to cold info - valid until next Poll */
    return &peer->cold.info;
}

/**
 * Get peer info by ID (copies to caller-provided structure)
 *
 * Safer than GetPeerByID - copies data so it remains valid.
 *
 * @param ctx Valid PeerTalk context
 * @param peer_id Peer ID to look up
 * @param info Buffer to receive peer info (caller-allocated)
 * @return PT_OK on success, PT_ERR_* on failure
 */
PeerTalk_Error PeerTalk_GetPeer(PeerTalk_Context *ctx_pub,
                                 PeerTalk_PeerID peer_id,
                                 PeerTalk_PeerInfo *info) {
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    struct pt_peer *peer;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_STATE;
    }
    if (!info) {
        return PT_ERR_INVALID_PARAM;
    }

    peer = pt_peer_find_by_id(ctx, peer_id);
    if (!peer) {
        return PT_ERR_PEER_NOT_FOUND;
    }

    /* Copy peer info using existing helper */
    pt_peer_get_info(peer, info);
    return PT_OK;
}

/**
 * Find peer by name
 *
 * Searches for peer with matching name string.
 *
 * @param ctx Valid PeerTalk context
 * @param name Name to search for (not null)
 * @param info Optional buffer to receive peer info (can be NULL)
 * @return Peer ID if found, 0 if not found
 */
PeerTalk_PeerID PeerTalk_FindPeerByName(PeerTalk_Context *ctx_pub,
                                         const char *name,
                                         PeerTalk_PeerInfo *info) {
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    struct pt_peer *peer;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC || !name) {
        return 0;
    }

    peer = pt_peer_find_by_name(ctx, name);
    if (!peer) {
        return 0;
    }

    /* Optionally copy peer info */
    if (info) {
        pt_peer_get_info(peer, info);
    }

    return peer->hot.id;
}

/**
 * Find peer by address
 *
 * Searches for peer with matching IP address and port.
 *
 * @param ctx Valid PeerTalk context
 * @param address IP address (host byte order)
 * @param port Port number (host byte order)
 * @param info Optional buffer to receive peer info (can be NULL)
 * @return Peer ID if found, 0 if not found
 */
PeerTalk_PeerID PeerTalk_FindPeerByAddress(PeerTalk_Context *ctx_pub,
                                            uint32_t address,
                                            uint16_t port,
                                            PeerTalk_PeerInfo *info) {
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    struct pt_peer *peer;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return 0;
    }

    peer = pt_peer_find_by_addr(ctx, address, port);
    if (!peer) {
        return 0;
    }

    /* Optionally copy peer info */
    if (info) {
        pt_peer_get_info(peer, info);
    }

    return peer->hot.id;
}

/* ========================================================================== */
/* Broadcast Message (Phase 1)                                               */
/* ========================================================================== */

/**
 * Broadcast message to all connected peers
 *
 * Sends a message to all peers in PT_PEER_CONNECTED state.
 * Returns error if no peers are connected.
 *
 * @param ctx Valid PeerTalk context
 * @param data Message data (not null)
 * @param length Message length (1-PT_MAX_MESSAGE bytes)
 *
 * @return PT_OK if sent to at least one peer, PT_ERR_* on failure
 */
PeerTalk_Error PeerTalk_Broadcast(PeerTalk_Context *ctx_pub,
                                   const void *data, uint16_t length) {
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    uint16_t i;
    uint16_t sent_count = 0;
    PeerTalk_Error last_err = PT_OK;

    /* Validate parameters */
    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_STATE;
    }
    if (!data || length == 0 || length > PT_MAX_MESSAGE_SIZE) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Iterate through all peer slots */
    for (i = 0; i < ctx->max_peers; i++) {
        struct pt_peer *peer = &ctx->peers[i];

        /* Skip non-connected peers */
        if (peer->hot.state != PT_PEER_CONNECTED) {
            continue;
        }

        /* Validate peer magic */
        if (peer->hot.magic != PT_PEER_MAGIC) {
            continue;
        }

        /* Send to this peer */
        PeerTalk_Error err = PeerTalk_Send(ctx_pub, peer->hot.id, data, length);
        if (err == PT_OK) {
            sent_count++;
        } else {
            /* Track last error but continue sending to other peers */
            last_err = err;
            PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
                       "Broadcast failed to peer %u: error %d",
                       peer->hot.id, err);
        }
    }

    /* If no peers were connected, return error */
    if (sent_count == 0) {
        if (last_err != PT_OK) {
            /* Had peers but all sends failed */
            return last_err;
        }
        /* No connected peers at all */
        return PT_ERR_PEER_NOT_FOUND;
    }

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_SEND,
                "Broadcast sent to %u peer(s)", sent_count);

    return PT_OK;
}

/* ========================================================================== */
/* Queue Status (Phase 4)                                                    */
/* ========================================================================== */

/**
 * Get send queue status for peer
 *
 * Returns the number of pending messages in the send queue and available
 * slots. Useful for monitoring backpressure and flow control.
 *
 * @param ctx Valid PeerTalk context
 * @param peer_id Peer ID to query
 * @param out_pending Receives count of pending messages (can be NULL)
 * @param out_available Receives count of available slots (can be NULL)
 * @return PT_OK on success, PT_ERR_* on failure
 */
PeerTalk_Error PeerTalk_GetQueueStatus(PeerTalk_Context *ctx_pub,
                                        PeerTalk_PeerID peer_id,
                                        uint16_t *out_pending,
                                        uint16_t *out_available) {
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    struct pt_peer *peer;
    struct pt_queue *queue;

    /* Validate parameters */
    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_STATE;
    }

    /* Find peer by ID */
    peer = pt_peer_find_by_id(ctx, peer_id);
    if (!peer) {
        return PT_ERR_PEER_NOT_FOUND;
    }

    /* Get send queue */
    queue = peer->send_queue;
    if (!queue || queue->magic != PT_QUEUE_MAGIC) {
        return PT_ERR_INVALID_STATE;
    }

    /* Return queue status */
    if (out_pending) {
        *out_pending = queue->count;
    }
    if (out_available) {
        *out_available = queue->capacity - queue->count;
    }

    return PT_OK;
}
