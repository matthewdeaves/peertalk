/* peer.c - Peer management implementation */

#include "peer.h"
#include "pt_compat.h"
#include "../../include/peertalk.h"

/* ========================================================================
 * Peer List Operations
 * ======================================================================== */

int pt_peer_list_init(struct pt_context *ctx, uint16_t max_peers)
{
    size_t alloc_size;
    uint16_t i;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Allocate peer array */
    alloc_size = sizeof(struct pt_peer) * max_peers;
    ctx->peers = (struct pt_peer *)pt_alloc_clear(alloc_size);
    if (!ctx->peers) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
                  "Failed to allocate %zu bytes for %u peers",
                  alloc_size, max_peers);
        return PT_ERR_NO_MEMORY;
    }

    /* Initialize all peer slots */
    for (i = 0; i < max_peers; i++) {
        struct pt_peer *peer = &ctx->peers[i];
        peer->hot.id = (PeerTalk_PeerID)(i + 1);  /* IDs start at 1 */
        peer->hot.state = PT_PEER_STATE_UNUSED;
        peer->hot.magic = 0;  /* Not valid until created */
        peer->hot.name_idx = (uint8_t)i;  /* Index into centralized name table */
    }

    ctx->max_peers = max_peers;
    ctx->peer_count = 0;

    PT_CTX_INFO(ctx, PT_LOG_CAT_INIT,
               "Peer list initialized: %u slots, %zu bytes",
               max_peers, alloc_size);

    return 0;
}

void pt_peer_list_free(struct pt_context *ctx)
{
    if (!ctx || !ctx->peers) {
        return;
    }

    pt_free(ctx->peers);
    ctx->peers = NULL;
    ctx->max_peers = 0;
    ctx->peer_count = 0;
}

/* ========================================================================
 * Peer Lookup Functions
 * ======================================================================== */

struct pt_peer *pt_peer_find_by_id(struct pt_context *ctx, PeerTalk_PeerID id)
{
    uint8_t index;
    struct pt_peer *peer;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return NULL;
    }

    /* ID 0 is invalid, IDs start at 1 */
    if (id == 0 || id > ctx->max_peers) {
        return NULL;
    }

    /* Convert ID to array index (IDs are 1-based) */
    index = (uint8_t)(id - 1);
    peer = &ctx->peers[index];

    /* Check if peer is valid */
    if (peer->hot.state == PT_PEER_STATE_UNUSED ||
        peer->hot.magic != PT_PEER_MAGIC) {
        return NULL;
    }

    return peer;
}

struct pt_peer *pt_peer_find_by_addr(struct pt_context *ctx,
                                      uint32_t ip, uint16_t port)
{
    uint16_t i;

    /* DOD PERFORMANCE NOTE:
     * This function is called on EVERY incoming packet to identify which peer
     * it belongs to. Currently it accesses peer->info.address and peer->info.port
     * which are in cold storage (~1.4KB per peer). On 68030 with 256-byte cache,
     * scanning 16 peers touches 22KB+ causing severe cache thrashing.
     *
     * RECOMMENDED OPTIMIZATIONS (Phase 1 modifications):
     * 1. Move address/port to pt_peer_hot struct (adds 6 bytes to hot data)
     * 2. OR: Add peer_addr_hash[] lookup table to pt_context (similar to
     *    peer_id_to_index[]) using simple hash: (ip ^ port) & (PT_MAX_PEERS-1)
     *
     * For low peer counts (<8), the current linear scan is acceptable.
     * For higher counts or on 68000/68020 (no cache), optimization is critical.
     *
     * CRITICAL: This function is called on EVERY incoming packet - must access
     * only hot data
     */

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return NULL;
    }

    for (i = 0; i < ctx->max_peers; i++) {
        struct pt_peer *peer = &ctx->peers[i];

        if (peer->hot.state != PT_PEER_STATE_UNUSED &&
            peer->cold.info.address == ip &&
            peer->cold.info.port == port) {
            return peer;
        }
    }

    return NULL;
}

struct pt_peer *pt_peer_find_by_name(struct pt_context *ctx, const char *name)
{
    uint16_t i;

    /* DOD Optimization: Use centralized peer_names[] table from Phase 1.
     * This avoids accessing cold storage (peer->info.name) which is ~1.4KB
     * per peer. On 68030 with 256-byte cache, this prevents severe cache
     * thrashing when scanning multiple peers.
     *
     * Phase 1 stores names in ctx->peer_names[name_idx] and the index
     * is stored in peer->hot.name_idx (hot storage, 32 bytes per peer).
     */

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return NULL;
    }

    if (!name || name[0] == '\0') {
        return NULL;
    }

    for (i = 0; i < ctx->max_peers; i++) {
        struct pt_peer *peer = &ctx->peers[i];
        const char *peer_name;

        if (peer->hot.state == PT_PEER_STATE_UNUSED) {
            continue;
        }

        /* Get peer name from centralized table */
        peer_name = ctx->peer_names[peer->hot.name_idx];

        /* Compare strings manually (no pt_strcmp available) */
        {
            const char *a = peer_name;
            const char *b = name;
            while (*a && *b && *a == *b) {
                a++;
                b++;
            }
            if (*a == *b) {
                return peer;
            }
        }
    }

    return NULL;
}

struct pt_peer *pt_peer_find_unused(struct pt_context *ctx)
{
    uint16_t i;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return NULL;
    }

    for (i = 0; i < ctx->max_peers; i++) {
        struct pt_peer *peer = &ctx->peers[i];
        if (peer->hot.state == PT_PEER_STATE_UNUSED) {
            return peer;
        }
    }

    return NULL;
}

/* ========================================================================
 * Peer Lifecycle
 * ======================================================================== */

struct pt_peer *pt_peer_create(struct pt_context *ctx,
                               const char *name,
                               uint32_t ip, uint16_t port)
{
    struct pt_peer *peer;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return NULL;
    }

    /* Check if peer already exists by address */
    peer = pt_peer_find_by_addr(ctx, ip, port);
    if (peer) {
        /* Update last_seen and name */
        peer->hot.last_seen = ctx->plat->get_ticks();

        if (name && name[0] != '\0') {
            size_t name_len = pt_strlen(name);
            if (name_len > PT_MAX_PEER_NAME) {
                name_len = PT_MAX_PEER_NAME;
            }
            pt_memcpy(ctx->peer_names[peer->hot.name_idx], name, name_len);
            ctx->peer_names[peer->hot.name_idx][name_len] = '\0';
        }

        return peer;
    }

    /* Find unused slot */
    peer = pt_peer_find_unused(ctx);
    if (!peer) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
                   "No available peer slots (max %u)", ctx->max_peers);
        return NULL;
    }

    /* Initialize peer */
    pt_memset(&peer->cold, 0, sizeof(peer->cold));

    /* Clear buffer lengths */
    peer->cold.obuflen = 0;
    peer->cold.ibuflen = 0;

#ifdef PT_DEBUG
    /* Set canaries in debug mode */
    peer->cold.obuf_canary = PT_CANARY_OBUF;
    peer->cold.ibuf_canary = PT_CANARY_IBUF;
#endif

    /* Set magic */
    peer->hot.magic = PT_PEER_MAGIC;

    /* Set address and port */
    peer->cold.info.address = ip;
    peer->cold.info.port = port;

    /* Initialize addresses array for PeerTalk_Connect() */
    peer->hot.address_count = 1;
    peer->cold.addresses[0].address = ip;
    peer->cold.addresses[0].port = port;
    peer->cold.addresses[0].transport = 0;  /* TCPIP transport */

    /* Clear connection state */
    peer->cold.info.connected = 0;
    peer->hot.latency_ms = 0;
    peer->cold.info.queue_pressure = 0;

    /* Set initial state */
    peer->hot.state = PT_PEER_STATE_DISCOVERED;

    /* Update last_seen */
    peer->hot.last_seen = ctx->plat->get_ticks();

    /* Clear connection timing and sequence numbers */
    peer->cold.ping_sent_time = 0;
    peer->hot.send_seq = 0;
    peer->hot.recv_seq = 0;

    /* Clear connection handle */
    peer->hot.connection = NULL;

    /* Copy name */
    if (name && name[0] != '\0') {
        size_t name_len = pt_strlen(name);
        if (name_len > PT_MAX_PEER_NAME) {
            name_len = PT_MAX_PEER_NAME;
        }
        pt_memcpy(ctx->peer_names[peer->hot.name_idx], name, name_len);
        ctx->peer_names[peer->hot.name_idx][name_len] = '\0';
    } else {
        ctx->peer_names[peer->hot.name_idx][0] = '\0';
    }

    /* Increment peer count */
    ctx->peer_count++;

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
               "Peer created: id=%u name='%s' addr=0x%08X port=%u",
               peer->hot.id, ctx->peer_names[peer->hot.name_idx], ip, port);

    return peer;
}

void pt_peer_destroy(struct pt_context *ctx, struct pt_peer *peer)
{
    if (!ctx || !peer || peer->hot.magic != PT_PEER_MAGIC) {
        return;
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
               "Peer destroyed: id=%u name='%s'",
               peer->hot.id, ctx->peer_names[peer->hot.name_idx]);

    /* Clear sensitive data */
    peer->hot.magic = 0;
    peer->hot.state = PT_PEER_STATE_UNUSED;
    ctx->peer_names[peer->hot.name_idx][0] = '\0';
    peer->cold.info.address = 0;
    peer->cold.info.port = 0;
    peer->cold.info.connected = 0;
    peer->hot.connection = NULL;

    /* Decrement peer count */
    if (ctx->peer_count > 0) {
        ctx->peer_count--;
    }
}

/* ========================================================================
 * State Management
 * ======================================================================== */

int pt_peer_set_state(struct pt_context *ctx, struct pt_peer *peer,
                      pt_peer_state new_state)
{
    pt_peer_state old_state;
    int valid_transition = 0;

    if (!peer || peer->hot.magic != PT_PEER_MAGIC) {
        return PT_ERR_INVALID_PARAM;
    }

    old_state = peer->hot.state;

    /* Check if transition is valid */
    switch (old_state) {
    case PT_PEER_STATE_UNUSED:
        valid_transition = (new_state == PT_PEER_STATE_DISCOVERED);
        break;

    case PT_PEER_STATE_DISCOVERED:
        valid_transition = (new_state == PT_PEER_STATE_CONNECTING ||
                           new_state == PT_PEER_STATE_CONNECTED ||
                           new_state == PT_PEER_STATE_DISCOVERED ||
                           new_state == PT_PEER_STATE_UNUSED);
        break;

    case PT_PEER_STATE_CONNECTING:
        valid_transition = (new_state == PT_PEER_STATE_CONNECTED ||
                           new_state == PT_PEER_STATE_FAILED ||
                           new_state == PT_PEER_STATE_UNUSED);
        break;

    case PT_PEER_STATE_CONNECTED:
        valid_transition = (new_state == PT_PEER_STATE_DISCONNECTING ||
                           new_state == PT_PEER_STATE_FAILED ||
                           new_state == PT_PEER_STATE_UNUSED);
        break;

    case PT_PEER_STATE_DISCONNECTING:
        valid_transition = (new_state == PT_PEER_STATE_UNUSED);
        break;

    case PT_PEER_STATE_FAILED:
        valid_transition = (new_state == PT_PEER_STATE_UNUSED ||
                           new_state == PT_PEER_STATE_DISCOVERED);
        break;

    default:
        valid_transition = 0;
        break;
    }

    if (!valid_transition) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
                       "Invalid state transition: %s → %s (peer id=%u)",
                       pt_peer_state_str(old_state),
                       pt_peer_state_str(new_state),
                       peer->hot.id);
        }
        return PT_ERR_INVALID_STATE;
    }

    /* Perform transition */
    peer->hot.state = new_state;

    /* Log transition */
    if (ctx) {
        if (new_state == PT_PEER_STATE_CONNECTED) {
            /* Operational visibility for connections */
            PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
                       "Peer state: %s → %s (peer id=%u)",
                       pt_peer_state_str(old_state),
                       pt_peer_state_str(new_state),
                       peer->hot.id);
        } else {
            /* Verbose diagnostics for other transitions */
            PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
                        "Peer state: %s → %s (peer id=%u)",
                        pt_peer_state_str(old_state),
                        pt_peer_state_str(new_state),
                        peer->hot.id);
        }
    }

    return 0;
}

const char *pt_peer_state_str(pt_peer_state state)
{
    switch (state) {
    case PT_PEER_STATE_UNUSED:        return "UNUSED";
    case PT_PEER_STATE_DISCOVERED:    return "DISCOVERED";
    case PT_PEER_STATE_CONNECTING:    return "CONNECTING";
    case PT_PEER_STATE_CONNECTED:     return "CONNECTED";
    case PT_PEER_STATE_DISCONNECTING: return "DISCONNECTING";
    case PT_PEER_STATE_FAILED:        return "FAILED";
    default:                          return "UNKNOWN";
    }
}

/* ========================================================================
 * Timeout & Validation
 * ======================================================================== */

int pt_peer_is_timed_out(struct pt_peer *peer, pt_tick_t now,
                         pt_tick_t timeout_ticks)
{
    pt_tick_t elapsed;

    if (!peer || peer->hot.last_seen == 0) {
        return 0;
    }

    elapsed = now - peer->hot.last_seen;
    return (elapsed > timeout_ticks) ? 1 : 0;
}

/* cppcheck-suppress constParameter ; peer could be const but keeping non-const for API consistency */
int pt_peer_check_canaries(struct pt_context *ctx, struct pt_peer *peer)
{
    int corrupted = 0;

    if (!peer) {
        return -1;
    }

#ifdef PT_DEBUG
    /* Check output buffer canary */
    if (peer->cold.obuf_canary != PT_CANARY_OBUF) {
        if (ctx) {
            PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
                      "Output buffer overflow detected (peer id=%u): "
                      "expected 0x%08X, got 0x%08X",
                      peer->hot.id, PT_CANARY_OBUF, peer->cold.obuf_canary);
        }
        corrupted = 1;
    }

    /* Check input buffer canary */
    if (peer->cold.ibuf_canary != PT_CANARY_IBUF) {
        if (ctx) {
            PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
                      "Input buffer overflow detected (peer id=%u): "
                      "expected 0x%08X, got 0x%08X",
                      peer->hot.id, PT_CANARY_IBUF, peer->cold.ibuf_canary);
        }
        corrupted = 1;
    }
#else
    /* In release builds, canaries are not present - always return valid */
    (void)ctx;  /* Suppress unused warning */
#endif

    return corrupted ? -1 : 0;
}

void pt_peer_get_info(struct pt_peer *peer, PeerTalk_PeerInfo *info)
{
    if (!peer || !info) {
        return;
    }

    /* Copy peer info */
    pt_memcpy(info, &peer->cold.info, sizeof(PeerTalk_PeerInfo));

    /* Update fields from hot data */
    info->id = peer->hot.id;
    info->latency_ms = peer->hot.latency_ms;
    info->name_idx = peer->hot.name_idx;

    /* Update connected field based on current state */
    info->connected = (peer->hot.state == PT_PEER_STATE_CONNECTED) ? 1 : 0;
}
