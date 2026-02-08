/*
 * PeerTalk Internal Declarations
 * Platform abstraction, internal peer structures, context definition
 */

#ifndef PT_INTERNAL_H
#define PT_INTERNAL_H

#include "pt_types.h"
#include "pt_log.h"
#include "send.h"           /* Phase 3: pt_batch type */
#include "direct_buffer.h"  /* Tier 2: large message buffers */

/* ========================================================================== */
/* PT_Log Integration (Phase 0)                                              */
/* ========================================================================== */

/**
 * Convenience Macros for Context-Based Logging
 *
 * These macros extract the log handle from a pt_context pointer,
 * reducing boilerplate in PeerTalk internal code.
 *
 * Usage:
 *     PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK, "Connection failed: %d", err);
 *     PT_CTX_INFO(ctx, PT_LOG_CAT_INIT, "PeerTalk initialized");
 */
#define PT_CTX_ERR(ctx, cat, ...) \
    PT_LOG_ERR((ctx)->log, cat, __VA_ARGS__)

#define PT_CTX_WARN(ctx, cat, ...) \
    PT_LOG_WARN((ctx)->log, cat, __VA_ARGS__)

#define PT_CTX_INFO(ctx, cat, ...) \
    PT_LOG_INFO((ctx)->log, cat, __VA_ARGS__)

#define PT_CTX_DEBUG(ctx, cat, ...) \
    PT_LOG_DEBUG((ctx)->log, cat, __VA_ARGS__)

/* ========================================================================== */
/* Platform Abstraction Layer                                                */
/* ========================================================================== */

/**
 * Platform-specific operations
 */
typedef struct {
    int             (*init)(struct pt_context *ctx);
    void            (*shutdown)(struct pt_context *ctx);
    int             (*poll)(struct pt_context *ctx);
    pt_tick_t       (*get_ticks)(void);
    unsigned long   (*get_free_mem)(void);
    unsigned long   (*get_max_block)(void);
    int             (*send_udp)(struct pt_context *ctx, struct pt_peer *peer,
                                const void *data, uint16_t len);
} pt_platform_ops;

/* Platform ops implementations (defined in platform-specific files) */
#ifdef PT_PLATFORM_POSIX
extern pt_platform_ops pt_posix_ops;
#endif

#ifdef PT_PLATFORM_MACTCP
extern pt_platform_ops pt_mactcp_ops;
#endif

#ifdef PT_PLATFORM_OT
extern pt_platform_ops pt_ot_ops;
#endif

#if defined(PT_PLATFORM_APPLETALK) || defined(PT_HAS_APPLETALK)
extern pt_platform_ops pt_appletalk_ops;
#endif

/* ========================================================================== */
/* Peer Capability Structure                                                  */
/* ========================================================================== */

/**
 * Per-peer capability storage
 *
 * Stored in pt_peer_cold (rarely accessed after negotiation).
 * Effective max is cached in pt_peer_hot for fast send-path access.
 */
typedef struct {
    uint16_t max_message_size;   /* Peer's max (256-8192), 0=unknown */
    uint16_t preferred_chunk;    /* Optimal chunk size */
    uint16_t capability_flags;   /* PT_CAPFLAG_* */
    uint8_t  buffer_pressure;    /* 0-100 constraint level */
    uint8_t  caps_exchanged;     /* 1 after exchange complete */
} pt_peer_caps;

/* ========================================================================== */
/* Fragment Reassembly State                                                  */
/* ========================================================================== */

/**
 * Per-peer fragment reassembly state
 *
 * Uses existing recv_direct buffer for storage. Only one message
 * can be reassembled at a time per peer.
 */
typedef struct {
    uint16_t message_id;         /* Current message being reassembled */
    uint16_t total_length;       /* Expected total message size */
    uint16_t received_length;    /* Bytes received so far */
    uint8_t  active;             /* 1 if reassembly in progress */
    uint8_t  reserved;
} pt_reassembly_state;

/* ========================================================================== */
/* Internal Peer Address Structure                                           */
/* ========================================================================== */

#define PT_MAX_PEER_ADDRESSES 2

/**
 * Per-peer address entry
 */
typedef struct {
    uint32_t            address;        /* IP or synthesized AppleTalk address */
    uint16_t            port;
    uint16_t            transport;      /* PT_TRANSPORT_* */
} pt_peer_address;  /* 8 bytes */

/* ========================================================================== */
/* Internal Peer Structure - Hot/Cold Split                                  */
/* ========================================================================== */

/**
 * Hot peer data - accessed every poll cycle
 * Optimized for cache efficiency (designed for 68030 32-byte cache lines)
 */
typedef struct {
    void               *connection;     /* Platform-specific connection handle (Phase 5) */
    uint32_t            magic;          /* PT_PEER_MAGIC - validation */
    pt_tick_t           last_seen;      /* Last activity timestamp */
    PeerTalk_PeerID     id;
    uint16_t            peer_flags;     /* PT_PEER_FLAG_* from discovery */
    uint16_t            latency_ms;     /* Estimated RTT */
    uint16_t            effective_max_msg; /* min(ours, theirs) - cached for send path */
    pt_peer_state       state;
    uint8_t             address_count;
    uint8_t             preferred_transport;
    uint8_t             send_seq;       /* Send sequence number (Phase 2) */
    uint8_t             recv_seq;       /* Receive sequence number (Phase 2) */
    uint8_t             name_idx;       /* Index into context name table */
    uint8_t             reserved;       /* Padding for alignment */
} pt_peer_hot;

/**
 * Cold peer data - accessed infrequently
 */
typedef struct {
    char                name[PT_MAX_PEER_NAME + 1];     /* 32 bytes */
    PeerTalk_PeerInfo   info;                           /* ~20 bytes */
    pt_peer_address     addresses[PT_MAX_PEER_ADDRESSES];  /* 16 bytes */
    pt_tick_t           last_discovery;
    PeerTalk_PeerStats  stats;
    pt_tick_t           ping_sent_time;
    uint16_t            rtt_samples[8];     /* Rolling RTT samples */
    uint8_t             rtt_index;
    uint8_t             rtt_count;
    pt_peer_caps        caps;               /* Peer capability info */
    pt_reassembly_state reassembly;         /* Fragment reassembly state */
    uint8_t             obuf[PT_FRAME_BUF_SIZE];  /* Output framing buffer */
    uint8_t             ibuf[PT_FRAME_BUF_SIZE];  /* Input framing buffer */
    uint16_t            obuflen;
    uint16_t            ibuflen;
#ifdef PT_DEBUG
    uint32_t            obuf_canary;
    uint32_t            ibuf_canary;
#endif
} pt_peer_cold;

/**
 * Complete peer structure
 */
struct pt_peer {
    pt_peer_hot         hot;            /* 32 bytes - frequently accessed */
    pt_peer_cold        cold;           /* ~1.4KB - rarely accessed */
    struct pt_queue    *send_queue;     /* Tier 1: 256-byte slots for control messages */
    struct pt_queue    *recv_queue;     /* Tier 1: 256-byte slots for control messages */
    pt_direct_buffer    send_direct;    /* Tier 2: 4KB buffer for large outgoing messages */
    pt_direct_buffer    recv_direct;    /* Tier 2: 4KB buffer for large incoming messages */
};

/* ========================================================================== */
/* Internal Context Structure                                                */
/* ========================================================================== */

#define PT_MAX_PEER_ID  256

/**
 * PeerTalk context (opaque to public API)
 */
struct pt_context {
    uint32_t            magic;          /* PT_CONTEXT_MAGIC */
    PeerTalk_Config     config;
    PeerTalk_Callbacks  callbacks;
    pt_platform_ops    *plat;
    PeerTalk_PeerInfo   local_info;
    PeerTalk_GlobalStats global_stats;
    struct pt_peer     *peers;          /* Array of peers */

    /* O(1) Peer ID Lookup Table */
    uint8_t             peer_id_to_index[PT_MAX_PEER_ID];  /* 0xFF = invalid */

    /* Centralized Name Table */
    char                peer_names[PT_MAX_PEERS][PT_MAX_PEER_NAME + 1];

    uint32_t            next_message_id;
    uint32_t            peers_version;      /* Increments when peers added/removed */
    uint16_t            local_flags;
    uint16_t            max_peers;
    uint16_t            peer_count;
    PeerTalk_PeerID     next_peer_id;
    uint16_t            available_transports;
    uint16_t            active_transports;
    uint16_t            log_categories;
    uint8_t             discovery_active;
    uint8_t             listening;
    uint8_t             initialized;
    uint8_t             reserved_byte;

    PT_Log             *log;            /* PT_Log handle from Phase 0 */

    /* Phase 3: Pre-allocated batch buffer (avoids 1.4KB stack allocation) */
    pt_batch            send_batch;     /* For pt_drain_send_queue() */

    /* Two-tier message queue configuration */
    uint16_t            direct_threshold;   /* Messages > this go to Tier 2 (default 256) */
    uint16_t            direct_buffer_size; /* Tier 2 buffer size (default 4096) */

    /* Capability negotiation configuration */
    uint16_t            local_max_message;      /* Our max message size (0=8192) */
    uint16_t            local_preferred_chunk;  /* Our preferred chunk (0=1024) */
    uint16_t            local_capability_flags; /* Our PT_CAPFLAG_* */
    uint8_t             enable_fragmentation;   /* 1=auto-fragment (default 1) */
    uint8_t             reserved_cap;

    /* Platform-specific data follows (allocated via pt_plat_extra_size) */
};

/* ========================================================================== */
/* Validation Functions                                                       */
/* ========================================================================== */

/**
 * Validate context magic number (inline for performance)
 */
static inline int pt_context_valid(const struct pt_context *ctx)
{
    return ctx != NULL && ctx->magic == PT_CONTEXT_MAGIC;
}

/**
 * Validate peer magic number (inline for performance)
 */
static inline int pt_peer_valid(const struct pt_peer *peer)
{
    return peer != NULL && peer->hot.magic == PT_PEER_MAGIC;
}

/**
 * Validate context structure (full validation)
 */
int pt_validate_context(struct pt_context *ctx);

/**
 * Validate peer structure (full validation)
 */
int pt_validate_peer(struct pt_peer *peer);

/**
 * Validate configuration
 */
int pt_validate_config(const PeerTalk_Config *config);

/* Validation macros for debug builds */
#ifdef PT_DEBUG
    #define PT_VALIDATE_CONTEXT(ctx) \
        do { \
            if (!pt_context_valid(ctx)) { \
                PT_Log_Err(ctx ? ctx->log : NULL, PT_LOG_CORE, \
                           "Invalid context magic: 0x%08X", \
                           ctx ? ctx->magic : 0); \
                return PT_ERR_INVALID_PARAM; \
            } \
        } while (0)

    #define PT_VALIDATE_PEER(peer) \
        do { \
            if (!pt_peer_valid(peer)) { \
                return PT_ERR_INVALID_PARAM; \
            } \
        } while (0)
#else
    #define PT_VALIDATE_CONTEXT(ctx) ((void)0)
    #define PT_VALIDATE_PEER(peer)   ((void)0)
#endif

/* ========================================================================== */
/* Peer Management Functions                                                 */
/* ========================================================================== */

/**
 * O(1) peer lookup by ID
 *
 * Returns NULL if peer not found
 */
static inline struct pt_peer *pt_find_peer_by_id(struct pt_context *ctx, PeerTalk_PeerID id)
{
    uint8_t index;

    if (id == 0 || id >= PT_MAX_PEER_ID)
        return NULL;

    index = ctx->peer_id_to_index[id];
    if (index == 0xFF || index >= ctx->peer_count)
        return NULL;

    return &ctx->peers[index];
}

/**
 * Linear scan for address lookup (called rarely)
 */
struct pt_peer *pt_find_peer_by_address(struct pt_context *ctx, uint32_t addr, uint16_t port);

/**
 * Linear scan for name lookup (cold path)
 */
struct pt_peer *pt_find_peer_by_name(struct pt_context *ctx, const char *name);

/**
 * Allocate peer slot (uses swap-back removal for O(1) allocation)
 */
struct pt_peer *pt_alloc_peer(struct pt_context *ctx);

/**
 * Free peer slot (uses swap-back removal for O(1) deallocation)
 *
 * Algorithm:
 * 1. Copy last peer to removed slot
 * 2. Update peer_id_to_index for moved peer's ID
 * 3. Decrement peer_count
 * 4. Invalidate old last slot
 *
 * IMPORTANT: Peer ordering is NOT preserved. Iterate by peer_id if ordering matters.
 */
void pt_free_peer(struct pt_context *ctx, struct pt_peer *peer);

/* ========================================================================== */
/* Name Table Access                                                          */
/* ========================================================================== */

/**
 * Get peer name from centralized table
 */
const char *pt_get_peer_name(struct pt_context *ctx, uint8_t name_idx);

/**
 * Allocate name slot in centralized table
 */
uint8_t pt_alloc_peer_name(struct pt_context *ctx, const char *name);

/**
 * Free name slot in centralized table
 */
void pt_free_peer_name(struct pt_context *ctx, uint8_t name_idx);

/* ========================================================================== */
/* Platform-Specific Allocation                                              */
/* ========================================================================== */

/**
 * Platform-agnostic memory allocation
 */
void *pt_plat_alloc(size_t size);

/**
 * Platform-agnostic memory deallocation
 */
void pt_plat_free(void *ptr);

/**
 * Platform-specific extra context size
 */
size_t pt_plat_extra_size(void);

#endif /* PT_INTERNAL_H */
