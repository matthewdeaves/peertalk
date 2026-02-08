/**
 * @file tcp_mactcp.c
 * @brief MacTCP TCP Stream Implementation
 *
 * TCP stream creation, release, and ASR callback handling.
 * Uses hot/cold struct split for 68k cache efficiency.
 *
 * References:
 * - MacTCP Programmer's Guide (1989), Chapter 3: "TCP"
 * - LaunchAPPL MacTCPStream.cc (Retro68) for cleanup patterns
 */

#include "mactcp_defs.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_MACTCP)

#include <Devices.h>
#include <OSUtils.h>  /* For TickCount() */

/* ========================================================================== */
/* External Accessors                                                         */
/* ========================================================================== */

extern short pt_mactcp_get_refnum(void);
extern TCPNotifyUPP pt_mactcp_get_tcp_upp(void);

/* From mactcp_driver.c */
extern Ptr pt_mactcp_alloc_buffer(unsigned long size);
extern void pt_mactcp_free_buffer(Ptr buffer);
extern unsigned long pt_mactcp_buffer_size_for_memory(struct pt_context *ctx);

/* ========================================================================== */
/* TCP ASR Callback                                                           */
/* ========================================================================== */

/**
 * TCP Asynchronous Notification Routine.
 *
 * CRITICAL: Called at INTERRUPT LEVEL.
 * From MacTCP Programmer's Guide:
 * - Cannot allocate or release memory
 * - Cannot make synchronous MacTCP calls
 * - CAN issue additional ASYNCHRONOUS MacTCP calls if needed
 * - Must preserve registers D3-D7, A3-A7 (A0-A2, D0-D2 may be modified)
 *
 * Event codes (from MacTCP.h):
 * - TCPClosing (1): Remote is closing (send pending data, then close)
 * - TCPULPTimeout (2): ULP timer expired (only if configured to report)
 * - TCPTerminate (3): Connection gone - terminReason tells why
 * - TCPDataArrival (4): Data waiting to be read
 * - TCPUrgent (5): Urgent data received
 * - TCPICMPReceived (6): ICMP message received
 *
 * Strategy: Set flags only, let main loop process.
 *
 * @param stream           TCP stream pointer
 * @param event_code       Event code (see above)
 * @param user_data        Pointer to pt_tcp_stream_hot struct
 * @param terminate_reason Termination reason (only for TCPTerminate)
 * @param icmp_msg         ICMP report (only for TCPICMPReceived)
 */
pascal void pt_tcp_asr(
    StreamPtr stream,
    unsigned short event_code,
    Ptr user_data,
    unsigned short terminate_reason,
    struct ICMPReport *icmp_msg)
{
    pt_tcp_stream_hot *hot = (pt_tcp_stream_hot *)user_data;

    (void)stream;   /* Unused - we get stream from hot struct */
    (void)icmp_msg; /* TODO: Could copy to pre-allocated buffer if needed */

    /* DOD: Use bitfield flags for single-byte atomic operations */
    switch (event_code) {
    case TCPDataArrival:
        hot->asr_flags |= PT_ASR_DATA_ARRIVED;
        hot->log_events |= PT_LOG_EVT_DATA_ARRIVED;
        break;

    case TCPClosing:
        hot->asr_flags |= PT_ASR_CONN_CLOSED;
        hot->log_events |= PT_LOG_EVT_CONN_CLOSED;
        /* Note: should try to send pending data before closing */
        break;

    case TCPTerminate:
        hot->asr_flags |= PT_ASR_CONN_CLOSED;
        hot->log_events |= PT_LOG_EVT_TERMINATED;
        /* Store terminate reason for main loop logging.
         * We can't access cold struct safely at interrupt time,
         * but we can store in a hot field for deferred logging. */
        hot->log_error_code = (int16_t)terminate_reason;
        break;

    case TCPULPTimeout:
        hot->asr_flags |= PT_ASR_TIMEOUT;
        break;

    case TCPUrgent:
        hot->asr_flags |= PT_ASR_URGENT_DATA;
        break;

    case TCPICMPReceived:
        hot->asr_flags |= PT_ASR_ICMP_RECEIVED;
        hot->log_events |= PT_LOG_EVT_ICMP;
        break;
    }

    /* CRITICAL: Do nothing else at interrupt time!
     * No memory allocation, no logging, no Toolbox calls.
     * Main loop will check flags and process. */
}

/* ========================================================================== */
/* TCP Stream Creation                                                        */
/* ========================================================================== */

/**
 * Create TCP stream for peer connection.
 *
 * From MacTCP Programmer's Guide: TCPCreate
 *
 * Buffer sizing:
 * - Minimum: 4096 bytes
 * - Character apps: 8192 bytes ("at least 8192 bytes is recommended")
 * - Block apps: 16384 bytes or more ("16 KB is recommended")
 * - Formula: 4*MTU + 1024 for good performance
 *
 * For Mac SE 4MB: Use memory-aware sizing to leave room for heap operations.
 * For Performa 6200 8MB+: Can use optimal formula for better throughput.
 *
 * DOD: Uses hot/cold struct split. Takes index to access parallel arrays.
 *
 * @param ctx  PeerTalk context
 * @param idx  Stream index (0 to PT_MAX_PEERS-1)
 * @return     0 on success, negative error code on failure
 */
int pt_mactcp_tcp_create(struct pt_context *ctx, int idx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot;
    pt_tcp_stream_cold *cold;
    OSErr err;
    unsigned long buf_size;
    unsigned long original_size;

    if (idx < 0 || idx >= PT_MAX_PEERS) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_CONNECT,
            "Invalid TCP stream index: %d", idx);
        return -1;
    }

    hot = &md->tcp_hot[idx];
    cold = &md->tcp_cold[idx];

    if (hot->state != PT_STREAM_UNUSED) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_CONNECT,
            "TCP stream %d already in use (state=%s)", idx, pt_state_name(hot->state));
        return -1;
    }

    /* Determine buffer size based on available memory
     * Uses conservative thresholds appropriate for Mac SE 4MB
     * See pt_mactcp_buffer_size_for_memory() for details
     */
    buf_size = pt_mactcp_buffer_size_for_memory(ctx);
    original_size = buf_size;

    /* Allocate receive buffer with fallback to smaller sizes
     * This handles low-memory situations gracefully
     */
    while (buf_size >= PT_TCP_RCV_BUF_MIN) {
        cold->rcv_buffer = pt_mactcp_alloc_buffer(buf_size);
        if (cold->rcv_buffer != NULL)
            break;
        /* Log allocation fallback */
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_MEMORY,
            "Buffer alloc failed (%lu), trying %lu", buf_size, buf_size / 2);
        buf_size /= 2;  /* Try smaller buffer */
    }

    if (cold->rcv_buffer != NULL && buf_size < original_size) {
        PT_LOG_INFO(ctx->log, PT_LOG_CAT_MEMORY,
            "TCP buffer allocated at reduced size: %lu (requested %lu)",
            buf_size, original_size);
    }

    if (cold->rcv_buffer == NULL) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_MEMORY,
            "Failed to allocate TCP receive buffer (tried down to %lu bytes)",
            (unsigned long)PT_TCP_RCV_BUF_MIN);
        return -1;
    }
    cold->rcv_buffer_size = buf_size;

    /* Clear ASR flags and log events */
    hot->asr_flags = 0;
    hot->log_events = 0;
    hot->log_error_code = 0;

    /* Setup parameter block (in cold struct) */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPCreate;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.ioResult = 1;

    cold->pb.csParam.create.rcvBuff = cold->rcv_buffer;
    cold->pb.csParam.create.rcvBuffLen = cold->rcv_buffer_size;

    /* CRITICAL: Use UPP created at init, NOT a cast function pointer.
     * MacTCP.h: "you must set up a NewRoutineDescriptor for every
     * non-nil completion routine and/or notifyProc parameter."
     * Pass hot struct as userDataPtr - ASR only touches hot data. */
    cold->pb.csParam.create.notifyProc = pt_mactcp_get_tcp_upp();
    cold->pb.csParam.create.userDataPtr = (Ptr)hot;

    hot->state = PT_STREAM_CREATING;

    /* Synchronous create */
    err = PBControlSync((ParmBlkPtr)&cold->pb);

    if (err != noErr) {
        /* Handle system-wide 64 stream limit gracefully */
        if (err == insufficientResources) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
                "System TCP stream limit (64) reached");
            pt_mactcp_free_buffer(cold->rcv_buffer);
            cold->rcv_buffer = NULL;
            hot->state = PT_STREAM_UNUSED;
            return PT_ERR_RESOURCE;
        }

        PT_LOG_ERR(ctx->log, PT_LOG_CAT_CONNECT,
            "TCPCreate failed: %d", (int)err);
        pt_mactcp_free_buffer(cold->rcv_buffer);
        cold->rcv_buffer = NULL;
        hot->state = PT_STREAM_UNUSED;
        return -1;
    }

    hot->stream = cold->pb.tcpStream;
    cold->local_ip = md->local_ip;
    hot->state = PT_STREAM_IDLE;
    hot->rds_outstanding = 0;
    hot->peer_idx = -1;  /* No peer yet */

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT,
        "TCP stream %d created: stream=0x%08lX bufsize=%lu",
        idx, (unsigned long)hot->stream, buf_size);

    return 0;
}

/**
 * Create TCP stream for listener.
 *
 * Listener uses separate hot/cold structs (md->listener_hot/cold).
 *
 * @param ctx  PeerTalk context
 * @return     0 on success, negative error code on failure
 */
int pt_mactcp_tcp_create_listener(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot = &md->listener_hot;
    pt_tcp_stream_cold *cold = &md->listener_cold;
    OSErr err;
    unsigned long buf_size;

    if (hot->state != PT_STREAM_UNUSED) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_CONNECT,
            "Listener stream already exists (state=%s)", pt_state_name(hot->state));
        return -1;
    }

    /* Use block-oriented buffer size for high throughput.
     * The listener's buffer is transferred to the connected stream
     * after TCPPassiveOpen completes. 16KB enables 4KB threshold
     * for the 25% completion rule. */
    buf_size = PT_TCP_RCV_BUF_BLOCK;

    cold->rcv_buffer = pt_mactcp_alloc_buffer(buf_size);
    if (cold->rcv_buffer == NULL) {
        /* Try minimum size */
        buf_size = PT_TCP_RCV_BUF_MIN;
        cold->rcv_buffer = pt_mactcp_alloc_buffer(buf_size);
        if (cold->rcv_buffer == NULL) {
            PT_LOG_ERR(ctx->log, PT_LOG_CAT_MEMORY,
                "Failed to allocate listener buffer");
            return -1;
        }
    }
    cold->rcv_buffer_size = buf_size;

    /* Clear ASR flags and log events */
    hot->asr_flags = 0;
    hot->log_events = 0;
    hot->log_error_code = 0;

    /* Setup parameter block */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPCreate;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.ioResult = 1;

    cold->pb.csParam.create.rcvBuff = cold->rcv_buffer;
    cold->pb.csParam.create.rcvBuffLen = cold->rcv_buffer_size;
    cold->pb.csParam.create.notifyProc = pt_mactcp_get_tcp_upp();
    cold->pb.csParam.create.userDataPtr = (Ptr)hot;

    hot->state = PT_STREAM_CREATING;

    err = PBControlSync((ParmBlkPtr)&cold->pb);

    if (err != noErr) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_CONNECT,
            "TCPCreate (listener) failed: %d", (int)err);
        pt_mactcp_free_buffer(cold->rcv_buffer);
        cold->rcv_buffer = NULL;
        hot->state = PT_STREAM_UNUSED;
        return -1;
    }

    hot->stream = cold->pb.tcpStream;
    cold->local_ip = md->local_ip;
    hot->state = PT_STREAM_IDLE;
    hot->rds_outstanding = 0;
    hot->peer_idx = -1;

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT,
        "Listener stream created: stream=0x%08lX bufsize=%lu",
        (unsigned long)hot->stream, buf_size);

    return 0;
}

/* ========================================================================== */
/* TCP Stream Release                                                         */
/* ========================================================================== */

/**
 * Release TCP stream.
 *
 * From MacTCP Programmer's Guide: TCPRelease
 *
 * DOD: Uses hot/cold struct split. Takes index to access parallel arrays.
 *
 * @param ctx  PeerTalk context
 * @param idx  Stream index (0 to PT_MAX_PEERS-1)
 * @return     0 on success
 */
int pt_mactcp_tcp_release(struct pt_context *ctx, int idx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot;
    pt_tcp_stream_cold *cold;
    OSErr err;

    if (idx < 0 || idx >= PT_MAX_PEERS) {
        return -1;
    }

    hot = &md->tcp_hot[idx];
    cold = &md->tcp_cold[idx];

    if (hot->state == PT_STREAM_UNUSED)
        return 0;  /* Already released */

    /* Return any outstanding RDS buffers first */
    if (hot->rds_outstanding > 0) {
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

    /* Issue release */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPRelease;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;

    hot->state = PT_STREAM_RELEASING;

    err = PBControlSync((ParmBlkPtr)&cold->pb);

    if (err != noErr && err != connectionDoesntExist) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
            "TCPRelease returned: %d", (int)err);
    }

    /* Free buffer */
    if (cold->rcv_buffer != NULL) {
        pt_mactcp_free_buffer(cold->rcv_buffer);
        cold->rcv_buffer = NULL;
    }

    hot->stream = 0;  /* StreamPtr is unsigned long */
    hot->peer_idx = -1;
    hot->state = PT_STREAM_UNUSED;

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT, "TCP stream %d released", idx);

    return 0;
}

/**
 * Release listener TCP stream.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success
 */
int pt_mactcp_tcp_release_listener(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    pt_tcp_stream_hot *hot = &md->listener_hot;
    pt_tcp_stream_cold *cold = &md->listener_cold;
    OSErr err;

    if (hot->state == PT_STREAM_UNUSED)
        return 0;  /* Already released */

    /* Return any outstanding RDS buffers first */
    if (hot->rds_outstanding > 0) {
        TCPiopb return_pb;
        pt_memset(&return_pb, 0, sizeof(return_pb));
        return_pb.csCode = TCPRcvBfrReturn;
        return_pb.ioCRefNum = md->driver_refnum;
        return_pb.tcpStream = hot->stream;
        return_pb.csParam.receive.rdsPtr = (Ptr)cold->rds;

        err = PBControlSync((ParmBlkPtr)&return_pb);
        if (err != noErr) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_NETWORK,
                "TCPRcvBfrReturn (listener) failed: %d", (int)err);
        }
        hot->rds_outstanding = 0;
    }

    /* Issue release */
    pt_memset(&cold->pb, 0, sizeof(cold->pb));
    cold->pb.csCode = TCPRelease;
    cold->pb.ioCRefNum = md->driver_refnum;
    cold->pb.tcpStream = hot->stream;

    hot->state = PT_STREAM_RELEASING;

    err = PBControlSync((ParmBlkPtr)&cold->pb);

    if (err != noErr && err != connectionDoesntExist) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
            "TCPRelease (listener) returned: %d", (int)err);
    }

    /* Free buffer */
    if (cold->rcv_buffer != NULL) {
        pt_mactcp_free_buffer(cold->rcv_buffer);
        cold->rcv_buffer = NULL;
    }

    hot->stream = 0;
    hot->peer_idx = -1;
    hot->state = PT_STREAM_UNUSED;

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT, "Listener stream released");

    return 0;
}

/* ========================================================================== */
/* Release All Streams (Shutdown)                                             */
/* ========================================================================== */

/**
 * Release all TCP streams.
 *
 * AMENDMENT (2026-02-03): LaunchAPPL Cleanup Pattern
 *
 * Verified from LaunchAPPL MacTCPStream.cc:61-79 - proper cleanup sequence:
 * 1. TCPAbort all streams (cancels pending operations)
 * 2. Spin-wait: while(ioResult > 0) {} for ALL parameter blocks
 * 3. TCPRelease each stream
 *
 * CRITICAL: Step 2 ensures async operations complete before releasing streams.
 * Without this, TCPRelease may fail or corrupt MacTCP driver state.
 *
 * @param ctx  PeerTalk context
 */
void pt_mactcp_tcp_release_all(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    TCPiopb abort_pb;
    int i;
    int any_pending;
    unsigned long timeout_start;

    /* Step 1: TCPAbort all active peer streams
     * This cancels any pending async operations
     */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        pt_tcp_stream_hot *hot = &md->tcp_hot[i];
        if (hot->state != PT_STREAM_UNUSED && hot->stream != 0) {
            pt_memset(&abort_pb, 0, sizeof(abort_pb));
            abort_pb.csCode = TCPAbort;
            abort_pb.ioCRefNum = md->driver_refnum;
            abort_pb.tcpStream = hot->stream;
            PBControlSync((ParmBlkPtr)&abort_pb);
            /* Ignore error - stream may already be dead */
        }
    }

    /* Abort listener if active */
    if (md->listener_hot.state != PT_STREAM_UNUSED && md->listener_hot.stream != 0) {
        pt_memset(&abort_pb, 0, sizeof(abort_pb));
        abort_pb.csCode = TCPAbort;
        abort_pb.ioCRefNum = md->driver_refnum;
        abort_pb.tcpStream = md->listener_hot.stream;
        PBControlSync((ParmBlkPtr)&abort_pb);
    }

    /* Step 2: Spin-wait for all pending async operations to complete
     * LaunchAPPL Pattern: while(readPB.ioResult > 0 || writePB.ioResult > 0) {}
     *
     * ioResult values (from MacTCP.h):
     *   > 0 : Operation in progress
     *   = 0 : Completed successfully
     *   < 0 : Completed with error
     *
     * Add timeout to prevent infinite loop if driver is wedged.
     */
    timeout_start = (unsigned long)TickCount();
    do {
        any_pending = 0;
        for (i = 0; i < PT_MAX_PEERS; i++) {
            pt_tcp_stream_hot *hot = &md->tcp_hot[i];
            pt_tcp_stream_cold *cold = &md->tcp_cold[i];
            if (hot->state != PT_STREAM_UNUSED) {
                if (cold->pb.ioResult > 0) {
                    any_pending = 1;
                }
            }
        }
        /* Check listener too */
        if (md->listener_hot.state != PT_STREAM_UNUSED) {
            if (md->listener_cold.pb.ioResult > 0) {
                any_pending = 1;
            }
        }
        /* Timeout after 5 seconds (300 ticks) */
        if (((unsigned long)TickCount() - timeout_start) > 300) {
            PT_LOG_WARN(ctx->log, PT_LOG_CAT_CONNECT,
                "Timeout waiting for async operations to complete");
            break;
        }
    } while (any_pending);

    /* Step 3: Now safe to release - all async ops guaranteed complete */
    pt_mactcp_tcp_release_listener(ctx);
    for (i = 0; i < PT_MAX_PEERS; i++) {
        pt_mactcp_tcp_release(ctx, i);
    }

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_CONNECT, "All TCP streams released");
}

#endif /* PT_PLATFORM_MACTCP */
