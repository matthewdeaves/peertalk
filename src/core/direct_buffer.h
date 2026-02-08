/* direct_buffer.h - Tier 2 direct buffers for large messages
 *
 * The queue system (Tier 1) uses 256-byte slots for control messages.
 * Direct buffers (Tier 2) handle messages up to 8192 bytes.
 *
 * Memory Budget (Mac SE with 4 peers):
 *   Tier 1: 4 peers x 2 queues x 16 slots x 268 bytes = 34 KB
 *   Tier 2: 4 peers x 2 buffers x 4 KB = 32 KB
 *   Total: ~67 KB (6.5% of 1MB available after system)
 */

#ifndef PT_DIRECT_BUFFER_H
#define PT_DIRECT_BUFFER_H

#include "pt_types.h"

/* Forward declarations */
struct pt_context;
struct pt_peer;

/* ========================================================================
 * Constants
 * ======================================================================== */

/* Default buffer size: 4KB - covers most large messages */
#define PT_DIRECT_DEFAULT_SIZE   4096

/* Maximum buffer size: 8KB - per PROJECT_GOALS.md requirement */
#define PT_DIRECT_MAX_SIZE       8192

/* Size threshold: messages <= this go to Tier 1 queue, larger to Tier 2 */
#define PT_DIRECT_THRESHOLD      256

/* ========================================================================
 * Direct Buffer State
 * ======================================================================== */

/**
 * Direct buffer states
 *
 * State transitions:
 *   IDLE -> QUEUED: Data copied to buffer via pt_direct_buffer_queue()
 *   QUEUED -> SENDING: Send started via pt_direct_buffer_send()
 *   SENDING -> IDLE: Send completed or failed
 *   QUEUED -> IDLE: Buffer cleared without sending (error recovery)
 */
typedef enum {
    PT_DIRECT_IDLE = 0,     /* Buffer available for new data */
    PT_DIRECT_QUEUED,       /* Data queued, waiting to send */
    PT_DIRECT_SENDING       /* Send in progress */
} pt_direct_state;

/* ========================================================================
 * Direct Buffer Structure
 * ======================================================================== */

/**
 * Direct buffer for large messages (Tier 2)
 *
 * One buffer per peer per direction (send/recv). Unlike Tier 1 queues which
 * can hold multiple messages, direct buffers hold one message at a time.
 * Applications must wait for PT_DIRECT_IDLE before queuing next message.
 *
 * MEMORY LAYOUT: Designed for 68k cache efficiency (32-byte lines on 68030).
 * Volatile state first for fast polling checks.
 */
typedef struct {
    volatile pt_direct_state state;  /* Current buffer state */
    uint16_t length;                 /* Payload length in buffer */
    uint16_t capacity;               /* Buffer size (default 4096) */
    uint8_t  priority;               /* Message priority (PT_PRIORITY_*) */
    uint8_t  reserved;               /* Explicit padding */
    uint8_t *data;                   /* Pre-allocated buffer pointer */
} pt_direct_buffer;

/* ========================================================================
 * Direct Buffer Operations
 * ======================================================================== */

/**
 * Initialize a direct buffer
 *
 * Allocates the data buffer. Call during peer creation.
 *
 * @param buf Buffer to initialize
 * @param capacity Buffer size (use PT_DIRECT_DEFAULT_SIZE or custom)
 * @return 0 on success, PT_ERR_NO_MEMORY on allocation failure
 */
int pt_direct_buffer_init(pt_direct_buffer *buf, uint16_t capacity);

/**
 * Free a direct buffer
 *
 * Deallocates the data buffer. Call during peer destruction.
 *
 * @param buf Buffer to free (NULL-safe)
 */
void pt_direct_buffer_free(pt_direct_buffer *buf);

/**
 * Queue data for sending via direct buffer
 *
 * Copies data to the buffer and sets state to QUEUED.
 * Returns PT_ERR_WOULD_BLOCK if buffer is busy (QUEUED or SENDING).
 *
 * @param buf Direct buffer
 * @param data Data to queue
 * @param length Data length (must be <= capacity)
 * @param priority Message priority
 * @return 0 on success, PT_ERR_WOULD_BLOCK if busy, PT_ERR_MESSAGE_TOO_LARGE if > capacity
 */
int pt_direct_buffer_queue(pt_direct_buffer *buf, const void *data,
                           uint16_t length, uint8_t priority);

/**
 * Mark buffer as sending
 *
 * Transitions state from QUEUED to SENDING.
 * Called when platform layer begins transmission.
 *
 * @param buf Direct buffer
 * @return 0 on success, -1 if not in QUEUED state
 */
int pt_direct_buffer_mark_sending(pt_direct_buffer *buf);

/**
 * Mark buffer as idle (send complete)
 *
 * Transitions state to IDLE, allowing next message to be queued.
 * Called when platform layer completes or fails transmission.
 *
 * @param buf Direct buffer
 */
void pt_direct_buffer_complete(pt_direct_buffer *buf);

/**
 * Check if buffer has data ready to send
 *
 * @param buf Direct buffer
 * @return 1 if state is QUEUED, 0 otherwise
 */
int pt_direct_buffer_ready(const pt_direct_buffer *buf);

/**
 * Check if buffer is available for new data
 *
 * @param buf Direct buffer
 * @return 1 if state is IDLE, 0 otherwise
 */
int pt_direct_buffer_available(const pt_direct_buffer *buf);

/* ========================================================================
 * Receive Path Operations
 * ======================================================================== */

/**
 * Receive large message into direct buffer
 *
 * For incoming messages > PT_DIRECT_THRESHOLD, copy to recv buffer
 * for immediate delivery to application callback.
 *
 * @param buf Receive direct buffer
 * @param data Incoming data
 * @param length Data length
 * @return 0 on success, PT_ERR_MESSAGE_TOO_LARGE if > capacity
 */
int pt_direct_buffer_receive(pt_direct_buffer *buf, const void *data,
                             uint16_t length);

#endif /* PT_DIRECT_BUFFER_H */
