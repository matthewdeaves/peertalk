/**
 * @file peer_multi.c
 * @brief Multi-Transport Peer Management
 *
 * Handles peer deduplication across transports, transport merging/removal,
 * transport preference selection, and filtered peer queries.
 *
 * When a Mac is discoverable via both UDP broadcast (TCP/IP) and NBP lookup
 * (AppleTalk), the SDK must recognize these as the same physical machine
 * and merge them into a single peer entry.
 *
 * Deduplication strategy:
 * 1. Name matching (case-insensitive) - if NBP name matches existing peer
 * 2. Address correlation - same address on different transports
 * 3. Manual merge - application calls PeerTalk_MergePeers()
 */

#include "peer.h"
#include "pt_internal.h"
#include "pt_compat.h"

/* ========================================================================== */
/* Internal String Helpers                                                     */
/* ========================================================================== */

/**
 * Case-insensitive string comparison.
 * Returns 0 if strings match (ignoring case), non-zero otherwise.
 *
 * Only handles ASCII (sufficient for peer names and NBP names).
 */
static int pt_strcasecmp(const char *a, const char *b)
{
    if (!a || !b) return (a != b) ? 1 : 0;

    while (*a && *b) {
        char ca = *a;
        char cb = *b;

        /* ASCII lowercase conversion */
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;

        if (ca != cb) return 1;
        a++;
        b++;
    }

    return (*a == *b) ? 0 : 1;
}

/* ========================================================================== */
/* Peer Name Matching for Deduplication                                       */
/* ========================================================================== */

/**
 * Match strength for peer deduplication.
 * Higher values indicate stronger matches.
 */
typedef enum {
    PT_MATCH_NONE       = 0,
    PT_MATCH_NAME       = 1,    /* Case-insensitive name match (weak) */
    PT_MATCH_NAME_EXACT = 2     /* Exact name match (strong) */
} pt_match_strength;

/**
 * Compare two peer names for potential match.
 *
 * Returns match strength indicating how confident we are that
 * these represent the same physical machine.
 */
static pt_match_strength pt_peer_name_match(const char *name1, const char *name2)
{
    const char *a;
    const char *b;

    if (!name1 || !name2 || !name1[0] || !name2[0]) {
        return PT_MATCH_NONE;
    }

    /* Check exact match first (no function call overhead) */
    a = name1;
    b = name2;
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    if (*a == *b) {
        return PT_MATCH_NAME_EXACT;
    }

    /* Check case-insensitive match */
    if (pt_strcasecmp(name1, name2) == 0) {
        return PT_MATCH_NAME;
    }

    return PT_MATCH_NONE;
}

/* ========================================================================== */
/* Find Matching Peer for Deduplication                                       */
/* ========================================================================== */

/**
 * Find an existing peer that might match a new discovery.
 *
 * Scans the peer list for a peer with matching name that doesn't
 * already have the new transport. Used during discovery to detect
 * when the same Mac is found via different transport mechanisms.
 *
 * @param ctx            Context
 * @param name           Discovered peer name
 * @param new_transport  Transport the peer was discovered on (PT_TRANSPORT_*)
 * @return               Matching peer, or NULL if no match found
 */
struct pt_peer *pt_peer_find_match(struct pt_context *ctx,
                                    const char *name,
                                    uint16_t new_transport)
{
    uint16_t i;
    struct pt_peer *best_match = NULL;
    pt_match_strength best_strength = PT_MATCH_NONE;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC || !name || !name[0]) {
        return NULL;
    }

    for (i = 0; i < ctx->max_peers; i++) {
        struct pt_peer *peer = &ctx->peers[i];
        const char *peer_name;
        pt_match_strength strength;

        if (peer->hot.state == PT_PEER_STATE_UNUSED) {
            continue;
        }

        /* Skip if peer already has this transport */
        if (peer->cold.info.transports_available & new_transport) {
            continue;
        }

        /* Get name from centralized table */
        peer_name = ctx->peer_names[peer->hot.name_idx];
        strength = pt_peer_name_match(peer_name, name);

        if (strength > best_strength) {
            best_strength = strength;
            best_match = peer;
        }
    }

    /* Only return if match is strong enough */
    if (best_strength >= PT_MATCH_NAME) {
        return best_match;
    }

    return NULL;
}

/* ========================================================================== */
/* Add Transport to Existing Peer (Merge)                                     */
/* ========================================================================== */

/**
 * Add a new transport to an existing peer.
 *
 * Called when the same physical Mac is discovered via a different
 * transport mechanism. Updates the peer's available_transports mask
 * and stores the transport-specific address.
 *
 * @param ctx            Context
 * @param peer           Existing peer to update
 * @param transport      New transport (PT_TRANSPORT_TCP, PT_TRANSPORT_ADSP)
 * @param address        Transport-specific address (IP for TCP, synthesized for ADSP)
 * @param port           Transport-specific port
 * @return               0 on success, -1 on error
 */
int pt_peer_add_transport(struct pt_context *ctx,
                           struct pt_peer *peer,
                           uint16_t transport,
                           uint32_t address, uint16_t port)
{
    uint16_t old_transports;
    int addr_idx;

    if (!ctx || !peer || peer->hot.magic != PT_PEER_MAGIC) {
        return -1;
    }

    old_transports = peer->cold.info.transports_available;
    peer->cold.info.transports_available |= transport;

    /* Store transport-specific address in addresses array */
    addr_idx = peer->hot.address_count;
    if (addr_idx < PT_MAX_PEER_ADDRESSES) {
        peer->cold.addresses[addr_idx].address = address;
        peer->cold.addresses[addr_idx].port = port;
        peer->cold.addresses[addr_idx].transport = transport;
        peer->hot.address_count++;
    }

    /* Update primary address if this is TCP (for backward compat) */
    if (transport & PT_TRANSPORT_TCP) {
        peer->cold.info.address = address;
        peer->cold.info.port = port;
    }

    /* Update last_seen */
    peer->hot.last_seen = ctx->plat->get_ticks();

    /* Fire callback if this is a NEW transport for this peer */
    if (!(old_transports & transport)) {
        if (ctx->callbacks.on_transport_added) {
            ctx->callbacks.on_transport_added(
                (PeerTalk_Context *)ctx,
                peer->hot.id,
                transport,
                ctx->callbacks.user_data);
        }

        PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
            "Peer '%s' (id=%u) now reachable via %s%s (transports: 0x%x)",
            ctx->peer_names[peer->hot.name_idx],
            peer->hot.id,
            (transport & PT_TRANSPORT_TCP) ? "TCP" : "",
            (transport & PT_TRANSPORT_ADSP) ? "ADSP" : "",
            peer->cold.info.transports_available);
    }

    return 0;
}

/* ========================================================================== */
/* Remove Transport from Peer                                                 */
/* ========================================================================== */

/**
 * Remove a transport from a peer.
 *
 * If this was the last transport, the peer is destroyed entirely
 * and the on_peer_lost callback is fired.
 *
 * @param ctx        Context
 * @param peer       Peer to update
 * @param transport  Transport to remove (PT_TRANSPORT_*)
 * @return           0 on success, -1 on error
 */
int pt_peer_remove_transport(struct pt_context *ctx,
                              struct pt_peer *peer,
                              uint16_t transport)
{
    if (!ctx || !peer || peer->hot.magic != PT_PEER_MAGIC) {
        return -1;
    }

    if (!(peer->cold.info.transports_available & transport)) {
        return 0;  /* Doesn't have this transport - nothing to do */
    }

    /* If currently connected via this transport, we need to note that */
    if (peer->cold.info.transport_connected == transport) {
        peer->cold.info.transport_connected = 0;
        peer->cold.info.connected = 0;
    }

    peer->cold.info.transports_available &= ~transport;

    /* Fire transport removed callback */
    if (ctx->callbacks.on_transport_removed) {
        ctx->callbacks.on_transport_removed(
            (PeerTalk_Context *)ctx,
            peer->hot.id,
            transport,
            ctx->callbacks.user_data);
    }

    /* If no transports left, peer is completely lost */
    if (peer->cold.info.transports_available == 0) {
        if (ctx->callbacks.on_peer_lost) {
            ctx->callbacks.on_peer_lost(
                (PeerTalk_Context *)ctx,
                peer->hot.id,
                ctx->callbacks.user_data);
        }
        pt_peer_destroy(ctx, peer);
    }

    return 0;
}

/* ========================================================================== */
/* Create Peer from Discovery (with Deduplication)                            */
/* ========================================================================== */

/**
 * Create or merge a peer from a discovery event.
 *
 * If auto_merge_peers is enabled in config and a peer with the same name
 * exists on a different transport, the new transport is merged into the
 * existing peer instead of creating a duplicate.
 *
 * @param ctx        Context
 * @param name       Discovered peer name
 * @param transport  Transport discovered on (PT_TRANSPORT_TCP, PT_TRANSPORT_ADSP)
 * @param address    Transport-specific address
 * @param port       Transport-specific port
 * @return           Peer pointer (new or existing), or NULL on failure
 */
struct pt_peer *pt_peer_create_from_discovery(struct pt_context *ctx,
                                               const char *name,
                                               uint16_t transport,
                                               uint32_t address,
                                               uint16_t port)
{
    struct pt_peer *peer;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return NULL;
    }

    /* Check for existing peer we should merge with */
    if (ctx->config.auto_merge_peers) {
        peer = pt_peer_find_match(ctx, name, transport);
        if (peer) {
            pt_peer_add_transport(ctx, peer, transport, address, port);
            return peer;
        }
    }

    /* Create new peer via standard path */
    peer = pt_peer_create(ctx, name, address, port);
    if (!peer) return NULL;

    /* Set transport info */
    peer->cold.info.transports_available = transport;
    peer->cold.addresses[0].transport = transport;

    return peer;
}

/* ========================================================================== */
/* Select Best Transport for Connection                                       */
/* ========================================================================== */

/**
 * Select the best transport to use for connecting to a peer.
 *
 * Considers peer-specific preference (if set), global preference from
 * config, and which transports are actually available.
 *
 * @param ctx   Context (for global preference)
 * @param peer  Peer to connect to
 * @return      Best transport (PT_TRANSPORT_*), or 0 if none available
 */
uint16_t pt_peer_select_transport(struct pt_context *ctx,
                                   struct pt_peer *peer)
{
    uint16_t available;
    PeerTalk_TransportPref pref;

    if (!ctx || !peer) return 0;

    available = peer->cold.info.transports_available;
    if (available == 0) return 0;

    /* Use peer-specific preference if set, otherwise global */
    pref = (PeerTalk_TransportPref)peer->hot.preferred_transport;
    if (pref == PT_PREFER_NONE) {
        pref = (PeerTalk_TransportPref)ctx->config.pref;
    }

    switch (pref) {
    case PT_PREFER_TCP:
        if (available & PT_TRANSPORT_TCP) return PT_TRANSPORT_TCP;
        if (available & PT_TRANSPORT_ADSP) return PT_TRANSPORT_ADSP;
        break;

    case PT_PREFER_ADSP:
        if (available & PT_TRANSPORT_ADSP) return PT_TRANSPORT_ADSP;
        if (available & PT_TRANSPORT_TCP) return PT_TRANSPORT_TCP;
        break;

    case PT_PREFER_FASTEST:
        /* Return any available - caller can refine based on timing */
        if (available & PT_TRANSPORT_TCP) return PT_TRANSPORT_TCP;
        if (available & PT_TRANSPORT_ADSP) return PT_TRANSPORT_ADSP;
        break;

    case PT_PREFER_NONE:
    default:
        /* Return any available */
        if (available & PT_TRANSPORT_TCP) return PT_TRANSPORT_TCP;
        if (available & PT_TRANSPORT_ADSP) return PT_TRANSPORT_ADSP;
        break;
    }

    return 0;  /* No transport available */
}

/* ========================================================================== */
/* Public API: Merge Two Peers                                                */
/* ========================================================================== */

/**
 * Manually merge two peers into one.
 *
 * Transfers all transport addresses from merge_peer to keep_peer,
 * fires the on_peers_merged callback, then destroys merge_peer.
 */
PeerTalk_Error PeerTalk_MergePeers(PeerTalk_Context *ctx,
                                    PeerTalk_PeerID keep_id,
                                    PeerTalk_PeerID merge_id)
{
    struct pt_context *c = (struct pt_context *)ctx;
    struct pt_peer *keep_peer;
    struct pt_peer *merge_peer;
    int i;

    if (!c || c->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

    keep_peer = pt_peer_find_by_id(c, keep_id);
    merge_peer = pt_peer_find_by_id(c, merge_id);

    if (!keep_peer || !merge_peer) {
        return PT_ERR_PEER_NOT_FOUND;
    }

    if (keep_peer == merge_peer) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Transfer all addresses from merge to keep */
    for (i = 0; i < merge_peer->hot.address_count; i++) {
        pt_peer_address *addr = &merge_peer->cold.addresses[i];
        if (addr->transport != 0) {
            pt_peer_add_transport(c, keep_peer,
                                  addr->transport,
                                  addr->address,
                                  addr->port);
        }
    }

    /* Fire merge callback */
    if (c->callbacks.on_peers_merged) {
        c->callbacks.on_peers_merged(ctx, keep_id, merge_id,
                                      c->callbacks.user_data);
    }

    PT_CTX_INFO(c, PT_LOG_CAT_CONNECT,
        "Merged peer %u into peer %u (transports: 0x%x)",
        merge_id, keep_id, keep_peer->cold.info.transports_available);

    /* Destroy merged peer */
    pt_peer_destroy(c, merge_peer);

    return PT_OK;
}

/* ========================================================================== */
/* Public API: Get Peers by Transport                                         */
/* ========================================================================== */

/**
 * Get peers filtered by transport mask.
 *
 * Returns only peers whose transports_available includes at least one
 * of the requested transports.
 */
PeerTalk_Error PeerTalk_GetPeersByTransport(PeerTalk_Context *ctx,
                                             uint16_t transport_mask,
                                             PeerTalk_PeerInfo *peers,
                                             uint16_t max_peers,
                                             uint16_t *out_count)
{
    struct pt_context *c = (struct pt_context *)ctx;
    uint16_t found = 0;
    uint16_t i;

    if (!c || c->magic != PT_CONTEXT_MAGIC || !peers || !out_count) {
        return PT_ERR_INVALID_PARAM;
    }

    for (i = 0; i < c->max_peers && found < max_peers; i++) {
        struct pt_peer *peer = &c->peers[i];

        if (peer->hot.state == PT_PEER_STATE_UNUSED) {
            continue;
        }

        /* Check if peer has any of the requested transports */
        if (peer->cold.info.transports_available & transport_mask) {
            pt_peer_get_info(peer, &peers[found]);
            found++;
        }
    }

    *out_count = found;
    return PT_OK;
}

/* ========================================================================== */
/* Public API: Get Peer Transports                                            */
/* ========================================================================== */

/**
 * Check which transports can reach a peer.
 */
uint16_t PeerTalk_GetPeerTransports(PeerTalk_Context *ctx,
                                     PeerTalk_PeerID peer_id)
{
    struct pt_context *c = (struct pt_context *)ctx;
    struct pt_peer *peer;

    if (!c || c->magic != PT_CONTEXT_MAGIC) {
        return 0;
    }

    peer = pt_peer_find_by_id(c, peer_id);
    if (!peer) {
        return 0;
    }

    return peer->cold.info.transports_available;
}

/* ========================================================================== */
/* Public API: Remove Peer                                                    */
/* ========================================================================== */

/**
 * Remove peer entirely (all transports).
 */
PeerTalk_Error PeerTalk_RemovePeer(PeerTalk_Context *ctx,
                                    PeerTalk_PeerID peer_id)
{
    struct pt_context *c = (struct pt_context *)ctx;
    struct pt_peer *peer;

    if (!c || c->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

    peer = pt_peer_find_by_id(c, peer_id);
    if (!peer) {
        return PT_ERR_PEER_NOT_FOUND;
    }

    PT_CTX_INFO(c, PT_LOG_CAT_CONNECT,
        "Removing peer %u '%s' (all transports)",
        peer_id, ctx->peer_names[peer->hot.name_idx]);

    pt_peer_destroy(c, peer);
    return PT_OK;
}

/* ========================================================================== */
/* Public API: Remove Peer Transport                                          */
/* ========================================================================== */

/**
 * Remove a specific transport from a peer.
 * If last transport, peer is removed entirely.
 */
PeerTalk_Error PeerTalk_RemovePeerTransport(PeerTalk_Context *ctx,
                                             PeerTalk_PeerID peer_id,
                                             uint16_t transport)
{
    struct pt_context *c = (struct pt_context *)ctx;
    struct pt_peer *peer;

    if (!c || c->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

    peer = pt_peer_find_by_id(c, peer_id);
    if (!peer) {
        return PT_ERR_PEER_NOT_FOUND;
    }

    pt_peer_remove_transport(c, peer, transport);
    return PT_OK;
}

/* ========================================================================== */
/* Public API: Set Peer Transport Preference                                  */
/* ========================================================================== */

/**
 * Set transport preference for a specific peer.
 */
PeerTalk_Error PeerTalk_SetPeerTransportPref(PeerTalk_Context *ctx,
                                              PeerTalk_PeerID peer_id,
                                              PeerTalk_TransportPref pref)
{
    struct pt_context *c = (struct pt_context *)ctx;
    struct pt_peer *peer;

    if (!c || c->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

    peer = pt_peer_find_by_id(c, peer_id);
    if (!peer) {
        return PT_ERR_PEER_NOT_FOUND;
    }

    peer->hot.preferred_transport = (uint8_t)pref;
    return PT_OK;
}

/* ========================================================================== */
/* Public API: Connect Via Specific Transport                                 */
/* ========================================================================== */

/**
 * Connect to peer via a specific transport.
 *
 * Stub implementation - the actual connection logic is in the platform
 * layer (posix, mactcp, ot). This function validates parameters and
 * sets the transport_connected field before delegating.
 */
PeerTalk_Error PeerTalk_ConnectVia(PeerTalk_Context *ctx,
                                    PeerTalk_PeerID peer_id,
                                    uint16_t transport)
{
    struct pt_context *c = (struct pt_context *)ctx;
    struct pt_peer *peer;

    if (!c || c->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

    peer = pt_peer_find_by_id(c, peer_id);
    if (!peer) {
        return PT_ERR_PEER_NOT_FOUND;
    }

    /* Verify peer is reachable via requested transport */
    if (!(peer->cold.info.transports_available & transport)) {
        return PT_ERR_NOT_SUPPORTED;
    }

    /* Set the transport to connect via */
    peer->cold.info.transport_connected = transport;

    /* Delegate to standard connect (platform layer uses transport_connected) */
    return PeerTalk_Connect(ctx, peer_id);
}

/* ========================================================================== */
/* Public API: Reconnect Via Alternate Transport                              */
/* ========================================================================== */

/**
 * Reconnect via an alternate transport.
 *
 * Disconnects from current transport and reconnects via the specified one.
 */
PeerTalk_Error PeerTalk_ReconnectVia(PeerTalk_Context *ctx,
                                      PeerTalk_PeerID peer_id,
                                      uint16_t transport)
{
    struct pt_context *c = (struct pt_context *)ctx;
    struct pt_peer *peer;

    if (!c || c->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

    peer = pt_peer_find_by_id(c, peer_id);
    if (!peer) {
        return PT_ERR_PEER_NOT_FOUND;
    }

    /* Verify peer is reachable via requested transport */
    if (!(peer->cold.info.transports_available & transport)) {
        return PT_ERR_NOT_SUPPORTED;
    }

    /* Disconnect if currently connected */
    if (peer->hot.state == PT_PEER_STATE_CONNECTED ||
        peer->hot.state == PT_PEER_STATE_CONNECTING) {
        PeerTalk_Disconnect(ctx, peer_id);
    }

    /* Connect via the new transport */
    return PeerTalk_ConnectVia(ctx, peer_id, transport);
}
