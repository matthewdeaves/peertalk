/*
 * pt_mactcp.c -- MacTCP platform backend
 *
 * Async parameter blocks + polling. ioCompletion=NULL on all PBs.
 * ASR sets volatile flags only; processing happens in poll.
 *
 * Targets 68k Macs with System 6.0.8 - 7.5.5.
 */

#include "pt_internal.h"

#ifdef PT_PLATFORM_MACTCP

#include <MacTCP.h>
#include <Devices.h>
#include <Memory.h>

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define MACTCP_DRIVER_NAME  "\p.IPP"
#define TCP_BUF_SIZE        8192
#define UDP_BUF_SIZE        2048
#define MAX_TCP_STREAMS     32

/* Stream states */
#define STREAM_FREE         0
#define STREAM_LISTENING    1
#define STREAM_CONNECTING   2
#define STREAM_CONNECTED    3

/* ASR flag bits */
#define FLAG_DATA_AVAIL     0x01
#define FLAG_REMOTE_CLOSE   0x02
#define FLAG_TERMINATED     0x04

/* UDP_FLAG_DATA removed (T127): poll checks read_pending/ioResult,
   not flags. UDPDataArrival rarely fires when UDPRead outstanding. */

/* ------------------------------------------------------------------ */
/* Per-stream state                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    StreamPtr   stream;
    TCPiopb     send_pb;
    TCPiopb     open_pb;
    Ptr         buffer;
    wdsEntry    wds[3];
    volatile unsigned char flags;
    int         state;
    int         send_pending;
    struct PT_Peer_Internal *owner; /* back-pointer to owning peer, NULL if free */
} TCPStreamSlot;

typedef struct {
    StreamPtr   stream;
    UDPiopb     read_pb;
    Ptr         buffer;
    int         read_pending;
} UDPStreamSlot;

/* ------------------------------------------------------------------ */
/* Platform state                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    short         driver_ref;
    ip_addr       local_ip;

    TCPStreamSlot tcp_streams[MAX_TCP_STREAMS];
    int           listen_active;

    UDPStreamSlot discovery_udp;
    UDPStreamSlot message_udp;

    TCPNotifyUPP  tcp_upp;
    UDPNotifyUPP  udp_upp;
    int           listener_count; /* track active listeners to avoid re-scan */

    /* Reusable param blocks for hot-path operations (avoid per-call memset) */
    TCPiopb       recv_pb;     /* TCPRcv in poll loop */
    UDPiopb       bfr_ret_pb;  /* UDPBfrReturn in poll loop */
} MacTCPState;

static MacTCPState g_mactcp;

/* ------------------------------------------------------------------ */
/* ASR callbacks (interrupt time -- set flags only)                     */
/* ------------------------------------------------------------------ */

static pascal void tcp_asr(StreamPtr stream, unsigned short event,
                           Ptr userDataPtr, unsigned short terminReason,
                           ICMPReport *icmpMsg)
{
    TCPStreamSlot *ts = (TCPStreamSlot *)userDataPtr;
    (void)stream; (void)terminReason; (void)icmpMsg;

    switch (event) {
    case TCPDataArrival:
        ts->flags |= FLAG_DATA_AVAIL;
        break;
    case TCPClosing:
        ts->flags |= FLAG_REMOTE_CLOSE;
        break;
    case TCPTerminate:
        ts->flags |= FLAG_TERMINATED;
        break;
    }
}

static pascal void udp_asr(StreamPtr stream, unsigned short event,
                           Ptr userDataPtr, ICMPReport *icmpMsg)
{
    /* T127: udp_asr is a no-op. Poll checks read_pending/ioResult
       directly. UDPDataArrival rarely fires when UDPRead outstanding. */
    (void)stream; (void)event; (void)userDataPtr; (void)icmpMsg;
}

/* ------------------------------------------------------------------ */
/* Interrupt control                                                   */
/* ------------------------------------------------------------------ */

#ifdef __m68k__
/* 68k: disable interrupts via SR manipulation (supervisor mode).
   MacTCP ASRs fire at hardware interrupt level and can preempt the
   main loop between flag read and clear. */
static short pt_disable_interrupts(void)
{
    short old_sr;
    __asm__ __volatile__(
        "move.w %%sr, %0\n\t"
        "ori.w #0x0700, %%sr"
        : "=d"(old_sr) : : "cc"
    );
    return old_sr;
}

static void pt_restore_interrupts(short old_sr)
{
    __asm__ __volatile__(
        "move.w %0, %%sr"
        : : "d"(old_sr) : "cc"
    );
}
#else
/* PPC: single-byte read and write are atomic on PPC. MacTCP on PPC
   uses the same ASR model but completion routines run at deferred
   task time, not hardware interrupt time. The snapshot-and-clear
   is effectively safe without SR manipulation. */
static short pt_disable_interrupts(void) { return 0; }
static void pt_restore_interrupts(short old_sr) { (void)old_sr; }
#endif

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static OSErr open_mactcp_driver(short *refNum)
{
    ParamBlockRec pb;
    OSErr err;

    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioNamePtr = MACTCP_DRIVER_NAME;
    pb.ioParam.ioPermssn = fsCurPerm;

    err = PBOpenSync(&pb);
    if (err != noErr) return err;

    *refNum = pb.ioParam.ioRefNum;
    return noErr;
}

static ip_addr get_my_ip(short driverRef)
{
    GetAddrParamBlock pb;
    OSErr err;

    memset(&pb, 0, sizeof(pb));
    pb.ioCRefNum = driverRef;
    pb.csCode = ipctlGetAddr;
    pb.ioCompletion = NULL;

    err = PBControlSync((ParmBlkPtr)&pb);
    if (err != noErr) return 0;

    return pb.ourAddress;
}

static StreamPtr create_tcp_stream(short driverRef, Ptr buffer,
                                   unsigned long bufLen,
                                   TCPNotifyUPP notifyProc,
                                   Ptr userData)
{
    TCPiopb pb;
    OSErr err;

    memset(&pb, 0, sizeof(pb));
    pb.ioCRefNum = driverRef;
    pb.csCode = TCPCreate;
    pb.csParam.create.rcvBuff = buffer;
    pb.csParam.create.rcvBuffLen = bufLen;
    pb.csParam.create.notifyProc = notifyProc;
    pb.csParam.create.userDataPtr = userData;

    err = PBControlSync((ParmBlkPtr)&pb);
    if (err != noErr) return 0;

    return pb.tcpStream;
}

static StreamPtr create_udp_stream(short driverRef, Ptr buffer,
                                   unsigned long bufLen,
                                   UDPNotifyUPP notifyProc,
                                   Ptr userData,
                                   unsigned short localPort)
{
    UDPiopb pb;
    OSErr err;

    memset(&pb, 0, sizeof(pb));
    pb.ioCRefNum = driverRef;
    pb.csCode = UDPCreate;
    pb.csParam.create.rcvBuff = buffer;
    pb.csParam.create.rcvBuffLen = bufLen;
    pb.csParam.create.notifyProc = notifyProc;
    pb.csParam.create.localPort = localPort;
    pb.csParam.create.userDataPtr = userData;

    err = PBControlSync((ParmBlkPtr)&pb);
    if (err != noErr) return 0;

    return pb.udpStream;
}

static int find_free_stream(void)
{
    int i;
    for (i = 0; i < MAX_TCP_STREAMS; i++) {
        if (g_mactcp.tcp_streams[i].stream &&
            g_mactcp.tcp_streams[i].state == STREAM_FREE) {
            return i;
        }
    }
    return -1;
}

static void abort_stream(int idx)
{
    TCPiopb pb;
    TCPStreamSlot *ts = &g_mactcp.tcp_streams[idx];

    if (ts->state == STREAM_LISTENING) {
        g_mactcp.listener_count--;
    }

    memset(&pb, 0, sizeof(pb));
    pb.ioCRefNum = g_mactcp.driver_ref;
    pb.tcpStream = ts->stream;
    pb.csCode = TCPAbort;
    PBControlSync((ParmBlkPtr)&pb);

    ts->flags = 0;
    ts->send_pending = 0;
    ts->state = STREAM_FREE;
    ts->owner = NULL;
}

static void issue_passive_open(int idx, short driverRef,
                               tcp_port localPort)
{
    TCPStreamSlot *ts = &g_mactcp.tcp_streams[idx];

    memset(&ts->open_pb, 0, sizeof(ts->open_pb));
    ts->open_pb.ioCRefNum = driverRef;
    ts->open_pb.tcpStream = ts->stream;
    ts->open_pb.csCode = TCPPassiveOpen;
    ts->open_pb.csParam.open.localPort = localPort;
    ts->open_pb.csParam.open.remoteHost = 0;
    ts->open_pb.csParam.open.remotePort = 0;
    ts->open_pb.csParam.open.commandTimeoutValue = 0;
    ts->open_pb.ioCompletion = NULL;

    PBControlAsync((ParmBlkPtr)&ts->open_pb);
    ts->state = STREAM_LISTENING;
    g_mactcp.listener_count++;
}

static void re_listen(void)
{
    int idx;
    if (!g_mactcp.listen_active) return;

    idx = find_free_stream();
    if (idx >= 0) {
        issue_passive_open(idx, g_mactcp.driver_ref, PT_TCP_PORT);
    }
}

static void issue_udp_read(UDPStreamSlot *us, short driverRef)
{
    memset(&us->read_pb, 0, sizeof(us->read_pb));
    us->read_pb.ioCRefNum = driverRef;
    us->read_pb.udpStream = us->stream;
    us->read_pb.csCode = UDPRead;
    us->read_pb.csParam.receive.timeOut = 0;  /* infinite -- UDPRelease cancels */
    us->read_pb.ioCompletion = NULL;

    PBControlAsync((ParmBlkPtr)&us->read_pb);
    us->read_pending = 1;
}

/* ------------------------------------------------------------------ */
/* PT_PlatformOps implementation                                       */
/* ------------------------------------------------------------------ */

static PT_Status mactcp_init(PT_Context_Internal *ctx)
{
    int i;
    int count;
    OSErr err;

    memset(&g_mactcp, 0, sizeof(g_mactcp));

    /* Open MacTCP driver */
    err = open_mactcp_driver(&g_mactcp.driver_ref);
    if (err != noErr) {
        CLOG_ERR("Failed to open MacTCP driver: %d", (int)err);
        return PT_ERR_INIT;
    }

    /* Get local IP */
    g_mactcp.local_ip = get_my_ip(g_mactcp.driver_ref);
    if (g_mactcp.local_ip == 0) {
        CLOG_ERR("Failed to get local IP");
        return PT_ERR_INIT;
    }
    ctx->local_ip = g_mactcp.local_ip;

    /* Create UPPs */
    g_mactcp.tcp_upp = NewTCPNotifyUPP(tcp_asr);
    g_mactcp.udp_upp = NewUDPNotifyUPP(udp_asr);
    if (!g_mactcp.tcp_upp || !g_mactcp.udp_upp) {
        CLOG_ERR("Failed to create UPPs (OOM)");
        goto fail_upps;
    }

    /* Create TCP stream pool (one per peer slot) */
    count = ctx->max_peers;
    if (count > MAX_TCP_STREAMS) count = MAX_TCP_STREAMS;
    for (i = 0; i < count; i++) {
        g_mactcp.tcp_streams[i].buffer = NewPtrClear(TCP_BUF_SIZE);
        if (!g_mactcp.tcp_streams[i].buffer) break;
        g_mactcp.tcp_streams[i].stream = create_tcp_stream(
            g_mactcp.driver_ref,
            g_mactcp.tcp_streams[i].buffer,
            TCP_BUF_SIZE, g_mactcp.tcp_upp,
            (Ptr)&g_mactcp.tcp_streams[i]);
        if (!g_mactcp.tcp_streams[i].stream) break;
        g_mactcp.tcp_streams[i].state = STREAM_FREE;
        g_mactcp.tcp_streams[i].owner = NULL;
    }

    /* Create UDP discovery stream (port 7353) */
    g_mactcp.discovery_udp.buffer = NewPtrClear(UDP_BUF_SIZE);
    if (!g_mactcp.discovery_udp.buffer) goto fail_tcp;
    g_mactcp.discovery_udp.stream = create_udp_stream(
        g_mactcp.driver_ref, g_mactcp.discovery_udp.buffer,
        UDP_BUF_SIZE, g_mactcp.udp_upp,
        (Ptr)&g_mactcp.discovery_udp,
        PT_DISCOVERY_PORT);
    if (!g_mactcp.discovery_udp.stream) goto fail_disc_buf;

    /* Create UDP message stream (port 7355) */
    g_mactcp.message_udp.buffer = NewPtrClear(UDP_BUF_SIZE);
    if (!g_mactcp.message_udp.buffer) goto fail_disc_udp;
    g_mactcp.message_udp.stream = create_udp_stream(
        g_mactcp.driver_ref, g_mactcp.message_udp.buffer,
        UDP_BUF_SIZE, g_mactcp.udp_upp,
        (Ptr)&g_mactcp.message_udp,
        PT_UDP_MSG_PORT);
    if (!g_mactcp.message_udp.stream) goto fail_msg_buf;

    ctx->platform_state = &g_mactcp;

    CLOG_INFO("MacTCP initialized (IP: %lu.%lu.%lu.%lu)",
              (g_mactcp.local_ip >> 24) & 0xFF,
              (g_mactcp.local_ip >> 16) & 0xFF,
              (g_mactcp.local_ip >> 8) & 0xFF,
              g_mactcp.local_ip & 0xFF);

    /* Pre-init reusable param blocks for hot-path poll operations.
       Only stream, buffer, and length change per call. */
    g_mactcp.recv_pb.ioCRefNum = g_mactcp.driver_ref;
    g_mactcp.recv_pb.csCode = TCPRcv;
    g_mactcp.recv_pb.ioCompletion = NULL;
    g_mactcp.bfr_ret_pb.ioCRefNum = g_mactcp.driver_ref;
    g_mactcp.bfr_ret_pb.csCode = UDPBfrReturn;

    return PT_OK;

fail_msg_buf:
    DisposePtr(g_mactcp.message_udp.buffer);
    g_mactcp.message_udp.buffer = NULL;
fail_disc_udp:
    {
        UDPiopb upb;
        memset(&upb, 0, sizeof(upb));
        upb.ioCRefNum = g_mactcp.driver_ref;
        upb.udpStream = g_mactcp.discovery_udp.stream;
        upb.csCode = UDPRelease;
        PBControlSync((ParmBlkPtr)&upb);
        g_mactcp.discovery_udp.stream = 0;
    }
fail_disc_buf:
    DisposePtr(g_mactcp.discovery_udp.buffer);
    g_mactcp.discovery_udp.buffer = NULL;
fail_tcp:
    {
        int j;
        TCPiopb pb;
        for (j = 0; j < MAX_TCP_STREAMS; j++) {
            if (g_mactcp.tcp_streams[j].stream) {
                memset(&pb, 0, sizeof(pb));
                pb.ioCRefNum = g_mactcp.driver_ref;
                pb.tcpStream = g_mactcp.tcp_streams[j].stream;
                pb.csCode = TCPRelease;
                PBControlSync((ParmBlkPtr)&pb);
                g_mactcp.tcp_streams[j].stream = 0;
            }
            if (g_mactcp.tcp_streams[j].buffer) {
                DisposePtr(g_mactcp.tcp_streams[j].buffer);
                g_mactcp.tcp_streams[j].buffer = NULL;
            }
        }
    }
fail_upps:
    if (g_mactcp.tcp_upp) {
        DisposeTCPNotifyUPP(g_mactcp.tcp_upp);
        g_mactcp.tcp_upp = NULL;
    }
    if (g_mactcp.udp_upp) {
        DisposeUDPNotifyUPP(g_mactcp.udp_upp);
        g_mactcp.udp_upp = NULL;
    }
    return PT_ERR_INIT;
}

static void mactcp_release_udp_stream(UDPStreamSlot *us, UDPiopb *upb,
                                     const char *label)
{
    /* UDPRelease cancels pending reads per MacTCP Programmer's Guide
     * line 1422: "Any outstanding commands on that stream are terminated
     * with an error."  With cleanup_streams ensuring clean state before
     * rediscovery, the old PPC-specific wait path is unnecessary. */
    if (us->stream) {
        memset(upb, 0, sizeof(*upb));
        upb->ioCRefNum = g_mactcp.driver_ref;
        upb->udpStream = us->stream;
        upb->csCode = UDPRelease;
        PBControlSync((ParmBlkPtr)upb);
        CLOG_DEBUG("  %s UDP: release result=%d", label, (int)upb->ioResult);
    }

    if (us->buffer) {
        DisposePtr(us->buffer);
    }
    CLOG_DEBUG("  %s UDP: released", label);
}

/* ------------------------------------------------------------------ */
/* Stream cleanup for rediscovery                                      */
/* ------------------------------------------------------------------ */

static void mactcp_cleanup_streams(PT_Context_Internal *ctx)
{
    int i;
    TCPiopb pb;
    UDPiopb upb;

    (void)ctx;

    CLOG_INFO("Cleaning up streams for rediscovery");

    /* TCP: TCPRelease each stream, then recreate fresh.
     * TCPRelease does implicit abort per MacTCP docs line 4012.
     * Buffer ownership returns to us (line 2440) -- reuse for
     * fresh TCPCreate (zero allocation per Principle V). */
    for (i = 0; i < MAX_TCP_STREAMS; i++) {
        TCPStreamSlot *ts = &g_mactcp.tcp_streams[i];
        if (ts->stream) {
            memset(&pb, 0, sizeof(pb));
            pb.ioCRefNum = g_mactcp.driver_ref;
            pb.tcpStream = ts->stream;
            pb.csCode = TCPRelease;
            PBControlSync((ParmBlkPtr)&pb);
            /* Buffer returned -- reuse for fresh stream */
            ts->stream = create_tcp_stream(g_mactcp.driver_ref,
                                           ts->buffer, TCP_BUF_SIZE,
                                           g_mactcp.tcp_upp, (Ptr)ts);
            ts->state = STREAM_FREE;
            ts->owner = NULL;
            ts->flags = 0;
            ts->send_pending = 0;
        }
    }
    g_mactcp.listener_count = 0;
    g_mactcp.listen_active = 0;

    /* UDP: UDPRelease cancels pending reads per MacTCP docs line 1422.
     * Buffer ownership returns to us (line 1424) -- reuse for
     * fresh UDPCreate (zero allocation per Principle V). */
    if (g_mactcp.discovery_udp.stream) {
        memset(&upb, 0, sizeof(upb));
        upb.ioCRefNum = g_mactcp.driver_ref;
        upb.udpStream = g_mactcp.discovery_udp.stream;
        upb.csCode = UDPRelease;
        PBControlSync((ParmBlkPtr)&upb);
        g_mactcp.discovery_udp.read_pending = 0;
        g_mactcp.discovery_udp.stream = create_udp_stream(
            g_mactcp.driver_ref, g_mactcp.discovery_udp.buffer,
            UDP_BUF_SIZE, g_mactcp.udp_upp,
            (Ptr)&g_mactcp.discovery_udp, PT_DISCOVERY_PORT);
    }

    if (g_mactcp.message_udp.stream) {
        memset(&upb, 0, sizeof(upb));
        upb.ioCRefNum = g_mactcp.driver_ref;
        upb.udpStream = g_mactcp.message_udp.stream;
        upb.csCode = UDPRelease;
        PBControlSync((ParmBlkPtr)&upb);
        g_mactcp.message_udp.read_pending = 0;
        g_mactcp.message_udp.stream = create_udp_stream(
            g_mactcp.driver_ref, g_mactcp.message_udp.buffer,
            UDP_BUF_SIZE, g_mactcp.udp_upp,
            (Ptr)&g_mactcp.message_udp, PT_UDP_MSG_PORT);
    }

    CLOG_INFO("Stream cleanup complete");
}

static void mactcp_shutdown(PT_Context_Internal *ctx)
{
    int i;
    TCPiopb pb;
    UDPiopb upb;

    /* TCP: TCPRelease each stream (does implicit abort if connected).
     * MacTCP Programmer's Guide line 4012: "If there is an open
     * connection on the stream, the connection is first broken as
     * though a TCPAbort command had been issued." */
    for (i = 0; i < MAX_TCP_STREAMS; i++) {
        TCPStreamSlot *ts = &g_mactcp.tcp_streams[i];
        if (ts->stream) {
            memset(&pb, 0, sizeof(pb));
            pb.ioCRefNum = g_mactcp.driver_ref;
            pb.tcpStream = ts->stream;
            pb.csCode = TCPRelease;
            PBControlSync((ParmBlkPtr)&pb);
        }
        if (ts->buffer) {
            DisposePtr(ts->buffer);
        }
    }

    /* UDP: UDPRelease cancels pending reads per MacTCP docs */
    mactcp_release_udp_stream(&g_mactcp.discovery_udp, &upb, "Discovery");
    mactcp_release_udp_stream(&g_mactcp.message_udp, &upb, "Message");

    /* Dispose UPPs after all async operations complete */
    if (g_mactcp.tcp_upp) DisposeTCPNotifyUPP(g_mactcp.tcp_upp);
    if (g_mactcp.udp_upp) DisposeUDPNotifyUPP(g_mactcp.udp_upp);

    ctx->platform_state = NULL;
}

static PT_Status mactcp_udp_broadcast(PT_Context_Internal *ctx,
                                      unsigned short port,
                                      const void *data, size_t len)
{
    UDPiopb pb;
    const UDPStreamSlot *us;
    wdsEntry wds[2];
    OSErr err;

    (void)ctx;

    us = (port == PT_DISCOVERY_PORT) ?
         &g_mactcp.discovery_udp : &g_mactcp.message_udp;

    wds[0].length = (unsigned short)len;
    wds[0].ptr = (Ptr)data;
    wds[1].length = 0;
    wds[1].ptr = NULL;

    memset(&pb, 0, sizeof(pb));
    pb.ioCRefNum = g_mactcp.driver_ref;
    pb.udpStream = us->stream;
    pb.csCode = UDPWrite;
    pb.csParam.send.remoteHost = 0xFFFFFFFF;
    pb.csParam.send.remotePort = port;
    pb.csParam.send.wdsPtr = (Ptr)wds;
    pb.csParam.send.checkSum = 1;
    pb.ioCompletion = NULL;

    err = PBControlSync((ParmBlkPtr)&pb);
    return (err == noErr) ? PT_OK : PT_ERR_SEND_FAILED;
}

static PT_Status mactcp_udp_send(PT_Context_Internal *ctx,
                                 const PT_Peer_Internal *peer,
                                 unsigned short port,
                                 const void *data, size_t len)
{
    UDPiopb pb;
    wdsEntry wds[2];
    OSErr err;

    (void)ctx;

    wds[0].length = (unsigned short)len;
    wds[0].ptr = (Ptr)data;
    wds[1].length = 0;
    wds[1].ptr = NULL;

    memset(&pb, 0, sizeof(pb));
    pb.ioCRefNum = g_mactcp.driver_ref;
    pb.udpStream = g_mactcp.message_udp.stream;
    pb.csCode = UDPWrite;
    pb.csParam.send.remoteHost = peer->ip_addr;
    pb.csParam.send.remotePort = port;
    pb.csParam.send.wdsPtr = (Ptr)wds;
    pb.csParam.send.checkSum = 1;
    pb.ioCompletion = NULL;

    err = PBControlSync((ParmBlkPtr)&pb);
    return (err == noErr) ? PT_OK : PT_ERR_SEND_FAILED;
}

static PT_Status mactcp_udp_listen(PT_Context_Internal *ctx,
                                   unsigned short port)
{
    UDPStreamSlot *us;
    (void)ctx;

    us = (port == PT_DISCOVERY_PORT) ?
         &g_mactcp.discovery_udp : &g_mactcp.message_udp;

    issue_udp_read(us, g_mactcp.driver_ref);
    return PT_OK;
}

static PT_Status mactcp_tcp_listen(PT_Context_Internal *ctx)
{
    int idx;
    (void)ctx;

    g_mactcp.listen_active = 1;

    idx = find_free_stream();
    if (idx < 0) {
        CLOG_WARN("No free TCP streams for listener");
        return PT_ERR_NO_ROOM;
    }

    issue_passive_open(idx, g_mactcp.driver_ref, PT_TCP_PORT);
    CLOG_INFO("Listening on TCP port %d (stream %d)", PT_TCP_PORT, idx);
    return PT_OK;
}

static PT_Status mactcp_tcp_connect(PT_Context_Internal *ctx,
                                    PT_Peer_Internal *peer)
{
    int idx;
    TCPStreamSlot *ts;

    (void)ctx;

    /* Find a free stream; reclaim listener if necessary */
    idx = find_free_stream();
    if (idx < 0) {
        int i;
        for (i = 0; i < MAX_TCP_STREAMS; i++) {
            if (g_mactcp.tcp_streams[i].stream &&
                g_mactcp.tcp_streams[i].state == STREAM_LISTENING) {
                abort_stream(i);
                idx = i;
                break;
            }
        }
    }
    if (idx < 0) return PT_ERR_NO_ROOM;

    ts = &g_mactcp.tcp_streams[idx];

    memset(&ts->open_pb, 0, sizeof(ts->open_pb));
    ts->open_pb.ioCRefNum = g_mactcp.driver_ref;
    ts->open_pb.tcpStream = ts->stream;
    ts->open_pb.csCode = TCPActiveOpen;
    ts->open_pb.csParam.open.localPort = 0;
    ts->open_pb.csParam.open.remoteHost = peer->ip_addr;
    ts->open_pb.csParam.open.remotePort = PT_TCP_PORT;
    ts->open_pb.csParam.open.commandTimeoutValue = 15;
    ts->open_pb.csParam.open.ulpTimeoutValue = 60;
    ts->open_pb.csParam.open.ulpTimeoutAction = 1;
    ts->open_pb.csParam.open.validityFlags = 0xC0;
    ts->open_pb.ioCompletion = NULL;

    PBControlAsync((ParmBlkPtr)&ts->open_pb);
    ts->state = STREAM_CONNECTING;

    peer->platform_peer.tcp_stream = ts;
    ts->owner = peer;

    return PT_OK;
}

static PT_Status mactcp_tcp_send(PT_Context_Internal *ctx,
                                 PT_Peer_Internal *peer,
                                 const void *data, size_t len)
{
    TCPStreamSlot *ts;

    (void)ctx;

    ts = (TCPStreamSlot *)peer->platform_peer.tcp_stream;
    if (!ts || ts->state != STREAM_CONNECTED) return PT_ERR_NOT_CONNECTED;

    /* No async check needed — sends are synchronous (R42/T135) */

    /* Copy data to send buffer (skip if already there, e.g. from
     * send_tcp_frame which builds frames directly in tcp_send_buf) */
    if (len > peer->tcp_send_size) len = peer->tcp_send_size;
    if ((const unsigned char *)data != peer->tcp_send_buf) {
        memcpy(peer->tcp_send_buf, data, len);
    }

    ts->wds[0].length = (unsigned short)len;
    ts->wds[0].ptr = (Ptr)peer->tcp_send_buf;
    ts->wds[1].length = 0;
    ts->wds[1].ptr = NULL;

    memset(&ts->send_pb, 0, sizeof(ts->send_pb));
    ts->send_pb.ioCRefNum = g_mactcp.driver_ref;
    ts->send_pb.tcpStream = ts->stream;
    ts->send_pb.csCode = TCPSend;
    ts->send_pb.csParam.send.wdsPtr = (Ptr)ts->wds;
    ts->send_pb.csParam.send.pushFlag = 1;
    ts->send_pb.ioCompletion = NULL;

    /* Synchronous send — blocks until complete (~1ms/chunk on LAN).
     * Required for chunked messages: the chunking loop in pt_messaging.c
     * issues multiple tcp_send calls per PT_Send, which fails with async
     * because the second chunk returns PT_ERR_SEND_FAILED while the first
     * is still pending. Sync is acceptable: Chat is user-paced, Bomberman
     * uses UDP. Reference: R42, MacTCP Programmer's Guide lines 700, 2939. */
    PBControlSync((ParmBlkPtr)&ts->send_pb);

    if (ts->send_pb.ioResult != noErr) return PT_ERR_SEND_FAILED;

    return PT_OK;
}

static void mactcp_tcp_disconnect(PT_Context_Internal *ctx,
                                  PT_Peer_Internal *peer)
{
    const TCPStreamSlot *ts;
    int i;

    (void)ctx;

    ts = (const TCPStreamSlot *)peer->platform_peer.tcp_stream;
    if (!ts) return;

    /* Find stream index and abort */
    for (i = 0; i < MAX_TCP_STREAMS; i++) {
        if (&g_mactcp.tcp_streams[i] == ts) {
            abort_stream(i);
            break;
        }
    }

    peer->platform_peer.tcp_stream = NULL;
}

static void mactcp_poll(PT_Context_Internal *ctx)
{
    int i;

    /* Process TCP streams */
    for (i = 0; i < MAX_TCP_STREAMS; i++) {
        TCPStreamSlot *ts = &g_mactcp.tcp_streams[i];
        if (!ts->stream) continue;

        /* ---- Passive open completion ---- */
        if (ts->state == STREAM_LISTENING &&
            ts->open_pb.ioResult != inProgress) {

            if (ts->open_pb.ioResult == noErr) {
                ip_addr remote_ip;
                PT_PlatformPeer ppeer;

                g_mactcp.listener_count--;
                ts->state = STREAM_CONNECTED; /* tentative */

                remote_ip = ts->open_pb.csParam.open.remoteHost;

                memset(&ppeer, 0, sizeof(ppeer));
                ppeer.tcp_stream = ts;

                pt_handle_incoming_connection(ctx, remote_ip, &ppeer);

                /* Find which peer (if any) now owns this stream */
                {
                    int j;
                    for (j = 0; j < ctx->max_peers; j++) {
                        if (ctx->peers[j].in_use &&
                            ctx->peers[j].platform_peer.tcp_stream == ts) {
                            ts->owner = &ctx->peers[j];
                            break;
                        }
                    }
                }

                if (!ts->owner) {
                    /* No room -- abort the accepted connection */
                    abort_stream(i);
                }
            } else {
                g_mactcp.listener_count--;
                ts->state = STREAM_FREE;
            }
            /* Re-listen happens at end of poll */
        }

        /* ---- Active open completion ---- */
        if (ts->state == STREAM_CONNECTING &&
            ts->open_pb.ioResult != inProgress) {

            PT_Peer_Internal *peer = ts->owner;

            if (ts->open_pb.ioResult == noErr && peer) {
                ts->state = STREAM_CONNECTED;
                peer->state = PT_PEER_CONNECTED;
                peer->last_tcp_activity = ctx->current_time;
                peer->last_tcp_send = ctx->current_time;
                peer->connect_start = 0;

                CLOG_INFO("TCP connected to %s", peer->name);
                if (ctx->callbacks.on_connected) {
                    ctx->callbacks.on_connected(
                        (PT_Peer *)peer,
                        ctx->callbacks.on_connected_data);
                }
            } else {
                /* Connection failed */
                abort_stream(i);
                if (peer) {
                    peer->connect_start = 0;
                    peer->state = PT_PEER_DISCONNECTED;
                    peer->platform_peer.tcp_stream = NULL;
                    pt_fire_error(ctx, peer, PT_ERR_SEND_FAILED,
                                  "TCP connect failed");
                }
            }
        }

        /* ---- Connected stream events ---- */
        if (ts->state != STREAM_CONNECTED) continue;

        /* Check send completion */
        if (ts->send_pending && ts->send_pb.ioResult != inProgress) {
            ts->send_pending = 0;
        }

        /* Snapshot and clear flags with interrupts disabled (R27).
           ASR runs at hardware interrupt level (MacTCP Guide line 2153)
           and can preempt between read and clear. Disabling interrupts
           ensures the pair is atomic. */
        {
            unsigned char local_flags;
            short saved_sr = pt_disable_interrupts();
            local_flags = ts->flags;
            ts->flags = 0;
            pt_restore_interrupts(saved_sr);

        /* Check data available (ASR flag) */
        if (local_flags & FLAG_DATA_AVAIL) {
            {
                PT_Peer_Internal *peer = ts->owner;
                if (peer) {
                    size_t space = peer->tcp_recv_size -
                                   peer->tcp_recv_len;
                    if (space > 0) {
                        g_mactcp.recv_pb.tcpStream = ts->stream;
                        g_mactcp.recv_pb.csParam.receive.rcvBuff =
                            (Ptr)(peer->tcp_recv_buf +
                                  peer->tcp_recv_len);
                        g_mactcp.recv_pb.csParam.receive.rcvBuffLen =
                            (unsigned short)space;

                        if (PBControlSync((ParmBlkPtr)&g_mactcp.recv_pb) == noErr) {
                            peer->tcp_recv_len +=
                                g_mactcp.recv_pb.csParam.receive.rcvBuffLen;
                            peer->last_tcp_activity =
                                ctx->current_time;
                            pt_messaging_process_tcp_data(ctx, peer);
                        }
                    }
                }
            }
        }

        /* Check remote close / terminate */
        if (local_flags & (FLAG_REMOTE_CLOSE | FLAG_TERMINATED)) {
            PT_Peer_Internal *peer;

            peer = ts->owner;
            if (peer) {
                /* Drain remaining TCP data — goodbye frame may be
                   buffered but unprocessed (R23). Read what we can
                   and parse; if goodbye is found, peer transitions
                   to DISCONNECTED with PT_QUIT. */
                {
                    size_t space = peer->tcp_recv_size -
                                   peer->tcp_recv_len;
                    if (space > 0 && ts->stream &&
                        ts->state != STREAM_FREE) {
                        OSErr rcv_err;
                        g_mactcp.recv_pb.tcpStream = ts->stream;
                        g_mactcp.recv_pb.csParam.receive.rcvBuff =
                            (Ptr)(peer->tcp_recv_buf +
                                  peer->tcp_recv_len);
                        g_mactcp.recv_pb.csParam.receive.rcvBuffLen =
                            (unsigned short)space;

                        rcv_err = PBControlSync(
                            (ParmBlkPtr)&g_mactcp.recv_pb);
                        if (rcv_err == noErr &&
                            g_mactcp.recv_pb.csParam.receive
                                .rcvBuffLen > 0) {
                            peer->tcp_recv_len +=
                                g_mactcp.recv_pb.csParam.receive
                                    .rcvBuffLen;
                            pt_messaging_process_tcp_data(ctx, peer);
                        } else if (rcv_err != noErr) {
                            CLOG_DEBUG("TCPRcv error %d in "
                                       "terminated drain",
                                       (int)rcv_err);
                        }
                    }
                    /* Also parse any data already in the buffer */
                    if (peer->tcp_recv_len > 0 &&
                        peer->state == PT_PEER_CONNECTED) {
                        pt_messaging_process_tcp_data(ctx, peer);
                    }
                }
                /* Only fire ERROR if goodbye wasn't found */
                if (peer->state == PT_PEER_CONNECTED) {
                    pt_handle_peer_disconnect(ctx, peer,
                                              PT_DISCONNECT_ERROR);
                }
            }
        }
        } /* end local_flags snapshot block */
    }

    /* ---- UDP discovery socket ---- */
    if (g_mactcp.discovery_udp.read_pending &&
        g_mactcp.discovery_udp.read_pb.ioResult != inProgress) {
        g_mactcp.discovery_udp.read_pending = 0;

        if (g_mactcp.discovery_udp.read_pb.ioResult == noErr) {
            Ptr data_ptr;
            unsigned short data_len;
            ip_addr src_addr;

            data_ptr = g_mactcp.discovery_udp.read_pb.csParam.receive.rcvBuff;
            data_len = g_mactcp.discovery_udp.read_pb.csParam.receive.rcvBuffLen;
            src_addr = g_mactcp.discovery_udp.read_pb.csParam.receive.remoteHost;

            pt_discovery_receive(ctx, data_ptr, data_len, src_addr);

            /* Return buffer to MacTCP (reuse pooled param block) */
            g_mactcp.bfr_ret_pb.udpStream = g_mactcp.discovery_udp.stream;
            g_mactcp.bfr_ret_pb.csParam.receive.rcvBuff = data_ptr;
            PBControlSync((ParmBlkPtr)&g_mactcp.bfr_ret_pb);
        }

        issue_udp_read(&g_mactcp.discovery_udp, g_mactcp.driver_ref);
    }

    /* ---- UDP message socket ---- */
    if (g_mactcp.message_udp.read_pending &&
        g_mactcp.message_udp.read_pb.ioResult != inProgress) {
        g_mactcp.message_udp.read_pending = 0;

        if (g_mactcp.message_udp.read_pb.ioResult == noErr) {
            Ptr data_ptr;
            unsigned short data_len;
            ip_addr src_addr;

            data_ptr = g_mactcp.message_udp.read_pb.csParam.receive.rcvBuff;
            data_len = g_mactcp.message_udp.read_pb.csParam.receive.rcvBuffLen;
            src_addr = g_mactcp.message_udp.read_pb.csParam.receive.remoteHost;

            pt_messaging_process_udp_data(ctx, data_ptr, data_len,
                                          src_addr);

            /* Return buffer to MacTCP (reuse pooled param block) */
            g_mactcp.bfr_ret_pb.udpStream = g_mactcp.message_udp.stream;
            g_mactcp.bfr_ret_pb.csParam.receive.rcvBuff = data_ptr;
            PBControlSync((ParmBlkPtr)&g_mactcp.bfr_ret_pb);
        }

        issue_udp_read(&g_mactcp.message_udp, g_mactcp.driver_ref);
    }

    /* ---- Ensure a listener is active (O(1) via counter) ---- */
    if (g_mactcp.listen_active && g_mactcp.listener_count <= 0) {
        re_listen();
    }
}

/* ------------------------------------------------------------------ */
/* Ops table                                                           */
/* ------------------------------------------------------------------ */

static PT_PlatformOps mactcp_ops = {
    mactcp_init,
    mactcp_shutdown,
    mactcp_udp_broadcast,
    mactcp_udp_send,
    mactcp_udp_listen,
    mactcp_tcp_listen,
    mactcp_tcp_connect,
    mactcp_tcp_send,
    mactcp_tcp_disconnect,
    mactcp_poll,
    mactcp_cleanup_streams
};

PT_PlatformOps *mactcp_get_ops(void)
{
    return &mactcp_ops;
}

#endif /* PT_PLATFORM_MACTCP */
