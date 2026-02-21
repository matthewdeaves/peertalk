/**
 * @file stream.c
 * @brief PeerTalk Streaming API Implementation
 *
 * Implements large data transfer bypassing the normal message queue.
 * Useful for log file transfers, state synchronization, etc.
 */

#include "pt_internal.h"
#include "peer.h"
#include "protocol.h"
#include "../../include/peertalk.h"

/* ========================================================================== */
/* Stream Send                                                                */
/* ========================================================================== */

PeerTalk_Error PeerTalk_StreamSend(
    PeerTalk_Context *ctx_pub,
    PeerTalk_PeerID peer_id,
    const void *data,
    uint32_t length,
    PeerTalk_StreamCompleteCB on_complete,
    void *user_data)
{
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    struct pt_peer *peer;

    /* Validate context */
    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_STATE;
    }

    /* Validate data */
    if (!data || length == 0) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Check max size */
    if (length > PT_MAX_STREAM_SIZE) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
            "Stream too large: %u > %u", length, PT_MAX_STREAM_SIZE);
        return PT_ERR_MESSAGE_TOO_LARGE;
    }

    /* Find peer */
    peer = pt_peer_find_by_id(ctx, peer_id);
    if (!peer) {
        return PT_ERR_PEER_NOT_FOUND;
    }

    /* Check peer is connected */
    if (peer->hot.state != PT_PEER_CONNECTED) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
            "Cannot stream to peer %u: not connected", peer_id);
        return PT_ERR_NOT_CONNECTED;
    }

    /* Check if stream already active */
    if (peer->stream.active) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
            "Stream already active for peer %u", peer_id);
        return PT_ERR_BUSY;
    }

    /* Initialize stream state */
    peer->stream.data = (const uint8_t *)data;
    peer->stream.total_length = length;
    peer->stream.bytes_sent = 0;
    peer->stream.on_complete = (void *)on_complete;
    peer->stream.user_data = user_data;
    peer->stream.cancelled = 0;
    peer->stream.active = 1;

    PT_CTX_INFO(ctx, PT_LOG_CAT_SEND,
        "Stream started for peer %u: %u bytes", peer_id, length);

    return PT_OK;
}

/* ========================================================================== */
/* Stream Cancel                                                              */
/* ========================================================================== */

PeerTalk_Error PeerTalk_StreamCancel(
    PeerTalk_Context *ctx_pub,
    PeerTalk_PeerID peer_id)
{
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    struct pt_peer *peer;

    /* Validate context */
    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_STATE;
    }

    /* Find peer */
    peer = pt_peer_find_by_id(ctx, peer_id);
    if (!peer) {
        return PT_ERR_PEER_NOT_FOUND;
    }

    /* Check if stream is active */
    if (!peer->stream.active) {
        return PT_ERR_NOT_FOUND;
    }

    /* Mark as cancelled - will be processed in next poll */
    peer->stream.cancelled = 1;

    PT_CTX_INFO(ctx, PT_LOG_CAT_SEND,
        "Stream cancel requested for peer %u", peer_id);

    return PT_OK;
}

/* ========================================================================== */
/* Stream Active Check                                                        */
/* ========================================================================== */

int PeerTalk_StreamActive(
    PeerTalk_Context *ctx_pub,
    PeerTalk_PeerID peer_id)
{
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    struct pt_peer *peer;

    /* Validate context */
    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC) {
        return 0;
    }

    /* Find peer */
    peer = pt_peer_find_by_id(ctx, peer_id);
    if (!peer) {
        return 0;
    }

    return peer->stream.active ? 1 : 0;
}

/* ========================================================================== */
/* Stream Poll (Internal)                                                     */
/* ========================================================================== */

/**
 * Process active stream for a peer
 *
 * Called from the poll loop to send the next chunk of stream data.
 * Uses the peer's effective_chunk size for optimal throughput.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer with active stream
 * @param send_func Platform-specific send function
 * @return 0 on success, negative on error
 */
int pt_stream_poll(struct pt_context *ctx, struct pt_peer *peer,
                   int (*send_func)(struct pt_context *, struct pt_peer *,
                                    const void *, size_t))
{
    pt_peer_stream *stream = &peer->stream;
    PeerTalk_StreamCompleteCB on_complete;
    uint32_t chunk_size;
    uint32_t remaining;
    int result;

    if (!stream->active) {
        return 0;  /* No active stream */
    }

    /* Check for cancellation */
    if (stream->cancelled) {
        on_complete = (PeerTalk_StreamCompleteCB)stream->on_complete;
        stream->active = 0;

        PT_CTX_INFO(ctx, PT_LOG_CAT_SEND,
            "Stream cancelled for peer %u at %u/%u bytes",
            peer->hot.id, stream->bytes_sent, stream->total_length);

        if (on_complete) {
            on_complete((PeerTalk_Context *)ctx, peer->hot.id,
                       stream->bytes_sent, PT_ERR_CANCELLED, stream->user_data);
        }
        return 0;
    }

    /* Calculate chunk size - use adaptive chunk or default */
    chunk_size = peer->hot.effective_chunk;
    if (chunk_size == 0) {
        chunk_size = 1024;  /* Default if not set */
    }

    /* Calculate remaining bytes */
    remaining = stream->total_length - stream->bytes_sent;
    if (chunk_size > remaining) {
        chunk_size = remaining;
    }

    /* Send next chunk */
    result = send_func(ctx, peer,
                       stream->data + stream->bytes_sent,
                       chunk_size);

    if (result == PT_ERR_WOULD_BLOCK) {
        /* Socket buffer full - try again next poll */
        return 0;
    }

    if (result < 0) {
        /* Send failed - abort stream */
        on_complete = (PeerTalk_StreamCompleteCB)stream->on_complete;
        stream->active = 0;

        PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
            "Stream failed for peer %u: error %d at %u/%u bytes",
            peer->hot.id, result, stream->bytes_sent, stream->total_length);

        if (on_complete) {
            on_complete((PeerTalk_Context *)ctx, peer->hot.id,
                       stream->bytes_sent, (PeerTalk_Error)result, stream->user_data);
        }
        return result;
    }

    /* Update bytes sent */
    stream->bytes_sent += chunk_size;

    /* Check if complete */
    if (stream->bytes_sent >= stream->total_length) {
        on_complete = (PeerTalk_StreamCompleteCB)stream->on_complete;
        stream->active = 0;

        PT_CTX_INFO(ctx, PT_LOG_CAT_SEND,
            "Stream complete for peer %u: %u bytes",
            peer->hot.id, stream->bytes_sent);

        if (on_complete) {
            on_complete((PeerTalk_Context *)ctx, peer->hot.id,
                       stream->bytes_sent, PT_OK, stream->user_data);
        }
    }

    return 0;
}
