/**
 * @file ot_multi.c
 * @brief Open Transport Multi-Transport Unified Implementation
 *
 * Provides multi-transport init, poll, send routing, and shutdown
 * for simultaneous TCP/IP and AppleTalk (ADSP/NBP) connectivity.
 *
 * This file ties together:
 * - TCP/IP endpoints (existing ot_driver.c, tcp_ot.c, etc.)
 * - ADSP endpoints (ot_adsp.c)
 * - NBP discovery (ot_nbp.c)
 * - Peer deduplication (peer_multi.c)
 *
 * The unified poll loop processes all enabled transports in a single
 * pass, using the same flag-based async pattern for both TCP and ADSP.
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 9: AppleTalk Services
 */

#include "ot_multi.h"
#include "pt_internal.h"
#include "peer.h"
#include "protocol.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_OT)

#include <MacMemory.h>
#include <OSUtils.h>

/* ========================================================================== */
/* Constants                                                                   */
/* ========================================================================== */

#define PT_OT_NBP_LOOKUP_INTERVAL_TICKS   (30 * 60)  /* 30 seconds at 60Hz */
#define PT_OT_ADSP_CLOSE_TIMEOUT_TICKS_   1800        /* 30 seconds */

/* ========================================================================== */
/* External Functions                                                          */
/* ========================================================================== */

/* From ot_driver.c */
extern int pt_ot_driver_init(struct pt_context *ctx);
extern void pt_ot_driver_shutdown(struct pt_context *ctx);
extern void pt_ot_close_all_endpoints(struct pt_context *ctx);

/* From poll_ot.c (existing TCP/IP poll) */
extern int pt_ot_poll(struct pt_context *ctx);

/* From discovery_ot.c */
extern int pt_ot_discovery_send(struct pt_context *ctx, uint8_t type);

/* From tcp_ot.c */
extern int pt_ot_tcp_send(struct pt_context *ctx, int idx,
                            const void *data, size_t len);

/* From ot_adsp.c */
extern pascal void pt_ot_adsp_notifier(void *context, OTEventCode code,
                                         OTResult result, void *cookie);
extern int pt_ot_adsp_create(struct pt_context *ctx);
extern int pt_ot_adsp_bind(struct pt_context *ctx, int idx,
                              uint8_t socket, int qlen);
extern int pt_ot_adsp_connect_by_name(struct pt_context *ctx, int idx,
                                         const char *name, const char *type,
                                         const char *zone);
extern int pt_ot_adsp_send(struct pt_context *ctx, int idx,
                              const void *data, uint16_t len, Boolean eom);
extern int pt_ot_adsp_recv(struct pt_context *ctx, int idx,
                              void *buffer, uint16_t max_len, Boolean *eom);
extern void pt_ot_adsp_close(struct pt_context *ctx, int idx);
extern void pt_ot_adsp_cleanup(struct pt_context *ctx, int idx);
extern int pt_ot_adsp_check_close_timeout(struct pt_context *ctx, int idx);
extern void pt_ot_adsp_process_log_events(struct pt_context *ctx,
                                            pt_adsp_endpoint_hot *hot,
                                            int idx);

/* From ot_nbp.c */
extern int pt_ot_nbp_init(struct pt_context *ctx);
extern int pt_ot_nbp_register(struct pt_context *ctx,
                                 const char *name, const char *type,
                                 const char *zone, DDPAddress *bound_addr);
extern int pt_ot_nbp_lookup(struct pt_context *ctx,
                               const char *type, const char *zone);
extern void pt_ot_nbp_unregister(struct pt_context *ctx);
extern void pt_ot_nbp_shutdown(struct pt_context *ctx);

/* From tcp_ot.c / udp_ot.c notifiers */
extern pascal void pt_ot_tcp_notifier(void *context, OTEventCode code,
                                        OTResult result, void *cookie);
extern pascal void pt_ot_udp_notifier(void *context, OTEventCode code,
                                        OTResult result, void *cookie);

/* ========================================================================== */
/* Context Accessor                                                            */
/* ========================================================================== */

/**
 * Get multi-transport OT data from context.
 *
 * Same pattern as pt_ot_get() - allocated immediately after pt_context.
 * NOTE: pt_ot_multi_data is only used when multi-transport is enabled.
 * When single-transport TCP/IP, the existing pt_ot_data is used instead.
 */
pt_ot_multi_data *pt_ot_multi_get(struct pt_context *ctx)
{
    return (pt_ot_multi_data *)((char *)ctx + sizeof(struct pt_context));
}

/* ========================================================================== */
/* ADSP Cold Data Allocation                                                   */
/* ========================================================================== */

/**
 * Allocate ADSP cold data structures.
 *
 * Mirrors pt_ot_alloc_cold_data() from ot_driver.c but for ADSP endpoints.
 */
static int pt_ot_alloc_adsp_cold_data(struct pt_context *ctx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);

    /* ADSP listener cold data */
    md->adsp_listener_cold = (pt_adsp_endpoint_cold *)NewPtrClear(
        (Size)sizeof(pt_adsp_endpoint_cold));
    if (md->adsp_listener_cold == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to alloc ADSP listener cold data (%lu bytes)",
            (unsigned long)sizeof(pt_adsp_endpoint_cold));
        return -1;
    }

    /* ADSP peer cold data (contiguous array for all peers) */
    md->adsp_cold = (pt_adsp_endpoint_cold *)NewPtrClear(
        (Size)(sizeof(pt_adsp_endpoint_cold) * PT_MAX_PEERS));
    if (md->adsp_cold == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to alloc ADSP peer cold data (%lu bytes)",
            (unsigned long)(sizeof(pt_adsp_endpoint_cold) * PT_MAX_PEERS));
        return -1;
    }

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_MEMORY,
        "ADSP cold data allocated: listener=%lu peers=%lu bytes",
        (unsigned long)sizeof(pt_adsp_endpoint_cold),
        (unsigned long)(sizeof(pt_adsp_endpoint_cold) * PT_MAX_PEERS));

    return 0;
}

/**
 * Free ADSP cold data structures.
 */
static void pt_ot_free_adsp_cold_data(struct pt_context *ctx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);

    if (md->adsp_cold != NULL) {
        DisposePtr((Ptr)md->adsp_cold);
        md->adsp_cold = NULL;
    }
    if (md->adsp_listener_cold != NULL) {
        DisposePtr((Ptr)md->adsp_listener_cold);
        md->adsp_listener_cold = NULL;
    }
}

/* ========================================================================== */
/* ADSP Poll Helpers                                                           */
/* ========================================================================== */

/**
 * Process NBP lookup results into peers.
 *
 * For each discovered NBP name, creates a peer using the multi-transport
 * deduplication system (pt_peer_create_from_discovery). If a peer with
 * the same name already exists via TCP, it's merged instead of duplicated.
 */
static void pt_ot_process_nbp_results(struct pt_context *ctx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    int i;
    char name_buf[PT_MAX_PEER_NAME + 1];

    for (i = 0; i < md->nbp.lookup_count; i++) {
        DDPAddress *addr = &md->nbp.lookup_addrs[i];

        /* Extract object name from NBPEntity.
         * OTExtractNBPName() extracts the object (name) portion as a
         * C string into the provided buffer. */
        OTExtractNBPName(&md->nbp.lookup_names[i], name_buf);

        /* Truncate if longer than max peer name */
        name_buf[PT_MAX_PEER_NAME] = '\0';

        if (name_buf[0] == '\0')
            continue;

        /* Create peer with deduplication.
         * If a peer with this name exists on TCP, it will be merged. */
        {
            uint32_t ddp_combined;

            /* Pack DDP address into uint32_t for peer address storage:
             * bits 31-16: network, bits 15-8: node, bits 7-0: socket */
            ddp_combined = ((uint32_t)addr->fNetwork << 16) |
                           ((uint32_t)addr->fNodeID << 8) |
                           (uint32_t)addr->fSocket;

            pt_peer_create_from_discovery(ctx, name_buf,
                                            PT_TRANSPORT_ADSP,
                                            ddp_combined,
                                            addr->fSocket);
        }
    }
}

/**
 * Poll connected ADSP endpoints for data and disconnects.
 *
 * Mirrors pt_ot_poll_connected() from poll_ot.c but for ADSP endpoints.
 */
static void pt_ot_adsp_poll_connected(struct pt_context *ctx,
                                         pt_ot_multi_data *md,
                                         int idx,
                                         pt_adsp_endpoint_hot *hot)
{
    struct pt_peer *peer = hot->peer;

    if (peer == NULL)
        return;

    /* Process deferred log events from notifier */
    pt_ot_adsp_process_log_events(ctx, hot, idx);

    /* --- Abortive disconnect (T_DISCONNECT) --- */
    if (PT_FLAG_TEST(hot->flags, PT_OT_FLAG_DISCONNECT)) {
        PeerTalk_PeerID disc_id = peer->hot.id;

        PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_DISCONNECT);
        OTRcvDisconnect(hot->ref, NULL);

        PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
            "ADSP[%d] peer %u disconnected (abortive)",
            idx, (unsigned)disc_id);

        /* CRITICAL: Cleanup BEFORE callback so application can
         * immediately reconnect in the callback if desired. */
        peer->hot.connection = NULL;
        peer->cold.info.transport_connected = 0;
        pt_peer_set_state(ctx, peer, PT_PEER_STATE_DISCOVERED);
        hot->peer = NULL;
        pt_ot_adsp_cleanup(ctx, idx);

        if (ctx->callbacks.on_peer_disconnected != NULL) {
            ctx->callbacks.on_peer_disconnected(
                (PeerTalk_Context *)ctx,
                disc_id, 0,
                ctx->callbacks.user_data);
        }

        return;
    }

    /* --- Orderly disconnect (T_ORDREL) --- */
    if (PT_FLAG_TEST(hot->flags, PT_OT_FLAG_ORDERLY_RELEASE)) {
        PeerTalk_PeerID disc_id = peer->hot.id;

        PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_ORDERLY_RELEASE);

        /* Acknowledge orderly disconnect */
        OTRcvOrderlyDisconnect(hot->ref);
        /* Send our orderly disconnect to complete handshake */
        OTSndOrderlyDisconnect(hot->ref);

        PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
            "ADSP[%d] peer %u disconnected (orderly)",
            idx, (unsigned)disc_id);

        /* CRITICAL: Cleanup BEFORE callback so application can
         * immediately reconnect in the callback if desired. */
        peer->hot.connection = NULL;
        peer->cold.info.transport_connected = 0;
        pt_peer_set_state(ctx, peer, PT_PEER_STATE_DISCOVERED);
        hot->peer = NULL;
        pt_ot_adsp_cleanup(ctx, idx);

        if (ctx->callbacks.on_peer_disconnected != NULL) {
            ctx->callbacks.on_peer_disconnected(
                (PeerTalk_Context *)ctx,
                disc_id, 0,
                ctx->callbacks.user_data);
        }

        return;
    }

    /* --- Data receive ---
     * Always try to read, don't gate on PT_OT_FLAG_DATA_AVAILABLE.
     * Same T_DATA race fix as TCP path: flag clear races with notifier. */
    {
        /* Read data into peer ibuf using ADSP recv */
        while (peer->cold.ibuflen < PT_FRAME_BUF_SIZE) {
            Boolean eom = false;
            uint16_t space = PT_FRAME_BUF_SIZE - peer->cold.ibuflen;
            int result;

            result = pt_ot_adsp_recv(ctx, idx,
                                       peer->cold.ibuf + peer->cold.ibuflen,
                                       space, &eom);
            if (result < 0) {
                /* Connection error */
                PeerTalk_PeerID disc_id = peer->hot.id;

                PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
                    "ADSP[%d] recv error, closing peer %u",
                    idx, (unsigned)disc_id);
                pt_ot_adsp_close(ctx, idx);

                /* CRITICAL: Cleanup BEFORE callback */
                peer->hot.connection = NULL;
                peer->cold.info.transport_connected = 0;
                pt_peer_set_state(ctx, peer, PT_PEER_STATE_DISCOVERED);
                hot->peer = NULL;

                if (ctx->callbacks.on_peer_disconnected != NULL) {
                    ctx->callbacks.on_peer_disconnected(
                        (PeerTalk_Context *)ctx,
                        disc_id, 0,
                        ctx->callbacks.user_data);
                }

                return;
            }

            if (result == 0)
                break;

            peer->cold.ibuflen += (uint16_t)result;
        }

        if (peer->cold.ibuflen > 0) {
            peer->hot.last_seen = (pt_tick_t)TickCount();
        }

        /* Note: Do NOT clear PT_OT_FLAG_DATA_AVAILABLE here.
         * pt_ot_adsp_recv() clears it on kOTNoDataErr. */
    }

    (void)md;
}

/* ========================================================================== */
/* Initialization                                                              */
/* ========================================================================== */

/**
 * Initialize multi-transport OT platform layer.
 *
 * Sets up ADSP endpoints and NBP discovery alongside the existing
 * TCP/IP infrastructure. Called from platform_ot.c when multi-transport
 * is configured.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success, -1 on failure
 */
int pt_ot_multi_init(struct pt_context *ctx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    int i;
    int listener_idx;

    md->transports = ctx->config.transports;

    /* Initialize ADSP endpoint pool */
    pt_endpoint_pool_init(&md->adsp_pool, PT_MAX_PEERS);

    /* Initialize ADSP hot endpoint refs to invalid */
    md->adsp_listener_hot.ref = kOTInvalidEndpointRef;
    md->adsp_listener_hot.state = PT_EP_UNUSED;

    for (i = 0; i < PT_MAX_PEERS; i++) {
        md->adsp_hot[i].ref = kOTInvalidEndpointRef;
        md->adsp_hot[i].state = PT_EP_UNUSED;
        md->adsp_hot[i].endpoint_idx = (uint8_t)i;
        md->adsp_hot[i].peer = NULL;
    }

    /* Allocate ADSP cold data */
    if (pt_ot_alloc_adsp_cold_data(ctx) != 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT, "ADSP cold data allocation failed");
        return -1;
    }

    /* Create ADSP notifier UPP */
    md->adsp_notifier_upp = NewOTNotifyUPP(pt_ot_adsp_notifier);
    if (md->adsp_notifier_upp == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT,
            "Failed to create ADSP notifier UPP");
        pt_ot_free_adsp_cold_data(ctx);
        return -1;
    }

    /* Create cached ADSP configuration */
    md->adsp_config = OTCreateConfiguration(PT_OT_ADSP_CONFIG);
    if (md->adsp_config == kOTInvalidConfigurationPtr ||
        md->adsp_config == kOTNoMemoryConfigurationPtr) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT,
            "Failed to create ADSP configuration");
        md->adsp_config = NULL;
        DisposeOTNotifyUPP(md->adsp_notifier_upp);
        md->adsp_notifier_upp = NULL;
        pt_ot_free_adsp_cold_data(ctx);
        return -1;
    }

    /* Create and bind ADSP listener */
    if (md->transports & PT_TRANSPORT_ADSP) {
        listener_idx = pt_ot_adsp_create(ctx);
        if (listener_idx < 0) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
                "ADSP listener creation failed, disabling ADSP");
            md->transports &= ~PT_TRANSPORT_ADSP;
        } else {
            /* Bind with qlen=4 for incoming connections */
            if (pt_ot_adsp_bind(ctx, listener_idx, 0, 4) != 0) {
                PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
                    "ADSP listener bind failed, disabling ADSP");
                pt_ot_adsp_cleanup(ctx, listener_idx);
                md->transports &= ~PT_TRANSPORT_ADSP;
            } else {
                /* Store listener endpoint data.
                 * Copy hot/cold to the dedicated listener slots. */
                md->adsp_listener_hot = md->adsp_hot[listener_idx];
                /* Free the pool slot since listener uses dedicated storage */
                pt_endpoint_pool_free(&md->adsp_pool, listener_idx);

                PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
                    "ADSP listener ready");
            }
        }
    }

    /* Initialize NBP mapper */
    if (md->transports & PT_TRANSPORT_NBP) {
        if (pt_ot_nbp_init(ctx) != 0) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_DISCOVERY,
                "NBP initialization failed, disabling NBP");
            md->transports &= ~PT_TRANSPORT_NBP;
        } else if (md->transports & PT_TRANSPORT_ADSP) {
            /* Register our name with NBP using ADSP listener address */
            const char *nbp_type = ctx->config.nbp_type[0]
                ? ctx->config.nbp_type : PT_OT_NBP_TYPE_DEFAULT;
            const char *nbp_zone = ctx->config.nbp_zone[0]
                ? ctx->config.nbp_zone : PT_OT_NBP_ZONE_DEFAULT;

            if (md->adsp_listener_cold != NULL) {
                pt_ot_nbp_register(ctx,
                    ctx->config.local_name,
                    nbp_type, nbp_zone,
                    &md->adsp_listener_cold->local_addr);
            }
        }
    }

    /* Initialize timing */
    md->last_nbp_lookup = 0;

    PT_CTX_INFO(ctx, PT_LOG_CAT_INIT,
        "Multi-transport init: ADSP=%d NBP=%d",
        (md->transports & PT_TRANSPORT_ADSP) != 0,
        (md->transports & PT_TRANSPORT_NBP) != 0);

    return 0;
}

/* ========================================================================== */
/* Multi-Transport Poll                                                        */
/* ========================================================================== */

/**
 * Unified multi-transport poll function.
 *
 * Called from PeerTalk_Poll() when multi-transport is enabled.
 * Runs the existing TCP/IP poll first, then adds ADSP and NBP processing.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success
 */
int pt_ot_multi_poll(struct pt_context *ctx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    unsigned long now = (unsigned long)TickCount();

    /* 1. Run existing TCP/IP poll (discovery, listener, connect, data) */
    pt_ot_poll(ctx);

    /* 2. Periodic NBP lookup */
    if ((md->transports & PT_TRANSPORT_NBP) &&
        (long)(now - md->last_nbp_lookup) > (long)PT_OT_NBP_LOOKUP_INTERVAL_TICKS) {
        const char *nbp_type = ctx->config.nbp_type[0]
            ? ctx->config.nbp_type : PT_OT_NBP_TYPE_DEFAULT;
        const char *nbp_zone = ctx->config.nbp_zone[0]
            ? ctx->config.nbp_zone : PT_OT_NBP_ZONE_DEFAULT;

        pt_ot_nbp_lookup(ctx, nbp_type, nbp_zone);
        md->last_nbp_lookup = now;

        /* Process results into peers with deduplication */
        pt_ot_process_nbp_results(ctx);
    }

    /* 3. Poll ADSP endpoints using bitmap iteration (same pattern as TCP) */
    if (md->transports & PT_TRANSPORT_ADSP) {
        uint32_t active;
        int bit;

        active = ~md->adsp_pool.free_bitmap
               & ((1UL << md->adsp_pool.capacity) - 1);

        while (active) {
            pt_adsp_endpoint_hot *hot;

#if defined(__GNUC__)
            bit = __builtin_ffs((int)active) - 1;
#else
            {
                uint32_t tmp = active;
                bit = 0;
                while ((tmp & 1) == 0) { tmp >>= 1; bit++; }
            }
#endif
            active &= ~(1UL << bit);

            hot = pt_ot_get_adsp_hot(md, bit);
            if (hot == NULL)
                continue;

            switch (hot->state) {
            case PT_EP_DATAXFER:
                pt_ot_adsp_poll_connected(ctx, md, bit, hot);
                break;

            case PT_EP_CLOSING:
                if (pt_ot_adsp_check_close_timeout(ctx, bit)) {
                    struct pt_peer *peer = hot->peer;
                    if (peer != NULL) {
                        PeerTalk_PeerID disc_id = peer->hot.id;

                        /* CRITICAL: Cleanup BEFORE callback */
                        peer->hot.connection = NULL;
                        peer->cold.info.transport_connected = 0;
                        pt_peer_set_state(ctx, peer,
                                           PT_PEER_STATE_DISCOVERED);

                        if (ctx->callbacks.on_peer_disconnected != NULL) {
                            ctx->callbacks.on_peer_disconnected(
                                (PeerTalk_Context *)ctx,
                                disc_id, 0,
                                ctx->callbacks.user_data);
                        }
                    }
                } else {
                    /* Check if orderly release arrived while closing */
                    if (PT_FLAG_TEST(hot->flags,
                                      PT_OT_FLAG_ORDERLY_RELEASE)) {
                        struct pt_peer *peer = hot->peer;
                        PT_FLAG_CLEAR(hot->flags,
                                       PT_OT_FLAG_ORDERLY_RELEASE);
                        OTRcvOrderlyDisconnect(hot->ref);

                        if (peer != NULL) {
                            PeerTalk_PeerID disc_id = peer->hot.id;

                            /* CRITICAL: Cleanup BEFORE callback */
                            peer->hot.connection = NULL;
                            peer->cold.info.transport_connected = 0;
                            pt_peer_set_state(ctx, peer,
                                               PT_PEER_STATE_DISCOVERED);

                            if (ctx->callbacks.on_peer_disconnected
                                != NULL) {
                                ctx->callbacks.on_peer_disconnected(
                                    (PeerTalk_Context *)ctx,
                                    disc_id, 0,
                                    ctx->callbacks.user_data);
                            }
                        }
                        hot->peer = NULL;
                        pt_ot_adsp_cleanup(ctx, bit);
                    }
                }
                break;

            default:
                break;
            }
        }
    }

    return 0;
}

/* ========================================================================== */
/* Multi-Transport Send Routing                                                */
/* ========================================================================== */

/**
 * Send data to a peer via the correct transport.
 *
 * Routes to TCP or ADSP based on peer's transport_connected field.
 * The endpoint index is stored in peer->hot.connection as (void*)(idx+1).
 *
 * @param ctx   PeerTalk context
 * @param peer  Target peer
 * @param data  Data to send
 * @param len   Data length
 * @return      Bytes sent (>0), 0 on flow control, -1 on error
 */
int pt_ot_multi_send(struct pt_context *ctx, struct pt_peer *peer,
                       const void *data, uint16_t len)
{
    int idx;

    if (peer == NULL || peer->hot.connection == NULL)
        return -1;

    idx = (int)(intptr_t)peer->hot.connection - 1;

    if (peer->cold.info.transport_connected == PT_TRANSPORT_TCP) {
        return pt_ot_tcp_send(ctx, idx, data, (size_t)len);
    }

    if (peer->cold.info.transport_connected == PT_TRANSPORT_ADSP) {
        return pt_ot_adsp_send(ctx, idx, data, len, true);
    }

    PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
        "No transport for peer %u (transport_connected=0x%X)",
        (unsigned)peer->hot.id,
        (unsigned)peer->cold.info.transport_connected);

    return -1;
}

/* ========================================================================== */
/* Multi-Transport Connect                                                     */
/* ========================================================================== */

/**
 * Connect to a peer via the specified transport.
 *
 * Routes to TCP OTConnect or ADSP connect-by-name based on transport.
 *
 * @param ctx        PeerTalk context
 * @param peer       Peer to connect to
 * @param transport  Transport to use (PT_TRANSPORT_TCP or PT_TRANSPORT_ADSP)
 * @return           0 on success, -1 on error
 */
int pt_ot_multi_connect(struct pt_context *ctx, struct pt_peer *peer)
{
    uint16_t transport;

    if (peer == NULL)
        return -1;

    /* Select best transport using preference system */
    transport = pt_peer_select_transport(ctx, peer);

    if (transport == PT_TRANSPORT_ADSP) {
        /* Connect via ADSP using NBP name */
        const char *nbp_type = ctx->config.nbp_type[0]
            ? ctx->config.nbp_type : PT_OT_NBP_TYPE_DEFAULT;
        const char *nbp_zone = ctx->config.nbp_zone[0]
            ? ctx->config.nbp_zone : PT_OT_NBP_ZONE_DEFAULT;
        int idx;

        idx = pt_ot_adsp_connect_by_name(ctx, -1,
                                            peer->cold.name,
                                            nbp_type, nbp_zone);
        if (idx >= 0) {
            pt_ot_multi_data *md = pt_ot_multi_get(ctx);
            md->adsp_hot[idx].peer = peer;
            peer->hot.connection = (void *)(intptr_t)(idx + 1);
            peer->cold.info.transport_connected = PT_TRANSPORT_ADSP;
            pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTING);
            return 0;
        }
        return -1;
    }

    /* Default: TCP connect (handled by existing tcp_connect_ot.c) */
    return -1;  /* Caller should use existing TCP connect path */
}

/* ========================================================================== */
/* Multi-Transport Disconnect                                                  */
/* ========================================================================== */

/**
 * Disconnect a peer from the specified transport.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to disconnect
 * @return      0 on success, -1 on error
 */
int pt_ot_multi_disconnect(struct pt_context *ctx, struct pt_peer *peer)
{
    int idx;

    if (peer == NULL || peer->hot.connection == NULL)
        return -1;

    idx = (int)(intptr_t)peer->hot.connection - 1;

    if (peer->cold.info.transport_connected == PT_TRANSPORT_ADSP) {
        pt_ot_adsp_close(ctx, idx);
        peer->hot.connection = NULL;
        peer->cold.info.transport_connected = 0;
        return 0;
    }

    /* TCP disconnect handled by existing code */
    return -1;
}

/* ========================================================================== */
/* Shutdown                                                                    */
/* ========================================================================== */

/**
 * Shut down multi-transport OT platform layer.
 *
 * Cleanup order:
 * 1. Close all ADSP endpoints
 * 2. Shut down NBP mapper
 * 3. Dispose ADSP notifier UPP
 * 4. Destroy ADSP configuration
 * 5. Free ADSP cold data
 *
 * TCP/IP shutdown is handled separately by pt_ot_driver_shutdown().
 */
void pt_ot_multi_shutdown(struct pt_context *ctx)
{
    pt_ot_multi_data *md = pt_ot_multi_get(ctx);
    int i;

    /* 1. Close all ADSP peer endpoints */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        if (pt_endpoint_pool_in_use(&md->adsp_pool, i)) {
            pt_ot_adsp_close(ctx, i);
        }
    }

    /* Close ADSP listener */
    if (md->adsp_listener_hot.ref != kOTInvalidEndpointRef) {
        if (md->adsp_listener_hot.state >= PT_EP_IDLE) {
            OTResult ep_state = OTGetEndpointState(md->adsp_listener_hot.ref);
            if (ep_state == T_IDLE)
                OTUnbind(md->adsp_listener_hot.ref);
        }
        OTCloseProvider(md->adsp_listener_hot.ref);
        md->adsp_listener_hot.ref = kOTInvalidEndpointRef;
        md->adsp_listener_hot.state = PT_EP_UNUSED;
    }

    /* 2. Shut down NBP */
    pt_ot_nbp_shutdown(ctx);

    /* 3. Dispose ADSP notifier UPP */
    if (md->adsp_notifier_upp != NULL) {
        DisposeOTNotifyUPP(md->adsp_notifier_upp);
        md->adsp_notifier_upp = NULL;
    }

    /* 4. Destroy ADSP configuration */
    if (md->adsp_config != NULL) {
        OTDestroyConfiguration(md->adsp_config);
        md->adsp_config = NULL;
    }

    /* 5. Free ADSP cold data */
    pt_ot_free_adsp_cold_data(ctx);

    PT_CTX_INFO(ctx, PT_LOG_CAT_INIT, "Multi-transport shutdown complete");
}

#endif /* PT_PLATFORM_OT */
