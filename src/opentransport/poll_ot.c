/**
 * @file poll_ot.c
 * @brief Open Transport Main Poll Function
 *
 * Integrates all OT components into a unified polling loop.
 * Mirrors poll_mactcp.c patterns adapted for OT endpoint model.
 *
 * Key differences from MacTCP poll:
 * - Bitmap iteration instead of linear array scan
 * - OTRcv instead of TCPNoCopyRcv (simpler, no RDS/BfrReturn)
 * - T_ORDREL orderly disconnect (MacTCP only has TCPClosing ASR event)
 * - OTSndOrderlyDisconnect + OTRcvOrderlyDisconnect handshake
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 5: "Connection-Oriented"
 */

#include "ot_defs.h"
#include "protocol.h"
#include "peer.h"
#include "queue.h"
#include "stream.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_OT)

#include <OSUtils.h>  /* TickCount() - main loop only */

/* ========================================================================== */
/* Constants                                                                   */
/* ========================================================================== */

#define PT_OT_ANNOUNCE_INTERVAL_TICKS  (15 * 60)  /* 15 seconds at 60Hz */
#define PT_OT_PEER_TIMEOUT_TICKS       (30 * 60)  /* 30 seconds */

/* Send coalescing: accumulate frames in obuf, flush as single large OTSnd.
 * With TCP_NODELAY, each OTSnd becomes a separate TCP segment. Coalescing
 * reduces 260 x 260-byte OTSnd calls to ~4 x 16KB calls, producing
 * MSS-sized TCP segments (1460 bytes) instead of tiny ones. */
#define PT_OT_COALESCE_THRESHOLD  8192  /* Flush when obuf >= 8KB */

/* ========================================================================== */
/* External Functions                                                          */
/* ========================================================================== */

/* From discovery_ot.c */
extern int pt_ot_discovery_poll(struct pt_context *ctx);
extern int pt_ot_discovery_send(struct pt_context *ctx, uint8_t type);

/* From tcp_server_ot.c */
extern int pt_ot_listen_poll(struct pt_context *ctx);

/* From tcp_connect_ot.c */
extern int pt_ot_connect_poll(struct pt_context *ctx);

/* From tcp_ot.c */
extern int pt_ot_tcp_send(struct pt_context *ctx, int idx,
                            const void *data, size_t len);
extern int pt_ot_tcp_recv(struct pt_context *ctx, int idx,
                            void *data, size_t len, OTFlags *out_flags);
extern void pt_ot_tcp_close(struct pt_context *ctx, int idx);
extern void pt_ot_tcp_cleanup(struct pt_context *ctx, int idx);
extern int pt_ot_tcp_check_close_timeout(struct pt_context *ctx, int idx);
extern void pt_ot_tcp_process_log_events(struct pt_context *ctx,
                                          pt_tcp_endpoint_hot *hot,
                                          int idx);

/**
 * Wrapper for pt_stream_poll send function.
 * Matches the signature expected by pt_stream_poll: (ctx, peer, data, len).
 * Extracts the endpoint index from the peer's connection field and sends.
 */
static int pt_ot_stream_send(struct pt_context *ctx, struct pt_peer *peer,
                               const void *data, size_t len)
{
    int idx;

    if (peer->hot.connection == NULL)
        return PT_ERR_NOT_CONNECTED;

    idx = (int)(intptr_t)peer->hot.connection - 1;
    return pt_ot_tcp_send(ctx, idx, data, len);
}

/* Forward declaration */
int pt_ot_send_framed(struct pt_context *ctx, int idx,
                        struct pt_peer *peer,
                        const void *data, uint16_t len,
                        uint8_t msg_flags);

/* ========================================================================== */
/* Async Send (Platform Op)                                                    */
/* ========================================================================== */

/**
 * Async TCP send for OT - bypasses queue for direct OTSnd.
 *
 * OT's OTSnd copies data to internal buffers and returns immediately
 * (unlike MacTCP which needs a true async pipeline with persistent WDS
 * buffers). This means we can frame the message and send it directly
 * without needing pipeline slot buffers.
 *
 * Called from PeerTalk_SendEx() when the async pipeline path is available.
 * On kOTFlowErr, returns PT_ERR_WOULD_BLOCK so send.c falls back to the
 * queue-based path.
 *
 * @param ctx    PeerTalk context
 * @param peer   Peer to send to
 * @param data   Raw payload data
 * @param len    Payload length
 * @param flags  Protocol message flags
 * @return       PT_OK on success, PT_ERR_WOULD_BLOCK on flow control,
 *               negative error on failure
 */
int pt_ot_tcp_send_async(struct pt_context *ctx, struct pt_peer *peer,
                           const void *data, uint16_t len, uint8_t flags)
{
    int idx;

    if (peer == NULL || peer->hot.connection == NULL)
        return PT_ERR_NOT_CONNECTED;

    idx = (int)(intptr_t)peer->hot.connection - 1;

    return pt_ot_send_framed(ctx, idx, peer, data, len, flags);
}

/* ========================================================================== */
/* Queue Management                                                            */
/* ========================================================================== */

/**
 * Free a peer queue.
 */
static void pt_ot_free_peer_queue(pt_queue *q)
{
    if (q) {
        pt_queue_free(q);
        pt_free(q);
    }
}

/**
 * Free send/recv queues for a disconnecting peer.
 */
static void pt_ot_free_peer_queues(struct pt_peer *peer)
{
    pt_ot_free_peer_queue(peer->send_queue);
    pt_ot_free_peer_queue(peer->recv_queue);
    peer->send_queue = NULL;
    peer->recv_queue = NULL;
}

/* ========================================================================== */
/* Send Coalescing                                                             */
/* ========================================================================== */

/**
 * Flush coalesced send data to OT TCP.
 *
 * Sends accumulated frames from peer->cold.obuf in a single OTSnd call.
 * This produces MSS-sized TCP segments instead of one tiny segment per frame.
 *
 * @param ctx   PeerTalk context
 * @param idx   TCP endpoint index
 * @param peer  Peer whose obuf to flush
 * @return      0 on success, PT_ERR_WOULD_BLOCK on flow control, -1 on error
 */
static int pt_ot_flush_send(struct pt_context *ctx, int idx,
                              struct pt_peer *peer)
{
    int result;

    if (peer->cold.obuflen == 0)
        return 0;

    result = pt_ot_tcp_send(ctx, idx, peer->cold.obuf, peer->cold.obuflen);

    if (result == (int)peer->cold.obuflen) {
        /* All data sent */
        peer->cold.obuflen = 0;
        return 0;
    }

    if (result == 0) {
        /* Flow control - nothing sent, keep buffer intact */
        return PT_ERR_WOULD_BLOCK;
    }

    if (result > 0) {
        /* Partial send - shift remaining data to front.
         * Safe: source is always after dest, forward copy works. */
        uint16_t sent = (uint16_t)result;
        uint16_t remaining = peer->cold.obuflen - sent;
        uint16_t i;
        for (i = 0; i < remaining; i++) {
            peer->cold.obuf[i] = peer->cold.obuf[sent + i];
        }
        peer->cold.obuflen = remaining;
        return PT_ERR_WOULD_BLOCK;
    }

    /* Error - discard buffer */
    peer->cold.obuflen = 0;
    return -1;
}

/* ========================================================================== */
/* Framed Send                                                                 */
/* ========================================================================== */

/**
 * Send a data message with protocol framing via OT TCP.
 *
 * Coalesces frames in the peer's obuf for efficient TCP segmentation.
 * Instead of calling OTSnd per frame (producing tiny TCP segments),
 * accumulates multiple frames and flushes when the buffer is large
 * enough to produce MSS-sized TCP segments.
 *
 * @param ctx       PeerTalk context
 * @param idx       TCP endpoint index
 * @param peer      Peer to send to
 * @param data      Raw payload data
 * @param len       Payload length
 * @param msg_flags Protocol message flags (e.g., PT_MSG_FLAG_FRAGMENT)
 * @return          0 on success, PT_ERR_WOULD_BLOCK on flow control, -1 on error
 */
int pt_ot_send_framed(struct pt_context *ctx, int idx,
                        struct pt_peer *peer,
                        const void *data, uint16_t len,
                        uint8_t msg_flags)
{
    uint8_t *dst;
    uint16_t total;
    int use_compact;

    /* Check compact mode - don't use for fragments (flag doesn't fit) */
    use_compact = peer->cold.caps.compact_mode &&
                  !(msg_flags & PT_MSG_FLAG_FRAGMENT);

    /* Calculate frame size */
    if (use_compact) {
        total = PT_COMPACT_HEADER_SIZE + len;
    } else {
        total = PT_MESSAGE_HEADER_SIZE + len + 2;  /* +2 for CRC */
    }

    /* If adding this frame would overflow obuf, flush first */
    if (peer->cold.obuflen + total > PT_FRAME_BUF_SIZE) {
        int flush_err = pt_ot_flush_send(ctx, idx, peer);
        /* Check again after flush - still no room? */
        if (peer->cold.obuflen + total > PT_FRAME_BUF_SIZE) {
            (void)flush_err;
            return PT_ERR_WOULD_BLOCK;
        }
    }

    /* Build frame at current obuf offset */
    dst = peer->cold.obuf + peer->cold.obuflen;

    if (use_compact) {
        pt_compact_header compact_hdr;

        compact_hdr.type = PT_MSG_TYPE_DATA;
        compact_hdr.flags = msg_flags;
        compact_hdr.payload_len = len;
        pt_message_encode_compact(&compact_hdr, dst);

        pt_memcpy(dst + PT_COMPACT_HEADER_SIZE, data, len);
    } else {
        pt_message_header hdr;
        uint16_t crc;

        hdr.version = PT_PROTOCOL_VERSION;
        hdr.type = PT_MSG_TYPE_DATA;
        hdr.flags = msg_flags;
        hdr.sequence = peer->hot.send_seq++;
        hdr.payload_len = len;
        pt_message_encode_header(&hdr, dst);

        pt_memcpy(dst + PT_MESSAGE_HEADER_SIZE, data, len);

        /* CRC over header + payload */
        crc = pt_crc16(dst, PT_MESSAGE_HEADER_SIZE + len);
        dst[PT_MESSAGE_HEADER_SIZE + len] = (uint8_t)(crc >> 8);
        dst[PT_MESSAGE_HEADER_SIZE + len + 1] = (uint8_t)(crc & 0xFF);
    }

    peer->cold.obuflen += total;

    /* Auto-flush when enough data for efficient TCP segments */
    if (peer->cold.obuflen >= PT_OT_COALESCE_THRESHOLD) {
        (void)pt_ot_flush_send(ctx, idx, peer);
        /* Ignore result - frame is accepted regardless */
    }

    return 0;
}

/* ========================================================================== */
/* Capability Handling                                                          */
/* ========================================================================== */

/**
 * Apply received capability message to peer state.
 *
 * Stores peer's capabilities, negotiates compact header mode,
 * calculates effective max message size and flow control window.
 * Mirrors the inline handling in MacTCP tcp_io.c.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer that sent capabilities
 * @param caps  Decoded capability message
 */
static void pt_ot_apply_capabilities(struct pt_context *ctx,
                                       struct pt_peer *peer,
                                       const pt_capability_msg *caps)
{
    uint16_t effective_max;

    /* Store peer's capabilities */
    peer->cold.caps.max_message_size = caps->max_message_size;
    peer->cold.caps.preferred_chunk = caps->preferred_chunk;
    peer->cold.caps.capability_flags = caps->capability_flags;
    peer->cold.caps.buffer_pressure = caps->buffer_pressure;
    peer->cold.caps.caps_exchanged = 1;
    peer->cold.caps.recv_buffer_size = caps->recv_buffer_size;
    peer->cold.caps.optimal_chunk = caps->optimal_chunk;

    /* Negotiate compact header mode - both must support it */
    if ((caps->capability_flags & PT_CAPFLAG_COMPACT_HEADER) &&
        (ctx->local_capability_flags & PT_CAPFLAG_COMPACT_HEADER)) {
        peer->cold.caps.compact_mode = 1;
    } else {
        peer->cold.caps.compact_mode = 0;
    }

    /* Check if peer needs push for performance */
    peer->cold.caps.push_preferred =
        (caps->capability_flags & PT_CAPFLAG_PUSH_PREFERRED) ? 1 : 0;

    /* Calculate effective max = min(ours, theirs) */
    effective_max = ctx->local_max_message;
    if (caps->max_message_size < effective_max)
        effective_max = caps->max_message_size;
    peer->hot.effective_max_msg = effective_max;

    /* OT flow control: disable app-level send window.
     *
     * OT provides native TCP backpressure via kOTFlowErr - when OT's
     * internal send buffer is full, OTSnd returns kOTFlowErr and we
     * stop sending until the buffer drains. This is sufficient flow
     * control for OT.
     *
     * The app-level send window (send_window > 0) counts queued
     * messages and blocks new sends when in_flight >= window. With
     * window=7 (65535/8192), this limits throughput to ~7 queued
     * messages worth of data, far below what OT can actually handle.
     * Setting window=0 disables this check (see send.c flow control). */
    peer->cold.caps.send_window = 0;

    PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK,
        "Capabilities from peer %u: max=%u chunk=%u pressure=%u "
        "compact=%u recv_buf=%u push=%u window=%u",
        (unsigned)peer->hot.id,
        (unsigned)caps->max_message_size,
        (unsigned)caps->preferred_chunk,
        (unsigned)caps->buffer_pressure,
        (unsigned)peer->cold.caps.compact_mode,
        (unsigned)caps->recv_buffer_size,
        (unsigned)peer->cold.caps.push_preferred,
        (unsigned)peer->cold.caps.send_window);

    /* Log interface MTU and netmask for diagnosing MSS=536 bottleneck.
     * These are captured during OT init but may not appear in test logs
     * if the log system isn't ready during init. Re-log here. */
    {
        pt_ot_data *od_diag = pt_ot_get(ctx);
        char ip_str_diag[PT_IP_STR_LEN];
        InetInterfaceInfo iface_info;
        pt_memset(&iface_info, 0, sizeof(iface_info));
        if (OTInetGetInterfaceInfo(&iface_info, kDefaultInetInterface) == noErr) {
            OTInetHostToString(iface_info.fNetmask, ip_str_diag);
            PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK,
                "Interface: mask=%s MTU=%lu",
                ip_str_diag, (unsigned long)iface_info.fIfMTU);
        }
        (void)od_diag;
    }
}

/* ========================================================================== */
/* TCP Receive Helper                                                          */
/* ========================================================================== */

/**
 * Receive and process data from a connected TCP endpoint.
 *
 * Reads data into the peer's ibuf, then parses complete messages.
 * Supports both full headers (10+2 bytes) and compact headers (4 bytes).
 *
 * @param ctx    PeerTalk context
 * @param idx    TCP endpoint index
 * @param peer   Associated peer
 * @return       Number of messages processed, 0 if no data, -1 on error
 */
static int pt_ot_tcp_recv_process(struct pt_context *ctx, int idx,
                                    struct pt_peer *peer)
{
    pt_ot_data *od = pt_ot_get(ctx);
    pt_tcp_endpoint_hot *hot = pt_ot_get_tcp_hot(od, idx);
    OTFlags recv_flags = 0;
    int result;
    int messages_processed = 0;
    uint16_t bytes_consumed;
    uint8_t *msg_start;

    if (hot == NULL || peer == NULL)
        return -1;

    /* Read data into peer ibuf until kOTNoDataErr */
    while (peer->cold.ibuflen < PT_FRAME_BUF_SIZE) {
        size_t space = PT_FRAME_BUF_SIZE - peer->cold.ibuflen;
        result = pt_ot_tcp_recv(ctx, idx,
                                 peer->cold.ibuf + peer->cold.ibuflen,
                                 space, &recv_flags);
        if (result < 0)
            return -1;  /* Connection error */
        if (result == 0)
            break;      /* No more data */

        peer->cold.ibuflen += (uint16_t)result;
    }

    /* Process complete messages in ibuf.
     * Supports both full headers (10+2) and compact headers (4, no CRC). */
    bytes_consumed = 0;
    msg_start = peer->cold.ibuf;

    while (peer->cold.ibuflen - bytes_consumed >= PT_COMPACT_HEADER_SIZE) {
        uint16_t remaining = peer->cold.ibuflen - bytes_consumed;
        int is_compact;
        uint16_t header_size;

        is_compact = pt_message_is_compact(msg_start, remaining);

        if (is_compact) {
            pt_compact_header compact_hdr;
            uint16_t total_size;

            if (pt_message_decode_compact(msg_start, remaining,
                                           &compact_hdr) < 0) {
                PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                    "Invalid compact header from peer %u",
                    (unsigned)peer->hot.id);
                peer->cold.ibuflen = 0;
                return -1;
            }

            header_size = PT_COMPACT_HEADER_SIZE;
            total_size = header_size + compact_hdr.payload_len;

            if (remaining < total_size)
                break;  /* Need more data */

            /* Dispatch by message type */
            {
                uint8_t *data_ptr = msg_start + header_size;

                switch (compact_hdr.type) {
                case PT_MSG_TYPE_DATA:
                    if (compact_hdr.flags & PT_MSG_FLAG_FRAGMENT) {
                        pt_fragment_header frag_hdr;
                        const uint8_t *complete_data;
                        uint16_t complete_len;
                        int reassembly_result;

                        if (pt_fragment_decode(data_ptr,
                                compact_hdr.payload_len, &frag_hdr) < 0)
                            break;

                        reassembly_result = pt_reassembly_process(ctx, peer,
                            data_ptr, compact_hdr.payload_len,
                            &frag_hdr, &complete_data, &complete_len);

                        if (reassembly_result == 1 &&
                            complete_data != NULL &&
                            ctx->callbacks.on_message_received != NULL) {
                            ctx->callbacks.on_message_received(
                                (PeerTalk_Context *)ctx,
                                peer->hot.id, complete_data, complete_len,
                                ctx->callbacks.user_data);
                        }
                    } else if (ctx->callbacks.on_message_received != NULL) {
                        ctx->callbacks.on_message_received(
                            (PeerTalk_Context *)ctx,
                            peer->hot.id, data_ptr,
                            compact_hdr.payload_len,
                            ctx->callbacks.user_data);
                    }
                    break;

                case PT_MSG_TYPE_PING:
                    /* Reply with PONG */
                    {
                        uint8_t pong_buf[PT_COMPACT_HEADER_SIZE];
                        pt_compact_header pong_hdr;
                        pong_hdr.type = PT_MSG_TYPE_PONG;
                        pong_hdr.flags = 0;
                        pong_hdr.payload_len = 0;
                        pt_message_encode_compact(&pong_hdr, pong_buf);
                        pt_ot_tcp_send(ctx, idx, pong_buf,
                                        PT_COMPACT_HEADER_SIZE);
                    }
                    break;

                case PT_MSG_TYPE_PONG:
                    peer->hot.last_seen = (pt_tick_t)TickCount();
                    break;

                case PT_MSG_TYPE_DISCONNECT:
                    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
                        "Received DISCONNECT from peer %u",
                        (unsigned)peer->hot.id);
                    peer->cold.ibuflen = 0;
                    return -1;

                case PT_MSG_TYPE_CAPABILITY:
                    {
                        pt_capability_msg caps;
                        if (pt_capability_decode(ctx, data_ptr,
                                compact_hdr.payload_len, &caps) == 0) {
                            pt_ot_apply_capabilities(ctx, peer, &caps);
                        }
                    }
                    break;

                default:
                    break;
                }
            }

            msg_start += total_size;
            bytes_consumed += total_size;
            messages_processed++;
        } else {
            /* Full header path */
            pt_message_header hdr;
            uint16_t total_size;

            if (remaining < PT_MESSAGE_HEADER_SIZE)
                break;

            if (pt_message_decode_header(ctx, msg_start, remaining,
                                          &hdr) < 0) {
                PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                    "Invalid message header from peer %u",
                    (unsigned)peer->hot.id);
                peer->cold.ibuflen = 0;
                return -1;
            }

            header_size = PT_MESSAGE_HEADER_SIZE;
            total_size = header_size + hdr.payload_len + 2;  /* +2 for CRC */

            if (remaining < total_size)
                break;

            /* Verify CRC */
            {
                uint16_t crc_expected =
                    ((uint16_t)msg_start[total_size - 2] << 8) |
                     (uint16_t)msg_start[total_size - 1];
                uint16_t crc_actual = pt_crc16(msg_start,
                                                total_size - 2);

                if (crc_actual != crc_expected) {
                    PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                        "CRC mismatch from peer %u: expected=%04X actual=%04X",
                        (unsigned)peer->hot.id,
                        (unsigned)crc_expected, (unsigned)crc_actual);
                    peer->cold.ibuflen = 0;
                    return -1;
                }
            }

            /* Dispatch by message type */
            {
                uint8_t *data_ptr = msg_start + header_size;

                switch (hdr.type) {
                case PT_MSG_TYPE_DATA:
                    if (hdr.flags & PT_MSG_FLAG_FRAGMENT) {
                        pt_fragment_header frag_hdr;
                        const uint8_t *complete_data;
                        uint16_t complete_len;
                        int reassembly_result;

                        if (pt_fragment_decode(data_ptr,
                                hdr.payload_len, &frag_hdr) < 0)
                            break;

                        reassembly_result = pt_reassembly_process(ctx, peer,
                            data_ptr, hdr.payload_len,
                            &frag_hdr, &complete_data, &complete_len);

                        if (reassembly_result == 1 &&
                            complete_data != NULL &&
                            ctx->callbacks.on_message_received != NULL) {
                            ctx->callbacks.on_message_received(
                                (PeerTalk_Context *)ctx,
                                peer->hot.id, complete_data, complete_len,
                                ctx->callbacks.user_data);
                        }
                    } else if (ctx->callbacks.on_message_received != NULL) {
                        ctx->callbacks.on_message_received(
                            (PeerTalk_Context *)ctx,
                            peer->hot.id, data_ptr, hdr.payload_len,
                            ctx->callbacks.user_data);
                    }
                    break;

                case PT_MSG_TYPE_PING:
                    {
                        uint8_t pong_buf[PT_MESSAGE_HEADER_SIZE + 2];
                        pt_message_header pong_hdr;
                        uint16_t crc;
                        pong_hdr.version = PT_PROTOCOL_VERSION;
                        pong_hdr.type = PT_MSG_TYPE_PONG;
                        pong_hdr.flags = 0;
                        pong_hdr.sequence = hdr.sequence;
                        pong_hdr.payload_len = 0;
                        pt_message_encode_header(&pong_hdr, pong_buf);
                        crc = pt_crc16(pong_buf, PT_MESSAGE_HEADER_SIZE);
                        pong_buf[PT_MESSAGE_HEADER_SIZE] =
                            (uint8_t)(crc >> 8);
                        pong_buf[PT_MESSAGE_HEADER_SIZE + 1] =
                            (uint8_t)(crc & 0xFF);
                        pt_ot_tcp_send(ctx, idx, pong_buf, sizeof(pong_buf));
                    }
                    break;

                case PT_MSG_TYPE_PONG:
                    peer->hot.last_seen = (pt_tick_t)TickCount();
                    break;

                case PT_MSG_TYPE_DISCONNECT:
                    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
                        "Received DISCONNECT from peer %u",
                        (unsigned)peer->hot.id);
                    peer->cold.ibuflen = 0;
                    return -1;

                case PT_MSG_TYPE_CAPABILITY:
                    {
                        pt_capability_msg caps;
                        if (pt_capability_decode(ctx, data_ptr,
                                hdr.payload_len, &caps) == 0) {
                            pt_ot_apply_capabilities(ctx, peer, &caps);
                        }
                    }
                    break;

                default:
                    break;
                }
            }

            msg_start += total_size;
            bytes_consumed += total_size;
            messages_processed++;
        }
    }

    /* Shift remaining partial message to front of buffer */
    if (bytes_consumed > 0 && bytes_consumed < peer->cold.ibuflen) {
        uint16_t remaining = peer->cold.ibuflen - bytes_consumed;
        pt_memmove(peer->cold.ibuf, peer->cold.ibuf + bytes_consumed,
                    remaining);
        peer->cold.ibuflen = remaining;
    } else if (bytes_consumed == peer->cold.ibuflen) {
        peer->cold.ibuflen = 0;
    }

    /* Note: Do NOT clear PT_OT_FLAG_DATA_AVAILABLE here.
     * pt_ot_tcp_recv() clears it when OTRcv returns kOTNoDataErr,
     * which is the correct place. Clearing here would race with the
     * notifier: new data could arrive between OTRcv draining and this
     * clear, causing lost T_DATA notifications and throughput stalls. */

    return messages_processed;
}

/* ========================================================================== */
/* Connected Endpoint Processing                                               */
/* ========================================================================== */

/**
 * Process a connected endpoint for disconnect events and data I/O.
 *
 * Priority: abortive disconnect > orderly disconnect > data receive.
 *
 * @param ctx  PeerTalk context
 * @param od   OT platform data
 * @param idx  Endpoint index
 * @param hot  Hot endpoint data
 */
static void pt_ot_poll_connected(struct pt_context *ctx,
                                   pt_ot_data *od,
                                   int idx,
                                   pt_tcp_endpoint_hot *hot)
{
    struct pt_peer *peer = hot->peer;
    char ip_str[PT_IP_STR_LEN];

    if (peer == NULL)
        return;

    /* Process deferred log events from notifier */
    pt_ot_tcp_process_log_events(ctx, hot, idx);

    /* --- Abortive disconnect (T_DISCONNECT) --- */
    if (PT_FLAG_TEST(hot->flags, PT_OT_FLAG_DISCONNECT)) {
        PeerTalk_PeerID disc_id = peer->hot.id;

        PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_DISCONNECT);

        /* Must call OTRcvDisconnect to acknowledge */
        OTRcvDisconnect(hot->ref, NULL);

        PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] peer %u disconnected (abortive)",
            idx, (unsigned)disc_id);

        /* CRITICAL: Cleanup BEFORE callback so application can
         * immediately reconnect in the callback if desired.
         * Matches MacTCP pattern (tcp_connect.c:419). */
        if (ctx->plat && ctx->plat->pipeline_cleanup)
            ctx->plat->pipeline_cleanup(ctx, peer);
        peer->cold.obuflen = 0;  /* Discard unsent coalesced data */
        pt_ot_free_peer_queues(peer);
        peer->hot.connection = NULL;
        pt_peer_set_state(ctx, peer, PT_PEER_STATE_DISCOVERED);
        hot->peer = NULL;
        pt_ot_tcp_cleanup(ctx, idx);

        if (ctx->callbacks.on_peer_disconnected != NULL) {
            ctx->callbacks.on_peer_disconnected(
                (PeerTalk_Context *)ctx,
                disc_id, 0,
                ctx->callbacks.user_data);
        }

        return;
    }

    /* --- Orderly disconnect (T_ORDREL) ---
     *
     * Per Networking With Open Transport p.516-517:
     * 1. Drain remaining data first
     * 2. Call OTRcvOrderlyDisconnect to acknowledge
     * 3. Send our orderly disconnect
     * 4. Clean up
     */
    if (PT_FLAG_TEST(hot->flags, PT_OT_FLAG_ORDERLY_RELEASE)) {
        PeerTalk_PeerID disc_id = peer->hot.id;

        PT_FLAG_CLEAR(hot->flags, PT_OT_FLAG_ORDERLY_RELEASE);

        /* Drain remaining data */
        pt_ot_tcp_recv_process(ctx, idx, peer);

        /* Acknowledge orderly disconnect */
        OTRcvOrderlyDisconnect(hot->ref);

        /* Send our orderly disconnect to complete handshake */
        OTSndOrderlyDisconnect(hot->ref);

        PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
            "TCP[%d] peer %u disconnected (orderly)",
            idx, (unsigned)disc_id);

        /* CRITICAL: Cleanup BEFORE callback so application can
         * immediately reconnect in the callback if desired. */
        if (ctx->plat && ctx->plat->pipeline_cleanup)
            ctx->plat->pipeline_cleanup(ctx, peer);
        peer->cold.obuflen = 0;  /* Discard unsent coalesced data */
        pt_ot_free_peer_queues(peer);
        peer->hot.connection = NULL;
        pt_peer_set_state(ctx, peer, PT_PEER_STATE_DISCOVERED);
        hot->peer = NULL;
        pt_ot_tcp_cleanup(ctx, idx);

        if (ctx->callbacks.on_peer_disconnected != NULL) {
            ctx->callbacks.on_peer_disconnected(
                (PeerTalk_Context *)ctx,
                disc_id, 0,
                ctx->callbacks.user_data);
        }

        return;
    }

    /* --- Data receive ---
     * Always try to read, don't gate on PT_OT_FLAG_DATA_AVAILABLE.
     * The flag is set by the notifier on T_DATA, but clearing it races
     * with new data arriving during OTRcv. Unconditional reads ensure
     * we never miss data; OTRcv returns kOTNoDataErr cheaply when empty. */
    {
        int result = pt_ot_tcp_recv_process(ctx, idx, peer);
        if (result < 0) {
            /* Protocol error or DISCONNECT message - close connection */
            PeerTalk_PeerID disc_id = peer->hot.id;

            PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
                "TCP[%d] recv error, closing peer %u",
                idx, (unsigned)disc_id);

            pt_ot_tcp_close(ctx, idx);

            /* CRITICAL: Cleanup BEFORE callback so application can
             * immediately reconnect in the callback if desired. */
            if (ctx->plat && ctx->plat->pipeline_cleanup)
                ctx->plat->pipeline_cleanup(ctx, peer);
            peer->cold.obuflen = 0;  /* Discard unsent coalesced data */
            pt_ot_free_peer_queues(peer);
            peer->hot.connection = NULL;
            pt_peer_set_state(ctx, peer, PT_PEER_STATE_DISCOVERED);
            hot->peer = NULL;

            if (ctx->callbacks.on_peer_disconnected != NULL) {
                ctx->callbacks.on_peer_disconnected(
                    (PeerTalk_Context *)ctx,
                    disc_id, 0,
                    ctx->callbacks.user_data);
            }

            return;
        }

        if (result > 0) {
            peer->hot.last_seen = (pt_tick_t)TickCount();
        }
    }

    /* --- Send queue drain: send queued messages with protocol framing ---
     *
     * Mirrors MacTCP poll_mactcp.c Tier 1 drain pattern.
     * With async send active, most messages bypass the queue entirely.
     * This drain handles overflow when async returns PT_ERR_WOULD_BLOCK.
     * Drain limit of 64 allows rapid recovery after flow control lifts.
     */
    if (peer->send_queue) {
        pt_queue *q = peer->send_queue;
        const void *data;
        uint16_t len;
        int drain_count = 0;
        const int max_drain = 64;

        while (drain_count < max_drain &&
               pt_queue_pop_priority_direct(q, &data, &len) == 0) {
            uint8_t slot_flags = q->slots[q->pending_pop_slot].flags;
            uint8_t msg_flags = (slot_flags & PT_SLOT_FRAGMENT)
                              ? PT_MSG_FLAG_FRAGMENT : 0;
            int result;

            result = pt_ot_send_framed(ctx, idx, peer, data, len,
                                         msg_flags);

            if (result == PT_ERR_WOULD_BLOCK) {
                /* OT flow control - rollback and retry next poll */
                pt_queue_pop_priority_rollback(q);
                break;
            }

            pt_queue_pop_priority_commit(q);
            drain_count++;

            if (result != 0) {
                PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                    "Tier 1 send to peer %u failed: %d",
                    (unsigned)peer->hot.id, result);
            }
        }
    }

    /* --- Tier 2: Direct buffer for large messages --- */
    if (pt_direct_buffer_ready(&peer->send_direct)) {
        pt_direct_buffer *buf = &peer->send_direct;
        int result;

        pt_direct_buffer_mark_sending(buf);
        result = pt_ot_send_framed(ctx, idx, peer, buf->data,
                                     buf->length, buf->msg_flags);

        if (result == PT_ERR_WOULD_BLOCK) {
            /* Flow control: transition back to QUEUED for retry next poll.
             * DO NOT call pt_direct_buffer_complete which would discard the
             * unsent data, causing silent message loss. */
            buf->state = PT_DIRECT_QUEUED;
        } else {
            pt_direct_buffer_complete(buf);
            if (result != 0) {
                PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                    "Tier 2 send to peer %u failed: %d",
                    (unsigned)peer->hot.id, result);
            }
        }
    }

    /* --- Stream send: process active stream transfers (e.g., log streaming) --- */
    pt_stream_poll(ctx, peer, pt_ot_stream_send);

    /* --- Flush coalesced send data ---
     *
     * Send any accumulated frames from the async send path and queue drain.
     * This produces fewer, larger OTSnd calls → MSS-sized TCP segments
     * instead of one tiny TCP segment per PeerTalk frame. */
    (void)pt_ot_flush_send(ctx, idx, peer);

    (void)od;
    (void)ip_str;
}

/* ========================================================================== */
/* Main Poll Function                                                          */
/* ========================================================================== */

/**
 * Main OT poll function.
 *
 * Integrates discovery, listener, connecting endpoints, and connected
 * endpoint I/O into a single polling pass. Called from PeerTalk_Poll().
 *
 * Uses bitmap-optimized iteration for TCP endpoints: only visits
 * slots that are actually in use.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success
 */
int pt_ot_poll(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    unsigned long now = (unsigned long)TickCount();
    unsigned long announce_interval;
    uint32_t active;
    unsigned int i;

    /* 1. Process discovery - drain all pending UDP packets */
    while (pt_ot_discovery_poll(ctx) > 0)
        ;

    /* 2. Process listener for incoming connections */
    pt_ot_listen_poll(ctx);

    /* 3. Process connecting endpoints */
    pt_ot_connect_poll(ctx);

    /* 4. Process connected/closing endpoints using bitmap iteration.
     *
     * O(active) not O(PT_MAX_PEERS): only visit in-use slots via bitmap.
     * Hot array iteration: only touch 32-byte hot structs for flag checks.
     * Cold data access: only when needed for actual I/O operations.
     */
    active = ~od->tcp_pool.free_bitmap
           & ((1UL << od->tcp_pool.capacity) - 1);

    while (active) {
        pt_tcp_endpoint_hot *hot;
        int bit;

#if defined(__GNUC__)
        bit = __builtin_ffs((int)active) - 1;
#else
        {
            uint32_t tmp = active;
            bit = 0;
            while ((tmp & 1) == 0) { tmp >>= 1; bit++; }
        }
#endif
        active &= ~(1UL << bit);

        hot = pt_ot_get_tcp_hot(od, bit);
        if (hot == NULL)
            continue;

        switch (hot->state) {
        case PT_EP_DATAXFER:
            pt_ot_poll_connected(ctx, od, bit, hot);
            break;

        case PT_EP_CLOSING:
            /* Check for close timeout (30s) */
            if (pt_ot_tcp_check_close_timeout(ctx, bit)) {
                /* Timeout forced cleanup - peer was already handled */
                struct pt_peer *peer = hot->peer;
                if (peer != NULL) {
                    PeerTalk_PeerID disc_id = peer->hot.id;

                    /* CRITICAL: Cleanup BEFORE callback */
                    if (ctx->plat && ctx->plat->pipeline_cleanup)
                        ctx->plat->pipeline_cleanup(ctx, peer);
                    pt_ot_free_peer_queues(peer);
                    peer->hot.connection = NULL;
                    pt_peer_set_state(ctx, peer,
                                      PT_PEER_STATE_DISCOVERED);

                    if (ctx->callbacks.on_peer_disconnected != NULL) {
                        ctx->callbacks.on_peer_disconnected(
                            (PeerTalk_Context *)ctx,
                            disc_id, 0,
                            ctx->callbacks.user_data);
                    }
                }
            } else {
                /* Still closing - check if orderly release arrived */
                if (PT_FLAG_TEST(hot->flags,
                                  PT_OT_FLAG_ORDERLY_RELEASE)) {
                    PT_FLAG_CLEAR(hot->flags,
                                   PT_OT_FLAG_ORDERLY_RELEASE);
                    OTRcvOrderlyDisconnect(hot->ref);
                    /* Cleanup now that both sides have disconnected */
                    {
                        struct pt_peer *peer = hot->peer;
                        if (peer != NULL) {
                            PeerTalk_PeerID disc_id = peer->hot.id;

                            /* CRITICAL: Cleanup BEFORE callback */
                            if (ctx->plat && ctx->plat->pipeline_cleanup)
                                ctx->plat->pipeline_cleanup(ctx, peer);
                            pt_ot_free_peer_queues(peer);
                            peer->hot.connection = NULL;
                            pt_peer_set_state(ctx, peer,
                                              PT_PEER_STATE_DISCOVERED);

                            if (ctx->callbacks.on_peer_disconnected
                                != NULL) {
                                ctx->callbacks.on_peer_disconnected(
                                    (PeerTalk_Context *)ctx,
                                    disc_id, 0,
                                    ctx->callbacks.user_data);
                            }
                        }
                        hot->peer = NULL;
                        pt_ot_tcp_cleanup(ctx, bit);
                    }
                }
            }
            break;

        /* PT_EP_OUTGOING handled by pt_ot_connect_poll */
        /* PT_EP_INCOMING handled by pt_ot_listen_poll */
        default:
            break;
        }
    }

    /* 5. Periodic discovery announce */
    announce_interval = PT_OT_ANNOUNCE_INTERVAL_TICKS;

    if (ctx->discovery_active &&
        (long)(now - od->last_announce_tick) > (long)announce_interval) {
        pt_ot_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);
        od->last_announce_tick = now;
    }

    /* 6. Check for peer timeouts (discovered peers that went silent) */
    for (i = 0; i < ctx->max_peers; i++) {
        struct pt_peer *peer = &ctx->peers[i];

        if (peer->hot.state != PT_PEER_STATE_DISCOVERED)
            continue;

        if ((long)(now - peer->hot.last_seen) >
            (long)PT_OT_PEER_TIMEOUT_TICKS) {
            PT_CTX_DEBUG(ctx, PT_LOG_CAT_CONNECT,
                "Peer %u timed out", (unsigned)peer->hot.id);

            if (ctx->callbacks.on_peer_lost != NULL) {
                ctx->callbacks.on_peer_lost((PeerTalk_Context *)ctx,
                                            peer->hot.id,
                                            ctx->callbacks.user_data);
            }

            pt_peer_destroy(ctx, peer);
        }
    }

    return 0;
}

/* ========================================================================== */
/* Fast Poll (TCP I/O Only)                                                    */
/* ========================================================================== */

/**
 * Fast OT poll function.
 *
 * Only handles TCP I/O for connected peers - no discovery, listener,
 * periodic announces, or peer timeouts. Use for tight game loops.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success
 */
int pt_ot_poll_fast(struct pt_context *ctx)
{
    pt_ot_data *od = pt_ot_get(ctx);
    uint32_t active;

    active = ~od->tcp_pool.free_bitmap
           & ((1UL << od->tcp_pool.capacity) - 1);

    while (active) {
        pt_tcp_endpoint_hot *hot;
        int bit;

#if defined(__GNUC__)
        bit = __builtin_ffs((int)active) - 1;
#else
        {
            uint32_t tmp = active;
            bit = 0;
            while ((tmp & 1) == 0) { tmp >>= 1; bit++; }
        }
#endif
        active &= ~(1UL << bit);

        hot = pt_ot_get_tcp_hot(od, bit);
        if (hot == NULL || hot->state != PT_EP_DATAXFER)
            continue;

        pt_ot_poll_connected(ctx, od, bit, hot);
    }

    return 0;
}

#endif /* PT_PLATFORM_OT */
