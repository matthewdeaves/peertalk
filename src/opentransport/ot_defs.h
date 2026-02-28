/**
 * @file ot_defs.h
 * @brief Open Transport Type Definitions for PeerTalk State Machine
 *
 * Data structures optimized for PPC/68040 cache efficiency using hot/cold split.
 * Hot structs are checked every poll (~32 bytes TCP, ~16 bytes UDP).
 * Cold structs contain large OT structures accessed during I/O.
 *
 * References:
 * - Networking With Open Transport (1997)
 * - OpenTransport.h, OpenTransportProviders.h (Retro68)
 */

#ifndef PT_OT_DEFS_H
#define PT_OT_DEFS_H

#include "pt_types.h"

/* Open Transport includes */
#include <OpenTransport.h>
#include <OpenTransportProviders.h>

/* Forward declaration */
struct pt_context;

/* ========================================================================== */
/* Endpoint States                                                             */
/* ========================================================================== */

/**
 * Endpoint state enum.
 *
 * Uses uint8_t for memory savings (same pattern as MacTCP pt_stream_state).
 * Maps loosely to OT T_* states but tracks PeerTalk-level transitions.
 */
typedef uint8_t pt_endpoint_state;
#define PT_EP_UNUSED      0   /* Slot available */
#define PT_EP_OPENING     1   /* OTOpenEndpoint issued (async) */
#define PT_EP_UNBOUND     2   /* Opened, not yet bound (T_UNBND) */
#define PT_EP_IDLE        3   /* Bound, ready for use (T_IDLE) */
#define PT_EP_OUTGOING    4   /* Outgoing connect in progress (T_OUTCON) */
#define PT_EP_INCOMING    5   /* Incoming connection pending (T_INCON) */
#define PT_EP_DATAXFER    6   /* Connected, data transfer (T_DATAXFER) */
#define PT_EP_CLOSING     7   /* Orderly disconnect in progress */

/**
 * Get human-readable name for endpoint state.
 * Useful for logging state transitions.
 */
static inline const char *pt_ep_state_name(pt_endpoint_state state)
{
    switch (state) {
    case PT_EP_UNUSED:     return "UNUSED";
    case PT_EP_OPENING:    return "OPENING";
    case PT_EP_UNBOUND:    return "UNBOUND";
    case PT_EP_IDLE:       return "IDLE";
    case PT_EP_OUTGOING:   return "OUTGOING";
    case PT_EP_INCOMING:   return "INCOMING";
    case PT_EP_DATAXFER:   return "DATAXFER";
    case PT_EP_CLOSING:    return "CLOSING";
    default:               return "UNKNOWN";
    }
}

/* ========================================================================== */
/* Notifier Event Flags                                                        */
/* ========================================================================== */

/**
 * Notifier event flag bit positions.
 *
 * Set from notifier using OTAtomicSetBit() (safe at deferred task time,
 * verified in Table C-1 of Networking With Open Transport).
 * Cleared from main poll loop using OTAtomicClearBit().
 *
 * Stored in volatile uint32_t per endpoint (4-byte aligned for atomics).
 */
#define PT_OT_FLAG_CONNECT_COMPLETE    0   /* T_CONNECT or T_PASSCON received */
#define PT_OT_FLAG_DATA_AVAILABLE      1   /* T_DATA: data ready to read */
#define PT_OT_FLAG_DISCONNECT          2   /* T_DISCONNECT: abortive disconnect */
#define PT_OT_FLAG_ORDERLY_RELEASE     3   /* T_ORDREL: orderly disconnect */
#define PT_OT_FLAG_ACCEPT_COMPLETE     4   /* T_ACCEPTCOMPLETE */
#define PT_OT_FLAG_SEND_COMPLETE       5   /* T_MEMORYRELEASED */
#define PT_OT_FLAG_LISTEN_PENDING      6   /* T_LISTEN: incoming connection */
#define PT_OT_FLAG_PASSCON             7   /* T_PASSCON: connection handed off */
#define PT_OT_FLAG_GODATA              8   /* T_GODATA: flow control lifted */
#define PT_OT_FLAG_BIND_COMPLETE       9   /* T_BINDCOMPLETE */
#define PT_OT_FLAG_UDERR_PENDING      10   /* T_UDERR: UDP error pending */

/**
 * Atomic flag macros using OT primitives.
 *
 * OTAtomicSetBit/ClearBit/TestBit operate on UInt8* with bit offsets.
 * For a uint32_t flags word, bit 0 is MSB of first byte (big-endian bit order).
 *
 * CRITICAL: These are ISR-safe (Table C-1). Use them in notifiers.
 */
#define PT_FLAG_SET(flags, bit)    OTAtomicSetBit((UInt8 *)&(flags), (bit))
#define PT_FLAG_CLEAR(flags, bit)  OTAtomicClearBit((UInt8 *)&(flags), (bit))
#define PT_FLAG_TEST(flags, bit)   OTAtomicTestBit((UInt8 *)&(flags), (bit))
#define PT_FLAGS_CLEAR_ALL(flags)  ((flags) = 0)

/**
 * Log event bits (set by notifier, cleared by main loop).
 *
 * Same pattern as MacTCP PT_LOG_EVT_* flags: notifier stores the error
 * code in a pre-allocated field and sets a log bit. Main loop checks
 * the bit, logs the event, and clears it.
 *
 * This avoids calling PT_Log from notifier context (NOT safe).
 */
#define PT_OT_LOG_EVT_CONNECT_DONE  0x01
#define PT_OT_LOG_EVT_LISTEN_DONE   0x02
#define PT_OT_LOG_EVT_ACCEPT_DONE   0x04
#define PT_OT_LOG_EVT_CLOSE_DONE    0x08
#define PT_OT_LOG_EVT_ERROR         0x10
#define PT_OT_LOG_EVT_DATA_IN       0x20
#define PT_OT_LOG_EVT_BIND_DONE     0x40

/* ========================================================================== */
/* TCP Endpoint Structures (Hot/Cold Split)                                    */
/* ========================================================================== */

/**
 * Hot path data - checked every poll loop iteration (~32 bytes)
 *
 * Fields ordered for alignment: 4-byte fields first, then 2-byte, then 1-byte.
 * Cache line sizes: 68040 (16B), PPC 601 (32B L1).
 * With PT_MAX_PEERS=8, hot array = 256 bytes = fits in PPC L1 cache.
 *
 * MANDATORY: Use uint8_t endpoint_idx instead of pointer.
 * Saves 3 bytes per endpoint and eliminates pointer chase in hot path.
 */
typedef struct pt_tcp_endpoint_hot {
    EndpointRef         ref;            /* 4 bytes - OT endpoint reference */
    volatile uint32_t   flags;          /* 4 bytes - notifier event flags (atomic) */
    OTResult            async_result;   /* 4 bytes - result from notifier */
    struct pt_peer     *peer;           /* 4 bytes - associated peer (NULL if unassigned) */
    unsigned long       close_start;    /* 4 bytes - tick when close initiated */
    volatile int32_t    log_error_code; /* 4 bytes - error code for deferred logging */
    volatile uint8_t    log_events;     /* 1 byte  - PT_LOG_EVT_* deferred log bits */
    pt_endpoint_state   state;          /* 1 byte  - PT_EP_* */
    uint8_t             endpoint_idx;   /* 1 byte  - index in pool (for notifier context) */
    uint8_t             _pad;           /* 1 byte  - alignment */
    /* Total: 32 bytes */
} pt_tcp_endpoint_hot;

/**
 * Cold path data - accessed during connection setup, I/O, teardown
 *
 * Contains OT TCall structures and receive buffers.
 * Allocated separately from hot data to avoid polluting cache.
 */
typedef struct pt_tcp_endpoint_cold {
    TCall               call;           /* ~36 bytes - for OTConnect/OTAccept */
    InetAddress         remote_addr;    /* ~8 bytes - remote peer address */
    InetAddress         local_addr;     /* ~8 bytes - local bound address */

    /* Listener-specific: pending connection info */
    TCall               pending_call;   /* ~36 bytes - for OTListen result */
    InetAddress         pending_addr;   /* ~8 bytes - pending caller address */

    /* Receive state */
    OTFlags             recv_flags;     /* 4 bytes - flags from OTRcv */
    uint8_t             recv_buf[1024]; /* 1024 bytes - staging buffer for OTRcv */
} pt_tcp_endpoint_cold;

/* ========================================================================== */
/* UDP Endpoint Structures (Hot/Cold Split)                                    */
/* ========================================================================== */

/**
 * Hot path data - checked every poll loop iteration (~16 bytes)
 */
typedef struct pt_udp_endpoint_hot {
    EndpointRef         ref;            /* 4 bytes */
    volatile uint32_t   flags;          /* 4 bytes - notifier flags (atomic) */
    pt_endpoint_state   state;          /* 1 byte */
    uint8_t             _pad[3];        /* 3 bytes - alignment */
    /* Total: 12 bytes, padded to 12 */
} pt_udp_endpoint_hot;

/**
 * Cold path data - accessed during I/O
 *
 * Contains TUnitData for OTSndUData/OTRcvUData and receive buffer.
 * Buffer sized for discovery packets (max 48 bytes) plus headroom.
 */
typedef struct pt_udp_endpoint_cold {
    TUnitData           udata;          /* ~24 bytes - for OTRcvUData */
    InetAddress         recv_addr;      /* ~8 bytes - source address of last recv */
    InetAddress         local_addr;     /* ~8 bytes - bound local address */
    uint8_t             recv_buf[2048]; /* 2048 bytes - UDP receive buffer */
} pt_udp_endpoint_cold;

/* ========================================================================== */
/* Endpoint Pool (O(1) Allocation via Bitmap)                                  */
/* ========================================================================== */

/**
 * Tracks free/allocated endpoint slots using a bitmap.
 *
 * Bit set (1) = slot is FREE.
 * Bit clear (0) = slot is IN USE.
 *
 * Allocation uses __builtin_ffs() on PPC (maps to cntlzw instruction)
 * or a fallback bit scan loop on 68k.
 *
 * Supports up to 32 endpoints (uint32_t bitmap).
 * PT_MAX_PEERS is typically 8-16, well within this limit.
 */
typedef struct pt_endpoint_pool {
    uint32_t            free_bitmap;    /* Bit set = free */
    uint8_t             count;          /* Currently allocated count */
    uint8_t             capacity;       /* Maximum slots (PT_MAX_PEERS) */
    uint8_t             _pad[2];        /* Alignment */
} pt_endpoint_pool;

/**
 * Initialize endpoint pool with all slots free.
 */
static inline void pt_endpoint_pool_init(pt_endpoint_pool *pool, uint8_t capacity)
{
    pool->capacity = capacity;
    pool->count = 0;
    /* Set bits 0..capacity-1 to indicate free */
    if (capacity >= 32)
        pool->free_bitmap = 0xFFFFFFFF;
    else
        pool->free_bitmap = (1UL << capacity) - 1;
}

/**
 * Allocate a slot from the pool.
 * @return Slot index (0..capacity-1), or -1 if pool is full.
 *
 * Logs warning at 75% capacity (caller should check).
 */
static inline int pt_endpoint_pool_alloc(pt_endpoint_pool *pool)
{
    int bit;

    if (pool->free_bitmap == 0)
        return -1;  /* Pool exhausted */

#if defined(__GNUC__)
    /* __builtin_ffs returns 1-based index of lowest set bit */
    bit = __builtin_ffs((int)pool->free_bitmap) - 1;
#else
    /* Fallback: linear scan */
    for (bit = 0; bit < (int)pool->capacity; bit++) {
        if (pool->free_bitmap & (1UL << bit))
            break;
    }
    if (bit >= (int)pool->capacity)
        return -1;
#endif

    pool->free_bitmap &= ~(1UL << bit);
    pool->count++;
    return bit;
}

/**
 * Free a slot back to the pool.
 */
static inline void pt_endpoint_pool_free(pt_endpoint_pool *pool, int idx)
{
    if (idx < 0 || idx >= (int)pool->capacity)
        return;
    pool->free_bitmap |= (1UL << idx);
    if (pool->count > 0)
        pool->count--;
}

/**
 * Check if a slot is in use.
 */
static inline int pt_endpoint_pool_in_use(const pt_endpoint_pool *pool, int idx)
{
    if (idx < 0 || idx >= (int)pool->capacity)
        return 0;
    return (pool->free_bitmap & (1UL << idx)) == 0;
}

/* ========================================================================== */
/* Platform Context Extension                                                  */
/* ========================================================================== */

/**
 * Open Transport platform-specific context extension.
 *
 * Layout mirrors MacTCP pt_mactcp_data for consistency:
 * - Network info
 * - UPPs (callback pointers)
 * - UDP discovery endpoint (hot/cold)
 * - TCP listener endpoint (hot/cold)
 * - Per-peer TCP endpoints (hot array + cold pointer)
 * - Timing state
 * - Master configurations
 *
 * Allocated immediately after pt_context via pt_plat_extra_size().
 * Cold data for TCP peers allocated separately at init to keep
 * the hot data contiguous for cache efficiency.
 */
typedef struct pt_ot_data {
    /* Network info */
    InetHost            local_ip;           /* Local IP (network byte order) */
    InetHost            net_mask;           /* Subnet mask (from InetInterfaceInfo) */

    /* Universal Procedure Pointers for callbacks.
     * CRITICAL: Create once at init, dispose AFTER closing all endpoints.
     * OT notifiers must use UPPs, not raw function pointers. */
    OTNotifyUPP         tcp_notifier_upp;
    OTNotifyUPP         udp_notifier_upp;

    /* UDP discovery endpoint (hot/cold split) */
    pt_udp_endpoint_hot  udp_hot;
    pt_udp_endpoint_cold *udp_cold;         /* Allocated at init, ~2KB */

    /* TCP listener endpoint (hot/cold split) */
    pt_tcp_endpoint_hot  listener_hot;
    pt_tcp_endpoint_cold *listener_cold;    /* Allocated at init, ~1.1KB */

    /* Per-peer TCP endpoint pool */
    pt_endpoint_pool     tcp_pool;
    pt_tcp_endpoint_hot  tcp_hot[PT_MAX_PEERS]; /* Contiguous hot array */
    pt_tcp_endpoint_cold *tcp_cold;         /* Allocated: PT_MAX_PEERS * sizeof */

    /* Timing */
    unsigned long       last_announce_tick;  /* Last discovery broadcast */

    /* Master configurations (cloned before each OTOpenEndpoint).
     * CRITICAL: OTOpenEndpoint disposes the config it receives,
     * so we must clone before each use. OTCloneConfiguration() is
     * ~5x faster than OTCreateConfiguration(). */
    OTConfigurationRef  tcp_config;
    OTConfigurationRef  udp_config;

} pt_ot_data;

/* ========================================================================== */
/* Accessor Functions                                                          */
/* ========================================================================== */

/**
 * Get OT platform data from context.
 *
 * The platform data is allocated immediately after the pt_context struct.
 * Implemented in ot_driver.c (requires full pt_context definition).
 *
 * @param ctx  PeerTalk context
 * @return     Pointer to OT platform data
 */
pt_ot_data *pt_ot_get(struct pt_context *ctx);

/**
 * Get hot endpoint struct for a peer index.
 */
static inline pt_tcp_endpoint_hot *pt_ot_get_tcp_hot(pt_ot_data *od, int idx)
{
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return NULL;
    return &od->tcp_hot[idx];
}

/**
 * Get cold endpoint struct for a peer index.
 * Returns NULL if cold data not allocated or index out of range.
 */
static inline pt_tcp_endpoint_cold *pt_ot_get_tcp_cold(pt_ot_data *od, int idx)
{
    if (idx < 0 || idx >= PT_MAX_PEERS || od->tcp_cold == NULL)
        return NULL;
    return &od->tcp_cold[idx];
}

/* ========================================================================== */
/* Configuration Constants                                                     */
/* ========================================================================== */

#define PT_OT_TCP_CONFIG    "tcp"
#define PT_OT_UDP_CONFIG    "udp"
#define PT_IP_STR_LEN       16          /* For OTInetHostToString */

/**
 * Maximum simultaneous TCP endpoints (peers + listener).
 * Mirrors MacTCP's PT_MAX_TCP_STREAMS.
 */
#define PT_MAX_OT_ENDPOINTS (PT_MAX_PEERS + 1)

#endif /* PT_OT_DEFS_H */
