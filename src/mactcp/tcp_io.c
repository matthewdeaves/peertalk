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
 */
static int pt_peer_stream_idx(struct pt_peer *peer)
{
    if (peer == NULL || peer->hot.connection == NULL)
        return -1;
    return (int)(intptr_t)peer->hot.connection;
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
        (unsigned)len, (unsigned)peer->cold.info.id, (unsigned)hdr.sequence);

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
 * NOTE: For simpler implementations or debugging, TCPRcv can be used
 * instead. TCPRcv copies data to your buffer directly, eliminating the
 * need for TCPRcvBfrReturn, at the cost of an extra memory copy. Use
 * TCPNoCopyRcv (this function) for production performance.
 *
 * DOD: Uses hot/cold struct split. Looks up stream by index stored in peer.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to receive from
 * @return      1 if message processed, 0 if no data, -1 on error/disconnect
 */
int pt_mactcp_tcp_recv(struct pt_context *ctx, struct pt_peer *peer)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_message_header hdr;
    uint8_t *data_ptr;
    uint16_t data_len;
    uint16_t crc_expected, crc_actual;
    OSErr err;
    int idx;
    pt_tcp_stream_hot *hot;
    pt_tcp_stream_cold *cold;
    int rds_idx;
    uint16_t expected_len;

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

    /* Check for data (hot struct flags) */
    if (!(hot->asr_flags & PT_ASR_DATA_ARRIVED))
        return 0;

    hot->asr_flags &= ~PT_ASR_DATA_ARRIVED;

    /* Return any previous RDS buffers (rds in cold struct) */
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
                "TCPRcvBfrReturn failed: %d (buffer leak possible)", (int)err);
        }
        hot->rds_outstanding = 0;
    }

    /*
     * Issue TCPNoCopyRcv (pb and rds in cold struct).
     * From MacTCP Programmer's Guide: "The minimum value of the command
     * timeout is 2 seconds; 0 means infinite."
     *
     * Since we only call this after ASR signals data arrival, data is
     * already buffered and will return immediately. The 2s timeout is
     * only a fallback for rare race conditions.
     */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPNoCopyRcv;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;

    cold->pb.csParam.receive.commandTimeoutValue = 2;  /* Minimum per docs */
    cold->pb.csParam.receive.rdsPtr = (Ptr)cold->rds;
    cold->pb.csParam.receive.rdsLength = sizeof(cold->rds) / sizeof(cold->rds[0]);

    err = PBControlSync((ParmBlkPtr)&cold->pb);

    if (err == commandTimeout) {
        /* No data ready */
        return 0;
    }

    if (err != noErr) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "TCPNoCopyRcv failed: %d", (int)err);

        if (err == connectionClosing || err == connectionTerminated) {
            return -1;
        }

        return 0;
    }

    /* Mark RDS as needing return (hot struct flag) */
    hot->rds_outstanding = 1;

    /* Process received data - may be in multiple RDS entries (in cold struct) */
    /* Copy to peer's ibuf and process there */
    data_len = 0;
    for (rds_idx = 0; rds_idx < 6 && cold->rds[rds_idx].length > 0; rds_idx++) {
        unsigned short chunk_len = cold->rds[rds_idx].length;
        if (data_len + chunk_len > sizeof(peer->cold.ibuf)) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                "Received data exceeds ibuf");
            break;
        }
        pt_memcpy(peer->cold.ibuf + data_len, cold->rds[rds_idx].ptr, chunk_len);
        data_len += chunk_len;
    }

    if (data_len < PT_MESSAGE_HEADER_SIZE + 2) {
        /* Not enough data for header + CRC - partial message */
        return 0;
    }

    /* Parse header */
    if (pt_message_decode_header(ctx, peer->cold.ibuf, data_len, &hdr) < 0) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "Invalid message header from peer %u", (unsigned)peer->cold.info.id);
        return 0;
    }

    /* Check we have complete message */
    expected_len = PT_MESSAGE_HEADER_SIZE + hdr.payload_len + 2;
    if (data_len < expected_len) {
        /* Partial message - need more data */
        return 0;
    }

    /* Verify CRC */
    data_ptr = peer->cold.ibuf + PT_MESSAGE_HEADER_SIZE;
    crc_expected = ((uint16_t)peer->cold.ibuf[PT_MESSAGE_HEADER_SIZE + hdr.payload_len] << 8) |
                    peer->cold.ibuf[PT_MESSAGE_HEADER_SIZE + hdr.payload_len + 1];
    crc_actual = pt_crc16(peer->cold.ibuf, PT_MESSAGE_HEADER_SIZE);
    if (hdr.payload_len > 0)
        crc_actual = pt_crc16_update(crc_actual, data_ptr, hdr.payload_len);

    if (crc_actual != crc_expected) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "CRC mismatch: expected=%04X actual=%04X",
            (unsigned)crc_expected, (unsigned)crc_actual);
        return 0;
    }

    /* Update peer state */
    peer->hot.last_seen = (pt_tick_t)TickCount();
    peer->hot.recv_seq = hdr.sequence;
    pt_peer_check_canaries(ctx, peer);

    /* Handle by message type */
    switch (hdr.type) {
    case PT_MSG_TYPE_DATA:
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
            "Received %u bytes from peer %u (seq=%u)",
            (unsigned)hdr.payload_len, (unsigned)peer->cold.info.id,
            (unsigned)hdr.sequence);

        if (ctx->callbacks.on_message_received != NULL) {
            ctx->callbacks.on_message_received(
                (PeerTalk_Context *)ctx,
                peer->cold.info.id, data_ptr, hdr.payload_len,
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
            "Received DISCONNECT from peer %u", (unsigned)peer->cold.info.id);
        return -1;

    default:
        PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
            "Unknown message type %u from peer %u",
            (unsigned)hdr.type, (unsigned)peer->cold.info.id);
        break;
    }

    return 1;
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
