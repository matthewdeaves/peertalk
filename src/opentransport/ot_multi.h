/**
 * @file ot_multi.h
 * @brief Open Transport Multi-Transport Support Types
 *
 * Provides ADSP endpoint types, NBP mapper, and multi-transport context
 * for simultaneous TCP/IP and AppleTalk connectivity through Open Transport's
 * unified endpoint API.
 *
 * Hot/Cold separation is used for ADSP endpoints (same pattern as TCP):
 * - Hot data (~32 bytes) polled every frame, fits in cache line
 * - Cold data (~1.1KB) accessed only during I/O
 *
 * References:
 * - Networking With Open Transport (1997), Chapter 9: AppleTalk Services
 * - OpenTransportProviders.h (DDPAddress, NBPEntity, etc.)
 */

#ifndef PT_OT_MULTI_H
#define PT_OT_MULTI_H

#include "ot_defs.h"     /* Existing TCP/IP types, endpoint pool, flags */
#include "pt_types.h"

/* ========================================================================== */
/* ADSP Endpoint Structures (Hot/Cold Split)                                  */
/* ========================================================================== */

/**
 * ADSP Hot path data - checked every poll loop iteration (~32 bytes)
 *
 * Parallel structure to pt_tcp_endpoint_hot. Same field ordering for
 * alignment: 4-byte fields first, then 2-byte, then 1-byte.
 *
 * Cache line sizes: 68040 (16B), PPC 601 (32B L1).
 * With PT_MAX_PEERS=8, hot array = 256 bytes = fits in PPC L1 cache.
 */
typedef struct pt_adsp_endpoint_hot {
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
    /* Total: 32 bytes - matches TCP, fits in PPC cache line */
} pt_adsp_endpoint_hot;

/**
 * ADSP Cold path data - accessed during connection setup, I/O, teardown
 *
 * Contains OT addressing structures and receive buffer.
 * Allocated separately from hot data to avoid polluting cache.
 *
 * DDPAddress is used for AppleTalk DDP-level addressing.
 * NBPEntity stores the NBP name (object:type@zone) of the remote peer.
 */
typedef struct pt_adsp_endpoint_cold {
    /* ADSP addressing */
    DDPAddress          local_addr;     /* ~8 bytes - our DDP address */
    DDPAddress          remote_addr;    /* ~8 bytes - peer's DDP address */
    NBPEntity           remote_name;    /* ~100 bytes - peer's NBP name */

    /* Connection setup */
    TCall               call;           /* ~36 bytes - for OTConnect/OTAccept */

    /* Receive state */
    OTFlags             recv_flags;     /* 4 bytes - flags from OTRcv */
    uint8_t             recv_buf[1024]; /* 1024 bytes - staging buffer for OTRcv */
} pt_adsp_endpoint_cold;

/* ========================================================================== */
/* NBP Mapper for Discovery                                                   */
/* ========================================================================== */

/**
 * NBP (Name Binding Protocol) mapper for AppleTalk discovery.
 *
 * Uses OT's mapper API (OTOpenMapper, OTRegisterName, OTLookupName)
 * for peer discovery on AppleTalk networks.
 *
 * The lookup_reply_buf is pre-allocated in the struct rather than
 * on the stack because:
 * 1. 68k Macs have limited stack space (~32KB typical)
 * 2. A 2KB stack allocation risks stack overflow
 * 3. Pre-allocation avoids cache pollution during lookup
 */
typedef struct pt_nbp_mapper {
    MapperRef           ref;            /* NBP mapper reference */
    Boolean             registered;     /* Our name registered? */
    Boolean             lookup_pending; /* Lookup in progress? */
    Boolean             lookup_complete;/* Results ready to process? */
    uint8_t             _pad;           /* Alignment */

    /* Our registered name */
    NBPEntity           our_entity;     /* object:type@zone */
    OTNameID            name_id;        /* For OTDeleteNameByID */

    /* Lookup results */
    DDPAddress          lookup_addrs[PT_MAX_PEERS];
    NBPEntity           lookup_names[PT_MAX_PEERS];
    int                 lookup_count;

    /* Pre-allocated lookup reply buffer (avoids 2KB stack allocation) */
    UInt8               lookup_reply_buf[2048];
} pt_nbp_mapper;

/* ========================================================================== */
/* Multi-Transport Context Extension                                          */
/* ========================================================================== */

/**
 * Multi-transport OT platform context.
 *
 * Extends the TCP/IP-only pt_ot_data with ADSP endpoints and NBP discovery.
 * Uses the same hot/cold/pool patterns for cache efficiency.
 *
 * Memory layout optimized for polling:
 * 1. HOT DATA first (polled every frame)
 * 2. WARM DATA (accessed during endpoint creation/callbacks)
 * 3. COLD POINTERS (only dereferenced during actual I/O)
 * 4. COLDEST DATA (NBP mapper with 2KB buffer, at end)
 *
 * NOTE: This struct replaces pt_ot_data when multi-transport is enabled.
 * The TCP/IP fields mirror pt_ot_data for source compatibility.
 */
typedef struct pt_ot_multi_data {
    /* ====================================================================
     * HOT DATA - Polled every frame, grouped first for cache efficiency
     * On PPC 601 (32-byte cache lines), hot arrays should be contiguous.
     * ==================================================================== */

    /* Network info (from TCP/IP interface) */
    InetHost            local_ip;           /* Local IP (network byte order) */
    InetHost            net_mask;           /* Subnet mask */

    /* Configuration & pools (frequently accessed) */
    uint32_t            transports;         /* Enabled transports (PT_TRANSPORT_*) */
    pt_endpoint_pool    tcp_pool;           /* O(1) TCP allocation */
    pt_endpoint_pool    adsp_pool;          /* O(1) ADSP allocation */

    /* Hot endpoint data - polled every frame */
    pt_udp_endpoint_hot   udp_hot;                      /* 12 bytes */
    pt_tcp_endpoint_hot   tcp_listener_hot;              /* 32 bytes */
    pt_tcp_endpoint_hot   tcp_hot[PT_MAX_PEERS];         /* 32 * PT_MAX_PEERS */
    pt_adsp_endpoint_hot  adsp_listener_hot;             /* 32 bytes */
    pt_adsp_endpoint_hot  adsp_hot[PT_MAX_PEERS];        /* 32 * PT_MAX_PEERS */

    /* Discovery timing (checked every poll) */
    unsigned long       last_udp_announce;  /* Last UDP broadcast tick */
    unsigned long       last_nbp_lookup;    /* Last NBP lookup tick */

    /* ====================================================================
     * WARM DATA - Accessed occasionally (endpoint creation, callbacks)
     * ==================================================================== */

    /* Master OT configurations (cloned before each OTOpenEndpoint)
     * CRITICAL: OTOpenEndpoint disposes the config it receives,
     * so we must clone before each use. */
    OTConfigurationRef  tcp_config;
    OTConfigurationRef  udp_config;
    OTConfigurationRef  adsp_config;        /* "adsp(EnableEOM=1)" */

    /* Notifier UPPs (referenced during endpoint setup) */
    OTNotifyUPP         tcp_notifier_upp;
    OTNotifyUPP         udp_notifier_upp;
    OTNotifyUPP         adsp_notifier_upp;

    /* ====================================================================
     * COLD POINTERS - Only dereferenced during actual I/O
     * ==================================================================== */

    pt_udp_endpoint_cold  *udp_cold;          /* Allocated at init, ~2KB */
    pt_tcp_endpoint_cold  *tcp_listener_cold;
    pt_tcp_endpoint_cold  *tcp_cold;          /* Allocated: PT_MAX_PEERS * sizeof */
    pt_adsp_endpoint_cold *adsp_listener_cold;
    pt_adsp_endpoint_cold *adsp_cold;         /* Allocated: PT_MAX_PEERS * sizeof */

    /* ====================================================================
     * COLDEST DATA - Rarely accessed (discovery operations only)
     * NBP mapper contains 2KB lookup_reply_buf - keep at end to avoid
     * cache pollution when polling hot data.
     * ==================================================================== */

    pt_nbp_mapper       nbp;

} pt_ot_multi_data;

/* ========================================================================== */
/* Accessor Functions                                                         */
/* ========================================================================== */

/**
 * Get multi-transport OT data from context.
 * Same pattern as pt_ot_get() - allocated immediately after pt_context.
 */
pt_ot_multi_data *pt_ot_multi_get(struct pt_context *ctx);

/**
 * Get hot ADSP endpoint struct for a peer index.
 */
static inline pt_adsp_endpoint_hot *pt_ot_get_adsp_hot(
    pt_ot_multi_data *md, int idx)
{
    if (idx < 0 || idx >= PT_MAX_PEERS)
        return NULL;
    return &md->adsp_hot[idx];
}

/**
 * Get cold ADSP endpoint struct for a peer index.
 * Returns NULL if cold data not allocated or index out of range.
 */
static inline pt_adsp_endpoint_cold *pt_ot_get_adsp_cold(
    pt_ot_multi_data *md, int idx)
{
    if (idx < 0 || idx >= PT_MAX_PEERS || md->adsp_cold == NULL)
        return NULL;
    return &md->adsp_cold[idx];
}

/* ========================================================================== */
/* Internal API (called from poll dispatch)                                   */
/* ========================================================================== */

/* Initialization */
int  pt_ot_multi_init(struct pt_context *ctx);
void pt_ot_multi_shutdown(struct pt_context *ctx);

/* Discovery */
int  pt_ot_multi_start_discovery(struct pt_context *ctx);
void pt_ot_multi_stop_discovery(struct pt_context *ctx);

/* Polling (called from PeerTalk_Poll) */
int  pt_ot_multi_poll(struct pt_context *ctx);

/* Connection (routes to correct transport) */
int  pt_ot_multi_connect(struct pt_context *ctx, struct pt_peer *peer);
int  pt_ot_multi_disconnect(struct pt_context *ctx, struct pt_peer *peer);

/* Send (routes to correct transport) */
int  pt_ot_multi_send(struct pt_context *ctx, struct pt_peer *peer,
                      const void *data, uint16_t len);

/* ========================================================================== */
/* Configuration Constants                                                    */
/* ========================================================================== */

/** ADSP configuration string with EOM (end-of-message) support */
#define PT_OT_ADSP_CONFIG   "adsp(EnableEOM=1)"

/** NBP defaults */
#define PT_OT_NBP_TYPE_DEFAULT  "PeerTalk"
#define PT_OT_NBP_ZONE_DEFAULT  "*"

#endif /* PT_OT_MULTI_H */
