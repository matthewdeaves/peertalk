/* send.h - Batch send operations for PeerTalk
 *
 * Provides batching to combine multiple small messages into one TCP packet.
 */

#ifndef PT_SEND_H
#define PT_SEND_H

#include "pt_types.h"
#include <stdint.h>

/* Forward declarations */
struct pt_context;
struct pt_peer;

/* ========================================================================
 * Batch Send Constants
 * ======================================================================== */

/* Batch size: 1400 = 1500 (Ethernet MTU) - 100 (TCP/IP headers margin) */
#define PT_BATCH_MAX_SIZE   1400
#define PT_BATCH_HEADER     4     /* Length prefix for each message */

/* ========================================================================
 * Batch Data Structures
 * ======================================================================== */

/* Batch buffer - pre-allocated in pt_context, NOT on stack
 *
 * MEMORY: Classic Mac apps often have 32KB stacks. Allocating 1.4KB
 * per call is significant and risks stack overflow with nested calls.
 * Pre-allocating in pt_context avoids this.
 *
 * CACHE EFFICIENCY: Metadata (used, count) placed BEFORE buffer so that
 * pt_batch_add() accesses metadata at offset 0, not at offset 1400.
 * On 68030 with 256-byte cache, this avoids guaranteed cache misses
 * when alternating between metadata updates and buffer writes.
 */
typedef struct {
    uint16_t used;                       /* HOT: accessed every pt_batch_add() */
    uint16_t count;                      /* HOT: accessed every pt_batch_add() */
    uint8_t  is_fragment;                /* Set if batch contains a fragment */
    uint8_t  reserved;                   /* Padding */
    uint8_t  buffer[PT_BATCH_MAX_SIZE];  /* COLD: sequential writes only */
} pt_batch;

/* ========================================================================
 * Batch Operations
 * ======================================================================== */

/*
 * Initialize batch buffer
 *
 * Sets used=0, count=0. Call before adding messages.
 */
void pt_batch_init(pt_batch *batch);

/*
 * Add message to batch
 *
 * Appends message with length prefix. Returns -1 if batch is full.
 * Message is prefixed with 4-byte header: len (2 bytes big-endian) + 2 reserved bytes.
 *
 * Args:
 *   batch - Batch buffer
 *   data  - Message data
 *   len   - Message length
 *
 * Returns: 0 on success, -1 if batch full
 */
int pt_batch_add(pt_batch *batch, const void *data, uint16_t len);

/*
 * Prepare batch for sending
 *
 * Builds message header for the batch. Caller should send:
 *   1. Header (PT_MESSAGE_HEADER_SIZE bytes)
 *   2. batch->buffer (batch->used bytes)
 *   3. CRC-16 of (header + payload)
 *
 * Args:
 *   peer  - Target peer (for sequence number)
 *   batch - Batch buffer
 *
 * Returns: Number of bytes in batch payload (batch->used), or 0 if empty
 */
int pt_batch_prepare(struct pt_peer *peer, pt_batch *batch);

/*
 * Platform-specific batch send callback type.
 *
 * This callback is provided by the platform layer (Phases 4/5/6):
 *   - POSIX: pt_posix_batch_send()
 *   - MacTCP: pt_mactcp_batch_send()
 *   - Open Transport: pt_ot_batch_send()
 *
 * Args:
 *   ctx   - PeerTalk context
 *   peer  - Target peer
 *   batch - Prepared batch
 *
 * Returns: 0 on success, -1 on error
 */
typedef int (*pt_batch_send_fn)(struct pt_context *ctx,
                                 struct pt_peer *peer,
                                 pt_batch *batch);

/*
 * Platform-specific direct send callback type.
 *
 * Used for Tier 2 large message sends. Sends raw data directly.
 *
 * Args:
 *   ctx    - PeerTalk context
 *   peer   - Target peer
 *   data   - Data buffer
 *   length - Data length
 *
 * Returns: 0 on success, -1 on error
 */
typedef int (*pt_direct_send_fn)(struct pt_context *ctx,
                                  struct pt_peer *peer,
                                  const void *data,
                                  uint16_t length);

/*
 * Drain send queue in batches
 *
 * Called from poll loop - combines queued messages into batches.
 * Uses pre-allocated ctx->send_batch to avoid stack allocation.
 * Messages are batched in priority order (CRITICAL → HIGH → NORMAL → LOW).
 *
 * Args:
 *   ctx     - PeerTalk context
 *   peer    - Target peer
 *   send_fn - Platform-specific send callback
 *
 * Returns: Number of batches sent
 */
int pt_drain_send_queue(struct pt_context *ctx, struct pt_peer *peer,
                        pt_batch_send_fn send_fn);

/*
 * Drain Tier 2 direct buffer (large messages)
 *
 * Called from poll loop BEFORE draining Tier 1 queue.
 * Large messages have priority to complete before batching small ones.
 *
 * Args:
 *   ctx     - PeerTalk context
 *   peer    - Target peer
 *   send_fn - Platform-specific direct send callback
 *
 * Returns: 1 if message sent, 0 if nothing to send, -1 on error
 */
int pt_drain_direct_buffer(struct pt_context *ctx, struct pt_peer *peer,
                           pt_direct_send_fn send_fn);

#endif /* PT_SEND_H */
