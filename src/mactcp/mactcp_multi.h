/**
 * @file mactcp_multi.h
 * @brief Multi-Transport Infrastructure for MacTCP + AppleTalk
 *
 * Provides unified transport layer for Macs with both TCP/IP and AppleTalk.
 * Works in MacTCP-only mode when AppleTalk (Phase 7) is not linked.
 *
 * Key features:
 * - Transport flags for tracking available/active transports
 * - Peer deduplication across transports (same name = same peer)
 * - Unified poll loop for both networks
 *
 * Session 5.9 of Phase 5 (MacTCP).
 */

#ifndef PT_MACTCP_MULTI_H
#define PT_MACTCP_MULTI_H

#include "mactcp_defs.h"

/* Forward declarations */
struct pt_context;
struct pt_peer;
struct pt_appletalk_data;  /* Defined in Phase 7 */

/* ========================================================================== */
/* Transport Flags                                                             */
/* ========================================================================== */

/**
 * Transport type flags.
 * Used to track which transports a peer is reachable on.
 */
#define PT_TRANSPORT_NONE       0x00
#define PT_TRANSPORT_TCP        0x01  /* TCP/IP via MacTCP */
#define PT_TRANSPORT_UDP        0x02  /* UDP/IP via MacTCP */
#define PT_TRANSPORT_APPLETALK  0x04  /* AppleTalk via ADSP */
#define PT_TRANSPORT_ALL        0x07  /* All transports */

/**
 * Transport preference for sending.
 * When a peer is reachable on multiple transports, use this order.
 */
#define PT_PREFER_TCP           0x01
#define PT_PREFER_APPLETALK     0x02

/* ========================================================================== */
/* Multi-Transport Context Extension                                           */
/* ========================================================================== */

/**
 * Multi-transport context.
 *
 * This structure extends pt_mactcp_data when AppleTalk support is linked.
 * The MacTCP data is always present; AppleTalk is optional.
 *
 * Memory layout:
 *   [pt_context][pt_mactcp_multi_data]
 *   Where pt_mactcp_multi_data contains:
 *     [pt_mactcp_data][pt_appletalk_data*]
 */
typedef struct pt_mactcp_multi_data {
    /* MacTCP data (always present) */
    pt_mactcp_data          mactcp;

    /* AppleTalk data pointer (NULL when Phase 7 not linked) */
    struct pt_appletalk_data *appletalk;

    /* Multi-transport state */
    uint8_t                 transports_available;  /* Bitmask of available transports */
    uint8_t                 transports_active;     /* Bitmask of currently active transports */
    uint8_t                 preferred_transport;   /* PT_PREFER_* for send routing */
    uint8_t                 appletalk_linked;      /* Non-zero if Phase 7 linked */

} pt_mactcp_multi_data;

/* ========================================================================== */
/* Accessor Functions                                                          */
/* ========================================================================== */

/**
 * Get multi-transport data from context.
 *
 * Works whether compiled in single-transport or multi-transport mode.
 * In single-transport mode, returns the same pointer as pt_mactcp_get().
 *
 * @param ctx  PeerTalk context
 * @return     Pointer to multi-transport data
 */
pt_mactcp_multi_data *pt_mactcp_multi_get(struct pt_context *ctx);

/* ========================================================================== */
/* Initialization / Shutdown                                                   */
/* ========================================================================== */

/**
 * Initialize multi-transport layer.
 *
 * Called from platform init. Initializes MacTCP first, then attempts
 * AppleTalk if linked. Either or both may succeed.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success (at least one transport available),
 *             negative error code if all transports failed
 */
int pt_mactcp_multi_init(struct pt_context *ctx);

/**
 * Shutdown multi-transport layer.
 *
 * Shuts down all active transports in reverse order of initialization.
 *
 * @param ctx  PeerTalk context
 */
void pt_mactcp_multi_shutdown(struct pt_context *ctx);

/* ========================================================================== */
/* Unified Poll                                                                */
/* ========================================================================== */

/**
 * Poll all active transports.
 *
 * Processes events from MacTCP and AppleTalk (if active) in a single call.
 * Called from PeerTalk_Poll().
 *
 * @param ctx  PeerTalk context
 * @return     0 on success
 */
int pt_mactcp_multi_poll(struct pt_context *ctx);

/* ========================================================================== */
/* Transport Query                                                             */
/* ========================================================================== */

/**
 * Get available transports.
 *
 * Returns bitmask of transports that were successfully initialized.
 *
 * @param ctx  PeerTalk context
 * @return     Bitmask of PT_TRANSPORT_* flags
 */
uint8_t pt_mactcp_multi_get_transports(struct pt_context *ctx);

/**
 * Check if a specific transport is available.
 *
 * @param ctx       PeerTalk context
 * @param transport PT_TRANSPORT_* flag to check
 * @return          Non-zero if available
 */
int pt_mactcp_multi_has_transport(struct pt_context *ctx, uint8_t transport);

/* ========================================================================== */
/* Peer Deduplication                                                          */
/* ========================================================================== */

/**
 * Find or create peer with transport deduplication.
 *
 * When the same peer is discovered on both TCP/IP and AppleTalk
 * (identified by matching name), we merge into a single peer entry
 * rather than creating duplicates.
 *
 * If peer exists by name:
 *   - Adds new transport to peer's transports_available
 *   - Updates address if TCP discovery
 *   - Updates last_seen
 *
 * If peer is new:
 *   - Creates new peer with initial transport
 *   - Sets address from TCP or leaves as 0 for AppleTalk-only
 *
 * @param ctx             PeerTalk context
 * @param name            Peer name (for deduplication)
 * @param tcp_ip          TCP/IP address (0 for AppleTalk-only)
 * @param tcp_port        TCP port (0 for AppleTalk-only)
 * @param transport_flags PT_TRANSPORT_* flags for this discovery
 * @return                Peer pointer or NULL on error
 */
struct pt_peer *pt_mactcp_multi_find_or_create_peer(
    struct pt_context *ctx,
    const char *name,
    ip_addr tcp_ip,
    tcp_port tcp_port,
    uint8_t transport_flags);

/**
 * Add transport to existing peer.
 *
 * Updates peer's transports_available bitmask.
 *
 * @param peer           Peer to update
 * @param transport_flag PT_TRANSPORT_* flag to add
 */
void pt_mactcp_multi_peer_add_transport(struct pt_peer *peer,
                                         uint8_t transport_flag);

/**
 * Remove transport from peer.
 *
 * Updates peer's transports_available bitmask.
 * If no transports remain, peer may be destroyed by caller.
 *
 * @param peer           Peer to update
 * @param transport_flag PT_TRANSPORT_* flag to remove
 */
void pt_mactcp_multi_peer_remove_transport(struct pt_peer *peer,
                                            uint8_t transport_flag);

/**
 * Get peer's available transports.
 *
 * @param peer  Peer to query
 * @return      Bitmask of PT_TRANSPORT_* flags
 */
uint8_t pt_mactcp_multi_peer_get_transports(struct pt_peer *peer);

/* ========================================================================== */
/* AppleTalk Weak Linking                                                      */
/* ========================================================================== */

/**
 * Register AppleTalk implementation.
 *
 * Called by Phase 7 during static initialization to register the
 * AppleTalk callbacks. This enables weak linking - if Phase 7 is
 * not linked, these remain NULL and AppleTalk is disabled.
 *
 * @param init_fn     AppleTalk initialization function
 * @param shutdown_fn AppleTalk shutdown function
 * @param poll_fn     AppleTalk poll function
 */
typedef int  (*pt_at_init_fn)(struct pt_context *ctx, struct pt_appletalk_data **out);
typedef void (*pt_at_shutdown_fn)(struct pt_context *ctx, struct pt_appletalk_data *at);
typedef int  (*pt_at_poll_fn)(struct pt_context *ctx, struct pt_appletalk_data *at);

void pt_mactcp_multi_register_appletalk(
    pt_at_init_fn init_fn,
    pt_at_shutdown_fn shutdown_fn,
    pt_at_poll_fn poll_fn);

#endif /* PT_MACTCP_MULTI_H */
