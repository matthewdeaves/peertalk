/* queue.h - Pre-allocated message queues for PeerTalk
 *
 * Implements fixed-size ring buffer queues with ISR-safe push operations
 * for MacTCP ASR and Open Transport notifier contexts.
 *
 * Key Features:
 * - Pre-allocated slots (no allocation at interrupt time)
 * - Power-of-two capacity for fast wrap-around (bitwise AND vs modulo)
 * - Backpressure detection (percentage-based)
 * - Coalescing support for position updates
 * - Zero-copy peek/consume pattern
 * - ISR-safe variants for MacTCP and Open Transport
 */

#ifndef PT_QUEUE_H
#define PT_QUEUE_H

#include "pt_internal.h"
#include "../../include/peertalk.h"

/* Forward declaration */
struct pt_context;

/* ========================================================================
 * Queue Constants
 * ======================================================================== */

/* Queue validation magic number */
#define PT_QUEUE_MAGIC 0x50545155  /* "PTQU" */

/* Slot size: 256 bytes for control messages, discovery, events */
#define PT_QUEUE_SLOT_SIZE 256

/* Slot flags */
#define PT_SLOT_USED        0x01  /* Slot contains valid data */
#define PT_SLOT_COALESCABLE 0x02  /* Message can be coalesced */
#define PT_SLOT_READY       0x04  /* Data fully written (OT reentrancy safety) */

/* ========================================================================
 * Phase 3: Priority & Coalescing Constants
 * ======================================================================== */

/* Priority levels */
typedef enum {
    PT_PRIO_LOW = 0,
    PT_PRIO_NORMAL = 1,
    PT_PRIO_HIGH = 2,
    PT_PRIO_CRITICAL = 3
} pt_priority;

#define PT_PRIO_COUNT 4  /* Number of priority levels */

/* Coalesce key - messages with same key are coalesced
 *
 * NOTE: For per-peer coalescing, combine key with peer ID:
 *   key = PT_COALESCE_POSITION | (peer_id << 8);
 * This ensures peer A's position updates don't coalesce with peer B's.
 */
typedef uint16_t pt_coalesce_key;

#define PT_COALESCE_NONE     0x0000  /* Never coalesce */
#define PT_COALESCE_POSITION 0x0001  /* Position updates */
#define PT_COALESCE_STATE    0x0002  /* State updates */
#define PT_COALESCE_CHAT     0x0003  /* Chat typing indicator */

/* Note: PT_COALESCE_KEY macro is defined in the public API (peertalk.h)
 * to allow users to create per-peer coalesce keys. Definition:
 *   #define PT_COALESCE_KEY(base, peer_id) ((base) | ((peer_id) << 8))
 */

/* Priority free-list constants */
#define PT_SLOT_NONE         0xFFFF  /* Invalid slot index (end of list) */
#define PT_QUEUE_MAX_SLOTS   32      /* Maximum slots per queue (for Classic Mac memory) */

/* Coalesce hash table size - power of 2 for fast modulo
 *
 * MEMORY OPTIMIZATION: Size matches PT_QUEUE_MAX_SLOTS (32) to avoid
 * wasting memory. With 32-entry hash and 32 slots, load factor is ~50%
 * which is acceptable for simple direct-mapped hash.
 */
#define PT_COALESCE_HASH_SIZE  32  /* Match PT_QUEUE_MAX_SLOTS */
#define PT_COALESCE_HASH_MASK  (PT_COALESCE_HASH_SIZE - 1)

/* Hash function for coalesce keys - mixes both type and peer_id bits
 *
 * Per-peer keys use PT_COALESCE_KEY(type, peer_id) = type | (peer_id << 8).
 * A simple (key & MASK) would only use the bottom 5 bits (type), causing
 * all peers with the same type to collide. This XOR-fold mixes the peer_id
 * (upper 8 bits) into the hash for better distribution.
 */
#define PT_COALESCE_HASH(key)  (((key) ^ ((key) >> 8)) & PT_COALESCE_HASH_MASK)

/* ========================================================================
 * Queue Data Structures
 * ======================================================================== */

/* Queue slot - Phase 3 Enhanced
 *
 * Fixed-size slot designed for control messages. Large DATA messages
 * (up to 8192 bytes) are handled separately via flags/direct buffer access.
 *
 * CACHE EFFICIENCY: Metadata fields are placed BEFORE data[] so that checking
 * flags/priority doesn't load the 256-byte payload into cache. On 68030 with
 * 256-byte data cache, this is critical - wrong ordering guarantees cache misses.
 *
 * TRAVERSAL LOCALITY: next_slot is stored IN the slot (not in pt_queue_ext) so
 * that traversing a priority list accesses contiguous memory. This avoids
 * ping-ponging between slots[] and ext.slot_next[] which would thrash the cache.
 *
 * Field ordering: largest alignment first within metadata, then data[] last.
 *
 * Size: 268 bytes (12 bytes metadata + 256 bytes data)
 */
typedef struct {
    uint32_t timestamp;      /* 4 bytes - age-based prioritization (ticks) */
    uint16_t length;         /* 2 bytes - payload length */
    uint16_t coalesce_key;   /* 2 bytes - for coalescing lookup */
    uint16_t next_slot;      /* 2 bytes - next slot in priority list (for traversal locality) */
    uint8_t  priority;       /* 1 byte - PT_PRIO_* */
    volatile uint8_t flags;  /* 1 byte - PT_SLOT_* flags */
    uint8_t  data[PT_QUEUE_SLOT_SIZE];  /* 256 bytes - COLD, placed last */
} pt_queue_slot;  /* Total: 12 bytes metadata + 256 bytes data = 268 bytes */

/* SIZE ASSERTION: Verify struct layout has no unexpected padding.
 * On Classic Mac, use #pragma pack if needed to ensure tight packing.
 * The actual metadata is 12 bytes (4+2+2+2+1+1), not 14 as originally stated.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    _Static_assert(sizeof(pt_queue_slot) == 268, "pt_queue_slot size mismatch");
#else
    /* For C89/C99: runtime check in pt_queue_ext_init() */
    #define PT_QUEUE_SLOT_EXPECTED_SIZE 268
#endif

/* Enhanced queue structure - O(1) priority dequeue and coalesce lookup
 *
 * PERFORMANCE: Classic Mac 68k has no/tiny cache. O(n) scans are unacceptable.
 * - Priority free-lists: O(1) dequeue from highest non-empty priority
 * - Coalesce hash table: O(1) lookup for existing key
 *
 * NOTE: slot_next is stored IN pt_queue_slot (not here) for traversal locality.
 * This avoids ping-ponging between slots[] and ext during list traversal.
 */
typedef struct pt_queue_ext {
    /* Priority-indexed free lists - O(1) dequeue */
    uint16_t prio_head[PT_PRIO_COUNT];  /* Head index per priority level */
    uint16_t prio_tail[PT_PRIO_COUNT];  /* Tail index per priority level */
    uint16_t prio_count[PT_PRIO_COUNT]; /* Count per priority level */

    /* Coalesce key hash table - O(1) lookup */
    uint16_t coalesce_hash[PT_COALESCE_HASH_SIZE];  /* key hash -> slot index */
} pt_queue_ext;

/* Message queue - Phase 3 Enhanced
 *
 * Pre-allocated ring buffer with power-of-two capacity for efficient
 * wrap-around using bitwise AND instead of modulo division (critical
 * on 68k where division takes 100+ cycles).
 *
 * Phase 3 adds: priority free-lists, coalesce hash table, pending pop state,
 * and ISR flags for deferred logging.
 */
typedef struct pt_queue {
    uint32_t magic;                 /* PT_QUEUE_MAGIC for validation */
    pt_queue_slot *slots;           /* Pre-allocated array */
    uint16_t capacity;              /* Number of slots (must be power of 2) */
    uint16_t capacity_mask;         /* capacity - 1, for fast wrap-around */
    volatile uint16_t write_idx;    /* Next slot to write */
    volatile uint16_t read_idx;     /* Next slot to read */
    volatile uint16_t count;        /* Current queue depth */
    volatile uint8_t has_data;      /* Flag for ISR signaling */
    uint8_t reserved;               /* Explicit padding */

    /* Phase 3: O(1) priority and coalescing data structures */
    pt_queue_ext ext;               /* Priority free-lists and coalesce hash */

    /* For direct pop (zero-copy) operations - tracks pending pop state */
    uint8_t  pending_pop_prio;      /* Priority level of pending pop */
    uint16_t pending_pop_slot;      /* Slot index of pending pop */

    /* ISR deferred logging flags - set in ISR, checked/logged in main loop.
     * These flags are in pt_queue (not pt_context) because pt_queue_push_coalesce_isr
     * only has access to the queue pointer. Main loop checks these via pt_poll().
     *
     * LIFECYCLE: Flags are volatile booleans, not counters. Multiple events of the
     * same type within one ISR call only set the flag once (not accumulated). Main
     * loop should call pt_check_queue_isr_flags() after each network operation and
     * the function clears each flag immediately after logging. If not checked
     * frequently enough, subsequent events of the same type may be lost (but the
     * first occurrence is always captured).
     */
    struct {
        volatile uint8_t queue_full    : 1;  /* Queue push failed in ISR */
        volatile uint8_t coalesce_hit  : 1;  /* Message coalesced in ISR */
        volatile uint8_t hash_collision: 1;  /* Hash collision in ISR */
    } isr_flags;
} pt_queue;

/* ========================================================================
 * Queue Management
 * ======================================================================== */

/* Initialize queue
 *
 * Allocates slots array and initializes queue state. Capacity must be
 * a power of two (2, 4, 8, 16, 32, etc.) to enable fast wrap-around.
 *
 * Args:
 *   ctx      - Context (for logging)
 *   q        - Queue to initialize
 *   capacity - Number of slots (must be power of 2)
 *
 * Returns: 0 on success, negative error code on failure
 */
int pt_queue_init(struct pt_context *ctx, pt_queue *q, uint16_t capacity);

/* Free queue resources
 *
 * Deallocates slots array and clears queue state.
 *
 * Args:
 *   q - Queue to free
 */
void pt_queue_free(pt_queue *q);

/* Reset queue
 *
 * Clears all messages without deallocating slots.
 *
 * Args:
 *   q - Queue to reset
 */
void pt_queue_reset(pt_queue *q);

/* ========================================================================
 * Push Operations
 * ======================================================================== */

/* Push message (main loop)
 *
 * Pushes message with backpressure logging at 80%, 90%, 95% thresholds.
 * Safe to call from main event loop only.
 *
 * Args:
 *   ctx      - Context (for logging)
 *   q        - Queue
 *   data     - Message data
 *   len      - Message length (must be <= PT_QUEUE_SLOT_SIZE)
 *   priority - Priority level (unused in Phase 2, for Phase 3)
 *   flags    - Slot flags (PT_SLOT_COALESCABLE, etc.)
 *
 * Returns: 0 on success, PT_ERR_BUFFER_FULL if full
 */
int pt_queue_push(struct pt_context *ctx, pt_queue *q,
                  const void *data, uint16_t len,
                  uint8_t priority, uint8_t flags);

/* Push message (MacTCP ASR)
 *
 * ISR-SAFETY WARNING: This function is designed to be called from MacTCP
 * ASR callbacks (interrupt context). It MUST NOT:
 * - Allocate or free memory
 * - Call PT_Log (logging is deferred to main loop)
 * - Call TickCount() or other non-interrupt-safe routines
 *
 * Uses pt_memcpy_isr() NOT pt_memcpy() for ISR safety.
 *
 * Args:
 *   q    - Queue
 *   data - Message data
 *   len  - Message length (must be <= PT_QUEUE_SLOT_SIZE)
 *
 * Returns: 0 on success, PT_ERR_BUFFER_FULL if full
 */
int pt_queue_push_isr(pt_queue *q, const void *data, uint16_t len);

/* Push message (Open Transport notifier)
 *
 * ISR-SAFETY WARNING: This function is designed to be called from Open
 * Transport notifier callbacks (interrupt context). Uses atomic operations
 * to prevent race conditions from OT notifier reentrancy.
 *
 * Sets PT_SLOT_READY flag after data copy to prevent reading partially-
 * written data if another notifier runs during copy.
 *
 * Args:
 *   q    - Queue
 *   data - Message data
 *   len  - Message length (must be <= PT_QUEUE_SLOT_SIZE)
 *
 * Returns: 0 on success, PT_ERR_BUFFER_FULL if full
 */
int pt_queue_push_isr_ot(pt_queue *q, const void *data, uint16_t len);

/* ========================================================================
 * Pop Operations
 * ======================================================================== */

/* Pop message
 *
 * Pops message and copies data to output buffer.
 *
 * Args:
 *   q    - Queue
 *   data - Output buffer (must be at least PT_QUEUE_SLOT_SIZE bytes)
 *   len  - Output: message length
 *
 * Returns: 0 on success, PT_ERR_QUEUE_EMPTY if empty
 */
int pt_queue_pop(pt_queue *q, void *data, uint16_t *len);

/* Peek at front message
 *
 * Zero-copy access to front slot. Pointer is valid until pt_queue_consume()
 * or pt_queue_push() is called.
 *
 * Args:
 *   q    - Queue
 *   data - Output: pointer to slot data (DO NOT MODIFY)
 *   len  - Output: message length
 *
 * Returns: 0 on success, PT_ERR_QUEUE_EMPTY if empty
 */
int pt_queue_peek(pt_queue *q, void **data, uint16_t *len);

/* Consume peeked message
 *
 * Marks front slot as consumed after pt_queue_peek().
 *
 * Args:
 *   q - Queue
 */
void pt_queue_consume(pt_queue *q);

/* ========================================================================
 * Queue Status
 * ======================================================================== */

/* Get current item count
 *
 * Args:
 *   q - Queue
 *
 * Returns: Number of messages in queue
 */
uint16_t pt_queue_count(pt_queue *q);

/* Get free slots
 *
 * Args:
 *   q - Queue
 *
 * Returns: Number of free slots
 */
uint16_t pt_queue_free_slots(pt_queue *q);

/* Get queue pressure
 *
 * Returns fill percentage (0-100) for backpressure monitoring.
 *
 * Args:
 *   q - Queue
 *
 * Returns: Pressure percentage (0 = empty, 100 = full)
 */
uint8_t pt_queue_pressure(pt_queue *q);

/* Check if queue is full
 *
 * Args:
 *   q - Queue
 *
 * Returns: 1 if full, 0 if not
 */
int pt_queue_is_full(pt_queue *q);

/* Check if queue is empty
 *
 * Args:
 *   q - Queue
 *
 * Returns: 1 if empty, 0 if not
 */
int pt_queue_is_empty(pt_queue *q);

/* ========================================================================
 * Coalescing
 * ======================================================================== */

/* Coalesce message
 *
 * Replaces last coalescable message in queue (within last 4 slots only).
 * Used for position updates that should replace previous positions.
 *
 * Args:
 *   q    - Queue
 *   data - New message data
 *   len  - Message length
 *
 * Returns: 0 if coalesced, -1 if no coalescable message found
 */
int pt_queue_coalesce(pt_queue *q, const void *data, uint16_t len);

/* ========================================================================
 * Phase 3: Priority & Coalescing Operations
 * ======================================================================== */

/*
 * Initialize extended queue data structures
 *
 * Call this after pt_queue_init() from Phase 2 to set up
 * priority free-lists and coalesce hash table.
 *
 * LOGGING: Logs DEBUG message on success with queue capacity.
 */
void pt_queue_ext_init(pt_queue *q);

/*
 * Pop highest priority message - O(1) using priority free-lists
 *
 * Strategy: Check priority lists from CRITICAL down to LOW.
 * Return head of first non-empty list (FIFO within priority).
 *
 * PERFORMANCE: On 68k with no cache, O(n) scans are unacceptable.
 * With 32 slots at 270 bytes each, scanning = 8.6KB memory reads per pop.
 * At ~2MB/s memory bandwidth, that's ~4ms per pop - unacceptable for games.
 * Priority free-lists make this O(1) regardless of queue size.
 *
 * NOTE: This function properly maintains priority lists and coalesce hash.
 * Always use this instead of peek/consume patterns.
 */
int pt_queue_pop_priority(pt_queue *q, void *data, uint16_t *len);

/*
 * Direct pop - returns pointer to slot data without copying (ZERO-COPY)
 *
 * PERFORMANCE: Avoids double-copy in batch send path. Instead of:
 *   1. pop_priority copies slot->data to temp buffer
 *   2. batch_add copies temp buffer to batch->buffer
 * We now have:
 *   1. pop_priority_direct returns pointer to slot->data
 *   2. batch_add copies directly from slot->data to batch->buffer
 *
 * USAGE:
 *   const void *data;
 *   uint16_t len;
 *   if (pt_queue_pop_priority_direct(q, &data, &len) == 0) {
 *       // Use data[0..len-1] - MUST call commit before next pop
 *       pt_batch_add(batch, data, len);
 *       pt_queue_pop_priority_commit(q);
 *   }
 *
 * WARNING: The returned pointer is only valid until pt_queue_pop_priority_commit()
 * is called. Do NOT store the pointer for later use.
 */
int pt_queue_pop_priority_direct(pt_queue *q, const void **data_out,
                                  uint16_t *len_out);

/*
 * Commit a direct pop - actually removes the slot from queue
 *
 * Call this AFTER processing the data returned by pt_queue_pop_priority_direct().
 * This completes the pop operation by updating the priority list and coalesce hash.
 */
void pt_queue_pop_priority_commit(pt_queue *q);

/*
 * Push with coalescing - O(1) using hash table lookup
 *
 * If a message with the same coalesce_key exists, replace it
 * rather than adding a new slot.
 *
 * PERFORMANCE: On 68k, O(n) key scan is unacceptable.
 * Position updates at 10Hz = 80KB/sec just for scanning.
 * Hash table makes coalesce lookup O(1) average case.
 *
 * LOGGING: Caller should log coalesce events and hash collisions:
 *   PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_PROTOCOL, "Coalesce hit: key=0x%04X", key);
 *   PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_PROTOCOL, "Hash collision at bucket %u", hash_idx);
 */
int pt_queue_push_coalesce(pt_queue *q, const void *data, uint16_t len,
                            uint8_t priority, pt_coalesce_key key);

/*
 * ISR-safe push with coalescing (for MacTCP ASR) - O(1) hash lookup
 *
 * CRITICAL ISR-SAFETY RULES:
 * - Uses pt_memcpy_isr(), NOT pt_memcpy() (which calls BlockMoveData)
 * - Does NOT call pt_get_ticks() or TickCount() (not in Table B-3)
 * - Does NOT call PT_Log (not interrupt-safe, uses File Manager)
 * - Does NOT allocate memory
 * See CLAUDE.md ASR rules.
 *
 * LOGGING: Do NOT call PT_Log from this function! Instead, set isr_flags
 * and log from the main event loop. The flags are in pt_queue (not pt_context)
 * because this function only has access to the queue pointer.
 *   ISR: q->isr_flags.queue_full = 1;
 *   Main: if (q->isr_flags.queue_full) { PT_LOG_WARN(ctx->log, ...); q->isr_flags.queue_full = 0; }
 *
 * Note: Uses same O(1) hash table as regular push - no scanning.
 */
int pt_queue_push_coalesce_isr(pt_queue *q, const void *data, uint16_t len,
                                uint8_t priority, pt_coalesce_key key);

/*
 * Check and log ISR flags from main loop
 *
 * Call this periodically from pt_poll() or main event loop to log
 * events that occurred during ISR callbacks. This function is safe
 * to call from main loop (not ISR) because it uses PT_Log.
 *
 * MUST be called frequently to capture ISR events - flags are booleans,
 * not counters, so subsequent events may be lost if not checked promptly.
 */
void pt_check_queue_isr_flags(struct pt_context *ctx, pt_queue *q);

/* ========================================================================
 * Phase 3: Backpressure & Batch Operations
 * ======================================================================== */

/* Pressure thresholds */
#define PT_PRESSURE_LOW      25  /* Below this: safe to send freely */
#define PT_PRESSURE_MEDIUM   50  /* Above this: reduce send rate */
#define PT_PRESSURE_HIGH     75  /* Above this: only critical messages */
#define PT_PRESSURE_CRITICAL 90  /* Above this: drop non-critical */

typedef enum {
    PT_BACKPRESSURE_NONE = 0,
    PT_BACKPRESSURE_LIGHT,      /* Recommend reducing rate */
    PT_BACKPRESSURE_HEAVY,      /* Only high priority */
    PT_BACKPRESSURE_BLOCKING    /* Queue full */
} pt_backpressure;

/*
 * Get backpressure status
 *
 * Returns current backpressure level based on queue fill percentage.
 *
 * LOGGING: Caller should log transitions between pressure levels:
 *   static pt_backpressure last_bp = PT_BACKPRESSURE_NONE;
 *   pt_backpressure bp = pt_queue_backpressure(q);
 *   if (bp != last_bp) {
 *       PT_LOG_WARN(ctx->log, PT_LOG_CAT_PROTOCOL, "Queue pressure: %s (%u%% full)",
 *               pressure_names[bp], pt_queue_pressure(q));
 *       last_bp = bp;
 *   }
 */
pt_backpressure pt_queue_backpressure(pt_queue *q);

/*
 * Try push with backpressure check
 *
 * Applies backpressure policy before pushing. Messages may be dropped
 * based on priority and current pressure level.
 *
 * LOGGING: Caller should log when messages are dropped due to backpressure:
 *   if (result < 0 && bp >= PT_BACKPRESSURE_HEAVY) {
 *       PT_LOG_WARN(ctx->log, PT_LOG_CAT_PROTOCOL, "Message dropped: backpressure %s, prio=%u",
 *               pressure_names[bp], priority);
 *   }
 */
int pt_queue_try_push(pt_queue *q, const void *data, uint16_t len,
                      uint8_t priority, pt_coalesce_key key,
                      pt_backpressure *pressure_out);

#endif /* PT_QUEUE_H */
