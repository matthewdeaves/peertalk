/*
 * PeerTalk - Cross-platform peer-to-peer networking library
 * Public API
 *
 * Supports:
 *   - POSIX (Linux/macOS) - TCP/UDP via BSD sockets
 *   - Classic Mac MacTCP (System 6-7.5, 68k) - TCP/UDP via MacTCP
 *   - Classic Mac Open Transport (System 7.6+, PPC/68k) - TCP/UDP via OT
 *   - Classic Mac AppleTalk (all systems) - ADSP/NBP
 */

#ifndef PEERTALK_H
#define PEERTALK_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Platform-Specific Type Definitions                                        */
/* ========================================================================== */

/*
 * Classic Mac compilers may not have stdint.h. Provide fallback definitions
 * with include guards to prevent redefinition conflicts with Retro68 headers.
 */
#if defined(__APPLE__) && defined(__MACH__) && !defined(__STDC_VERSION__)
    /* Classic Mac (pre-C99) */
    #ifndef _UINT8_T
        #define _UINT8_T
        typedef unsigned char uint8_t;
    #endif
    #ifndef _INT8_T
        #define _INT8_T
        typedef signed char int8_t;
    #endif
    #ifndef _UINT16_T
        #define _UINT16_T
        typedef unsigned short uint16_t;
    #endif
    #ifndef _INT16_T
        #define _INT16_T
        typedef signed short int16_t;
    #endif
    #ifndef _UINT32_T
        #define _UINT32_T
        typedef unsigned long uint32_t;
    #endif
    #ifndef _INT32_T
        #define _INT32_T
        typedef signed long int32_t;
    #endif
#else
    /* Modern platforms (C99+) */
    #include <stdint.h>
#endif

/* ========================================================================== */
/* Version Information                                                        */
/* ========================================================================== */

#define PEERTALK_VERSION_MAJOR  1
#define PEERTALK_VERSION_MINOR  0
#define PEERTALK_VERSION_PATCH  0

#define PT_VERSION_STRING       "1.0.0"

/**
 * Returns the PeerTalk version string
 */
const char *PeerTalk_Version(void);

/* ========================================================================== */
/* Configuration Constants                                                    */
/* ========================================================================== */

#define PT_MAX_PEER_NAME        31      /* Max peer name length (excl. null) */
#define PT_MAX_PEERS            16      /* Default max peer slots */
#define PT_MAX_MESSAGE_SIZE     8192    /* Max TCP message size */
#define PT_MAX_UDP_MESSAGE_SIZE 512     /* Max UDP message size */
#define PT_MAX_BATCH_SIZE       16      /* Max messages per batch callback */

#define PT_DEFAULT_DISCOVERY_PORT   7353
#define PT_DEFAULT_TCP_PORT         7354
#define PT_DEFAULT_UDP_PORT         7355

/* ========================================================================== */
/* Transport Types                                                            */
/* ========================================================================== */

/**
 * Transport mechanisms (bitmask)
 */
typedef enum {
    PT_TRANSPORT_NONE       = 0x00,
    PT_TRANSPORT_TCP        = 0x01,     /* Reliable stream (POSIX/MacTCP/OT) */
    PT_TRANSPORT_UDP        = 0x02,     /* Unreliable datagram (POSIX/MacTCP/OT) */
    PT_TRANSPORT_ADSP       = 0x04,     /* AppleTalk Data Stream Protocol */
    PT_TRANSPORT_NBP        = 0x08,     /* AppleTalk Name Binding Protocol */
    PT_TRANSPORT_APPLETALK  = 0x0C,     /* ADSP | NBP */
    PT_TRANSPORT_ALL        = 0xFF
} PeerTalk_Transport;

/**
 * Returns bitmask of available transports on current platform
 */
uint16_t PeerTalk_GetAvailableTransports(void);

/* ========================================================================== */
/* Error Codes                                                                */
/* ========================================================================== */

typedef enum {
    PT_OK                       = 0,

    /* Parameter & State Errors */
    PT_ERR_INVALID_PARAM        = -1,
    PT_ERR_NO_MEMORY            = -2,
    PT_ERR_NOT_INITIALIZED      = -3,
    PT_ERR_ALREADY_INITIALIZED  = -4,
    PT_ERR_INVALID_STATE        = -10,
    PT_ERR_NOT_SUPPORTED        = -17,

    /* Network Errors */
    PT_ERR_NETWORK              = -5,
    PT_ERR_TIMEOUT              = -6,
    PT_ERR_CONNECTION_REFUSED   = -7,
    PT_ERR_CONNECTION_CLOSED    = -8,
    PT_ERR_NO_NETWORK           = -13,
    PT_ERR_NOT_CONNECTED        = -18,
    PT_ERR_WOULD_BLOCK          = -19,

    /* Buffer & Queue Errors */
    PT_ERR_BUFFER_FULL          = -9,
    PT_ERR_QUEUE_EMPTY          = -15,
    PT_ERR_MESSAGE_TOO_LARGE    = -16,
    PT_ERR_BACKPRESSURE         = -25,

    /* Peer Errors */
    PT_ERR_PEER_NOT_FOUND       = -11,
    PT_ERR_DISCOVERY_ACTIVE     = -12,

    /* Protocol Errors (Phase 2) */
    PT_ERR_CRC                  = -20,
    PT_ERR_MAGIC                = -21,
    PT_ERR_TRUNCATED            = -22,
    PT_ERR_VERSION              = -23,
    PT_ERR_NOT_POWER2           = -24,

    /* System Errors */
    PT_ERR_PLATFORM             = -14,
    PT_ERR_RESOURCE             = -26,
    PT_ERR_INTERNAL             = -99
} PeerTalk_Error;

/* Aliases for convenience */
#define PT_ERR_INVALID      PT_ERR_INVALID_PARAM
#define PT_ERR_NOT_FOUND    PT_ERR_PEER_NOT_FOUND

/**
 * Returns human-readable error string
 */
const char *PeerTalk_ErrorString(PeerTalk_Error error);

/* ========================================================================== */
/* Priority Levels                                                            */
/* ========================================================================== */

typedef enum {
    PT_PRIORITY_LOW         = 0,
    PT_PRIORITY_NORMAL      = 1,
    PT_PRIORITY_HIGH        = 2,
    PT_PRIORITY_CRITICAL    = 3
} PeerTalk_Priority;

/* ========================================================================== */
/* Send Flags                                                                 */
/* ========================================================================== */

#define PT_SEND_DEFAULT         0x00
#define PT_SEND_UNRELIABLE      0x01    /* Use UDP if available */
#define PT_SEND_COALESCABLE     0x02    /* Allow message coalescing */
#define PT_SEND_NO_DELAY        0x04    /* Disable Nagle algorithm */

/* ========================================================================== */
/* Coalesce Keys                                                              */
/* ========================================================================== */

/*
 * Keys 0x0000-0x00FF are reserved for PeerTalk
 * Keys 0x0100+ are available for application use
 */
#define PT_COALESCE_NONE        0x0000
#define PT_COALESCE_POSITION    0x0001  /* Position updates */
#define PT_COALESCE_STATE       0x0002  /* State sync messages */
#define PT_COALESCE_TYPING      0x0003  /* Typing indicators */

/* Create per-peer coalesce key */
#define PT_COALESCE_KEY(base, peer_id)  ((base) | ((peer_id) << 8))

/* ========================================================================== */
/* Peer Flags                                                                 */
/* ========================================================================== */

/*
 * Peer flags are split into:
 *   - 0x0001-0x000F: PeerTalk reserved (core roles)
 *   - 0x0010-0x0080: Reserved for future PeerTalk use
 *   - 0x0100-0x8000: Application-defined flags
 */
#define PT_PEER_FLAG_HOST       0x0001  /* Peer is session host */
#define PT_PEER_FLAG_ACCEPTING  0x0002  /* Peer accepting connections */
#define PT_PEER_FLAG_SPECTATOR  0x0004  /* Peer is spectator (read-only) */
#define PT_PEER_FLAG_READY      0x0008  /* Peer is ready (app-defined) */

/* Application-defined flags */
#define PT_PEER_FLAG_APP_0      0x0100
#define PT_PEER_FLAG_APP_1      0x0200
#define PT_PEER_FLAG_APP_2      0x0400
#define PT_PEER_FLAG_APP_3      0x0800
#define PT_PEER_FLAG_APP_4      0x1000
#define PT_PEER_FLAG_APP_5      0x2000
#define PT_PEER_FLAG_APP_6      0x4000
#define PT_PEER_FLAG_APP_7      0x8000

/* ========================================================================== */
/* Rejection Reasons                                                          */
/* ========================================================================== */

typedef enum {
    PT_REJECT_UNSPECIFIED       = 0,
    PT_REJECT_SERVER_FULL       = 1,
    PT_REJECT_BANNED            = 2,
    PT_REJECT_WRONG_VERSION     = 3,
    PT_REJECT_GAME_IN_PROGRESS  = 4
} PeerTalk_RejectReason;

/* ========================================================================== */
/* Core Types                                                                 */
/* ========================================================================== */

/**
 * Opaque context handle
 */
typedef struct pt_context PeerTalk_Context;

/**
 * Peer identifier (unique per session)
 */
typedef uint16_t PeerTalk_PeerID;

/* ========================================================================== */
/* Peer Information                                                           */
/* ========================================================================== */

/**
 * Peer information structure
 *
 * Layout optimized for cache efficiency on Classic Mac (68030 has 32-byte cache lines).
 * Hot fields (accessed during polling) are grouped first.
 */
typedef struct {
    /* Hot fields - accessed frequently during polling (20 bytes) */
    uint32_t        address;                /* IPv4 or pseudo-address for AppleTalk */
    PeerTalk_PeerID id;                     /* Unique peer ID */
    uint16_t        flags;                  /* PT_PEER_FLAG_* */
    uint16_t        transports_available;   /* Bitmask: how peer is reachable */
    uint16_t        transport_connected;    /* Which transport we're connected via */
    uint16_t        port;
    uint16_t        latency_ms;             /* Estimated RTT */
    uint16_t        queue_pressure;         /* Send queue fill 0-100 */
    uint8_t         connected;
    uint8_t         name_idx;               /* Index into context name table */
} PeerTalk_PeerInfo;

/**
 * Address structure for multi-transport peers
 */
typedef struct {
    uint16_t        transport;              /* PT_TRANSPORT_* */
    uint32_t        address;                /* Transport-specific address */
    uint16_t        port;
    uint16_t        reserved;
} PeerTalk_Address;

/* ========================================================================== */
/* Statistics                                                                 */
/* ========================================================================== */

/**
 * Per-peer statistics
 */
typedef struct {
    uint32_t        bytes_sent;
    uint32_t        bytes_received;
    uint32_t        messages_sent;
    uint32_t        messages_received;
    uint16_t        send_errors;
    uint16_t        receive_errors;
    uint16_t        dropped_messages;
    uint16_t        retransmissions;
    uint16_t        latency_ms;
    uint16_t        latency_variance_ms;
    uint8_t         send_queue_pressure;    /* 0-100 */
    uint8_t         recv_queue_pressure;    /* 0-100 */
    uint8_t         quality;                /* 0-100, 100=excellent */
    uint8_t         reserved;
} PeerTalk_PeerStats;

/**
 * Global statistics
 */
typedef struct {
    uint32_t        total_bytes_sent;
    uint32_t        total_bytes_received;
    uint32_t        total_messages_sent;
    uint32_t        total_messages_received;
    uint16_t        discovery_packets_sent;
    uint16_t        discovery_packets_received;
    uint16_t        peers_discovered;
    uint16_t        peers_connected;
    uint16_t        connections_accepted;
    uint16_t        connections_rejected;
    uint32_t        memory_used;
    uint16_t        streams_active;
    uint16_t        reserved;
} PeerTalk_GlobalStats;

/* ========================================================================== */
/* Configuration                                                              */
/* ========================================================================== */

/**
 * Configuration structure
 *
 * Zero values use defaults:
 *   - transports: PT_TRANSPORT_ALL
 *   - ports: PT_DEFAULT_* constants
 *   - max_peers: PT_MAX_PEERS (16)
 *   - buffer sizes: auto (platform-dependent)
 *   - discovery_interval: 5000ms
 *   - peer_timeout: 15000ms
 *   - auto_accept: 1 (enabled)
 *   - auto_cleanup: 1 (enabled)
 */
typedef struct {
    /* Embedded name - eliminates pointer indirection */
    char            local_name[PT_MAX_PEER_NAME + 1];   /* Required, max 31 chars + null */

    /* 16-bit fields grouped together */
    uint16_t        transports;             /* Bitmask: 0 = PT_TRANSPORT_ALL */
    uint16_t        discovery_port;         /* 0 = 7353 */
    uint16_t        tcp_port;               /* 0 = 7354 */
    uint16_t        udp_port;               /* 0 = 7355 */
    uint16_t        max_peers;              /* 0 = 16 */
    uint16_t        recv_buffer_size;       /* 0 = auto */
    uint16_t        send_buffer_size;       /* 0 = auto */
    uint16_t        discovery_interval;     /* ms, 0 = 5000 */
    uint16_t        peer_timeout;           /* ms, 0 = 15000 */

    /* 8-bit fields grouped together */
    uint8_t         auto_accept;            /* Auto-accept connections, default = 1 */
    uint8_t         auto_cleanup;           /* Auto-remove timed-out peers, default = 1 */
    uint8_t         log_level;              /* 0=off, 1=err, 2=warn, 3=info, 4=debug */
    uint8_t         reserved;
} PeerTalk_Config;

/* ========================================================================== */
/* Callbacks                                                                  */
/* ========================================================================== */

/*
 * IMPORTANT: All callbacks are invoked from PeerTalk_Poll() which runs in the
 * MAIN EVENT LOOP, NOT from interrupt context. Therefore, callbacks MAY:
 *   - Allocate memory (NewPtr, malloc)
 *   - Call File Manager (FSRead, FSWrite)
 *   - Call PT_Log functions
 *   - Call any Toolbox routine
 *   - Block (though this delays other events)
 */

/**
 * Peer discovered via broadcast
 */
typedef void (*PeerTalk_PeerDiscoveredCB)(
    PeerTalk_Context *ctx,
    const PeerTalk_PeerInfo *peer,
    void *user_data);

/**
 * Peer lost (timeout or explicit removal)
 */
typedef void (*PeerTalk_PeerLostCB)(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    void *user_data);

/**
 * Peer connected successfully
 */
typedef void (*PeerTalk_PeerConnectedCB)(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    void *user_data);

/**
 * Peer disconnected
 */
typedef void (*PeerTalk_PeerDisconnectedCB)(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    PeerTalk_Error reason,
    void *user_data);

/**
 * Single message received (TCP/reliable)
 */
typedef void (*PeerTalk_MessageReceivedCB)(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID from_peer,
    const void *data,
    uint16_t length,
    void *user_data);

/**
 * Single UDP message received
 */
typedef void (*PeerTalk_UDPReceivedCB)(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID from_peer,
    uint32_t from_address,
    uint16_t from_port,
    const void *data,
    uint16_t length,
    void *user_data);

/**
 * Connection request received (return 1 to accept, 0 to reject)
 */
typedef int (*PeerTalk_ConnectionRequestedCB)(
    PeerTalk_Context *ctx,
    const PeerTalk_PeerInfo *peer,
    void *user_data);

/**
 * Message send completed
 */
typedef void (*PeerTalk_MessageSentCB)(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    uint32_t message_id,
    PeerTalk_Error result,
    void *user_data);

/**
 * Batch message structure (for high-frequency messages)
 */
typedef struct {
    const void     *data;               /* Pointer first for alignment */
    PeerTalk_PeerID from_peer;
    uint16_t        length;
} PeerTalk_MessageBatch;

/**
 * Batch UDP message structure
 */
typedef struct {
    const void     *data;               /* Pointer first for alignment */
    uint32_t        from_address;
    PeerTalk_PeerID from_peer;
    uint16_t        from_port;
    uint16_t        length;
    uint16_t        reserved;           /* Explicit padding */
} PeerTalk_UDPBatch;

/**
 * Batch message callback (invoked once per poll with up to PT_MAX_BATCH_SIZE messages)
 */
typedef void (*PeerTalk_MessageBatchCB)(
    PeerTalk_Context *ctx,
    const PeerTalk_MessageBatch *messages,
    uint16_t count,
    void *user_data);

/**
 * Batch UDP callback
 */
typedef void (*PeerTalk_UDPBatchCB)(
    PeerTalk_Context *ctx,
    const PeerTalk_UDPBatch *messages,
    uint16_t count,
    void *user_data);

/**
 * Callback structure
 *
 * Batch callbacks (on_message_batch, on_udp_batch) are preferred if set.
 * Per-event callbacks (on_message_received, on_udp_received) are used otherwise.
 */
typedef struct {
    /* Per-event callbacks */
    PeerTalk_PeerDiscoveredCB       on_peer_discovered;
    PeerTalk_PeerLostCB             on_peer_lost;
    PeerTalk_PeerConnectedCB        on_peer_connected;
    PeerTalk_PeerDisconnectedCB     on_peer_disconnected;
    PeerTalk_MessageReceivedCB      on_message_received;
    PeerTalk_UDPReceivedCB          on_udp_received;
    PeerTalk_ConnectionRequestedCB  on_connection_requested;
    PeerTalk_MessageSentCB          on_message_sent;

    /* Batch callbacks (preferred if set) */
    PeerTalk_MessageBatchCB         on_message_batch;
    PeerTalk_UDPBatchCB             on_udp_batch;

    void                           *user_data;
} PeerTalk_Callbacks;

/* ========================================================================== */
/* Lifecycle Functions                                                        */
/* ========================================================================== */

/**
 * Initialize PeerTalk with configuration
 *
 * Returns context handle on success, NULL on failure
 */
PeerTalk_Context *PeerTalk_Init(const PeerTalk_Config *config);

/**
 * Shutdown PeerTalk and free resources
 */
void PeerTalk_Shutdown(PeerTalk_Context *ctx);

/**
 * Poll for network events and invoke callbacks
 *
 * Should be called frequently from main event loop (e.g., 60Hz)
 */
PeerTalk_Error PeerTalk_Poll(PeerTalk_Context *ctx);

/**
 * Set callbacks
 */
PeerTalk_Error PeerTalk_SetCallbacks(
    PeerTalk_Context *ctx,
    const PeerTalk_Callbacks *callbacks);

/* ========================================================================== */
/* Discovery Functions                                                        */
/* ========================================================================== */

/**
 * Start discovery broadcasts
 */
PeerTalk_Error PeerTalk_StartDiscovery(PeerTalk_Context *ctx);

/**
 * Stop discovery broadcasts
 */
PeerTalk_Error PeerTalk_StopDiscovery(PeerTalk_Context *ctx);

/**
 * Get list of discovered peers
 *
 * Returns PT_OK on success, fills out_count with actual peer count
 */
PeerTalk_Error PeerTalk_GetPeers(
    PeerTalk_Context *ctx,
    PeerTalk_PeerInfo *peers,
    uint16_t max_peers,
    uint16_t *out_count);

/**
 * Get peer list version (increments when peers added/removed)
 *
 * Allows detecting changes without copying entire peer list
 */
uint32_t PeerTalk_GetPeersVersion(PeerTalk_Context *ctx);

/* ========================================================================== */
/* Peer Lookup Functions                                                      */
/* ========================================================================== */

/**
 * Get peer info by ID (returns pointer to internal structure, valid until next Poll)
 *
 * Returns NULL if peer not found
 */
const PeerTalk_PeerInfo *PeerTalk_GetPeerByID(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id);

/**
 * Get peer info by ID (copies to caller-provided structure)
 */
PeerTalk_Error PeerTalk_GetPeer(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    PeerTalk_PeerInfo *info);

/**
 * Get peer name by name index
 */
const char *PeerTalk_GetPeerName(PeerTalk_Context *ctx, uint8_t name_idx);

/**
 * Find peer by name
 *
 * Returns peer ID if found, 0 if not found
 */
PeerTalk_PeerID PeerTalk_FindPeerByName(
    PeerTalk_Context *ctx,
    const char *name,
    PeerTalk_PeerInfo *info);

/**
 * Find peer by address
 *
 * Returns peer ID if found, 0 if not found
 */
PeerTalk_PeerID PeerTalk_FindPeerByAddress(
    PeerTalk_Context *ctx,
    uint32_t address,
    uint16_t port,
    PeerTalk_PeerInfo *info);

/**
 * Get all addresses for a multi-transport peer
 *
 * Returns number of addresses filled (may be less than max_addresses)
 */
int PeerTalk_GetPeerAddresses(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    PeerTalk_Address *addresses,
    int max_addresses);

/* ========================================================================== */
/* Connection Functions                                                       */
/* ========================================================================== */

/**
 * Connect to discovered peer
 */
PeerTalk_Error PeerTalk_Connect(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id);

/**
 * Disconnect from peer
 */
PeerTalk_Error PeerTalk_Disconnect(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id);

/**
 * Reject incoming connection
 */
PeerTalk_Error PeerTalk_RejectConnection(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    PeerTalk_RejectReason reason);

/* ========================================================================== */
/* Listen Control                                                             */
/* ========================================================================== */

/**
 * Start listening for incoming connections
 */
PeerTalk_Error PeerTalk_StartListening(PeerTalk_Context *ctx);

/**
 * Stop listening for incoming connections
 */
PeerTalk_Error PeerTalk_StopListening(PeerTalk_Context *ctx);

/**
 * Check if listening
 */
int PeerTalk_IsListening(PeerTalk_Context *ctx);

/**
 * Get listen port for transport
 */
uint16_t PeerTalk_GetListenPort(PeerTalk_Context *ctx, uint16_t transport);

/* ========================================================================== */
/* Messaging Functions (TCP/Reliable)                                        */
/* ========================================================================== */

/**
 * Send message to peer (reliable TCP/ADSP)
 */
PeerTalk_Error PeerTalk_Send(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    const void *data,
    uint16_t length);

/**
 * Send with priority and flags
 */
PeerTalk_Error PeerTalk_SendEx(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    const void *data,
    uint16_t length,
    uint8_t priority,
    uint8_t flags,
    uint16_t coalesce_key);

/**
 * Send via specific transport
 */
PeerTalk_Error PeerTalk_SendVia(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    const void *data,
    uint16_t length,
    uint16_t transport,
    uint8_t priority,
    uint8_t flags,
    uint16_t coalesce_key);

/**
 * Send with message ID tracking (for on_message_sent callback)
 */
PeerTalk_Error PeerTalk_SendTracked(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    const void *data,
    uint16_t length,
    uint32_t *out_message_id);

/**
 * Broadcast to all connected peers
 */
PeerTalk_Error PeerTalk_Broadcast(
    PeerTalk_Context *ctx,
    const void *data,
    uint16_t length);

/* ========================================================================== */
/* Messaging Functions (UDP/Unreliable)                                      */
/* ========================================================================== */

/**
 * Send unreliable UDP message to peer
 */
PeerTalk_Error PeerTalk_SendUDP(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    const void *data,
    uint16_t length);

/**
 * Broadcast unreliable UDP to all peers
 */
PeerTalk_Error PeerTalk_BroadcastUDP(
    PeerTalk_Context *ctx,
    const void *data,
    uint16_t length);

/* ========================================================================== */
/* Queue Status                                                               */
/* ========================================================================== */

/**
 * Get send queue status for peer
 */
PeerTalk_Error PeerTalk_GetQueueStatus(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    uint16_t *out_pending,
    uint16_t *out_available);

/* ========================================================================== */
/* Statistics                                                                 */
/* ========================================================================== */

/**
 * Get per-peer statistics
 */
PeerTalk_Error PeerTalk_GetPeerStats(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    PeerTalk_PeerStats *stats);

/**
 * Get global statistics
 */
PeerTalk_Error PeerTalk_GetGlobalStats(
    PeerTalk_Context *ctx,
    PeerTalk_GlobalStats *stats);

/**
 * Reset statistics for peer (or all peers if peer_id == 0)
 */
PeerTalk_Error PeerTalk_ResetStats(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id);

/* ========================================================================== */
/* Peer Flags                                                                 */
/* ========================================================================== */

/**
 * Set local peer flags
 */
PeerTalk_Error PeerTalk_SetFlags(PeerTalk_Context *ctx, uint16_t flags);

/**
 * Get local peer flags
 */
uint16_t PeerTalk_GetFlags(PeerTalk_Context *ctx);

/**
 * Modify local peer flags (set and clear in one operation)
 */
PeerTalk_Error PeerTalk_ModifyFlags(
    PeerTalk_Context *ctx,
    uint16_t set_flags,
    uint16_t clear_flags);

/* ========================================================================== */
/* Utility Functions                                                          */
/* ========================================================================== */

/**
 * Get local peer info
 */
PeerTalk_Error PeerTalk_GetLocalInfo(
    PeerTalk_Context *ctx,
    PeerTalk_PeerInfo *out_info);

#ifdef __cplusplus
}
#endif

#endif /* PEERTALK_H */
