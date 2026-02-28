/**
 * @file at_defs.h
 * @brief AppleTalk platform types and constants
 *
 * Defines all types needed for ADSP connections and NBP discovery
 * on classic AppleTalk (System 6+). Uses data-oriented hot/cold
 * separation for cache efficiency on 68k.
 *
 * Hot structs: Polled every frame, kept small for cache lines
 * Cold structs: Accessed during I/O, allocated separately
 */

#ifndef PT_APPLETALK_DEFS_H
#define PT_APPLETALK_DEFS_H

#if defined(PT_PLATFORM_APPLETALK)

/* System includes */
#include <AppleTalk.h>
#include <ADSP.h>
#include <string.h>

/* PeerTalk includes */
#include "pt_log.h"
#include "pt_types.h"

/* ========================================================================== */
/* Constants                                                                   */
/* ========================================================================== */

#define PT_ADSP_SEND_QUEUE_SIZE   2048   /* Minimum 100, recommended 2048 */
#define PT_ADSP_RECV_QUEUE_SIZE   2048   /* Minimum 100, recommended 2048 */
#define PT_ADSP_ATTN_BUF_SIZE     570    /* Exactly 570 bytes required */

/* NBP entity limits */
#define PT_NBP_OBJECT_MAX    32
#define PT_NBP_TYPE_MAX      32
#define PT_NBP_ZONE_MAX      32

/* PeerTalk NBP type - all peers register with this type */
#define PT_NBP_TYPE          "\pPeerTalk"

/* ========================================================================== */
/* Portable Bit Operations                                                     */
/*                                                                             */
/* ffs() and popcount are POSIX/GCC-specific and may not be available on       */
/* MPW or all Retro68 configurations. Provide fallbacks.                       */
/* ========================================================================== */

#ifndef pt_ffs
/* Find first set bit (1-indexed, returns 0 if no bits set) */
static inline int pt_ffs(unsigned int x) {
    int r = 1;
    if (!x) return 0;
    if (!(x & 0xFFFF)) { x >>= 16; r += 16; }
    if (!(x & 0xFF))   { x >>= 8;  r += 8; }
    if (!(x & 0xF))    { x >>= 4;  r += 4; }
    if (!(x & 0x3))    { x >>= 2;  r += 2; }
    if (!(x & 0x1))    { r += 1; }
    return r;
}
#endif

#ifndef pt_popcount
/* Count set bits in a 32-bit word */
static inline int pt_popcount(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555);
    x = (x & 0x33333333) + ((x >> 2) & 0x33333333);
    x = (x + (x >> 4)) & 0x0F0F0F0F;
    return (int)((x * 0x01010101) >> 24);
}
#endif

/* ========================================================================== */
/* ADSP States                                                                 */
/*                                                                             */
/* Use uint8_t instead of enum to save 3 bytes per connection.                 */
/* On 68k compilers, enums typically become int (4 bytes).                     */
/* ========================================================================== */

typedef uint8_t pt_adsp_state;
#define PT_ADSP_UNUSED          0
#define PT_ADSP_INITIALIZING    1
#define PT_ADSP_IDLE            2   /* CCB initialized, ready to listen/connect */
#define PT_ADSP_LISTENING       3   /* Passive open pending */
#define PT_ADSP_CONNECTING      4   /* Active open pending */
#define PT_ADSP_CONNECTED       5   /* Connection established */
#define PT_ADSP_CLOSING         6   /* Close in progress */
#define PT_ADSP_ERROR           7

/* ========================================================================== */
/* Event Flags (set by callbacks, cleared by poll loop)                        */
/*                                                                             */
/* Map to CCB userFlags bits from ADSP.h:                                      */
/*   eClosed    = 0x80 (bit 7) - connection closed                             */
/*   eTearDown  = 0x40 (bit 6) - connection broken                             */
/*   eAttention = 0x20 (bit 5) - attention message received                    */
/*   eFwdReset  = 0x10 (bit 4) - forward reset received                        */
/*                                                                             */
/* Note: There is NO "data arrived" userFlag. Data arrival is detected         */
/* through async dspRead completion or by polling recvQPending via dspStatus.  */
/* ========================================================================== */

#define PT_AT_FLAG_CONNECTION_CLOSED   0x01
#define PT_AT_FLAG_ATTENTION           0x02
#define PT_AT_FLAG_FWD_RESET           0x04
#define PT_AT_FLAG_ASYNC_COMPLETE      0x08

/* ========================================================================== */
/* Extended Parameter Block                                                    */
/*                                                                             */
/* ADSP ioCompletion receives A0 pointing to the parameter block.              */
/* We prepend a context pointer so we can recover our connection structure.     */
/*                                                                             */
/* Memory layout:                                                              */
/*   [context pointer (4 bytes)][DSPParamBlock...]                             */
/*   ^                          ^                                              */
/*   |                          A0 points here                                 */
/*   Structure start                                                           */
/* ========================================================================== */

typedef struct {
    void           *context;    /* 4-byte context pointer BEFORE param block */
    DSPParamBlock   pb;         /* Actual ADSP parameter block */
} pt_adsp_extended_pb;

/* Access context from parameter block pointer (A0) - for ioCompletion */
#define PT_ADSP_GET_CONTEXT(pb) \
    (((pt_adsp_extended_pb *)((char *)(pb) - sizeof(void *)))->context)

/* ========================================================================== */
/* ADSP Connection Structures - HOT/COLD SEPARATION                            */
/*                                                                             */
/* Hot struct: <32 bytes to fit in a cache line, polled every frame             */
/* Cold struct: Contains CCB, buffers, param blocks - allocated separately     */
/* ========================================================================== */

/**
 * pt_adsp_connection_hot - Polled every frame (14 bytes)
 *
 * Contains ONLY data accessed during the main poll loop.
 * Keep it small so iterating all connections fits in cache.
 *
 * Field ordering: Largest fields first to minimize padding.
 * AddrBlock is 4 bytes: {short aNet; char aNode; char aSocket}
 */
typedef struct pt_adsp_connection_hot {
    struct pt_peer         *peer;           /* 4 bytes: Associated peer */
    AddrBlock               remote_addr;    /* 4 bytes: Peer address */
    volatile uint16_t       flags;          /* 2 bytes: Event flags */
    volatile short          async_result;   /* 2 bytes: Result from callback */
    pt_adsp_state           state;          /* 1 byte: Current state */
    uint8_t                 slot_index;     /* 1 byte: Index into cold array */
} pt_adsp_connection_hot;  /* Total: 14 bytes, no padding */

/**
 * pt_adsp_connection_cold - Accessed only during I/O operations
 *
 * IMPORTANT: TRCCB ccb MUST be first member so we can recover connection
 * from CCB pointer in userRoutine callback (which receives TPCCB, not pb).
 */
typedef struct pt_adsp_connection_cold {
    /* CCB - MUST be first for userRoutine context recovery */
    TRCCB               ccb;
    short               ccb_refnum;

    /* Buffers - all must be locked (NewPtrClear) */
    Ptr                 send_queue;
    Ptr                 recv_queue;
    Ptr                 attn_buffer;

    /* Extended parameter block for ioCompletion */
    pt_adsp_extended_pb epb;

    /* Connection info */
    AddrBlock           local_addr;

    /* Back-pointer to hot struct */
    struct pt_adsp_connection_hot *hot;

    /* User data */
    Ptr                 user_data;
} pt_adsp_connection_cold;

/* ========================================================================== */
/* Listener Structures - HOT/COLD SEPARATION                                   */
/* ========================================================================== */

/**
 * pt_adsp_listener_hot - Polled every frame (10 bytes)
 */
typedef struct pt_adsp_listener_hot {
    AddrBlock               remote_addr;        /* 4 bytes: Pending connection */
    volatile uint16_t       flags;              /* 2 bytes: Event flags */
    volatile short          async_result;       /* 2 bytes */
    pt_adsp_state           state;              /* 1 byte */
    volatile uint8_t        connection_pending; /* 1 byte */
} pt_adsp_listener_hot;  /* Total: 10 bytes */

/**
 * pt_adsp_listener_cold - Accessed during accept/deny operations
 *
 * IMPORTANT: TRCCB ccb MUST be first for userRoutine context recovery.
 */
typedef struct pt_adsp_listener_cold {
    TRCCB               ccb;
    short               ccb_refnum;
    pt_adsp_extended_pb epb;

    /* Sync fields from dspCLListen completion */
    unsigned short      remote_cid;
    unsigned long       send_seq;
    unsigned short      send_window;
    unsigned long       attn_send_seq;

    /* Back-pointer to hot struct */
    struct pt_adsp_listener_hot *hot;
} pt_adsp_listener_cold;

/* ========================================================================== */
/* NBP Discovery Structures - HOT/COLD SEPARATION                              */
/* ========================================================================== */

/**
 * pt_nbp_entry_hot - Minimal data for iteration (6 bytes)
 */
typedef struct {
    AddrBlock           addr;           /* 4 bytes: network:node:socket */
    uint8_t             slot_index;     /* 1 byte: Index into cold name array */
    uint8_t             _pad;           /* 1 byte: Alignment */
} pt_nbp_entry_hot;

/**
 * pt_nbp_entry_cold - Name string storage (accessed for display only)
 */
typedef struct {
    char                name[PT_NBP_OBJECT_MAX + 1];  /* 33 bytes: C string */
    uint8_t             _pad;                          /* 1 byte alignment */
} pt_nbp_entry_cold;

#define PT_NBP_MAX_ENTRIES 32
#define PT_NBP_LOOKUP_BUF_SIZE 1024

/**
 * pt_nbp_state_hot - Minimal polling data (4 bytes)
 */
typedef struct {
    uint8_t             entry_count;    /* Max 32 entries */
    uint8_t             registered;     /* Boolean */
    uint8_t             _pad[2];        /* Alignment */
} pt_nbp_state_hot;

/**
 * pt_nbp_state_cold - Allocated separately, accessed during NBP operations
 */
typedef struct {
    MPPParamBlock       pb;
    NamesTableEntry     nte;
    unsigned char       local_name[PT_NBP_OBJECT_MAX + 1]; /* Pascal string */
    unsigned char      *lookup_buf;     /* Pointer to 1KB buffer */
    pt_nbp_entry_hot   *entries;        /* Pointer to hot entry array */
    pt_nbp_entry_cold  *entry_names;    /* Pointer to cold name array */
    EntityName          temp_entity;    /* Reusable temp for NBP operations */
} pt_nbp_state_cold;

/* ========================================================================== */
/* Hot/Cold Access Helpers                                                     */
/* ========================================================================== */

/* Get cold connection from hot connection */
#define PT_AT_CONN_COLD(ctx, hot) \
    (&(ctx)->cold->connections[(hot)->slot_index])

/* Get hot connection from cold connection (via back-pointer) */
#define PT_AT_CONN_HOT(cold) ((cold)->hot)

/* Get cold listener from context */
#define PT_AT_LISTENER_COLD(ctx) (&(ctx)->cold->listener)

/* Get NBP cold state from context */
#define PT_AT_NBP_COLD(ctx) (&(ctx)->cold->nbp)

/* ========================================================================== */
/* Cold Data Block (allocated once at init)                                    */
/* ========================================================================== */

typedef struct pt_at_context_cold {
    /* NBP cold state */
    pt_nbp_state_cold   nbp;
    unsigned char       nbp_lookup_buf[PT_NBP_LOOKUP_BUF_SIZE];
    pt_nbp_entry_hot    nbp_entries[PT_NBP_MAX_ENTRIES];
    pt_nbp_entry_cold   nbp_entry_names[PT_NBP_MAX_ENTRIES];

    /* Listener cold state */
    pt_adsp_listener_cold listener;

    /* Connection cold array */
    pt_adsp_connection_cold connections[PT_MAX_PEERS];
} pt_at_context_cold;

/* ========================================================================== */
/* AppleTalk Platform Context                                                  */
/* ========================================================================== */

/**
 * pt_at_context - Main context, kept small for cache efficiency
 *
 * Contains only frequently-accessed data. Large buffers and CCBs
 * are in pt_at_context_cold, allocated separately.
 */
typedef struct pt_at_context {
    /* Logging */
    PT_Log             *log;

    /* Driver references */
    short               mpp_refnum;     /* .MPP driver refnum */
    short               dsp_refnum;     /* .DSP driver refnum */
    uint8_t             drivers_open;
    uint8_t             _pad1;

    /* NBP discovery state (hot) */
    pt_nbp_state_hot    nbp;

    /* Connection listener (hot) */
    pt_adsp_listener_hot listener;

    /* Active connections tracking - bitmask for O(active) polling */
#if PT_MAX_PEERS <= 32
    uint32_t            active_mask;
#else
    uint8_t             active_count;
    uint8_t             active_connections[PT_MAX_PEERS];
#endif

    /* Connection pool (hot data only) */
    pt_adsp_connection_hot connections[PT_MAX_PEERS];

    /* Pointer to cold data (allocated separately) */
    pt_at_context_cold *cold;

    /* Callback UPPs */
    ADSPCompletionUPP       completion_upp;
    ADSPCompletionUPP       listener_completion_upp;
    ADSPConnectionEventUPP  event_upp;
} pt_at_context;

/* ========================================================================== */
/* Function Prototypes                                                         */
/* ========================================================================== */

/* at_driver.c - Init, shutdown, local address */
int  pt_at_init(pt_at_context *ctx, PT_Log *log);
void pt_at_shutdown(pt_at_context *ctx);
int  pt_at_get_local_addr(pt_at_context *ctx, AddrBlock *addr);

/* nbp_appletalk.c - Name registration and discovery */
int  pt_nbp_register(pt_at_context *ctx, const char *peer_name, short socket);
int  pt_nbp_unregister(pt_at_context *ctx);
int  pt_nbp_lookup(pt_at_context *ctx);
int  pt_nbp_get_peers(pt_at_context *ctx, pt_nbp_entry_hot *entries,
                      int max_entries);
int  pt_nbp_get_peer_name(pt_at_context *ctx, uint8_t slot_index,
                           char *name_out, int max_len);
void pt_nbp_get_name(const EntityName *entity, char *name_out, int max_len);

/* adsp_appletalk.c - CCB management and connection pool */
int  pt_adsp_init_ccb(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                      short socket);
int  pt_adsp_remove_ccb(pt_at_context *ctx, pt_adsp_connection_hot *conn);
pt_adsp_connection_hot *pt_adsp_alloc(pt_at_context *ctx);
void pt_adsp_release(pt_at_context *ctx, pt_adsp_connection_hot *conn);

/* adsp_listen.c - Connection listener */
int  pt_adsp_listener_init(pt_at_context *ctx, short socket);
int  pt_adsp_listener_listen(pt_at_context *ctx);
int  pt_adsp_listener_accept(pt_at_context *ctx,
                              pt_adsp_connection_hot *conn);
int  pt_adsp_listener_deny(pt_at_context *ctx);
int  pt_adsp_listener_remove(pt_at_context *ctx);

/* adsp_connect.c - Active connections */
int  pt_adsp_connect(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                     AddrBlock *remote_addr);
int  pt_adsp_check_connect(pt_at_context *ctx,
                            pt_adsp_connection_hot *conn);
int  pt_adsp_close(pt_at_context *ctx, pt_adsp_connection_hot *conn);
int  pt_adsp_abort(pt_at_context *ctx, pt_adsp_connection_hot *conn);

/* adsp_io.c - Data I/O */
int  pt_adsp_write(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                   const void *data, unsigned short len, Boolean eom);
int  pt_adsp_write_check(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                          unsigned short *bytes_sent);
int  pt_adsp_read(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                  void *buffer, unsigned short buf_size);
int  pt_adsp_read_check(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                         unsigned short *bytes_received, Boolean *eom);
int  pt_adsp_get_status(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                         unsigned short *send_free,
                         unsigned short *recv_pending);
int  pt_adsp_attention(pt_at_context *ctx, pt_adsp_connection_hot *conn,
                        unsigned short code, const void *data,
                        unsigned short len);

/* poll_appletalk.c - Integration and poll loop */
void pt_at_poll(pt_at_context *ctx);
void pt_at_discover(pt_at_context *ctx);
int  pt_at_start(pt_at_context *ctx, PT_Log *log,
                 const char *local_name, short socket);
void pt_at_stop(pt_at_context *ctx);

#endif /* PT_PLATFORM_APPLETALK */

#endif /* PT_APPLETALK_DEFS_H */
