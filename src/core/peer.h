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
 * Multi-Transport Peer Management (peer_multi.c)
 * ======================================================================== */

/* Find existing peer matching a discovery (for deduplication)
 *
 * Scans peer list for a peer with matching name that doesn't already
 * have the specified transport. Used during discovery to detect when
 * the same Mac is found via different transport mechanisms.
 *
 * Args:
 *   ctx            - Context
 *   name           - Discovered peer name
 *   new_transport  - Transport being discovered (PT_TRANSPORT_*)
 *
 * Returns: Matching peer, or NULL if no match
 */
struct pt_peer *pt_peer_find_match(struct pt_context *ctx,
                                    const char *name,
                                    uint16_t new_transport);

/* Add transport to existing peer (merge)
 *
 * Stores transport-specific address and fires on_transport_added callback.
 *
 * Args:
 *   ctx       - Context
 *   peer      - Existing peer
 *   transport - New transport (PT_TRANSPORT_TCP, PT_TRANSPORT_ADSP)
 *   address   - Transport-specific address
 *   port      - Transport-specific port
 *
 * Returns: 0 on success, -1 on error
 */
int pt_peer_add_transport(struct pt_context *ctx,
                           struct pt_peer *peer,
                           uint16_t transport,
                           uint32_t address, uint16_t port);

/* Remove transport from peer
 *
 * If last transport, peer is destroyed and on_peer_lost fires.
 *
 * Args:
 *   ctx       - Context
 *   peer      - Peer to update
 *   transport - Transport to remove (PT_TRANSPORT_*)
 *
 * Returns: 0 on success, -1 on error
 */
int pt_peer_remove_transport(struct pt_context *ctx,
                              struct pt_peer *peer,
                              uint16_t transport);

/* Create peer from discovery with deduplication
 *
 * If auto_merge_peers is enabled and a peer with matching name exists
 * on a different transport, merges instead of creating duplicate.
 *
 * Args:
 *   ctx       - Context
 *   name      - Discovered peer name
 *   transport - Transport discovered on
 *   address   - Transport-specific address
 *   port      - Transport-specific port
 *
 * Returns: Peer pointer (new or existing), or NULL on failure
 */
struct pt_peer *pt_peer_create_from_discovery(struct pt_context *ctx,
                                               const char *name,
                                               uint16_t transport,
                                               uint32_t address,
                                               uint16_t port);

/* Select best transport for connecting to a peer
 *
 * Considers peer-specific and global transport preferences.
 *
 * Args:
 *   ctx  - Context
 *   peer - Peer to connect to
 *
 * Returns: Best transport (PT_TRANSPORT_*), or 0 if none available
 */
uint16_t pt_peer_select_transport(struct pt_context *ctx,
                                   struct pt_peer *peer);

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

/* Check rate limit using token bucket algorithm
 *
 * Checks if sending 'bytes' would exceed the peer's rate limit.
 * If rate limiting is active (rate_limit_bytes_per_sec > 0), the
 * function refills tokens based on elapsed time and checks if
 * enough tokens are available for this send.
 *
 * Call from the send path after pressure-based throttling. Returns
 * non-zero if the send should be rate-limited.
 *
 * Args:
 *   ctx   - Context (for pt_get_ticks())
 *   peer  - Peer to check
 *   bytes - Number of bytes to send
 *
 * Returns: 0 if send is allowed, 1 if rate limited
 */
int pt_peer_check_rate_limit(struct pt_context *ctx, struct pt_peer *peer,
                              uint16_t bytes);

/* ========================================================================
 * Adaptive Performance Tuning
 * ======================================================================== */

/* Update adaptive chunk size based on RTT
 *
 * Adjusts effective_chunk and pipeline_depth based on measured RTT.
 * Call this after updating peer->hot.latency_ms.
 *
 * Tuning logic:
 *   RTT < 50ms:  chunk=4096, pipeline=4 (Fast LAN)
 *   RTT < 100ms: chunk=2048, pipeline=3
 *   RTT < 200ms: chunk=1024, pipeline=2
 *   RTT >= 200ms: chunk=512, pipeline=1 (Slow/lossy)
 *
 * Args:
 *   ctx  - Context (for logging)
 *   peer - Peer to update
 */
void pt_peer_update_adaptive_params(struct pt_context *ctx, struct pt_peer *peer);

/* ========================================================================
 * Async Send Pipeline
 * ======================================================================== */

/* Initialize async send pipeline for a peer
 *
 * Allocates send buffers for pipelined async sends. Call when peer
 * transitions to CONNECTED state. On MacTCP, also allocates TCPiopb
 * structures for each slot.
 *
 * Memory per peer (standard build, depth=4):
 *   - 4 x buffer (~4KB each) = 16,448 bytes
 *   - 4 x pt_send_slot = 96 bytes
 *   Total: ~17KB per peer
 *
 * Memory per peer (lowmem build, depth=2):
 *   - 2 x buffer (~1KB each) = 2,080 bytes
 *   - 2 x pt_send_slot = 48 bytes
 *   Total: ~2.3KB per peer
 *
 * Args:
 *   ctx  - Context
 *   peer - Peer to initialize pipeline for
 *
 * Returns: PT_OK on success, PT_ERR_NO_MEMORY if allocation fails
 */
int pt_pipeline_init(struct pt_context *ctx, struct pt_peer *peer);

/* Cleanup async send pipeline for a peer
 *
 * Frees send buffers. Call when peer disconnects or is destroyed.
 * Safe to call on uninitialized pipeline.
 *
 * Note: If sends are pending (in_use == 1), they are abandoned.
 * The platform layer should ensure async operations complete before
 * calling this (e.g., TCPAbort on MacTCP).
 *
 * Args:
 *   ctx  - Context (for logging, can be NULL)
 *   peer - Peer to cleanup
 */
void pt_pipeline_cleanup(struct pt_context *ctx, struct pt_peer *peer);

/* Get a free pipeline slot for sending
 *
 * Finds and returns a free send slot. On PT_LOWMEM builds, allocates
 * the buffer lazily on first use to save memory.
 *
 * Use this instead of directly iterating peer->pipeline.slots to
 * ensure proper lazy allocation on low-memory systems.
 *
 * Args:
 *   ctx  - Context (for logging and allocation)
 *   peer - Peer to get slot from
 *
 * Returns: Pointer to free slot, or NULL if all slots busy or alloc failed
 */
pt_send_slot *pt_pipeline_get_slot(struct pt_context *ctx, struct pt_peer *peer);

#endif /* PT_PEER_H */
