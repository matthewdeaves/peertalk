/**
 * @file ot_driver.c
 * @brief Open Transport Driver Initialization and Endpoint Management
 *
 * Handles OT initialization, cold data allocation, IP configuration,
 * endpoint cleanup, and shutdown.
 *
 * Note: Notifier forward declarations are NOT included here to avoid
 * ISR safety false positives. Notifiers are wired up in the endpoint
 * creation files (udp_ot.c, tcp_ot.c) where they are actually needed.
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 2: "Getting Started"
 * - OpenTransport.h, OpenTransportProviders.h (Retro68)
 */

#include "ot_defs.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_OT)

#include <Gestalt.h>
#include <MacMemory.h>

/* ========================================================================== */
/* Context Accessor                                                            */
/* ========================================================================== */

/**
 * Get OT platform data from context.
 *
 * Platform data is allocated immediately after the pt_context struct
 * (same pattern as MacTCP's pt_mactcp_get).
 */
pt_ot_data *pt_ot_get(struct pt_context *ctx)
{
    return (pt_ot_data *)((char *)ctx + sizeof(struct pt_context));
}

/* ========================================================================== */
/* Cold Data Allocation                                                        */
/* ========================================================================== */

/**
 * Allocate cold data structures (large buffers, TCall structs).
 *
 * Memory breakdown (with PT_MAX_PEERS=16):
 *   UDP cold:          ~2KB
 *   TCP listener cold: ~1.1KB
 *   TCP peer cold:     16 x ~1.1KB = ~17.6KB
 *   Total:             ~20.7KB
 *
 * @return 0 on success, -1 on allocation failure
 */
static int pt_ot_alloc_cold_data(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    Size total = 0;

    /* UDP cold data */
    od->udp_cold = (pt_udp_endpoint_cold *)NewPtrClear(
        (Size)sizeof(pt_udp_endpoint_cold));
    if (od->udp_cold == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to alloc UDP cold data (%lu bytes)",
            (unsigned long)sizeof(pt_udp_endpoint_cold));
        return -1;
    }
    total += (Size)sizeof(pt_udp_endpoint_cold);

    /* TCP listener cold data */
    od->listener_cold = (pt_tcp_endpoint_cold *)NewPtrClear(
        (Size)sizeof(pt_tcp_endpoint_cold));
    if (od->listener_cold == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to alloc TCP listener cold data (%lu bytes)",
            (unsigned long)sizeof(pt_tcp_endpoint_cold));
        return -1;
    }
    total += (Size)sizeof(pt_tcp_endpoint_cold);

    /* TCP peer cold data (contiguous array for all peers) */
    od->tcp_cold = (pt_tcp_endpoint_cold *)NewPtrClear(
        (Size)(sizeof(pt_tcp_endpoint_cold) * PT_MAX_PEERS));
    if (od->tcp_cold == NULL) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to alloc TCP peer cold data (%lu bytes)",
            (unsigned long)(sizeof(pt_tcp_endpoint_cold) * PT_MAX_PEERS));
        return -1;
    }
    total += (Size)(sizeof(pt_tcp_endpoint_cold) * PT_MAX_PEERS);

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_MEMORY,
        "OT cold data allocated: %ld bytes total", (long)total);

    return 0;
}

/**
 * Free all cold data structures.
 * Safe to call even if some allocations failed (checks for NULL).
 */
static void pt_ot_free_cold_data(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);

    if (od->tcp_cold != NULL) {
        DisposePtr((Ptr)od->tcp_cold);
        od->tcp_cold = NULL;
    }
    if (od->listener_cold != NULL) {
        DisposePtr((Ptr)od->listener_cold);
        od->listener_cold = NULL;
    }
    if (od->udp_cold != NULL) {
        DisposePtr((Ptr)od->udp_cold);
        od->udp_cold = NULL;
    }
}

/* ========================================================================== */
/* IP Configuration                                                            */
/* ========================================================================== */

/**
 * Get local IP address from OT.
 *
 * Uses OTInetGetInterfaceInfo with kDefaultInetInterface to retrieve
 * the primary network interface's IP address.
 *
 * @return 0 on success, -1 on error
 */
static int pt_ot_get_local_ip(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    InetInterfaceInfo info;
    OSStatus err;
    char ip_str[PT_IP_STR_LEN];

    pt_memset(&info, 0, sizeof(info));

    err = OTInetGetInterfaceInfo(&info, kDefaultInetInterface);
    if (err != noErr) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
            "OTInetGetInterfaceInfo failed: %ld", (long)err);
        return -1;
    }

    od->local_ip = info.fAddress;
    od->net_mask = info.fNetmask;

    OTInetHostToString(od->local_ip, ip_str);
    PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK, "Local IP: %s", ip_str);

    OTInetHostToString(od->net_mask, ip_str);
    PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK, "Netmask: %s MTU: %lu",
        ip_str, (unsigned long)info.fIfMTU);

    return 0;
}

/* ========================================================================== */
/* Endpoint Cleanup                                                            */
/* ========================================================================== */

/**
 * Close a single endpoint reference.
 *
 * Attempts orderly unbind if endpoint is in T_IDLE state,
 * then closes the provider. Sets state to PT_EP_UNUSED.
 */
static void pt_ot_close_endpoint(EndpointRef *ref_ptr,
                                  pt_endpoint_state *state_ptr)
{
    EndpointRef ref;

    if (ref_ptr == NULL || *ref_ptr == kOTInvalidEndpointRef)
        return;

    ref = *ref_ptr;

    /* Try orderly unbind if endpoint is bound but idle */
    if (state_ptr != NULL && *state_ptr >= PT_EP_IDLE) {
        OTResult ep_state = OTGetEndpointState(ref);
        if (ep_state == T_IDLE)
            OTUnbind(ref);
    }

    OTCloseProvider(ref);
    *ref_ptr = kOTInvalidEndpointRef;

    if (state_ptr != NULL)
        *state_ptr = PT_EP_UNUSED;
}

/**
 * Close all open endpoints in preparation for shutdown.
 *
 * Order: UDP first, then TCP peers, then listener.
 * Uses pool bitmap for efficient iteration of active TCP peers.
 *
 * CRITICAL: Must be called BEFORE disposing UPPs, since endpoints
 * reference the UPPs for their notifiers.
 */
void pt_ot_close_all_endpoints(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    int i;

    /* Close UDP endpoint */
    pt_ot_close_endpoint(&od->udp_hot.ref, &od->udp_hot.state);

    /* Close active TCP peer endpoints (use bitmap for efficiency) */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        if (pt_endpoint_pool_in_use(&od->tcp_pool, i)) {
            pt_ot_close_endpoint(&od->tcp_hot[i].ref,
                                  &od->tcp_hot[i].state);
            pt_endpoint_pool_free(&od->tcp_pool, i);
        }
    }

    /* Close TCP listener last */
    pt_ot_close_endpoint(&od->listener_hot.ref, &od->listener_hot.state);
}

/* ========================================================================== */
/* Initialization                                                              */
/* ========================================================================== */

/**
 * Initialize Open Transport platform layer.
 *
 * Called from platform_ot.c ot_init() after InitOpenTransport succeeds.
 * Sets up:
 * 1. Hot data (zeroed)
 * 2. Endpoint references (set to invalid)
 * 3. Cold data structures (allocated via NewPtrClear)
 * 4. Master configurations (cloned before each endpoint open)
 * 5. Endpoint pool (all slots free)
 * 6. Local IP address
 *
 * Note: UPPs for notifier callbacks are created by the platform layer
 * (platform_ot.c) which has visibility to the notifier functions.
 * They are stored in od->tcp_notifier_upp and od->udp_notifier_upp.
 *
 * @return 0 on success, -1 on failure
 */
int pt_ot_driver_init(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    int i;

    /* Clear all hot data */
    pt_memset(od, 0, sizeof(pt_ot_data));

    /* Initialize endpoint references to invalid */
    od->udp_hot.ref = kOTInvalidEndpointRef;
    od->udp_hot.state = PT_EP_UNUSED;

    od->listener_hot.ref = kOTInvalidEndpointRef;
    od->listener_hot.state = PT_EP_UNUSED;

    for (i = 0; i < PT_MAX_PEERS; i++) {
        od->tcp_hot[i].ref = kOTInvalidEndpointRef;
        od->tcp_hot[i].state = PT_EP_UNUSED;
        od->tcp_hot[i].endpoint_idx = (uint8_t)i;
        od->tcp_hot[i].peer = NULL;
    }

    /* Allocate cold data (large buffers, TCall structs) */
    if (pt_ot_alloc_cold_data(ctx) != 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT, "Cold data allocation failed");
        return -1;
    }

    /* Create master configurations.
     * These are cloned before each OTOpenEndpoint call because
     * OTOpenEndpoint disposes the config it receives.
     * OTCloneConfiguration() is ~5x faster than OTCreateConfiguration(). */
    od->tcp_config = OTCreateConfiguration(PT_OT_TCP_CONFIG);
    if (od->tcp_config == kOTInvalidConfigurationPtr ||
        od->tcp_config == kOTNoMemoryConfigurationPtr) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT,
            "Failed to create TCP configuration");
        od->tcp_config = NULL;
        return -1;
    }

    od->udp_config = OTCreateConfiguration(PT_OT_UDP_CONFIG);
    if (od->udp_config == kOTInvalidConfigurationPtr ||
        od->udp_config == kOTNoMemoryConfigurationPtr) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_INIT,
            "Failed to create UDP configuration");
        od->udp_config = NULL;
        return -1;
    }

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_INIT, "OT master configs created");

    /* Initialize endpoint pool (all slots free) */
    pt_endpoint_pool_init(&od->tcp_pool, PT_MAX_PEERS);

    /* Get local IP address */
    if (pt_ot_get_local_ip(ctx) != 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
            "Could not get local IP - discovery may not work");
        /* Non-fatal: continue without IP (may get it later) */
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_INIT,
        "OT driver initialized (max_peers=%d, pool_cap=%d)",
        PT_MAX_PEERS, (int)od->tcp_pool.capacity);

    return 0;
}

/* ========================================================================== */
/* Shutdown                                                                    */
/* ========================================================================== */

/**
 * Shut down Open Transport platform layer.
 *
 * Cleanup order:
 * 1. Close all endpoints (they reference UPPs)
 * 2. Destroy master configurations
 * 3. Free cold data structures
 *
 * Note: UPP disposal is handled by platform_ot.c (which created them).
 * This function must be called BEFORE UPPs are disposed.
 *
 * Called from platform_ot.c ot_shutdown() before CloseOpenTransport().
 */
void pt_ot_driver_shutdown(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);

    /* 1. Close all open endpoints first */
    pt_ot_close_all_endpoints(ctx);

    /* 2. Destroy master configurations */
    if (od->tcp_config != NULL) {
        OTDestroyConfiguration(od->tcp_config);
        od->tcp_config = NULL;
    }
    if (od->udp_config != NULL) {
        OTDestroyConfiguration(od->udp_config);
        od->udp_config = NULL;
    }

    /* 3. Free cold data structures */
    pt_ot_free_cold_data(ctx);

    PT_CTX_INFO(ctx, PT_LOG_CAT_INIT, "OT driver shutdown complete");
}

/* ========================================================================== */
/* Extra Size                                                                  */
/* ========================================================================== */

/**
 * Return platform-specific extra size needed after pt_context.
 *
 * This is called during PeerTalk_Init to allocate the context with
 * enough space for the platform extension.
 */
size_t pt_ot_extra_size(void)
{
    return sizeof(pt_ot_data);
}

#endif /* PT_PLATFORM_OT */
