/**
 * @file mactcp_driver.c
 * @brief MacTCP Driver Initialization and Buffer Management
 *
 * Functions for MacTCP driver access, IP configuration, and memory-aware
 * buffer sizing for Classic Mac systems.
 *
 * References:
 * - MacTCP Programmer's Guide (1989)
 * - LaunchAPPL MacTCPStream.cc (Retro68)
 */

#include "mactcp_defs.h"
#include "pt_internal.h"
#include "pt_compat.h"

#if defined(PT_PLATFORM_MACTCP)

#include <Devices.h>
#include <MacMemory.h>

/* ========================================================================== */
/* External UPP Accessors (from platform_mactcp.c)                            */
/* ========================================================================== */

extern short pt_mactcp_get_refnum(void);

/* ========================================================================== */
/* Context Accessor                                                            */
/* ========================================================================== */

/**
 * Get MacTCP platform data from context.
 *
 * The platform data is allocated immediately after the pt_context struct.
 * Must be implemented here (not inline in header) because it requires
 * the full pt_context definition from pt_internal.h.
 *
 * @param ctx  PeerTalk context
 * @return     Pointer to MacTCP platform data
 */
pt_mactcp_data *pt_mactcp_get(struct pt_context *ctx)
{
    return (pt_mactcp_data *)((char *)ctx + sizeof(struct pt_context));
}

/* ========================================================================== */
/* Buffer Allocation                                                           */
/* ========================================================================== */

/**
 * Allocate locked, non-relocatable memory for MacTCP buffers.
 * CRITICAL: MacTCP requires this memory to remain fixed while stream is open.
 *
 * Uses NewPtr (application heap, non-relocatable).
 * Clears buffer to zero.
 *
 * @param size  Size in bytes to allocate
 * @return      Pointer to buffer, or NULL on failure
 */
Ptr pt_mactcp_alloc_buffer(unsigned long size)
{
    Ptr buffer;

    /* NewPtr allocates from application heap, non-relocatable */
    buffer = NewPtr((Size)size);
    if (buffer == NULL)
        return NULL;

    /* Clear the buffer */
    pt_memset(buffer, 0, (size_t)size);

    return buffer;
}

/**
 * Free a buffer allocated with pt_mactcp_alloc_buffer.
 *
 * @param buffer  Buffer to free (safe to pass NULL)
 */
void pt_mactcp_free_buffer(Ptr buffer)
{
    if (buffer != NULL)
        DisposePtr(buffer);
}

/* ========================================================================== */
/* IP Configuration                                                            */
/* ========================================================================== */

/**
 * Get local IP address and network mask.
 *
 * From MacTCP Programmer's Guide: Uses ipctlGetAddr control call.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success, -1 on error
 */
int pt_mactcp_get_local_ip(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    GetAddrParamBlock pb;
    OSErr err;

    pt_memset(&pb, 0, sizeof(pb));
    pb.csCode = ipctlGetAddr;
    pb.ioCRefNum = pt_mactcp_get_refnum();
    pb.ioResult = 1;  /* Non-zero to detect completion */

    err = PBControlSync((ParmBlkPtr)&pb);

    if (err != noErr) {
        PT_LOG_ERR(ctx->log, PT_LOG_CAT_PLATFORM,
            "Failed to get local IP (ipctlGetAddr): %d", (int)err);
        return -1;
    }

    md->local_ip = pb.ourAddress;
    md->net_mask = pb.ourNetMask;

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_INIT,
        "Local IP: %lu.%lu.%lu.%lu netmask: 0x%08lX",
        (md->local_ip >> 24) & 0xFF,
        (md->local_ip >> 16) & 0xFF,
        (md->local_ip >> 8) & 0xFF,
        md->local_ip & 0xFF,
        md->net_mask);

    return 0;
}

/* ========================================================================== */
/* System Limits Query                                                         */
/* ========================================================================== */

/**
 * Query system limits via TCPGlobalInfo.
 *
 * Instead of hardcoding 64 streams, query the actual system limit.
 * This is non-fatal if it fails - we fall back to defaults.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success, -1 on error (non-fatal)
 */
int pt_mactcp_query_limits(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);
    TCPiopb pb;
    OSErr err;

    pt_memset(&pb, 0, sizeof(pb));
    pb.csCode = TCPGlobalInfo;
    pb.ioCRefNum = pt_mactcp_get_refnum();

    err = PBControlSync((ParmBlkPtr)&pb);

    if (err != noErr) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_PLATFORM,
            "TCPGlobalInfo failed: %d, using defaults", (int)err);
        md->max_tcp_connections = 64;  /* Default fallback */
        md->max_udp_streams = 64;
        return -1;
    }

    /* Extract limits from globalInfo structure
     * maxTCPConnections is in the TCPGlobalInfoPB directly
     * tcpMaxConn is in the TCPParam structure (pointed to by tcpParamPtr)
     */
    md->max_tcp_connections = pb.csParam.globalInfo.maxTCPConnections;
    md->max_udp_streams = 64;  /* UDP doesn't have a separate limit query */

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_INIT,
        "MacTCP limits: max_connections=%u",
        (unsigned)md->max_tcp_connections);

    return 0;
}

/* ========================================================================== */
/* Buffer Sizing                                                               */
/* ========================================================================== */

/**
 * Get optimal TCP buffer size based on physical MTU.
 *
 * From MacTCP Programmer's Guide: "An application should allocate memory
 * by first finding the MTU of the physical network (see the UDPMTU section).
 * The minimum memory allocation should be 4N + 1024, where N is the size
 * of the physical MTU returned by the UDPMTU call."
 *
 * This ensures the receive window can hold enough data for good throughput.
 *
 * @param ctx  PeerTalk context
 * @return     Optimal buffer size in bytes
 */
unsigned long pt_mactcp_optimal_buffer_size(struct pt_context *ctx)
{
    UDPiopb pb;
    OSErr err;
    unsigned short mtu;
    unsigned long optimal;

    pt_memset(&pb, 0, sizeof(pb));
    pb.csCode = UDPMaxMTUSize;  /* Get physical MTU */
    pb.ioCRefNum = pt_mactcp_get_refnum();

    /* remoteHost=0 gets local interface MTU */
    pb.csParam.mtu.remoteHost = 0;

    err = PBControlSync((ParmBlkPtr)&pb);

    if (err != noErr) {
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_PLATFORM,
            "UDPMTU query failed: %d, using default buffer size", (int)err);
        return PT_TCP_RCV_BUF_CHAR;  /* Safe default */
    }

    mtu = pb.csParam.mtu.mtuSize;

    /* Per documentation: optimal = 4 * MTU + 1024 */
    optimal = (4UL * mtu) + 1024;

    /* Clamp to reasonable range */
    if (optimal < PT_TCP_RCV_BUF_MIN)
        optimal = PT_TCP_RCV_BUF_MIN;
    if (optimal > PT_TCP_RCV_BUF_MAX)
        optimal = PT_TCP_RCV_BUF_MAX;

    PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_PLATFORM,
        "Physical MTU=%u, optimal buffer=%lu", (unsigned)mtu, optimal);

    return optimal;
}

/**
 * Memory-aware buffer sizing for constrained systems (Mac SE 4MB).
 *
 * Mac SE with 4MB RAM, System 6.0.8:
 * - System uses ~400-600KB
 * - App partition ~2.5-3MB available
 * - MacTCP allocates ~64KB internal buffers for 4MB machine
 * - Leave 500KB+ free for app heap operations
 *
 * This function considers available memory and returns appropriate buffer size.
 *
 * @param ctx  PeerTalk context
 * @return     Recommended buffer size in bytes
 */
unsigned long pt_mactcp_buffer_size_for_memory(struct pt_context *ctx)
{
    long free_mem = FreeMem();
    long max_block = MaxBlock();
    unsigned long buf_size;

    /* Log memory state for debugging on real hardware */
    PT_LOG_INFO(ctx->log, PT_LOG_CAT_MEMORY,
        "Memory check: FreeMem=%ld MaxBlock=%ld", free_mem, max_block);

    /*
     * Conservative sizing - leave room for heap operations
     * Mac SE 4MB typical: FreeMem ~2.5MB at app launch
     */
    if (free_mem > PT_MEM_PLENTY) {
        /* Plenty of memory - use optimal formula */
        buf_size = pt_mactcp_optimal_buffer_size(ctx);
    } else if (free_mem > PT_MEM_MODERATE) {
        /* Moderate memory - use 8KB (character app) */
        buf_size = PT_TCP_RCV_BUF_CHAR;
    } else if (free_mem > PT_MEM_LOW) {
        /* Low memory - use minimum viable */
        buf_size = PT_TCP_RCV_BUF_MIN;
    } else {
        /* Critical - warn and use minimum */
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_MEMORY,
            "Low memory warning: FreeMem=%ld - using minimum buffers", free_mem);
        buf_size = PT_TCP_RCV_BUF_MIN;
    }

    /* Don't allocate more than MaxBlock can provide
     * Leave headroom for other allocations */
    if ((long)buf_size > max_block / 2) {
        buf_size = PT_TCP_RCV_BUF_MIN;
        PT_LOG_WARN(ctx->log, PT_LOG_CAT_MEMORY,
            "MaxBlock too small (%ld), using minimum buffer", max_block);
    }

    return buf_size;
}

/* ========================================================================== */
/* Stream Initialization                                                       */
/* ========================================================================== */

/**
 * Initialize all MacTCP stream states to unused.
 *
 * Called from pt_mactcp_init() to reset all hot/cold structs.
 *
 * @param md  MacTCP platform data
 */
void pt_mactcp_init_streams(pt_mactcp_data *md)
{
    int i;

    /* Initialize discovery UDP stream
     * Note: StreamPtr is unsigned long in MacTCP, 0 means unused */
    md->discovery_hot.stream = 0;
    md->discovery_hot.state = PT_STREAM_UNUSED;
    md->discovery_hot.asr_flags = 0;
    md->discovery_hot.async_pending = 0;
    md->discovery_hot.data_ready = 0;
    md->discovery_cold.rcv_buffer = NULL;
    md->discovery_cold.rcv_buffer_size = 0;

    /* Initialize TCP listener stream */
    md->listener_hot.stream = 0;
    md->listener_hot.state = PT_STREAM_UNUSED;
    md->listener_hot.asr_flags = 0;
    md->listener_hot.async_pending = 0;
    md->listener_hot.peer_idx = -1;
    md->listener_hot.log_events = 0;
    md->listener_cold.rcv_buffer = NULL;
    md->listener_cold.rcv_buffer_size = 0;

    /* Initialize per-peer TCP streams */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        md->tcp_hot[i].stream = 0;
        md->tcp_hot[i].state = PT_STREAM_UNUSED;
        md->tcp_hot[i].asr_flags = 0;
        md->tcp_hot[i].async_pending = 0;
        md->tcp_hot[i].rds_outstanding = 0;
        md->tcp_hot[i].peer_idx = -1;
        md->tcp_hot[i].log_events = 0;
        md->tcp_cold[i].rcv_buffer = NULL;
        md->tcp_cold[i].rcv_buffer_size = 0;
    }

    /* Initialize timing */
    md->last_announce_tick = 0;
    md->ticks_per_second = 60;  /* Mac tick rate */
}

/**
 * Initialize MacTCP platform data.
 *
 * Called by platform_mactcp.c mactcp_init() after driver is opened.
 * Sets up stream arrays, queries system limits, gets local IP.
 *
 * @param ctx  PeerTalk context
 * @return     0 on success, -1 on error
 */
int pt_mactcp_data_init(struct pt_context *ctx)
{
    pt_mactcp_data *md = pt_mactcp_get(ctx);

    /* Store driver refnum from platform_mactcp.c */
    md->driver_refnum = pt_mactcp_get_refnum();

    /* Initialize all stream states */
    pt_mactcp_init_streams(md);

    /* Note: UPPs are created in platform_mactcp.c mactcp_init()
     * because the ASR callbacks are defined there.
     * We just store pointers here for access. */

    /* Query system limits (non-fatal if it fails) */
    pt_mactcp_query_limits(ctx);

    /* Get local IP address */
    if (pt_mactcp_get_local_ip(ctx) < 0) {
        /* This is a critical failure - can't operate without IP */
        return -1;
    }

    return 0;
}

/* ========================================================================== */
/* Extra Size for Context Allocation                                           */
/* ========================================================================== */

/**
 * Return the size of MacTCP platform data.
 *
 * Called by pt_plat_extra_size() to determine how much extra space
 * to allocate after the pt_context struct.
 *
 * @return  sizeof(pt_mactcp_data)
 */
size_t pt_mactcp_extra_size(void)
{
    return sizeof(pt_mactcp_data);
}

#endif /* PT_PLATFORM_MACTCP */
