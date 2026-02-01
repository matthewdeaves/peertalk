/* send.c - Batch send operations for PeerTalk
 *
 * Implements batching to combine multiple small messages into one TCP packet
 * for improved efficiency. Common in games where many small control messages
 * (position updates, input events) would otherwise have high TCP/IP overhead.
 */

#include "pt_internal.h"
#include "send.h"
#include "queue.h"
#include "protocol.h"
#include "pt_compat.h"

/* ========================================================================
 * Batch Send Operations
 * ======================================================================== */

/*
 * Batch send - combine multiple small messages into one TCP packet
 *
 * This improves efficiency by reducing TCP/IP overhead for
 * many small messages common in games.
 *
 * NOTE: The actual pt_batch_send() function requires a platform-specific
 * send function. This is provided by Phases 3/4/5:
 *   - POSIX: pt_posix_send()
 *   - MacTCP: pt_mactcp_send()
 *   - Open Transport: pt_ot_send()
 *
 * The batch building (pt_batch_init, pt_batch_add) is platform-independent.
 */

/* Batch constants and type are defined in send.h */

void pt_batch_init(pt_batch *batch) {
    batch->used = 0;
    batch->count = 0;
}

int pt_batch_add(pt_batch *batch, const void *data, uint16_t len) {
    if (batch->used + PT_BATCH_HEADER + len > PT_BATCH_MAX_SIZE)
        return -1;  /* Batch full */

    /* Add length prefix (big-endian) */
    batch->buffer[batch->used++] = (len >> 8) & 0xFF;
    batch->buffer[batch->used++] = len & 0xFF;

    /* Reserved bytes for flags/type */
    batch->buffer[batch->used++] = 0;
    batch->buffer[batch->used++] = 0;

    /* Add data */
    pt_memcpy(batch->buffer + batch->used, data, len);
    batch->used += len;
    batch->count++;

    return 0;
}

/*
 * Send a batch as a single framed message
 *
 * This function builds the complete framed message but delegates
 * the actual send to the platform layer. The caller (poll loop)
 * should use the appropriate platform send function.
 *
 * Returns: bytes to send (stored in batch->buffer with header prepended),
 *          or 0 if batch is empty
 */
int pt_batch_prepare(struct pt_peer *peer, pt_batch *batch) {
    pt_message_header hdr;
    uint8_t header_buf[PT_MESSAGE_HEADER_SIZE];

    if (batch->count == 0)
        return 0;

    /* Build header */
    hdr.version = PT_PROTOCOL_VERSION;
    hdr.type = PT_MSG_TYPE_DATA;
    hdr.flags = PT_MSG_FLAG_BATCH;
    hdr.sequence = peer->hot.send_seq++;
    hdr.payload_len = batch->used;

    pt_message_encode_header(&hdr, header_buf);

    /*
     * The caller should send:
     *   1. header_buf (PT_MESSAGE_HEADER_SIZE bytes)
     *   2. batch->buffer (batch->used bytes)
     *   3. CRC-16 of (header + payload)
     *
     * Or use the platform's send_framed() function if available.
     */
    return batch->used;
}

/*
 * NOTE: pt_queue_peek() and pt_queue_consume() were REMOVED.
 *
 * These functions bypassed the priority free-lists and coalesce hash,
 * causing data structure corruption. The batch send code now uses
 * pt_queue_pop_priority() which properly maintains all data structures.
 *
 * If you need peek-like functionality, pop into a local buffer instead:
 *   uint8_t buf[PT_QUEUE_SLOT_SIZE];
 *   uint16_t len;
 *   if (pt_queue_pop_priority(q, buf, &len) == 0) {
 *       // Process buf[0..len-1]
 *   }
 */

/* pt_batch_send_fn typedef is in send.h */

/*
 * Drain send queue in batches
 *
 * Called from poll loop - combines queued messages into batches.
 * The send_fn callback is platform-specific and provided by the
 * networking layer (Phase 4 POSIX, Phase 5 MacTCP, Phase 6 OT).
 *
 * Uses pre-allocated batch buffer from ctx->send_batch to avoid
 * 1.4KB stack allocation per call.
 *
 * IMPORTANT: Uses pt_queue_pop_priority() which properly maintains
 * priority free-lists and coalesce hash. Messages are batched in
 * priority order (CRITICAL first, then HIGH, NORMAL, LOW).
 *
 * LOGGING: Logs DEBUG on successful batch send, ERR on failure, WARN
 * for oversized messages.
 */
int pt_drain_send_queue(struct pt_context *ctx, struct pt_peer *peer,
                        pt_batch_send_fn send_fn) {
    pt_batch *batch;
    pt_queue *q = peer->send_queue;
    const void *msg_data;  /* Zero-copy pointer to slot data */
    uint16_t len;
    int sent = 0;

    if (!ctx || !q || pt_queue_is_empty(q) || !send_fn)
        return 0;

    /* Use pre-allocated batch buffer from context */
    batch = &ctx->send_batch;
    pt_batch_init(batch);

    /* Pop messages in priority order using ZERO-COPY direct pop.
     * This avoids double-copy: instead of slot->temp->batch, we do slot->batch.
     * On 68k with 2-10 MB/s memory bandwidth, this saves significant time.
     *
     * PROTOCOL: Only commit after successful batch_add to avoid message loss. */
    while (pt_queue_pop_priority_direct(q, &msg_data, &len) == 0) {
        if (pt_batch_add(batch, msg_data, len) < 0) {
            /* Batch full - DON'T commit yet, send current batch first */
            if (send_fn(ctx, peer, batch) == 0) {
                sent++;
                PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL,
                    "Batch sent: %u messages, %u bytes", batch->count, batch->used);
            } else {
                PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL, "Batch send failed");
            }
            pt_batch_init(batch);

            /* Now try adding the message to the fresh batch */
            if (pt_batch_add(batch, msg_data, len) < 0) {
                /* Message too large even for empty batch - should not happen
                 * if PT_BATCH_MAX_SIZE > PT_QUEUE_SLOT_SIZE */
                PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                    "Message too large for batch (%u bytes), dropped", len);
                /* Commit the pop anyway - message is lost (config error) */
                pt_queue_pop_priority_commit(q);
                continue;
            }
        }
        /* Message added to batch - now commit the pop */
        pt_queue_pop_priority_commit(q);
    }

    /* Send remaining batch */
    if (batch->count > 0) {
        if (send_fn(ctx, peer, batch) == 0) {
            sent++;
            PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL,
                "Batch sent: %u messages, %u bytes", batch->count, batch->used);
        } else {
            PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL, "Batch send failed");
        }
    }

    return sent;
}
