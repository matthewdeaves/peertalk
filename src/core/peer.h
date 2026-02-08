/* peer.h - Peer management for PeerTalk
 *
 * Implements peer tracking with state machine for lifecycle management,
 * including timeout tracking and connection state.
 */

#ifndef PT_PEER_H
#define PT_PEER_H

#include "pt_internal.h"
#include "../../include/peertalk.h"

/* Forward declaration */
struct pt_context;
struct pt_peer;

/* ========================================================================
 * Peer State Aliases
 * ======================================================================== */

/* Convenience aliases for peer states (from pt_types.h) */
#define PT_PEER_UNUSED        PT_PEER_STATE_UNUSED
#define PT_PEER_DISCOVERED    PT_PEER_STATE_DISCOVERED
#define PT_PEER_CONNECTING    PT_PEER_STATE_CONNECTING
#define PT_PEER_CONNECTED     PT_PEER_STATE_CONNECTED
#define PT_PEER_DISCONNECTING PT_PEER_STATE_DISCONNECTING
#define PT_PEER_FAILED        PT_PEER_STATE_FAILED

/* ========================================================================
 * Peer List Operations
 * ======================================================================== */

/* Initialize peer list
 *
 * Allocates array of peers and initializes lookup structures.
 *
 * Args:
 *   ctx       - Context
 *   max_peers - Maximum number of peers to support
 *
 * Returns: 0 on success, negative error code on failure
 */
int pt_peer_list_init(struct pt_context *ctx, uint16_t max_peers);

/* Free peer list
 *
 * Deallocates peer array and clears lookup structures.
 *
 * Args:
 *   ctx - Context
 */
void pt_peer_list_free(struct pt_context *ctx);

/* ========================================================================
 * Peer Lookup Functions
 * ======================================================================== */

/* Find peer by ID
 *
 * O(1) lookup using peer_id_to_index table.
 *
 * Args:
 *   ctx - Context
 *   id  - Peer ID (1-based)
 *
 * Returns: Peer pointer or NULL if not found/invalid
 */
struct pt_peer *pt_peer_find_by_id(struct pt_context *ctx, PeerTalk_PeerID id);

/* Find peer by address
 *
 * HOT PATH: Called on EVERY incoming packet.
 * Linear scan through peers checking address/port.
 *
 * DOD Performance Note: Currently accesses address/port from cold storage
 * causing cache thrashing on 68030. See implementation for optimization notes.
 *
 * Args:
 *   ctx  - Context
 *   ip   - IPv4 address
 *   port - Port number
 *
 * Returns: Peer pointer or NULL if not found
 */
struct pt_peer *pt_peer_find_by_addr(struct pt_context *ctx,
                                      uint32_t ip, uint16_t port);

/* Find peer by name
 *
 * Required by Phase 5.9 for cross-transport deduplication.
 * Linear scan using centralized peer_names[] table.
 *
 * Args:
 *   ctx  - Context
 *   name - Peer name (null-terminated)
 *
 * Returns: Peer pointer or NULL if not found
 */
struct pt_peer *pt_peer_find_by_name(struct pt_context *ctx, const char *name);

/* Find unused peer slot
 *
 * Linear scan for first peer with state == PT_PEER_UNUSED.
 *
 * Args:
 *   ctx - Context
 *
 * Returns: Unused peer pointer or NULL if all slots occupied
 */
struct pt_peer *pt_peer_find_unused(struct pt_context *ctx);

/* ========================================================================
 * Peer Lifecycle
 * ======================================================================== */

/* Create peer
 *
 * Allocates peer slot and initializes with discovery information.
 * If peer already exists by address, updates last_seen and name.
 *
 * Args:
 *   ctx  - Context
 *   name - Peer name (can be NULL or empty)
 *   ip   - IPv4 address
 *   port - Port number
 *
 * Returns: Peer pointer or NULL if no slots available
 */
struct pt_peer *pt_peer_create(struct pt_context *ctx,
                               const char *name,
                               uint32_t ip, uint16_t port);

/* Destroy peer
 *
 * Clears sensitive data and marks slot as UNUSED.
 * Decrements peer_count.
 *
 * Args:
 *   ctx  - Context
 *   peer - Peer to destroy
 */
void pt_peer_destroy(struct pt_context *ctx, struct pt_peer *peer);

/* ========================================================================
 * State Management
 * ======================================================================== */

/* Set peer state
 *
 * Validates and performs state transition according to state machine rules.
 *
 * Valid transitions:
 * - UNUSED → DISCOVERED
 * - DISCOVERED → CONNECTING, CONNECTED, DISCOVERED (refresh), UNUSED
 * - CONNECTING → CONNECTED, FAILED, UNUSED
 * - CONNECTED → DISCONNECTING, FAILED, UNUSED
 * - DISCONNECTING → UNUSED
 * - FAILED → UNUSED, DISCOVERED (recovery)
 *
 * Logging:
 * - INFO: Transitions TO CONNECTED (operational visibility)
 * - DEBUG: All other successful transitions
 * - WARN: Invalid transitions
 *
 * Args:
 *   ctx       - Context (can be NULL to disable logging)
 *   peer      - Peer
 *   new_state - New state
 *
 * Returns: 0 on success, -1 on invalid transition
 */
int pt_peer_set_state(struct pt_context *ctx, struct pt_peer *peer,
                      pt_peer_state new_state);

/* Get peer state string
 *
 * Args:
 *   state - Peer state
 *
 * Returns: String representation (e.g., "CONNECTED")
 */
const char *pt_peer_state_str(pt_peer_state state);

/* ========================================================================
 * Timeout & Validation
 * ======================================================================== */

/* Check if peer is timed out
 *
 * Compares (now - peer->last_seen) against timeout threshold.
 *
 * Args:
 *   peer          - Peer to check
 *   now           - Current tick count
 *   timeout_ticks - Timeout threshold in ticks
 *
 * Returns: 1 if timed out, 0 if not
 */
int pt_peer_is_timed_out(struct pt_peer *peer, pt_tick_t now,
                         pt_tick_t timeout_ticks);

/* Check buffer canaries for overflow detection
 *
 * ISR-SAFETY WARNING: This function calls PT_Log and is NOT ISR-safe.
 * It MUST be called from the main event loop only, NOT from:
 * - MacTCP ASR callbacks
 * - Open Transport notifiers
 * - ADSP completion routines
 *
 * Returns: 0 if canaries are valid, -1 if corruption detected.
 * On corruption, also sets peer->canary_corrupt flag (volatile) for
 * checking from ISR context without logging.
 *
 * Args:
 *   ctx  - Logging context (can be NULL to disable logging, but flag still set)
 *   peer - Peer to check
 *
 * Returns: 0 if valid, -1 if corruption detected
 */
int pt_peer_check_canaries(struct pt_context *ctx, struct pt_peer *peer);

/* Get peer information
 *
 * Copies peer data to public PeerTalk_PeerInfo structure.
 *
 * Args:
 *   peer - Peer
 *   info - Output info structure
 */
void pt_peer_get_info(struct pt_peer *peer, PeerTalk_PeerInfo *info);

/* ========================================================================
 * Flow Control
 * ======================================================================== */

/* Pressure change threshold for sending updates.
 * When local pressure crosses these thresholds, we send a capability update
 * to inform the peer. This avoids sending updates on minor fluctuations.
 */
#define PT_PRESSURE_UPDATE_THRESHOLD 25  /* Report when crossing 25%, 50%, 75% */

/* Check if pressure update needed for a peer
 *
 * Compares current recv queue pressure against last_reported_pressure.
 * If pressure crossed a threshold (25%, 50%, 75%), marks update pending.
 *
 * Call this from poll loop after processing received data.
 *
 * Args:
 *   ctx  - Context
 *   peer - Peer to check
 *
 * Returns: 1 if update needed, 0 if not
 */
int pt_peer_check_pressure_update(struct pt_context *ctx, struct pt_peer *peer);

/* Get pressure-based throttle decision
 *
 * Checks peer's reported buffer_pressure and returns whether sending
 * should be throttled. Used in send path for flow control.
 *
 * Decision thresholds:
 *   0-50:  No throttle (send normally)
 *   50-75: Light throttle (skip LOW priority)
 *   75-90: Heavy throttle (skip NORMAL and LOW)
 *   90+:   Blocking (only CRITICAL passes)
 *
 * Args:
 *   peer     - Peer to check
 *   priority - Message priority (PT_PRIORITY_*)
 *
 * Returns: 1 if should throttle (skip send), 0 if should send
 */
int pt_peer_should_throttle(struct pt_peer *peer, uint8_t priority);

#endif /* PT_PEER_H */
