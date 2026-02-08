/**
 * @file tcp_io.c
 * @brief MacTCP TCP I/O Implementation
 *
 * TCPSend with WDS and TCPNoCopyRcv for high-performance data transfer.
 * Uses hot/cold struct split for 68k cache efficiency.
 *
 * Key Insight from MacTCP Programmer's Guide:
 * "Using the TCPNoCopyRcv routine is the high-performance method. Data is
 * delivered to the user directly from the internal TCP receive buffers
 * and no copy is required."
 *
 * References:
 * - MacTCP Programmer's Guide (1989), Chapter 3: "TCP"
 */

#include "mactcp_defs.h"
#include "protocol.h"
#include "peer.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_MACTCP)

#include <Devices.h>
#include <OSUtils.h>  /* For TickCount() - only in main loop! */

/* ========================================================================== */
/* External Accessors                                                         */
/* ========================================================================== */

extern short pt_mactcp_get_refnum(void);

/* ========================================================================== */
/* Helper Functions                                                           */
/* ========================================================================== */

/**
 * Get stream index from peer's connection handle.
 * Connection stores idx+1 so that stream 0 doesn't become NULL.
 */
static int pt_peer_stream_idx(struct pt_peer *peer)
{
    if (peer == NULL || peer->hot.connection == NULL)
        return -1;
    return (int)(intptr_t)peer->hot.connection - 1;
}

/* Forward declaration */
static int pt_mactcp_tcp_send_control(struct pt_context *ctx,
                                      int stream_idx,
                                      uint8_t msg_type);

/* ========================================================================== */
/* TCP Send                                                                   */
/* ========================================================================== */

/**
 * Send data on TCP stream.
 *
 * From MacTCP Programmer's Guide: TCPSend with WDS.
 *
 * DOD: Uses hot/cold struct split. Looks up stream by index stored in peer.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to send to
 * @param data  Data to send
 * @param len   Data length
 * @return      0 on success, negative error code on failure
 */
int pt_mactcp_tcp_send(struct pt_context *ctx, struct pt_peer *peer,
                       const void *data, uint16_t len)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_message_header hdr;
    uint8_t header_buf[PT_MESSAGE_HEADER_SIZE];
    uint8_t crc_buf[2];
    uint16_t crc;
    wdsEntry wds[4];
    OSErr err;
    int idx;
    pt_tcp_stream_hot *hot;
    pt_tcp_stream_cold *cold;

    if (peer == NULL || peer->hot.magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    idx = pt_peer_stream_idx(peer);
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return PT_ERR_INVALID_STATE;

    hot = &md->tcp_hot[idx];
    cold = &md->tcp_cold[idx];

    if (hot->state != PT_STREAM_CONNECTED)
        return PT_ERR_INVALID_STATE;

    if (len > PT_MESSAGE_MAX_PAYLOAD)
        return PT_ERR_INVALID_PARAM;

    /* Build message header */
    hdr.version = PT_PROTOCOL_VERSION;
    hdr.type = PT_MSG_TYPE_DATA;
    hdr.flags = 0;
    hdr.sequence = peer->hot.send_seq++;
    hdr.payload_len = len;

    pt_message_encode_header(&hdr, header_buf);

    /* Calculate CRC over header + payload */
    crc = pt_crc16(header_buf, PT_MESSAGE_HEADER_SIZE);
    if (len > 0)
        crc = pt_crc16_update(crc, data, len);
    crc_buf[0] = (crc >> 8) & 0xFF;
    crc_buf[1] = crc & 0xFF;

    /*
     * Build WDS: header + payload + CRC
     *
     * AMENDMENT (2026-02-03): LaunchAPPL Pattern Verification
     * Verified from LaunchAPPL MacTCPStream.cc:96-98 - WDS array pattern:
     *   wdsEntry wds[2] = { {(unsigned short)n, (Ptr)p}, {0, nullptr} };
     *
     * Stack-allocated WDS is safe because TCPSend is synchronous
     * (PBControlSync blocks until complete).
     */
    wds[0].length = PT_MESSAGE_HEADER_SIZE;
    wds[0].ptr = (Ptr)header_buf;
    wds[1].length = len;
    wds[1].ptr = (Ptr)data;
    wds[2].length = 2;
    wds[2].ptr = (Ptr)crc_buf;
    wds[3].length = 0;  /* Terminator - WDS array MUST end with zero-length entry */
    wds[3].ptr = NULL;

    /* Setup send call (pb in cold struct) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPSend;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;

    cold->pb.csParam.send.ulpTimeoutValue = 30;
    cold->pb.csParam.send.ulpTimeoutAction = 1;
    cold->pb.csParam.send.validityFlags = 0xC0;
    cold->pb.csParam.send.pushFlag = 1;  /* Push immediately */
    cold->pb.csParam.send.urgentFlag = 0;
    cold->pb.csParam.send.wdsPtr = (Ptr)wds;

    /*
     * Synchronous send - simplifies buffer lifetime.
     *
     * From MacTCP Programmer's Guide: "The command is completed when all
     * data has been sent and acknowledged or when an error occurs."
     *
     * NOTE: This can block for 30+ seconds on slow/lossy connections.
     */
    err = PBControlSync((ParmBlkPtr)&cold->pb);

    if (err != noErr) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "TCPSend failed: %d", (int)err);

        if (err == connectionClosing || err == connectionTerminated) {
            hot->asr_flags |= PT_ASR_CONN_CLOSED;
        }

        return PT_ERR_NETWORK;
    }

    PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
        "Sent %u bytes to peer %u (seq=%u)",
        (unsigned)len, (unsigned)peer->hot.id, (unsigned)hdr.sequence);

    /* Update last seen timestamp (main loop context - TickCount() is safe here) */
    peer->hot.last_seen = (pt_tick_t)TickCount();
    pt_peer_check_canaries(ctx, peer);

    return 0;
}

/* ========================================================================== */
/* TCP Receive                                                                */
/* ========================================================================== */

/**
 * Receive data using TCPNoCopyRcv (high-performance method).
 *
 * Implements proper message framing to handle:
 * - Multiple messages arriving in one TCPNoCopyRcv call
 * - Partial messages split across multiple calls
 *
 * Uses peer->cold.ibuf as a persistent receive buffer with ibuflen
 * tracking unprocessed bytes.
 *
 * DOD: Uses hot/cold struct split. Looks up stream by index stored in peer.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to receive from
 * @return      Number of messages processed (0+), or -1 on error/disconnect
 */
int pt_mactcp_tcp_recv(struct pt_context *ctx, struct pt_peer *peer)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_message_header hdr;
    uint8_t *msg_start;
    uint8_t *data_ptr;
    uint16_t crc_expected, crc_actual;
    OSErr err;
    int idx;
    pt_tcp_stream_hot *hot;
    pt_tcp_stream_cold *cold;
    int rds_idx;
    uint16_t expected_len;
    uint16_t bytes_consumed;
    int messages_processed = 0;

    if (peer == NULL || peer->hot.magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    idx = pt_peer_stream_idx(peer);
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return 0;

    hot = &md->tcp_hot[idx];
    cold = &md->tcp_cold[idx];

    /* Check for connection close (hot struct flags) */
    if (hot->asr_flags & PT_ASR_CONN_CLOSED) {
        hot->asr_flags &= ~PT_ASR_CONN_CLOSED;
        return -1;  /* Trigger disconnect */
    }

    /* Fetch new data if ASR signaled arrival */
    if (hot->asr_flags & PT_ASR_DATA_ARRIVED) {
        int fetch_loops = 0;
        const int max_fetch_loops = 8;  /* Handle multiple 4KB messages per poll */

        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
            "RCV: ASR signaled, stream=%d ibuflen=%u",
            idx, (unsigned)peer->cold.ibuflen);
        hot->asr_flags &= ~PT_ASR_DATA_ARRIVED;

        /*
         * THROUGHPUT OPTIMIZATION: Loop to drain MacTCP's receive buffers.
         *
         * Due to the 25% threshold, large messages trigger early completion.
         * After returning RDS, use TCPStatus to check for more unread data.
         * If data remains, issue another TCPNoCopyRcv immediately instead of
         * waiting for the next ASR (which won't fire until all data is consumed).
         */
        do {
            uint16_t ibuflen_before = peer->cold.ibuflen;

            /* Check if ibuf has room for more data */
            if (peer->cold.ibuflen >= sizeof(peer->cold.ibuf) - 1500) {
                break;  /* ibuf nearly full, process what we have */
            }

            /*
             * Issue TCPNoCopyRcv (pb and rds in cold struct).
             * From MacTCP Programmer's Guide: "The minimum value of the command
             * timeout is 2 seconds; 0 means infinite."
             */
            pt_memset(&cold->pb, 0, sizeof(cold->pb));
            cold->pb.csCode = TCPNoCopyRcv;
            cold->pb.ioCRefNum = md->driver_refnum;
            cold->pb.tcpStream = hot->stream;

            cold->pb.csParam.receive.commandTimeoutValue = 2;
            cold->pb.csParam.receive.rdsPtr = (Ptr)cold->rds;
            cold->pb.csParam.receive.rdsLength = PT_MAX_RDS_ENTRIES;

            err = PBControlSync((ParmBlkPtr)&cold->pb);

            if (err == commandTimeout) {
                /* No data ready - exit loop */
                break;
            } else if (err != noErr) {
                PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                    "TCPNoCopyRcv failed: %d", (int)err);

                if (err == connectionClosing || err == connectionTerminated) {
                    return -1;
                }
                break;
            }

            /* Append RDS data to ibuf after existing content */
            hot->rds_outstanding = 1;

            for (rds_idx = 0; rds_idx < PT_MAX_RDS_ENTRIES && cold->rds[rds_idx].length > 0; rds_idx++) {
                unsigned short chunk_len = cold->rds[rds_idx].length;
                if (peer->cold.ibuflen + chunk_len > sizeof(peer->cold.ibuf)) {
                    PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                        "RCV: data exceeds ibuf (%u + %u > %u)",
                        (unsigned)peer->cold.ibuflen, (unsigned)chunk_len,
                        (unsigned)sizeof(peer->cold.ibuf));
                    break;
                }
                pt_memcpy(peer->cold.ibuf + peer->cold.ibuflen,
                          cold->rds[rds_idx].ptr, chunk_len);
                peer->cold.ibuflen += chunk_len;
            }

            PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
                "RCV: +%u bytes in %d chunks (ibuflen %u->%u)",
                (unsigned)(peer->cold.ibuflen - ibuflen_before), rds_idx,
                (unsigned)ibuflen_before, (unsigned)peer->cold.ibuflen);

            /*
             * CRITICAL: Return RDS buffers IMMEDIATELY after copying.
             * MacTCP won't send another ASR until buffers are returned.
             */
            if (hot->rds_outstanding) {
                TCPiopb return_pb;
                pt_memset(&return_pb, 0, sizeof(return_pb));
                return_pb.csCode = TCPRcvBfrReturn;
                return_pb.ioCRefNum = md->driver_refnum;
                return_pb.tcpStream = hot->stream;
                return_pb.csParam.receive.rdsPtr = (Ptr)cold->rds;

                err = PBControlSync((ParmBlkPtr)&return_pb);
                if (err != noErr) {
                    PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                        "TCPRcvBfrReturn failed: %d", (int)err);
                }
                hot->rds_outstanding = 0;
            }

            /* If no data was received this iteration, exit loop */
            if (peer->cold.ibuflen == ibuflen_before) {
                break;
            }

            fetch_loops++;

            /*
             * Check if more data is waiting using TCPStatus.
             * This avoids waiting for ASR when data is already buffered.
             */
            if (fetch_loops < max_fetch_loops) {
                TCPiopb status_pb;
                pt_memset(&status_pb, 0, sizeof(status_pb));
                status_pb.csCode = TCPStatus;
                status_pb.ioCRefNum = md->driver_refnum;
                status_pb.tcpStream = hot->stream;

                if (PBControlSync((ParmBlkPtr)&status_pb) == noErr) {
                    /* amtUnreadData is in csParam.status.amtUnreadData */
                    if (status_pb.csParam.status.amtUnreadData == 0) {
                        break;  /* No more data waiting */
                    }
                    /* More data available - continue loop */
                } else {
                    break;  /* Status failed, exit loop */
                }
            }
        } while (fetch_loops < max_fetch_loops);
    }

    /*
     * Process all complete messages in ibuf.
     * This handles multiple messages arriving in one receive call.
     */
    bytes_consumed = 0;
    msg_start = peer->cold.ibuf;

    while (peer->cold.ibuflen - bytes_consumed >= PT_MESSAGE_HEADER_SIZE + 2) {
        uint16_t remaining = peer->cold.ibuflen - bytes_consumed;

        /* Parse header */
        if (pt_message_decode_header(ctx, msg_start, remaining, &hdr) < 0) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                "Invalid message header from peer %u (offset %u)",
                (unsigned)peer->hot.id, (unsigned)bytes_consumed);
            /* Discard buffer on framing error - can't recover */
            peer->cold.ibuflen = 0;
            return messages_processed > 0 ? messages_processed : 0;
        }

        /* Check we have complete message */
        expected_len = PT_MESSAGE_HEADER_SIZE + hdr.payload_len + 2;
        if (remaining < expected_len) {
            /* Partial message - wait for more data */
            break;
        }

        /* Verify CRC */
        data_ptr = msg_start + PT_MESSAGE_HEADER_SIZE;
        crc_expected = ((uint16_t)msg_start[PT_MESSAGE_HEADER_SIZE + hdr.payload_len] << 8) |
                        msg_start[PT_MESSAGE_HEADER_SIZE + hdr.payload_len + 1];
        crc_actual = pt_crc16(msg_start, PT_MESSAGE_HEADER_SIZE);
        if (hdr.payload_len > 0)
            crc_actual = pt_crc16_update(crc_actual, data_ptr, hdr.payload_len);

        if (crc_actual != crc_expected) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                "CRC mismatch: expected=%04X actual=%04X",
                (unsigned)crc_expected, (unsigned)crc_actual);
            /* Discard buffer on CRC error - can't recover */
            peer->cold.ibuflen = 0;
            return messages_processed > 0 ? messages_processed : 0;
        }

        /* Message is valid - consume it */
        bytes_consumed += expected_len;
        messages_processed++;

        /* Update peer state */
        peer->hot.last_seen = (pt_tick_t)TickCount();
        peer->hot.recv_seq = hdr.sequence;
        pt_peer_check_canaries(ctx, peer);

        /* Handle by message type */
        switch (hdr.type) {
        case PT_MSG_TYPE_DATA:
            PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
                "Received %u bytes from peer %u (seq=%u)",
                (unsigned)hdr.payload_len, (unsigned)peer->hot.id,
                (unsigned)hdr.sequence);

            if (ctx->callbacks.on_message_received != NULL) {
                ctx->callbacks.on_message_received(
                    (PeerTalk_Context *)ctx,
                    peer->hot.id, data_ptr, hdr.payload_len,
                    ctx->callbacks.user_data);
            }
            break;

        case PT_MSG_TYPE_PING:
            pt_mactcp_tcp_send_control(ctx, idx, PT_MSG_TYPE_PONG);
            break;

        case PT_MSG_TYPE_PONG:
            /* Update latency estimate - could calculate RTT here */
            break;

        case PT_MSG_TYPE_DISCONNECT:
            PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK,
                "Received DISCONNECT from peer %u", (unsigned)peer->hot.id);
            peer->cold.ibuflen = 0;
            return -1;

        default:
            PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
                "Unknown message type %u from peer %u",
                (unsigned)hdr.type, (unsigned)peer->hot.id);
            break;
        }

        /* Advance to next message */
        msg_start += expected_len;
    }

    /*
     * Shift remaining partial message to front of buffer.
     * This preserves data for the next receive call.
     */
    if (bytes_consumed > 0 && bytes_consumed < peer->cold.ibuflen) {
        uint16_t remaining = peer->cold.ibuflen - bytes_consumed;
        pt_memmove(peer->cold.ibuf, peer->cold.ibuf + bytes_consumed, remaining);
        peer->cold.ibuflen = remaining;
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
            "RCV: shifted %u bytes, %u remaining",
            (unsigned)bytes_consumed, (unsigned)remaining);
    } else if (bytes_consumed == peer->cold.ibuflen) {
        /* All data consumed */
        peer->cold.ibuflen = 0;
    }

    /*
     * Proactive check: If we processed messages and have room in ibuf,
     * check TCPStatus for more data. Set ASR flag if data is waiting
     * so next poll will fetch immediately.
     */
    if (messages_processed > 0 && peer->cold.ibuflen < sizeof(peer->cold.ibuf) / 2) {
        TCPiopb status_pb;
        pt_memset(&status_pb, 0, sizeof(status_pb));
        status_pb.csCode = TCPStatus;
        status_pb.ioCRefNum = md->driver_refnum;
        status_pb.tcpStream = hot->stream;

        if (PBControlSync((ParmBlkPtr)&status_pb) == noErr) {
            if (status_pb.csParam.status.amtUnreadData > 0) {
                hot->asr_flags |= PT_ASR_DATA_ARRIVED;
            }
        }
    }

    return messages_processed;
}

/* ========================================================================== */
/* Control Messages                                                           */
/* ========================================================================== */

/**
 * Send control message (ping/pong/disconnect).
 *
 * DOD: Uses hot/cold struct split. Takes stream index.
 *
 * @param ctx         PeerTalk context
 * @param stream_idx  Stream index
 * @param msg_type    Message type (PT_MSG_TYPE_*)
 * @return            0 on success, -1 on failure
 */
static int pt_mactcp_tcp_send_control(struct pt_context *ctx,
                                      int stream_idx,
                                      uint8_t msg_type)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot;
    pt_tcp_stream_cold *cold;
    pt_message_header hdr;
    uint8_t buf[PT_MESSAGE_HEADER_SIZE + 2];
    uint16_t crc;
    wdsEntry wds[2];
    OSErr err;

    if (stream_idx < 0 || stream_idx >= PT_MAX_PEERS)
        return -1;

    hot = &md->tcp_hot[stream_idx];
    cold = &md->tcp_cold[stream_idx];

    hdr.version = PT_PROTOCOL_VERSION;
    hdr.type = msg_type;
    hdr.flags = 0;
    hdr.sequence = 0;
    hdr.payload_len = 0;

    pt_message_encode_header(&hdr, buf);

    crc = pt_crc16(buf, PT_MESSAGE_HEADER_SIZE);
    buf[PT_MESSAGE_HEADER_SIZE] = (crc >> 8) & 0xFF;
    buf[PT_MESSAGE_HEADER_SIZE + 1] = crc & 0xFF;

    wds[0].length = sizeof(buf);
    wds[0].ptr = (Ptr)buf;
    wds[1].length = 0;
    wds[1].ptr = NULL;

    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPSend;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;
    cold->pb.csParam.send.pushFlag = 1;
    cold->pb.csParam.send.wdsPtr = (Ptr)wds;

    err = PBControlSync((ParmBlkPtr)&cold->pb);

    if (err == noErr) {
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
            "Control message sent: type=%d", (int)msg_type);
    } else {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "Control message send failed: type=%d err=%d", (int)msg_type, (int)err);
    }

    return (err == noErr) ? 0 : -1;
}

/**
 * Send ping to peer.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to ping
 * @return      0 on success, negative error code on failure
 */
int pt_mactcp_tcp_ping(struct pt_context *ctx, struct pt_peer *peer)
{
    int idx;

    if (peer == NULL || peer->hot.magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    idx = pt_peer_stream_idx(peer);
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return PT_ERR_INVALID_STATE;

    return pt_mactcp_tcp_send_control(ctx, idx, PT_MSG_TYPE_PING);
}

/**
 * Send disconnect to peer (graceful shutdown notification).
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to notify
 * @return      0 on success, negative error code on failure
 */
int pt_mactcp_tcp_send_disconnect(struct pt_context *ctx, struct pt_peer *peer)
{
    int idx;

    if (peer == NULL || peer->hot.magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    idx = pt_peer_stream_idx(peer);
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return PT_ERR_INVALID_STATE;

    return pt_mactcp_tcp_send_control(ctx, idx, PT_MSG_TYPE_DISCONNECT);
}

#endif /* PT_PLATFORM_MACTCP */
