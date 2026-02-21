/**
 * @file udp_mactcp.c
 * @brief MacTCP UDP Stream Implementation
 *
 * UDP stream creation, release, send, and receive for discovery.
 * Uses hot/cold struct split for 68k cache efficiency.
 *
 * References:
 * - MacTCP Programmer's Guide (1989), Chapter 4: "UDP"
 */

#include "mactcp_defs.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_MACTCP)

#include <Devices.h>

/* ========================================================================== */
/* External Accessors                                                         */
/* ========================================================================== */

extern short pt_mactcp_get_refnum(void);
extern UDPNotifyUPP pt_mactcp_get_udp_upp(void);

/* From mactcp_driver.c */
extern Ptr pt_mactcp_alloc_buffer(unsigned long size);
extern void pt_mactcp_free_buffer(Ptr buffer);

/* ========================================================================== */
/* UDP ASR Callback                                                           */
/* ========================================================================== */

/**
 * UDP Asynchronous Notification Routine.
 *
 * CRITICAL: Called at INTERRUPT LEVEL.
 * From MacTCP Programmer's Guide:
 * - Cannot allocate or release memory
 * - Cannot make synchronous MacTCP calls
 * - CAN issue additional ASYNCHRONOUS MacTCP calls if needed
 * - Must preserve registers D3-D7, A3-A7 (A0-A2, D0-D2 may be modified)
 *
 * Strategy: Set flags only, let main loop process.
 *
 * @param stream      UDP stream pointer
 * @param event_code  UDPDataArrival or UDPICMPReceived
 * @param user_data   Pointer to pt_udp_stream_hot struct
 * @param icmp_msg    ICMP report (only valid for UDPICMPReceived)
 */
pascal void pt_udp_asr(
    StreamPtr stream,
    unsigned short event_code,
    Ptr user_data,
    struct ICMPReport *icmp_msg)
{
    pt_udp_stream_hot *hot = (pt_udp_stream_hot *)user_data;

    (void)stream;   /* Unused - we get stream from hot struct */
    (void)icmp_msg; /* TODO: Could log ICMP info if needed */

    /* DOD: Use bitfield flags for single-byte atomic operations */
    switch (event_code) {
    case UDPDataArrival:
        hot->asr_flags |= PT_ASR_DATA_ARRIVED;
        break;

    case UDPICMPReceived:
        hot->asr_flags |= PT_ASR_ICMP_RECEIVED;
        break;
    }

    /* DO NOT do any other work here!
     * No memory allocation, no logging, no Toolbox calls.
     * Main loop will check flags and process. */
}

/* ========================================================================== */
/* UDP Stream Creation                                                        */
/* ========================================================================== */

/**
 * Create UDP stream for discovery.
 *
 * From MacTCP Programmer's Guide: UDPCreate
 *
 * UDP buffer sizing (per MPG): "minimum allowed size is 2048 bytes,
 * but it should be at least 2N + 256 bytes where N is the size in
 * bytes of the largest UDP datagram you expect to receive"
 *
 * For discovery: packets are ~100 bytes, so 1408 bytes (2×576+256) is plenty.
 * This saves 2.6KB vs the old 4096 size - matters on Mac SE 4MB.
 *
 * DOD: Uses hot/cold struct split for cache efficiency.
 *
 * @param ctx         PeerTalk context
 * @param local_port  Port to bind (0 for system-assigned)
 * @return            0 on success, -1 on error
 */
int pt_mactcp_udp_create(struct pt_context *ctx, udp_port local_port)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_udp_stream_hot *hot = &md->discovery_hot;
    pt_udp_stream_cold *cold = &md->discovery_cold;
    OSErr err;

    if (hot->state != PT_STREAM_UNUSED) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_NETWORK,
            "UDP stream already exists");
        return -1;
    }

    /* Allocate receive buffer
     * PT_UDP_RCV_BUF_SIZE = 1408 = 2×576 + 256 (safe for max datagram)
     * Note: Must use minimum 2048 per MacTCP docs */
    cold->rcv_buffer = pt_mactcp_alloc_buffer(PT_UDP_RCV_BUF_MIN);
    if (cold->rcv_buffer == NULL) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_MEMORY,
            "Failed to allocate UDP receive buffer");
        return -1;
    }
    cold->rcv_buffer_size = PT_UDP_RCV_BUF_MIN;

    /* Clear ASR flags (single byte bitfield) */
    hot->asr_flags = 0;

    /* Setup parameter block (in cold struct) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = UDPCreate;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.ioResult = 1;

    cold->pb.csParam.create.rcvBuff = cold->rcv_buffer;
    cold->pb.csParam.create.rcvBuffLen = cold->rcv_buffer_size;

    /* CRITICAL: Use UPP created at init, NOT a cast function pointer.
     * MacTCP.h: "you must set up a NewRoutineDescriptor for every
     * non-nil completion routine and/or notifyProc parameter."
     * Pass hot struct as userDataPtr - ASR only touches hot data. */
    cold->pb.csParam.create.notifyProc = pt_mactcp_get_udp_upp();
    cold->pb.csParam.create.localPort = local_port;
    cold->pb.csParam.create.userDataPtr = (Ptr)hot;

    hot->state = PT_STREAM_CREATING;

    /* Synchronous call - safe from main loop */
    err = PBControlSync((ParmBlkPtr)&cold->pb);

    if (err != noErr) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_NETWORK,
            "UDPCreate failed: %d", (int)err);
        pt_mactcp_free_buffer(cold->rcv_buffer);
        cold->rcv_buffer = NULL;
        hot->state = PT_STREAM_UNUSED;
        return -1;
    }

    hot->stream = cold->pb.udpStream;
    cold->local_port = local_port;
    cold->local_ip = md->local_ip;
    hot->state = PT_STREAM_IDLE;

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK,
        "UDP stream created: stream=0x%08lX port=%u",
        (unsigned long)hot->stream, (unsigned)local_port);

    return 0;
}

/* ========================================================================== */
/* UDP Stream Release                                                         */
/* ========================================================================== */

/**
 * Release UDP stream.
 *
 * From MacTCP Programmer's Guide: UDPRelease
 *
 * DOD: Uses hot/cold struct split.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success (always succeeds)
 */
int pt_mactcp_udp_release(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_udp_stream_hot *hot = &md->discovery_hot;
    pt_udp_stream_cold *cold = &md->discovery_cold;
    OSErr err;

    if (hot->state == PT_STREAM_UNUSED)
        return 0;  /* Already released */

    /* Setup release call (in cold struct) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = UDPRelease;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.udpStream = hot->stream;

    hot->state = PT_STREAM_RELEASING;

    err = PBControlSync((ParmBlkPtr)&cold->pb);

    if (err != noErr && err != connectionDoesntExist) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "UDPRelease returned: %d", (int)err);
    }

    /* Free buffer - ownership returned to us */
    if (cold->rcv_buffer != NULL) {
        pt_mactcp_free_buffer(cold->rcv_buffer);
        cold->rcv_buffer = NULL;
    }

    hot->stream = 0;  /* StreamPtr is unsigned long, not pointer */
    hot->state = PT_STREAM_UNUSED;

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_NETWORK, "UDP stream released");

    return 0;
}

/* ========================================================================== */
/* UDP Send                                                                   */
/* ========================================================================== */

/**
 * Send UDP datagram.
 *
 * From MacTCP Programmer's Guide: UDPWrite
 *
 * DOD: Uses hot/cold struct split. Send uses cold for pb.
 *
 * @param ctx        PeerTalk context
 * @param dest_ip    Destination IP address
 * @param dest_port  Destination port
 * @param data       Data to send
 * @param len        Length of data
 * @return           0 on success, -1 on error
 */
int pt_mactcp_udp_send(struct pt_context *ctx,
                       ip_addr dest_ip, udp_port dest_port,
                       const void *data, unsigned short len)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_udp_stream_hot *hot = &md->discovery_hot;
    pt_udp_stream_cold *cold = &md->discovery_cold;
    wdsEntry wds[2];
    OSErr err;

    if (hot->state != PT_STREAM_IDLE) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "UDP stream not idle: state=%s", pt_state_name(hot->state));
        return -1;
    }

    /* Build WDS (Write Data Structure) */
    wds[0].length = len;
    wds[0].ptr = (Ptr)data;
    wds[1].length = 0;  /* Terminator */
    wds[1].ptr = NULL;

    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = UDPWrite;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.udpStream = hot->stream;

    cold->pb.csParam.send.remoteHost = dest_ip;
    cold->pb.csParam.send.remotePort = dest_port;
    cold->pb.csParam.send.wdsPtr = (Ptr)wds;
    cold->pb.csParam.send.checkSum = 1;  /* Calculate checksum */

    /* Synchronous send */
    err = PBControlSync((ParmBlkPtr)&cold->pb);

    if (err != noErr) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "UDPWrite failed: %d", (int)err);
        return -1;
    }

    return 0;
}

/* ========================================================================== */
/* UDP Receive                                                                */
/* ========================================================================== */

/**
 * Receive UDP datagram (non-blocking).
 *
 * From MacTCP Programmer's Guide: "The minimum allowed value for the
 * command timeout is 2 seconds. A zero command timeout means infinite."
 *
 * Strategy: Since we ONLY call this after ASR signals data arrival,
 * we know data is already buffered. UDPRead with minimum timeout (2s)
 * will return immediately with buffered data. If ASR fired but no data
 * (race condition), we'll get commandTimeout after 2s - acceptable since
 * this should be rare.
 *
 * DOD: Uses hot/cold struct split. Checks hot flags, uses cold for pb.
 *
 * @param ctx        PeerTalk context
 * @param from_ip    [out] Source IP address
 * @param from_port  [out] Source port
 * @param data       [out] Buffer to receive data
 * @param len        [in/out] Buffer size in, bytes received out
 * @return           1 if data received, 0 if no data, -1 on error
 */
int pt_mactcp_udp_recv(struct pt_context *ctx,
                       ip_addr *from_ip, udp_port *from_port,
                       void *data, unsigned short *len)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_udp_stream_hot *hot = &md->discovery_hot;
    pt_udp_stream_cold *cold = &md->discovery_cold;
    UDPiopb return_pb;
    unsigned short data_len;
    OSErr err;

    if (hot->state != PT_STREAM_IDLE)
        return 0;

    /* Quick check: ASR flag indicates data is already buffered */
    if (!(hot->asr_flags & PT_ASR_DATA_ARRIVED))
        return 0;

    /* Clear flag (atomic on 68k) */
    hot->asr_flags &= ~PT_ASR_DATA_ARRIVED;

    /* Issue UDPRead - data should be immediately available since ASR fired */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = UDPRead;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.udpStream = hot->stream;

    /* Per docs: minimum timeout is 2 seconds, 0 = infinite.
     * We use 2 since data should already be buffered (ASR fired).
     * This is effectively non-blocking when data exists. */
    cold->pb.csParam.receive.timeOut = 2;

    /* Synchronous call - data is already buffered so returns immediately */
    err = PBControlSync((ParmBlkPtr)&cold->pb);

    if (err == commandTimeout) {
        /* No data despite flag - rare race condition, try again later */
        return 0;
    }

    if (err != noErr) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
            "UDPRead failed: %d", (int)err);
        return -1;
    }

    /* Extract data */
    *from_ip = cold->pb.csParam.receive.remoteHost;
    *from_port = cold->pb.csParam.receive.remotePort;

    data_len = cold->pb.csParam.receive.rcvBuffLen;
    if (data_len > *len)
        data_len = *len;

    pt_memcpy(data, cold->pb.csParam.receive.rcvBuff, data_len);
    *len = data_len;

    /* CRITICAL: Return buffer to MacTCP for reuse */
    if (data_len > 0) {
        pt_memset(&return_pb, 0, sizeof(return_pb));
        return_pb.csCode = UDPBfrReturn;
        return_pb.ioCRefNum = md->driver_refnum;
        return_pb.udpStream = hot->stream;
        return_pb.csParam.receive.rcvBuff = cold->pb.csParam.receive.rcvBuff;

        err = PBControlSync((ParmBlkPtr)&return_pb);
        if (err != noErr) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                "UDPBfrReturn failed: %d (buffer leak possible)", (int)err);
        }
    }

    PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_NETWORK,
        "UDP recv %u bytes from %lu.%lu.%lu.%lu:%u",
        (unsigned)data_len,
        (*from_ip >> 24) & 0xFF,
        (*from_ip >> 16) & 0xFF,
        (*from_ip >> 8) & 0xFF,
        *from_ip & 0xFF,
        (unsigned)*from_port);

    return 1;  /* Got data */
}

#endif /* PT_PLATFORM_MACTCP */
