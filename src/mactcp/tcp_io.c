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
#include "queue.h"
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
 * Send data on TCP stream with custom message flags.
 *
 * From MacTCP Programmer's Guide: TCPSend with WDS.
 *
 * DOD: Uses hot/cold struct split. Looks up stream by index stored in peer.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to send to
 * @param data  Data to send
 * @param len   Data length
 * @param flags Message flags (PT_MSG_FLAG_*)
 * @return      0 on success, negative error code on failure
 */
int pt_mactcp_tcp_send_with_flags(struct pt_context *ctx, struct pt_peer *peer,
                                  const void *data, uint16_t len, uint8_t flags)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_message_header hdr;
    pt_compact_header compact_hdr;
    uint8_t header_buf[PT_MESSAGE_HEADER_SIZE];  /* Large enough for either format */
    uint8_t crc_buf[2];
    uint16_t crc;
    wdsEntry wds[4];
    OSErr err;
    int idx;
    pt_tcp_stream_hot *hot;
    pt_tcp_stream_cold *cold;
    int use_compact;
    uint16_t header_size;

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

    /* Check if compact headers are negotiated with this peer.
     * IMPORTANT: Don't use compact headers for fragments because
     * PT_MSG_FLAG_FRAGMENT (0x10) doesn't fit in the 4-bit flags field. */
    use_compact = peer->cold.caps.compact_mode &&
                  !(flags & PT_MSG_FLAG_FRAGMENT);

    if (use_compact) {
        /* Build compact header (4 bytes, no CRC) */
        compact_hdr.type = PT_MSG_TYPE_DATA;
        compact_hdr.flags = flags;
        compact_hdr.payload_len = len;
        pt_message_encode_compact(&compact_hdr, header_buf);
        header_size = PT_COMPACT_HEADER_SIZE;

        /* WDS: header + payload only (no CRC for compact) */
        wds[0].length = header_size;
        wds[0].ptr = (Ptr)header_buf;
        wds[1].length = len;
        wds[1].ptr = (Ptr)data;
        wds[2].length = 0;  /* Terminator */
        wds[2].ptr = NULL;
    } else {
        /* Build full message header (10 bytes + 2 CRC) */
        hdr.version = PT_PROTOCOL_VERSION;
        hdr.type = PT_MSG_TYPE_DATA;
        hdr.flags = flags;
        hdr.sequence = peer->hot.send_seq++;
        hdr.payload_len = len;

        pt_message_encode_header(&hdr, header_buf);
        header_size = PT_MESSAGE_HEADER_SIZE;

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
        wds[0].length = header_size;
        wds[0].ptr = (Ptr)header_buf;
        wds[1].length = len;
        wds[1].ptr = (Ptr)data;
        wds[2].length = 2;
        wds[2].ptr = (Ptr)crc_buf;
        wds[3].length = 0;  /* Terminator - WDS array MUST end with zero-length entry */
        wds[3].ptr = NULL;
    }

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

/**
 * Send data on TCP stream (convenience wrapper).
 *
 * Calls pt_mactcp_tcp_send_with_flags with flags=0 for normal data messages.
 */
int pt_mactcp_tcp_send(struct pt_context *ctx, struct pt_peer *peer,
                       const void *data, uint16_t len)
{
    return pt_mactcp_tcp_send_with_flags(ctx, peer, data, len, 0);
}

/* ========================================================================== */
/* Async Send Pipeline (Session 4)                                            */
/* ========================================================================== */

/* Forward declaration for slot getter */
extern pt_send_slot *pt_pipeline_get_slot(struct pt_context *ctx, struct pt_peer *peer);

/**
 * Send data asynchronously using the send pipeline.
 *
 * Unlike pt_mactcp_tcp_send, this function returns immediately after
 * issuing PBControlAsync. The send completes in the background and
 * is polled for completion via mactcp_poll_send_completions.
 *
 * Benefits:
 * - Multiple sends in-flight simultaneously (default 4)
 * - No blocking on per-message ACK
 * - Throughput improvement of 200-400% over sync sends
 *
 * Per MacTCP Guide (Lines 2959-2961): "You must not modify or relocate
 * the WDS and the buffers it describes until the TCPSend command has
 * been completed." This is why we copy data to the slot buffer.
 *
 * CRITICAL: ioCompletion is NULL - we poll ioResult instead.
 * Per Inside Macintosh Vol V (Lines 61337-61340), completion routines
 * execute at interrupt level and cannot call Memory Manager.
 * Additionally, TCPAbort fires pending completions during shutdown,
 * which can crash if memory is already freed. Polling is safer.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to send to
 * @param data  Data to send
 * @param len   Data length
 * @return      PT_OK on success, PT_ERR_WOULD_BLOCK if all slots busy,
 *              negative error code on failure
 */
int pt_mactcp_tcp_send_async(struct pt_context *ctx, struct pt_peer *peer,
                              const void *data, uint16_t len)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_send_slot *slot;
    TCPiopb *pb;
    pt_message_header hdr;
    pt_compact_header compact_hdr;
    uint16_t crc;
    int idx;
    pt_tcp_stream_hot *hot;
    OSErr err;
    int use_compact;
    uint16_t header_size;

    if (peer == NULL || peer->hot.magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    idx = pt_peer_stream_idx(peer);
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return PT_ERR_INVALID_STATE;

    hot = &md->tcp_hot[idx];

    if (hot->state != PT_STREAM_CONNECTED)
        return PT_ERR_INVALID_STATE;

    if (len > PT_PIPELINE_MAX_PAYLOAD)
        return PT_ERR_MESSAGE_TOO_LARGE;

    /* Get a free slot (handles lazy allocation for PT_LOWMEM) */
    slot = pt_pipeline_get_slot(ctx, peer);
    if (!slot) {
        return PT_ERR_WOULD_BLOCK;  /* All slots busy */
    }

    pb = (TCPiopb *)slot->platform_data;
    if (!pb) {
        return PT_ERR_INVALID_STATE;  /* TCPiopb not allocated */
    }

    /* Check if compact headers are negotiated with this peer */
    use_compact = peer->cold.caps.compact_mode;

    if (use_compact) {
        /* Build compact header (4 bytes, no CRC) */
        compact_hdr.type = PT_MSG_TYPE_DATA;
        compact_hdr.flags = 0;
        compact_hdr.payload_len = len;
        pt_message_encode_compact(&compact_hdr, slot->buffer);
        header_size = PT_COMPACT_HEADER_SIZE;

        /* Copy payload after compact header */
        if (len > 0) {
            pt_memcpy(slot->buffer + header_size, data, len);
        }

        slot->message_len = header_size + len;  /* No CRC for compact */
    } else {
        /* Build full message header (10 bytes + 2 CRC) */
        hdr.version = PT_PROTOCOL_VERSION;
        hdr.type = PT_MSG_TYPE_DATA;
        hdr.flags = 0;
        hdr.sequence = peer->hot.send_seq++;
        hdr.payload_len = len;

        pt_message_encode_header(&hdr, slot->buffer);
        header_size = PT_MESSAGE_HEADER_SIZE;

        /* Copy payload after header */
        if (len > 0) {
            pt_memcpy(slot->buffer + header_size, data, len);
        }

        /* Calculate and append CRC */
        crc = pt_crc16(slot->buffer, PT_MESSAGE_HEADER_SIZE);
        if (len > 0) {
            crc = pt_crc16_update(crc, data, len);
        }
        slot->buffer[header_size + len] = (crc >> 8) & 0xFF;
        slot->buffer[header_size + len + 1] = crc & 0xFF;

        slot->message_len = header_size + len + 2;
    }

    /* Setup WDS pointing to slot buffer */
    slot->wds[0].length = slot->message_len;
    slot->wds[0].ptr = (void *)slot->buffer;
    slot->wds[1].length = 0;  /* Sentinel */
    slot->wds[1].ptr = NULL;

    /* Setup async send - do NOT use completion routine */
    pt_memset(pb, 0, sizeof(*pb));
    pb->csCode = TCPSend;
    pb->ioCRefNum = md->driver_refnum;
    pb->tcpStream = hot->stream;
    pb->csParam.send.ulpTimeoutValue = 30;
    pb->csParam.send.ulpTimeoutAction = 1;
    pb->csParam.send.validityFlags = 0xC0;
    pb->csParam.send.pushFlag = 0;  /* Don't push - allow batching */
    pb->csParam.send.urgentFlag = 0;
    pb->csParam.send.wdsPtr = (Ptr)slot->wds;
    pb->ioCompletion = NULL;  /* CRITICAL: No completion - poll instead */

    slot->in_use = 1;
    slot->ioResult = 1;  /* Mark as in-progress */
    slot->completed = 0;
    peer->pipeline.pending_count++;

    /* Issue async send */
    err = PBControlAsync((ParmBlkPtr)pb);

    if (err != noErr) {
        /* Immediate error - release slot */
        slot->in_use = 0;
        peer->pipeline.pending_count--;
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "PBControlAsync failed immediately: %d", (int)err);
        return PT_ERR_NETWORK;
    }

    PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
        "Async send queued: peer=%u len=%u seq=%u pending=%u",
        peer->hot.id, len, hdr.sequence, peer->pipeline.pending_count);

    return PT_OK;
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
     * Supports both full headers (10+2) and compact headers (4, no CRC).
     */
    bytes_consumed = 0;
    msg_start = peer->cold.ibuf;

    /* Minimum: 4 bytes (enough to detect compact header and read payload length) */
    while (peer->cold.ibuflen - bytes_consumed >= PT_COMPACT_HEADER_SIZE) {
        uint16_t remaining = peer->cold.ibuflen - bytes_consumed;
        int is_compact;
        uint16_t header_size;

        /* Detect header format: compact (4 bytes, no CRC) vs full (10+2 bytes) */
        is_compact = pt_message_is_compact(msg_start, remaining);

        if (is_compact) {
            /* Compact header (4 bytes, no CRC) */
            pt_compact_header compact_hdr;

            if (pt_message_decode_compact(msg_start, remaining, &compact_hdr) < 0) {
                PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                    "Invalid compact header from peer %u (offset %u)",
                    (unsigned)peer->hot.id, (unsigned)bytes_consumed);
                peer->cold.ibuflen = 0;
                return messages_processed > 0 ? messages_processed : 0;
            }

            header_size = PT_COMPACT_HEADER_SIZE;
            expected_len = header_size + compact_hdr.payload_len;  /* No CRC */

            if (remaining < expected_len) {
                break;  /* Partial message */
            }

            /* Convert to standard header struct for message handling */
            hdr.version = PT_PROTOCOL_VERSION;
            hdr.type = compact_hdr.type;
            hdr.flags = compact_hdr.flags;
            hdr.sequence = 0;  /* Compact headers don't have sequence */
            hdr.payload_len = compact_hdr.payload_len;

            data_ptr = msg_start + header_size;
            /* No CRC verification for compact headers */

        } else {
            /* Full header (10 bytes + 2 CRC) - need at least 12 bytes */
            if (remaining < PT_MESSAGE_HEADER_SIZE + 2) {
                break;  /* Wait for more data */
            }

            if (pt_message_decode_header(ctx, msg_start, remaining, &hdr) < 0) {
                PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                    "Invalid message header from peer %u (offset %u)",
                    (unsigned)peer->hot.id, (unsigned)bytes_consumed);
                peer->cold.ibuflen = 0;
                return messages_processed > 0 ? messages_processed : 0;
            }

            header_size = PT_MESSAGE_HEADER_SIZE;
            expected_len = header_size + hdr.payload_len + 2;  /* With CRC */

            if (remaining < expected_len) {
                break;  /* Partial message */
            }

            /* Verify CRC for full headers */
            data_ptr = msg_start + header_size;
            crc_expected = ((uint16_t)msg_start[header_size + hdr.payload_len] << 8) |
                            msg_start[header_size + hdr.payload_len + 1];
            crc_actual = pt_crc16(msg_start, header_size);
            if (hdr.payload_len > 0)
                crc_actual = pt_crc16_update(crc_actual, data_ptr, hdr.payload_len);

            if (crc_actual != crc_expected) {
                PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                    "CRC mismatch: expected=%04X actual=%04X",
                    (unsigned)crc_expected, (unsigned)crc_actual);
                peer->cold.ibuflen = 0;
                return messages_processed > 0 ? messages_processed : 0;
            }
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

            /* Check for fragmented message */
            if (hdr.flags & PT_MSG_FLAG_FRAGMENT) {
                /* Fragment - process through reassembly */
                pt_fragment_header frag_hdr;
                const uint8_t *complete_data = NULL;
                uint16_t complete_len = 0;
                int reassembly_result;

                /* Decode fragment header from payload */
                if (pt_fragment_decode(data_ptr, hdr.payload_len, &frag_hdr) != 0) {
                    PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                        "Failed to decode fragment header from peer %u",
                        (unsigned)peer->hot.id);
                    break;
                }

                /* Process fragment through reassembly */
                reassembly_result = pt_reassembly_process(ctx, peer,
                    data_ptr, hdr.payload_len,
                    &frag_hdr, &complete_data, &complete_len);

                if (reassembly_result == 1 && complete_data != NULL) {
                    /* Complete message reassembled - deliver to app callback */
                    if (ctx->callbacks.on_message_received != NULL) {
                        ctx->callbacks.on_message_received(
                            (PeerTalk_Context *)ctx,
                            peer->hot.id, complete_data, complete_len,
                            ctx->callbacks.user_data);
                    }
                } else if (reassembly_result < 0) {
                    PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                        "Fragment reassembly error: %d", reassembly_result);
                }
                /* If 0, more fragments expected - nothing to do yet */
            } else {
                /* Non-fragmented - fire callback directly */
                if (ctx->callbacks.on_message_received != NULL) {
                    ctx->callbacks.on_message_received(
                        (PeerTalk_Context *)ctx,
                        peer->hot.id, data_ptr, hdr.payload_len,
                        ctx->callbacks.user_data);
                }
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

        case PT_MSG_TYPE_CAPABILITY:
            {
                pt_capability_msg caps;
                uint16_t effective_max;

                if (pt_capability_decode(ctx, data_ptr, hdr.payload_len, &caps) == 0) {
                    /* Store peer's capabilities in cold struct */
                    peer->cold.caps.max_message_size = caps.max_message_size;
                    peer->cold.caps.preferred_chunk = caps.preferred_chunk;
                    peer->cold.caps.capability_flags = caps.capability_flags;
                    peer->cold.caps.buffer_pressure = caps.buffer_pressure;
                    peer->cold.caps.caps_exchanged = 1;

                    /* Negotiate compact header mode - both must support it */
                    if ((caps.capability_flags & PT_CAPFLAG_COMPACT_HEADER) &&
                        (ctx->local_capability_flags & PT_CAPFLAG_COMPACT_HEADER)) {
                        peer->cold.caps.compact_mode = 1;
                    } else {
                        peer->cold.caps.compact_mode = 0;
                    }

                    /* Calculate effective max = min(ours, theirs) */
                    effective_max = ctx->local_max_message;
                    if (caps.max_message_size < effective_max) {
                        effective_max = caps.max_message_size;
                    }
                    peer->hot.effective_max_msg = effective_max;

                    PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK,
                        "Received capabilities from peer %u: max=%u chunk=%u pressure=%u compact=%u",
                        (unsigned)peer->hot.id,
                        (unsigned)caps.max_message_size,
                        (unsigned)caps.preferred_chunk,
                        (unsigned)caps.buffer_pressure,
                        (unsigned)peer->cold.caps.compact_mode);
                } else {
                    PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                        "Failed to decode capabilities from peer %u",
                        (unsigned)peer->hot.id);
                }
            }
            break;

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

/* ========================================================================== */
/* Capability Exchange                                                        */
/* ========================================================================== */

/**
 * Send capability message to peer.
 *
 * Called after connection is established to exchange capabilities.
 * Enables automatic fragmentation for constrained peers.
 *
 * DOD: Uses local buffer on stack - synchronous send completes before return.
 *
 * @param ctx   PeerTalk context
 * @param peer  Peer to send to
 * @return      0 on success, negative error code on failure
 */
int pt_mactcp_send_capability(struct pt_context *ctx, struct pt_peer *peer)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_capability_msg caps;
    pt_message_header hdr;
    uint8_t header_buf[PT_MESSAGE_HEADER_SIZE];
    uint8_t payload_buf[16];  /* TLV payload max ~12 bytes */
    uint8_t crc_buf[2];
    uint16_t crc;
    int payload_len;
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

    /* Fill in our capabilities */
    caps.max_message_size = ctx->local_max_message;
    caps.preferred_chunk = ctx->local_preferred_chunk;
    caps.capability_flags = ctx->local_capability_flags;

    /* Calculate current buffer pressure from BOTH queues - report the worse one.
     * On MacTCP, recv uses zero-copy so recv_queue is often empty.
     * The real bottleneck shows up in send_queue when echoing back.
     * By reporting MAX, we capture whichever is the actual constraint.
     */
    {
        uint8_t send_pressure = peer->send_queue ? pt_queue_pressure(peer->send_queue) : 0;
        uint8_t recv_pressure = peer->recv_queue ? pt_queue_pressure(peer->recv_queue) : 0;
        caps.buffer_pressure = (send_pressure > recv_pressure) ? send_pressure : recv_pressure;
    }

    /* Track what we reported for flow control threshold detection */
    peer->cold.caps.last_reported_pressure = caps.buffer_pressure;
    peer->cold.caps.pressure_update_pending = 0;

    /* Encode TLV payload */
    payload_len = pt_capability_encode(&caps, payload_buf, sizeof(payload_buf));
    if (payload_len < 0) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "Failed to encode capabilities");
        return PT_ERR_INTERNAL;
    }

    /* Build message header */
    hdr.version = PT_PROTOCOL_VERSION;
    hdr.type = PT_MSG_TYPE_CAPABILITY;
    hdr.flags = 0;
    hdr.sequence = peer->hot.send_seq++;
    hdr.payload_len = (uint16_t)payload_len;

    pt_message_encode_header(&hdr, header_buf);

    /* Calculate CRC over header + payload */
    crc = pt_crc16(header_buf, PT_MESSAGE_HEADER_SIZE);
    crc = pt_crc16_update(crc, payload_buf, (size_t)payload_len);
    crc_buf[0] = (crc >> 8) & 0xFF;
    crc_buf[1] = crc & 0xFF;

    /* Build WDS: header + payload + CRC */
    wds[0].length = PT_MESSAGE_HEADER_SIZE;
    wds[0].ptr = (Ptr)header_buf;
    wds[1].length = (unsigned short)payload_len;
    wds[1].ptr = (Ptr)payload_buf;
    wds[2].length = 2;
    wds[2].ptr = (Ptr)crc_buf;
    wds[3].length = 0;
    wds[3].ptr = NULL;

    /* Setup send call */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPSend;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;

    cold->pb.csParam.send.ulpTimeoutValue = 30;
    cold->pb.csParam.send.ulpTimeoutAction = 1;
    cold->pb.csParam.send.validityFlags = 0xC0;
    cold->pb.csParam.send.pushFlag = 1;
    cold->pb.csParam.send.urgentFlag = 0;
    cold->pb.csParam.send.wdsPtr = (Ptr)wds;

    /* Synchronous send */
    err = PBControlSync((ParmBlkPtr)&cold->pb);

    if (err != noErr) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "Failed to send capabilities: %d", (int)err);
        return PT_ERR_NETWORK;
    }

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK,
        "Sent capabilities to peer %u: max=%u chunk=%u",
        (unsigned)peer->hot.id,
        (unsigned)caps.max_message_size,
        (unsigned)caps.preferred_chunk);

    return 0;
}

#endif /* PT_PLATFORM_MACTCP */
