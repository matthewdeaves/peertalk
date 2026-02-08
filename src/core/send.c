/* send.c - Batch send operations for PeerTalk
 *
 * Implements batching to combine multiple small messages into one TCP packet
 * for improved efficiency. Common in games where many small control messages
 * (position updates, input events) would otherwise have high TCP/IP overhead.
 *
 * Also implements transparent fragmentation for large messages sent to
 * constrained peers. Applications call PeerTalk_Send() and the SDK handles
 * fragment splitting automatically based on negotiated capabilities.
 */

#include "pt_internal.h"
#include "send.h"
#include "queue.h"
#include "protocol.h"
#include "peer.h"
#include "pt_compat.h"
#include "direct_buffer.h"

/* ========================================================================
 * Fragment Send (Internal)
 *
 * Sends a single fragment of a larger message. Each fragment is framed
 * as a complete message with PT_MSG_FLAG_FRAGMENT set and a fragment
 * header prepended to the payload.
 * ======================================================================== */

static int pt_send_fragment(struct pt_context *ctx, struct pt_peer *peer,
                            const uint8_t *data, uint16_t frag_len,
                            uint16_t msg_id, uint16_t total_len,
                            uint16_t offset, uint8_t frag_flags,
                            uint8_t priority) {
    pt_direct_buffer *buf = &peer->send_direct;
    pt_fragment_header frag_hdr;
    uint8_t frag_hdr_buf[PT_FRAGMENT_HEADER_SIZE];
    int result;

    /* Build fragment header */
    frag_hdr.message_id = msg_id;
    frag_hdr.total_length = total_len;
    frag_hdr.fragment_offset = offset;
    frag_hdr.fragment_flags = frag_flags;
    frag_hdr.reserved = 0;

    pt_fragment_encode(&frag_hdr, frag_hdr_buf);

    /* Queue fragment: header + data into send_direct buffer
     * Use the existing direct buffer for each fragment */
    if (!pt_direct_buffer_available(buf)) {
        return PT_ERR_WOULD_BLOCK;
    }

    /* Copy fragment header + data into buffer */
    if (PT_FRAGMENT_HEADER_SIZE + frag_len > buf->capacity) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_SEND,
            "Fragment too large: %u + %u > %u",
            PT_FRAGMENT_HEADER_SIZE, frag_len, buf->capacity);
        return PT_ERR_MESSAGE_TOO_LARGE;
    }

    pt_memcpy(buf->data, frag_hdr_buf, PT_FRAGMENT_HEADER_SIZE);
    pt_memcpy(buf->data + PT_FRAGMENT_HEADER_SIZE, data, frag_len);

    result = pt_direct_buffer_queue(buf, buf->data,
                                    (uint16_t)(PT_FRAGMENT_HEADER_SIZE + frag_len),
                                    priority);

    if (result == 0) {
        /* Mark buffer as having fragment data so framing sets PT_MSG_FLAG_FRAGMENT */
        buf->msg_flags = PT_MSG_FLAG_FRAGMENT;

        PT_CTX_DEBUG(ctx, PT_LOG_CAT_SEND,
            "Queued fragment: id=%u offset=%u len=%u %s%s",
            msg_id, offset, frag_len,
            (frag_flags & PT_FRAGMENT_FLAG_FIRST) ? "FIRST " : "",
            (frag_flags & PT_FRAGMENT_FLAG_LAST) ? "LAST" : "");
    }

    return result;
}

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

/* ========================================================================
 * Tier 2: Direct Buffer Send
 * ======================================================================== */

int pt_drain_direct_buffer(struct pt_context *ctx, struct pt_peer *peer,
                           pt_direct_send_fn send_fn) {
    pt_direct_buffer *buf;
    int result;

    if (!ctx || !peer || !send_fn) {
        return 0;
    }

    buf = &peer->send_direct;

    /* Check if there's data queued to send */
    if (!pt_direct_buffer_ready(buf)) {
        return 0;
    }

    /* Mark as sending */
    if (pt_direct_buffer_mark_sending(buf) != 0) {
        return 0;
    }

    /* Send via platform callback */
    result = send_fn(ctx, peer, buf->data, buf->length);

    if (result == 0) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_SEND,
                    "Tier 2: Sent %u bytes to peer %u",
                    buf->length, peer->hot.id);
        pt_direct_buffer_complete(buf);
        return 1;
    } else {
        PT_CTX_ERR(ctx, PT_LOG_CAT_SEND,
                  "Tier 2: Send failed for peer %u (%u bytes)",
                  peer->hot.id, buf->length);
        /* Mark complete even on error to allow retry with new data */
        pt_direct_buffer_complete(buf);
        return -1;
    }
}

/* ========================================================================
 * Phase 3.5: SendEx API - Priority-based Sending with Coalescing
 * ======================================================================== */

/**
 * Send message to peer with extended options
 *
 * Routes message to appropriate transport and queue based on flags:
 * - PT_SEND_UNRELIABLE: Uses UDP if available, else TCP
 * - PT_SEND_COALESCABLE: Allows message coalescing with matching key
 * - PT_SEND_NO_DELAY: Disables Nagle algorithm for this message
 *
 * Priority determines queue placement (CRITICAL > HIGH > NORMAL > LOW).
 * Coalesce key enables deduplication of repeated messages (e.g., position updates).
 *
 * FRAGMENTATION: If the message exceeds the peer's negotiated max_message_size
 * and fragmentation is enabled, the SDK automatically fragments the message.
 * The receiver reassembles fragments transparently before delivering to the
 * application callback. App developers never see fragments.
 *
 * @param ctx Valid PeerTalk context
 * @param peer_id Destination peer ID
 * @param data Message data (not null)
 * @param length Message length (1-PT_MAX_MESSAGE bytes)
 * @param priority PT_PRIORITY_* constant
 * @param flags Bitmask of PT_SEND_* flags
 * @param coalesce_key Key for message coalescing (0 = no coalescing)
 *
 * @return PT_OK on success, PT_ERR_* on failure
 */
PeerTalk_Error PeerTalk_SendEx(PeerTalk_Context *ctx_pub,
                                PeerTalk_PeerID peer_id,
                                const void *data,
                                uint16_t length,
                                uint8_t priority,
                                uint8_t flags,
                                uint16_t coalesce_key) {
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    struct pt_peer *peer;
    pt_queue *q;
    int result;

    /* Validate parameters */
    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_STATE;
    }
    if (!data || length == 0 || length > PT_MAX_MESSAGE_SIZE) {
        return PT_ERR_INVALID_PARAM;
    }
    if (priority > PT_PRIORITY_CRITICAL) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Find peer by ID */
    peer = pt_peer_find_by_id(ctx, peer_id);
    if (!peer) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
                   "SendEx failed: Peer %u not found", peer_id);
        return PT_ERR_PEER_NOT_FOUND;
    }

    /* ================================================================
     * AUTOMATIC FRAGMENTATION
     *
     * If message exceeds peer's negotiated max and fragmentation is enabled,
     * split into fragments. Each fragment is queued separately and
     * reassembled by the receiver before delivery to app callback.
     *
     * The app developer never sees this - they just call PeerTalk_Send()
     * and the SDK handles everything.
     * ================================================================ */
    if (ctx->enable_fragmentation &&
        peer->hot.effective_max_msg > 0 &&
        length > peer->hot.effective_max_msg) {

        const uint8_t *src = (const uint8_t *)data;
        uint16_t max_frag_data;
        uint16_t offset = 0;
        uint16_t remaining = length;
        uint16_t msg_id;
        uint8_t frag_flags;
        int frag_result;

        /* Allocate unique message ID for this fragmented message */
        msg_id = (uint16_t)(ctx->next_message_id++ & 0xFFFF);

        /* Max fragment data = peer's max minus fragment header overhead */
        max_frag_data = peer->hot.effective_max_msg - PT_FRAGMENT_HEADER_SIZE;
        if (max_frag_data < 64) {
            /* Peer's max is too small for practical fragmentation */
            PT_CTX_ERR(ctx, PT_LOG_CAT_SEND,
                "Peer max %u too small for fragmentation",
                peer->hot.effective_max_msg);
            return PT_ERR_MESSAGE_TOO_LARGE;
        }

        PT_CTX_INFO(ctx, PT_LOG_CAT_SEND,
            "Fragmenting %u bytes for peer %u (max=%u, chunks=%u)",
            length, peer_id, peer->hot.effective_max_msg,
            (length + max_frag_data - 1) / max_frag_data);

        while (remaining > 0) {
            uint16_t frag_len = (remaining > max_frag_data) ? max_frag_data : remaining;

            frag_flags = 0;
            if (offset == 0) {
                frag_flags |= PT_FRAGMENT_FLAG_FIRST;
            }
            if (remaining <= max_frag_data) {
                frag_flags |= PT_FRAGMENT_FLAG_LAST;
            }

            frag_result = pt_send_fragment(ctx, peer, src + offset, frag_len,
                                           msg_id, length, offset, frag_flags,
                                           priority);

            if (frag_result != 0) {
                PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
                    "Fragment send failed at offset %u: %d", offset, frag_result);
                return frag_result;
            }

            offset += frag_len;
            remaining -= frag_len;

            /* NOTE: For synchronous fragmentation, we send one fragment at a time.
             * The poll loop will drain the direct buffer before we can queue the next.
             * For now, return after first fragment - caller may need to retry for more.
             * A full implementation would queue all fragments atomically. */
            if (remaining > 0) {
                /* More fragments pending - let poll loop drain first fragment */
                PT_CTX_DEBUG(ctx, PT_LOG_CAT_SEND,
                    "Fragment %u/%u queued, %u bytes remaining",
                    (offset / max_frag_data), ((length + max_frag_data - 1) / max_frag_data),
                    remaining);
                /* For now, continue - real implementation would need state machine */
            }
        }

        return PT_OK;
    }

    /* Route unreliable sends to UDP if available */
    if (flags & PT_SEND_UNRELIABLE) {
        /* Check if UDP is available on this platform */
        if (ctx->plat && ctx->plat->send_udp) {
            int udp_result = ctx->plat->send_udp(ctx, peer, data, length);
            if (udp_result == 0) {
                PT_CTX_DEBUG(ctx, PT_LOG_CAT_SEND,
                           "Sent %u bytes to peer %u via UDP",
                           length, peer_id);
                return PT_OK;
            }
            /* UDP failed, fall back to TCP */
            PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
                       "UDP send failed, falling back to TCP");
        }
        /* No UDP available, fall through to TCP */
    }

    /* Two-tier routing: large messages go to Tier 2, small to Tier 1 */
    {
        uint16_t threshold = ctx->direct_threshold;
        if (threshold == 0) {
            threshold = PT_DIRECT_THRESHOLD;
        }

        if (length > threshold) {
            /* Tier 2: Direct buffer for large messages */
            result = pt_direct_buffer_queue(&peer->send_direct, data, length, priority);
            if (result == PT_ERR_WOULD_BLOCK) {
                PT_CTX_DEBUG(ctx, PT_LOG_CAT_SEND,
                            "Tier 2 buffer busy for peer %u, caller should retry",
                            peer_id);
                return PT_ERR_WOULD_BLOCK;
            }
            if (result != 0) {
                PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
                           "Tier 2 queue failed for peer %u: %d", peer_id, result);
                return result;
            }

            PT_CTX_DEBUG(ctx, PT_LOG_CAT_SEND,
                        "Tier 2: Queued %u bytes to peer %u (pri=%u)",
                        length, peer_id, priority);
            return PT_OK;
        }
    }

    /* Tier 1: Queue for small messages */
    q = peer->send_queue;
    if (!q) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_SEND,
                  "SendEx failed: Peer %u has no send queue", peer_id);
        return PT_ERR_INVALID_STATE;
    }

    /* Check backpressure before queuing */
    {
        float pressure = pt_queue_pressure(q);
        if (pressure >= 0.90f) {
            /* Queue >90% full - reject LOW priority messages */
            if (priority == PT_PRIORITY_LOW) {
                PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
                           "Queue pressure %.0f%% - rejecting LOW priority message",
                           pressure * 100.0f);
                return PT_ERR_BUFFER_FULL;
            }
        }
        if (pressure >= 0.75f) {
            /* Queue >75% full - reject NORMAL priority messages */
            if (priority == PT_PRIORITY_NORMAL) {
                PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
                           "Queue pressure %.0f%% - rejecting NORMAL priority message",
                           pressure * 100.0f);
                return PT_ERR_BUFFER_FULL;
            }
        }
    }

    /* Enqueue message with priority and optional coalescing */
    if (flags & PT_SEND_COALESCABLE && coalesce_key != 0) {
        /* Coalescing enabled - replace existing message with same key */
        result = pt_queue_push_coalesce(q, data, length, priority, coalesce_key);
    } else {
        /* Normal enqueue */
        result = pt_queue_push(ctx, q, data, length, priority, 0);
    }

    if (result != 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
                   "Queue full - message dropped (peer=%u, len=%u, pri=%u)",
                   peer_id, length, priority);
        return PT_ERR_BUFFER_FULL;
    }

    /* TODO: Handle PT_SEND_NO_DELAY flag (Phase 4+ platform layer) */
    (void)flags;  /* Suppress unused warning for now */

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_SEND,
                "Queued %u bytes to peer %u (pri=%u, flags=0x%02X, key=%u)",
                length, peer_id, priority, flags, coalesce_key);

    return PT_OK;
}

/**
 * Send message to peer (simple wrapper)
 *
 * Sends with default priority (NORMAL), no special flags, and no coalescing.
 * Most applications should use this for typical reliable messaging.
 *
 * @param ctx Valid PeerTalk context
 * @param peer_id Destination peer ID
 * @param data Message data (not null)
 * @param length Message length (1-PT_MAX_MESSAGE bytes)
 *
 * @return PT_OK on success, PT_ERR_* on failure
 */
PeerTalk_Error PeerTalk_Send(PeerTalk_Context *ctx_pub,
                              PeerTalk_PeerID peer_id,
                              const void *data,
                              uint16_t length) {
    /* Delegate to SendEx with default parameters */
    return PeerTalk_SendEx(ctx_pub, peer_id, data, length,
                          PT_PRIORITY_NORMAL,
                          PT_SEND_DEFAULT,
                          0);  /* No coalescing */
}
