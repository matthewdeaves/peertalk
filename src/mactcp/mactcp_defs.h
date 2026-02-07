/**
 * @file mactcp_defs.h
 * @brief MacTCP Type Definitions for PeerTalk State Machine
 *
 * Data structures optimized for 68k cache efficiency using hot/cold split.
 * Hot structs are checked every poll (~14 bytes TCP, ~10 bytes UDP).
 * Cold structs contain large parameter blocks accessed during I/O.
 *
 * References:
 * - MacTCP Programmer's Guide (1989)
 * - Inside Macintosh: Networking
 */

#ifndef PT_MACTCP_DEFS_H
#define PT_MACTCP_DEFS_H

#include "pt_types.h"

/* MacTCP includes - these are in the Retro68 include path
 *
 * Note: GetMyIPAddr.h no longer exists as a separate header.
 * All IP configuration (GetAddrParamBlock, ipctlGetAddr) is
 * consolidated into MacTCP.h per the Developer Notes section.
 *
 * WARNING: Do NOT include AddressXlation.h here - it conflicts with
 * MacTCP.h (duplicate type definitions). MacTCP.h already includes
 * all the types we need.
 */
#include <MacTCP.h>

/* Forward declaration */
struct pt_context;

/* ========================================================================== */
/* Stream States                                                               */
/* ========================================================================== */

/**
 * Stream state enum.
 *
 * DOD: Use uint8_t instead of enum to save 1-3 bytes per stream
 * and avoid alignment padding on 68k. Enum defaults to int (2-4 bytes).
 */
typedef uint8_t pt_stream_state;
#define PT_STREAM_UNUSED      0
#define PT_STREAM_CREATING    1
#define PT_STREAM_IDLE        2
#define PT_STREAM_LISTENING   3
#define PT_STREAM_CONNECTING  4
#define PT_STREAM_CONNECTED   5
#define PT_STREAM_CLOSING     6
#define PT_STREAM_RELEASING   7

/**
 * Get human-readable name for stream state.
 * Useful for logging state transitions.
 */
static inline const char *pt_state_name(pt_stream_state state)
{
    switch (state) {
    case PT_STREAM_UNUSED:     return "UNUSED";
    case PT_STREAM_CREATING:   return "CREATING";
    case PT_STREAM_IDLE:       return "IDLE";
    case PT_STREAM_LISTENING:  return "LISTENING";
    case PT_STREAM_CONNECTING: return "CONNECTING";
    case PT_STREAM_CONNECTED:  return "CONNECTED";
    case PT_STREAM_CLOSING:    return "CLOSING";
    case PT_STREAM_RELEASING:  return "RELEASING";
    default:                   return "UNKNOWN";
    }
}

/* ========================================================================== */
/* ASR Event Flags                                                             */
/* ========================================================================== */

/**
 * ASR event flags - set by ASR, cleared by poll loop.
 *
 * DOD: Use single volatile uint8_t bitfield instead of 5 separate bytes.
 * On 68k, each byte access requires a separate memory fetch. Collapsing
 * to one byte means a single memory access for all flag checks.
 * Original: 5-6 bytes (with padding). Now: 1 byte.
 */
typedef volatile uint8_t pt_asr_flags;
#define PT_ASR_DATA_ARRIVED     0x01
#define PT_ASR_CONN_CLOSED      0x02
#define PT_ASR_URGENT_DATA      0x04
#define PT_ASR_ICMP_RECEIVED    0x08
#define PT_ASR_TIMEOUT          0x10

/**
 * Log event bits (set by ASR or completion routines, cleared by main loop)
 *
 * These enable deferred logging - ASR sets the bit, main loop does the
 * actual PT_Log call (which is NOT interrupt-safe).
 */
#define PT_LOG_EVT_DATA_ARRIVED     0x01
#define PT_LOG_EVT_CONN_CLOSED      0x02
#define PT_LOG_EVT_TERMINATED       0x04
#define PT_LOG_EVT_ICMP             0x08
#define PT_LOG_EVT_ERROR            0x10
#define PT_LOG_EVT_LISTEN_COMPLETE  0x20
#define PT_LOG_EVT_CONNECT_COMPLETE 0x40
#define PT_LOG_EVT_CLOSE_COMPLETE   0x80

/* ========================================================================== */
/* TCP Stream Structures (Hot/Cold Split)                                      */
/* ========================================================================== */

/**
 * Hot path data - checked every poll loop iteration (~14 bytes)
 *
 * DOD: Fields ordered to ensure int16_t at even offsets (68k 2-byte alignment).
 * All 4-byte fields first, then 2-byte fields, then 1-byte fields.
 *
 * MANDATORY: Use int8_t peer_idx instead of struct pt_peer* pointer.
 * This saves 3 bytes per stream (24 bytes for 8 streams) and eliminates
 * a pointer dereference in the hot poll path. For 68030's 256-byte cache,
 * this is a significant improvement. Trade-off: requires bounds checking
 * but improves cache locality. Use -1 for "no peer".
 */
typedef struct pt_tcp_stream_hot {
    StreamPtr         stream;           /* 4 bytes - offset 0 */
    volatile int16_t  async_result;     /* 2 bytes - offset 4 */
    volatile int16_t  log_error_code;   /* 2 bytes - offset 6 (ISR-safe logging) */
    pt_stream_state   state;            /* 1 byte  - offset 8 */
    pt_asr_flags      asr_flags;        /* 1 byte  - offset 9 */
    volatile uint8_t  async_pending;    /* 1 byte  - offset 10 */
    uint8_t           rds_outstanding;  /* 1 byte  - offset 11 */
    volatile uint8_t  log_events;       /* 1 byte  - offset 12 (ISR-safe logging) */
    int8_t            peer_idx;         /* 1 byte  - offset 13 (-1 = no peer) */
    /* Total: 14 bytes, 2-byte aligned, fits in minimal cache line */
    /* Polling 8 streams loads ~112 bytes (was ~144 with pointer) */
} pt_tcp_stream_hot;

/**
 * Cold path data - accessed during setup, I/O, teardown
 */
typedef struct pt_tcp_stream_cold {
    TCPiopb           pb;               /* Parameter block for calls (~100 bytes) */

    /* Buffer management (must be locked, non-relocatable) */
    Ptr               rcv_buffer;       /* Passed to TCPCreate */
    unsigned long     rcv_buffer_size;

    /* For TCPNoCopyRcv - Read Data Structure */
    rdsEntry          rds[6];           /* 6 entries for multi-buffer receive */

    /* Connection info */
    ip_addr           local_ip;
    tcp_port          local_port;
    ip_addr           remote_ip;
    tcp_port          remote_port;

    /* Close timeout tracking */
    unsigned long     close_start;      /* Ticks when close was initiated */

    /* From TCPTerminate ASR - reason codes from MacTCP.h:
     * TCPRemoteAbort=2, TCPNetworkFailure=3, TCPSecPrecMismatch=4,
     * TCPULPTimeoutTerminate=5, TCPULPAbort=6, TCPULPClose=7, TCPServiceError=8
     */
    volatile unsigned short terminate_reason;

    /* User data for callbacks (points back to hot struct for ASR access) */
    Ptr               user_data;
} pt_tcp_stream_cold;

/* Helper macro to get peer from index (bounds-checked) */
#define PT_PEER_FROM_IDX(ctx, idx) \
    (((idx) >= 0 && (idx) < (int)(ctx)->max_peers) ? &(ctx)->peers[(idx)] : NULL)

/* ========================================================================== */
/* UDP Stream Structures (Hot/Cold Split)                                      */
/* ========================================================================== */

/**
 * Hot path data - checked every poll loop iteration (~10 bytes)
 * DOD: Fields ordered largest-to-smallest to eliminate alignment padding.
 */
typedef struct pt_udp_stream_hot {
    StreamPtr         stream;           /* 4 bytes - offset 0 */
    volatile int16_t  async_result;     /* 2 bytes - offset 4 */
    pt_stream_state   state;            /* 1 byte  - offset 6 */
    pt_asr_flags      asr_flags;        /* 1 byte  - offset 7 */
    volatile uint8_t  async_pending;    /* 1 byte  - offset 8 */
    volatile uint8_t  data_ready;       /* 1 byte  - offset 9 */
    /* Total: 10 bytes, no padding */
} pt_udp_stream_hot;

/**
 * Cold path data - accessed during setup, I/O, teardown
 */
typedef struct pt_udp_stream_cold {
    UDPiopb           pb;               /* Parameter block (~80 bytes) */

    Ptr               rcv_buffer;
    unsigned long     rcv_buffer_size;

    ip_addr           local_ip;
    udp_port          local_port;

    /* Pending receive data */
    ip_addr           last_remote_ip;
    udp_port          last_remote_port;
    Ptr               last_data;
    unsigned short    last_data_len;

    Ptr               user_data;
} pt_udp_stream_cold;

/* ========================================================================== */
/* Buffer Size Constants                                                       */
/* ========================================================================== */

/**
 * Buffer size recommendations from MacTCP Programmer's Guide.
 *
 * TCP (per MPG): "minimum of 4096 bytes", "at least 8192 bytes is recommended"
 *                for character apps, "16 KB is recommended" for block apps.
 *                Formula: optimal = 4 × MTU + 1024
 *
 * UDP (per MPG): "minimum allowed size is 2048 bytes, but it should be at
 *                least 2N + 256 bytes where N is the size in bytes of the
 *                largest UDP datagram you expect to receive"
 *
 * For Mac SE 4MB: Use conservative sizes to leave room for heap operations.
 * For Performa 6200 8MB+: Can use larger buffers for better throughput.
 */
#define PT_TCP_RCV_BUF_MIN      4096   /* Absolute minimum per MacTCP */
#define PT_TCP_RCV_BUF_CHAR     8192   /* "at least 8192 bytes is recommended" */
#define PT_TCP_RCV_BUF_BLOCK    16384  /* "16 KB is recommended" for block apps */
#define PT_TCP_RCV_BUF_MAX      65536  /* Up to 128KB can be useful but overkill */

#define PT_UDP_RCV_BUF_MIN      2048   /* Actual minimum per MacTCP */
#define PT_UDP_RCV_BUF_SIZE     1408   /* 2×576 + 256 = safe for max UDP datagram */

/**
 * Memory thresholds for buffer sizing (conservative for 4MB Mac SE).
 * FreeMem() returns bytes available in application heap.
 */
#define PT_MEM_PLENTY           (2048L * 1024L)  /* >2MB: use optimal sizing */
#define PT_MEM_MODERATE         (1024L * 1024L)  /* 1-2MB: use character app size */
#define PT_MEM_LOW              (512L * 1024L)   /* 512K-1MB: use minimum */
#define PT_MEM_CRITICAL         (256L * 1024L)   /* <512K: warn and use minimum */

/**
 * Maximum simultaneous streams (MacTCP limit is 64 total system-wide).
 * For Mac SE 4MB: Recommend PT_MAX_PEERS = 4-6 to conserve memory.
 */
#define PT_MAX_TCP_STREAMS      (PT_MAX_PEERS + 1)  /* +1 for listener */

/* ========================================================================== */
/* Platform Context Extension                                                  */
/* ========================================================================== */

/**
 * MacTCP platform-specific context extension.
 *
 * DOD: Uses hot/cold split for TCP and UDP streams.
 * Hot arrays are contiguous for cache-efficient polling.
 * Cold arrays are separate and only accessed during I/O operations.
 *
 * This structure is appended to pt_context and accessed via pt_mactcp_get().
 */
typedef struct pt_mactcp_data {
    /* MacTCP driver reference */
    short             driver_refnum;
    short             reserved1;        /* Alignment padding */

    /* System limits (queried via TCPGlobalInfo at init) */
    unsigned short    max_tcp_connections;  /* System-wide limit */
    unsigned short    max_udp_streams;      /* System-wide limit */

    /* Local network info */
    ip_addr           local_ip;
    long              net_mask;

    /* Universal Procedure Pointers for callbacks
     * CRITICAL: MacTCP.h requires UPPs for all notifyProc and ioCompletion
     * fields. Raw function pointers will NOT work correctly.
     * Create once at init, dispose at shutdown.
     */
    TCPNotifyUPP      tcp_notify_upp;
    UDPNotifyUPP      udp_notify_upp;
    TCPIOCompletionUPP tcp_listen_completion_upp;
    TCPIOCompletionUPP tcp_connect_completion_upp;
    TCPIOCompletionUPP tcp_close_completion_upp;

    /* UDP discovery stream (hot/cold split) */
    pt_udp_stream_hot  discovery_hot;
    pt_udp_stream_cold discovery_cold;

    /* TCP listener stream (hot/cold split) */
    pt_tcp_stream_hot  listener_hot;
    pt_tcp_stream_cold listener_cold;

    /* Per-peer TCP streams (hot/cold split)
     *
     * DOD: Hot arrays are contiguous - polling all 8 streams loads
     * only ~112 bytes instead of ~1600 bytes with struct-of-arrays.
     */
    pt_tcp_stream_hot  tcp_hot[PT_MAX_PEERS];
    pt_tcp_stream_cold tcp_cold[PT_MAX_PEERS];

    /* Timing */
    unsigned long     last_announce_tick;
    unsigned long     ticks_per_second;  /* 60 on Mac */

} pt_mactcp_data;

/* ========================================================================== */
/* Accessor Functions                                                          */
/* ========================================================================== */

/**
 * Get MacTCP platform data from context.
 *
 * The platform data is allocated immediately after the pt_context struct.
 * This function is implemented in mactcp_driver.c because it requires
 * the full pt_context definition (from pt_internal.h).
 *
 * @param ctx  PeerTalk context
 * @return     Pointer to MacTCP platform data
 */
pt_mactcp_data *pt_mactcp_get(struct pt_context *ctx);

/**
 * Get hot stream struct for a peer index.
 */
static inline pt_tcp_stream_hot *pt_mactcp_get_tcp_hot(pt_mactcp_data *md, int idx)
{
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return NULL;
    return &md->tcp_hot[idx];
}

/**
 * Get cold stream struct for a peer index.
 */
static inline pt_tcp_stream_cold *pt_mactcp_get_tcp_cold(pt_mactcp_data *md, int idx)
{
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return NULL;
    return &md->tcp_cold[idx];
}

#endif /* PT_MACTCP_DEFS_H */
