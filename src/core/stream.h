/**
 * @file stream.h
 * @brief PeerTalk Streaming Internal API
 *
 * Internal functions for stream transfer processing.
 */

#ifndef PT_STREAM_H
#define PT_STREAM_H

#include "pt_internal.h"

/**
 * Process active stream for a peer
 *
 * Called from the poll loop to send the next chunk of stream data.
 * Uses the peer's effective_chunk size for optimal throughput.
 *
 * @param ctx       PeerTalk context
 * @param peer      Peer with active stream
 * @param send_func Platform-specific send function
 * @return 0 on success, negative on error
 */
int pt_stream_poll(struct pt_context *ctx, struct pt_peer *peer,
                   int (*send_func)(struct pt_context *, struct pt_peer *,
                                    const void *, size_t));

#endif /* PT_STREAM_H */
