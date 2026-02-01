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
 * Queue Data Structures
 * ======================================================================== */

/* Queue slot
 *
 * Fixed-size slot designed for control messages. Large DATA messages
 * (up to 8192 bytes) are handled separately via flags/direct buffer access.
 *
 * Size: 260 bytes (4 metadata + 256 data)
 */
typedef struct {
    uint16_t length;                /* 2 bytes - checked first on pop */
    uint8_t  priority;              /* 1 byte */
    volatile uint8_t  flags;        /* 1 byte - volatile for OT atomic access */
    uint8_t  data[PT_QUEUE_SLOT_SIZE];  /* 256 bytes */
} pt_queue_slot;

/* Message queue
 *
 * Pre-allocated ring buffer with power-of-two capacity for efficient
 * wrap-around using bitwise AND instead of modulo division (critical
 * on 68k where division takes 100+ cycles).
 *
 * Size: 20 bytes
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
    uint8_t reserved;               /* Explicit padding for 20-byte struct */
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

#endif /* PT_QUEUE_H */
