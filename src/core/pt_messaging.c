/*
 * pt_messaging.c -- PeerTalk message framing, chunking, reassembly
 *
 * Implements PT_Send, PT_Broadcast, TCP frame parsing, chunked
 * message reassembly, and UDP fast message handling.
 */

#include "pt_internal.h"

/* ------------------------------------------------------------------ */
/* Internal: send a single TCP frame                                   */
/* ------------------------------------------------------------------ */

static PT_Status send_tcp_frame(PT_Context_Internal *ctx,
                                PT_Peer_Internal *peer,
                                unsigned char type,
                                unsigned char flags,
                                unsigned short seq,
                                unsigned short total,
                                const void *payload,
                                size_t payload_len)
{
    /* Build complete frame (header + payload) in peer's send buffer
     * so we make exactly ONE tcp_send call. MacTCP async sends fail
     * if a second send is issued before the first completes. */
    unsigned char *frame = peer->tcp_send_buf;
    size_t hdr_size;
    size_t frame_size;

    /* Check frame size BEFORE writing to buffer (T071) */
    hdr_size = (flags & PT_CHUNK_FLAG) ? PT_TCP_CHUNK_HEADER
                                       : PT_TCP_HEADER_SIZE;
    frame_size = hdr_size + payload_len;
    if (frame_size > peer->tcp_send_size) {
        return PT_ERR_SEND_FAILED;
    }

    if (flags & PT_CHUNK_FLAG) {
        frame[0] = (unsigned char)((payload_len >> 8) & 0xFF);
        frame[1] = (unsigned char)(payload_len & 0xFF);
        frame[2] = type;
        frame[3] = flags;
        frame[4] = (unsigned char)((seq >> 8) & 0xFF);
        frame[5] = (unsigned char)(seq & 0xFF);
        frame[6] = (unsigned char)((total >> 8) & 0xFF);
        frame[7] = (unsigned char)(total & 0xFF);
    } else {
        frame[0] = (unsigned char)((payload_len >> 8) & 0xFF);
        frame[1] = (unsigned char)(payload_len & 0xFF);
        frame[2] = type;
        frame[3] = flags;
    }

    /* Copy payload after header */
    if (payload_len > 0 && payload) {
        memcpy(frame + hdr_size, payload, payload_len);
    }

    return ctx->platform_ops->tcp_send(ctx, peer, frame, frame_size);
}

/* ------------------------------------------------------------------ */
/* Public API: PT_Send                                                 */
/* ------------------------------------------------------------------ */

PT_Status PT_Send(PT_Context *pub_ctx, PT_Peer *pub_peer,
                  unsigned char type, const void *data, size_t len)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    PT_Peer_Internal *peer = (PT_Peer_Internal *)pub_peer;
    PT_Transport transport;

    if (!ctx || !peer) return PT_ERR_INVALID_ARG;
    if (!data && len > 0) return PT_ERR_INVALID_ARG;
    if (peer->state != PT_PEER_CONNECTED) return PT_ERR_NOT_CONNECTED;

    transport = ctx->message_types[type];

    if (transport == PT_RELIABLE) {
        /* TCP path */
        size_t max_single = peer->tcp_send_size - PT_TCP_HEADER_SIZE;

        if (len <= max_single) {
            /* Single frame */
            return send_tcp_frame(ctx, peer, type, 0, 0, 0,
                                  data, len);
        } else {
            /* Chunked: split into chunks */
            size_t max_chunk_payload = peer->tcp_send_size -
                                       PT_TCP_CHUNK_HEADER;
            unsigned short total_chunks;
            unsigned short seq;
            const unsigned char *src = (const unsigned char *)data;
            size_t remaining = len;

            total_chunks = (unsigned short)
                ((len + max_chunk_payload - 1) / max_chunk_payload);

            for (seq = 0; seq < total_chunks; seq++) {
                size_t chunk_len = remaining;
                PT_Status st;

                if (chunk_len > max_chunk_payload)
                    chunk_len = max_chunk_payload;

                st = send_tcp_frame(ctx, peer, type, PT_CHUNK_FLAG,
                                    seq, total_chunks,
                                    src, chunk_len);
                if (st != PT_OK) return st;

                src += chunk_len;
                remaining -= chunk_len;
            }
            return PT_OK;
        }
    } else {
        /* UDP (PT_FAST) path — build header in ctx->udp_send_buf
         * (R48: moved from stack to context to avoid 68k stack
         * overflow on Mac SE with ~8KB application stack) */
        unsigned char *buf = ctx->udp_send_buf;

        if (len > PT_UDP_MTU_SAFE) return PT_ERR_SEND_FAILED;

        buf[0] = (unsigned char)((len >> 8) & 0xFF);
        buf[1] = (unsigned char)(len & 0xFF);
        buf[2] = type;

        if (len > 0) {
            memcpy(buf + PT_UDP_HEADER_SIZE, data, len);
        }

        return ctx->platform_ops->udp_send(ctx, peer,
                                            PT_UDP_MSG_PORT,
                                            buf,
                                            PT_UDP_HEADER_SIZE + len);
    }
}

/* ------------------------------------------------------------------ */
/* Public API: PT_Broadcast                                            */
/* ------------------------------------------------------------------ */

PT_Status PT_Broadcast(PT_Context *pub_ctx, unsigned char type,
                       const void *data, size_t len)
{
    PT_Context_Internal *ctx = (PT_Context_Internal *)pub_ctx;
    int i;
    int sent_any = 0;

    if (!ctx) return PT_ERR_INVALID_ARG;

    for (i = 0; i < ctx->max_peers; i++) {
        if (ctx->peers[i].in_use &&
            ctx->peers[i].state == PT_PEER_CONNECTED) {
            PT_Status st = PT_Send(pub_ctx,
                                   (PT_Peer *)&ctx->peers[i],
                                   type, data, len);
            if (st == PT_OK) sent_any = 1;
        }
    }

    return sent_any ? PT_OK : PT_ERR_SEND_FAILED;
}

/* ------------------------------------------------------------------ */
/* Internal: TCP frame parsing and dispatch                            */
/* ------------------------------------------------------------------ */

void pt_messaging_process_tcp_data(PT_Context_Internal *ctx,
                                   PT_Peer_Internal *peer)
{
    while (peer->tcp_recv_len >= PT_TCP_HEADER_SIZE) {
        unsigned short payload_len;
        unsigned char msg_type;
        unsigned char flags;
        size_t frame_size;

        /* Parse header */
        payload_len = (unsigned short)(
            ((unsigned short)peer->tcp_recv_buf[0] << 8) |
            (unsigned short)peer->tcp_recv_buf[1]);
        msg_type = peer->tcp_recv_buf[2];
        flags = peer->tcp_recv_buf[3];

        if (flags & PT_CHUNK_FLAG) {
            /* Chunked frame -- need 8-byte header */
            unsigned short seq, total;
            size_t chunk_payload;

            if (peer->tcp_recv_len < PT_TCP_CHUNK_HEADER) return;

            seq = (unsigned short)(
                ((unsigned short)peer->tcp_recv_buf[4] << 8) |
                (unsigned short)peer->tcp_recv_buf[5]);
            total = (unsigned short)(
                ((unsigned short)peer->tcp_recv_buf[6] << 8) |
                (unsigned short)peer->tcp_recv_buf[7]);

            chunk_payload = (size_t)payload_len;
            frame_size = PT_TCP_CHUNK_HEADER + chunk_payload;

            if (peer->tcp_recv_len < frame_size) return;

            /* Process chunk */
            if (seq == 0) {
                /* First chunk -- record stride and calculate total size.
                   The stride is the first chunk's payload length, which
                   reflects the SENDER's buffer size (may differ from ours
                   on cross-platform connections). */
                size_t total_size = (size_t)(total - 1) * chunk_payload +
                                    chunk_payload;

                if (total_size > peer->reassembly_buf_size) {
                    /* Message too large */
                    pt_fire_error(ctx, PT_ERR_NO_ROOM,
                                  "Reassembly buffer too small");
                    peer->reassembly_total = 0;
                    peer->reassembly_received = 0;
                } else {
                    peer->reassembly_type = msg_type;
                    peer->reassembly_total = total;
                    peer->reassembly_received = 0;
                    peer->reassembly_timer = ctx->current_time;
                    peer->reassembly_stride = chunk_payload;
                }
            }

            if (peer->reassembly_total > 0 && seq < total &&
                msg_type == peer->reassembly_type) {
                /* Copy chunk into reassembly buffer using stride
                   derived from first chunk's payload size */
                size_t offset = (size_t)seq * peer->reassembly_stride;

                if (offset + chunk_payload <=
                    peer->reassembly_buf_size) {
                    memcpy(peer->reassembly_buf + offset,
                           peer->tcp_recv_buf + PT_TCP_CHUNK_HEADER,
                           chunk_payload);
                    peer->reassembly_received++;
                }

                /* Check if complete */
                if (peer->reassembly_received ==
                    peer->reassembly_total) {
                    /* Total size = (N-1) full chunks + last chunk */
                    size_t total_len = (size_t)(total - 1) *
                                       peer->reassembly_stride +
                                       chunk_payload;

                    if (ctx->callbacks.on_message[msg_type]) {
                        ctx->callbacks.on_message[msg_type](
                            (PT_Peer *)peer,
                            peer->reassembly_buf,
                            total_len,
                            ctx->callbacks.on_message_data[msg_type]);
                    }
                    peer->reassembly_total = 0;
                    peer->reassembly_received = 0;
                }
            }
        } else {
            /* Normal (non-chunked) frame */
            frame_size = PT_TCP_HEADER_SIZE + (size_t)payload_len;

            if (peer->tcp_recv_len < frame_size) return;

            /* Check for goodbye message */
            if (msg_type == PT_MSG_TYPE_GOODBYE) {
                /* Consume frame then disconnect */
                size_t remaining = peer->tcp_recv_len - frame_size;
                if (remaining > 0) {
                    memmove(peer->tcp_recv_buf,
                            peer->tcp_recv_buf + frame_size,
                            remaining);
                }
                peer->tcp_recv_len = remaining;

                pt_handle_peer_disconnect(ctx, peer, PT_QUIT);
                return;
            }

            /* Dispatch message callback */
            if (ctx->callbacks.on_message[msg_type]) {
                ctx->callbacks.on_message[msg_type](
                    (PT_Peer *)peer,
                    peer->tcp_recv_buf + PT_TCP_HEADER_SIZE,
                    (size_t)payload_len,
                    ctx->callbacks.on_message_data[msg_type]);
            }
        }

        /* Consume frame from buffer */
        {
            size_t remaining = peer->tcp_recv_len - frame_size;
            if (remaining > 0) {
                memmove(peer->tcp_recv_buf,
                        peer->tcp_recv_buf + frame_size,
                        remaining);
            }
            peer->tcp_recv_len = remaining;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Internal: UDP message processing                                    */
/* ------------------------------------------------------------------ */

void pt_messaging_process_udp_data(PT_Context_Internal *ctx,
                                   const void *data, size_t len,
                                   unsigned long source_ip)
{
    const unsigned char *pkt = (const unsigned char *)data;
    unsigned short payload_len;
    unsigned char msg_type;
    PT_Peer_Internal *peer;

    if (len < PT_UDP_HEADER_SIZE) return;

    payload_len = (unsigned short)(
        ((unsigned short)pkt[0] << 8) | (unsigned short)pkt[1]);
    msg_type = pkt[2];

    if (PT_UDP_HEADER_SIZE + (size_t)payload_len > len) return;

    /* Find sender peer */
    peer = pt_find_peer_by_ip(ctx, source_ip);
    if (!peer || peer->state != PT_PEER_CONNECTED) return;

    if (ctx->callbacks.on_message[msg_type]) {
        ctx->callbacks.on_message[msg_type](
            (PT_Peer *)peer,
            pkt + PT_UDP_HEADER_SIZE,
            (size_t)payload_len,
            ctx->callbacks.on_message_data[msg_type]);
    }
}

/* ------------------------------------------------------------------ */
/* Internal: Reassembly timeout check                                  */
/* ------------------------------------------------------------------ */

void pt_messaging_check_reassembly_timeouts(PT_Context_Internal *ctx)
{
    int i;

    for (i = 0; i < ctx->max_peers; i++) {
        if (!ctx->peers[i].in_use) continue;
        if (ctx->peers[i].reassembly_total == 0) continue;

        if (ctx->current_time - ctx->peers[i].reassembly_timer >=
            PT_REASSEMBLY_TIMEOUT) {
            CLOG_WARN("Reassembly timeout for peer %s",
                      ctx->peers[i].name);
            ctx->peers[i].reassembly_total = 0;
            ctx->peers[i].reassembly_received = 0;
        }
    }
}
