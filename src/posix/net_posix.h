/*
 * PeerTalk POSIX Networking Layer
 *
 * Platform-specific networking implementation using BSD sockets.
 * Implements UDP discovery, TCP connections, and message I/O.
 */

#ifndef PT_NET_POSIX_H
#define PT_NET_POSIX_H

#include "pt_internal.h"
#include "protocol.h"
#include <sys/uio.h>     /* For struct iovec, writev() */
#include <sys/select.h>  /* For fd_set, FD_SET, etc. */

/* ========================================================================== */
/* Receive State Machine (Data-Oriented Design)                              */
/* ========================================================================== */

/**
 * Receive state - using uint8_t instead of enum for cache efficiency
 *
 * On 68030, enum uses 4 bytes. Using uint8_t reduces hot section from
 * 4 bytes to 1 byte, keeping per-peer state smaller.
 */
typedef uint8_t pt_recv_state;
#define PT_RECV_HEADER   0   /* Waiting for header bytes */
#define PT_RECV_PAYLOAD  1   /* Waiting for payload bytes */
#define PT_RECV_CRC      2   /* Waiting for CRC bytes */

/* ========================================================================== */
/* Per-Peer Receive Buffers (HOT/COLD Separation)                           */
/* ========================================================================== */

/**
 * HOT Section - checked every poll cycle per active connection (8 bytes)
 *
 * CRITICAL 68k ALIGNMENT: _pad0 ensures bytes_needed starts at even offset.
 * On 68000/68020/68030, uint16_t at odd offsets causes performance penalty
 * or crashes. This padding is NOT optional.
 *
 * With 16 peers, hot scan touches only 128 bytes (fits in 68030 L1 cache)
 * instead of 131KB if payload buffers were inline.
 */
typedef struct {
    pt_recv_state state;           /* 1 byte at offset 0 */
    uint8_t       _pad0;           /* 1 byte at offset 1: CRITICAL for 68k alignment */
    uint16_t      bytes_needed;    /* 2 bytes at offset 2 */
    uint16_t      bytes_received;  /* 2 bytes at offset 4 */
    uint16_t      _pad1;           /* 2 bytes at offset 6: align to 8 bytes */
} pt_recv_hot;  /* Total: 8 bytes, all uint16_t at even offsets */

/**
 * COLD Section - only accessed when actual I/O happens
 *
 * Large buffers separated to avoid cache pollution during hot scans.
 */
typedef struct {
    pt_message_header hdr;
    uint8_t header_buf[PT_MESSAGE_HEADER_SIZE];  /* 10 bytes */
    uint8_t crc_buf[2];
    uint8_t payload_buf[PT_MAX_MESSAGE_SIZE];
} pt_recv_cold;

/**
 * Complete receive buffer - hot and cold sections
 *
 * IMPORTANT: These are allocated separately (not inline in pt_posix_data)
 * to prevent massive payload buffers from polluting cache when accessing
 * hot fields in the platform context.
 */
typedef struct {
    pt_recv_hot  hot;
    pt_recv_cold cold;
} pt_recv_buffer;

/* ========================================================================== */
/* Platform Context Extension (pt_posix_data)                                */
/* ========================================================================== */

/**
 * POSIX platform-specific data
 *
 * Extends pt_context with networking state. Allocated after pt_context
 * structure via pt_posix_extra_size().
 *
 * Field ordering follows Data-Oriented Design:
 * - HOT: Accessed every poll cycle
 * - WARM: Accessed on socket operations
 * - COLD: Per-peer data
 *
 * recv_bufs is a POINTER, not inline array, to avoid cache pollution.
 */
typedef struct {
    /* HOT - accessed every poll cycle (20 bytes on 32-bit) */
    int max_fd;                    /* 4 bytes: highest fd for select() */
    uint8_t active_count;          /* 1 byte: number of active peer connections */
    uint8_t fd_dirty;              /* 1 byte: non-zero when fd_sets need rebuild */
    uint16_t batch_count;          /* 2 bytes: messages in current batch */
    pt_tick_t last_announce;       /* 4 bytes: tick of last discovery announce */
    uint32_t local_ip;             /* 4 bytes: our IP (for filtering own broadcasts) */
    uint8_t _pad0[4];              /* 4 bytes: align to 8-byte boundary */

    /* Active peer tracking - O(1) removal via swap-back */
    uint8_t active_peers[PT_MAX_PEERS];     /* Indices of peers with active sockets */
    uint8_t active_position[PT_MAX_PEERS];  /* Reverse mapping for O(1) removal */

    /* WARM - accessed on socket operations (24 bytes) */
    int discovery_sock;
    int listen_sock;
    int udp_msg_sock;
    uint32_t broadcast_addr;       /* INADDR_BROADCAST */
    uint16_t discovery_port;
    uint16_t listen_port;
    uint16_t udp_msg_port;
    uint16_t _pad1;

    /* Cached fd_set - rebuilt only when fd_dirty is set */
    fd_set cached_read_fds;
    fd_set cached_write_fds;

    /* COLD - per-peer data */
    int tcp_socks[PT_MAX_PEERS];   /* TCP socket per peer, -1 if not connected */

    /* SEPARATE ALLOCATION - do NOT embed inline */
    pt_recv_buffer *recv_bufs;     /* Pointer to allocated array */
} pt_posix_data;

/* ========================================================================== */
/* Helper Inline Functions                                                   */
/* ========================================================================== */

/**
 * Get POSIX platform data from context
 *
 * The platform data is allocated immediately after the context structure.
 */
static inline pt_posix_data *pt_posix_get(struct pt_context *ctx) {
    return (pt_posix_data *)((char *)ctx + sizeof(struct pt_context));
}

/* ========================================================================== */
/* Platform Size/Init                                                        */
/* ========================================================================== */

/**
 * Return extra memory size needed for POSIX platform data
 */
size_t pt_posix_extra_size(void);

/**
 * Initialize POSIX networking subsystem
 *
 * Creates sockets, initializes data structures, detects local IP.
 * Called from PeerTalk_Init().
 *
 * Returns: 0 on success, -1 on failure
 */
int pt_posix_net_init(struct pt_context *ctx);

/**
 * Shutdown POSIX networking subsystem
 *
 * Closes all sockets, frees allocated buffers.
 * Called from PeerTalk_Shutdown().
 */
void pt_posix_net_shutdown(struct pt_context *ctx);

/* ========================================================================== */
/* Discovery                                                                  */
/* ========================================================================== */

/**
 * Start UDP discovery broadcasts
 *
 * Creates discovery socket, enables broadcast, sends initial announcement.
 *
 * Returns: 0 on success, -1 on failure
 */
int pt_posix_discovery_start(struct pt_context *ctx);

/**
 * Stop UDP discovery broadcasts
 *
 * Closes discovery socket.
 */
void pt_posix_discovery_stop(struct pt_context *ctx);

/**
 * Poll discovery socket for incoming packets
 *
 * Non-blocking receive of discovery packets. Processes ANNOUNCE, QUERY,
 * GOODBYE types. Creates/updates/removes peers as appropriate.
 *
 * Returns: 1 if packet processed, 0 if no data, -1 on error
 */
int pt_posix_discovery_poll(struct pt_context *ctx);

/**
 * Send discovery packet
 *
 * @param ctx Context
 * @param type PT_DISC_TYPE_ANNOUNCE, PT_DISC_TYPE_QUERY, or PT_DISC_TYPE_GOODBYE
 *
 * Returns: 0 on success, -1 on failure
 */
int pt_posix_discovery_send(struct pt_context *ctx, uint8_t type);

/* ========================================================================== */
/* TCP Server                                                                 */
/* ========================================================================== */

/**
 * Start TCP listening socket
 *
 * Creates listening socket on TCP port for incoming connections.
 *
 * Returns: 0 on success, -1 on failure
 */
int pt_posix_listen_start(struct pt_context *ctx);

/**
 * Stop TCP listening socket
 */
void pt_posix_listen_stop(struct pt_context *ctx);

/**
 * Poll TCP listening socket for incoming connections
 *
 * Non-blocking accept() of incoming connections.
 *
 * Returns: 1 if connection accepted, 0 if no pending, -1 on error
 */
int pt_posix_listen_poll(struct pt_context *ctx);

/* ========================================================================== */
/* TCP Client                                                                 */
/* ========================================================================== */

/**
 * Connect to a peer via TCP
 *
 * Initiates non-blocking connection to peer. Connection completes
 * asynchronously; poll for writability to detect completion.
 *
 * Returns: 0 on success (connecting), -1 on failure
 */
int pt_posix_connect(struct pt_context *ctx, struct pt_peer *peer);

/**
 * Disconnect from a peer
 *
 * Closes TCP connection, removes from active peer tracking.
 *
 * Returns: 0 on success, -1 on failure
 */
int pt_posix_disconnect(struct pt_context *ctx, struct pt_peer *peer);

/* ========================================================================== */
/* I/O                                                                        */
/* ========================================================================== */

/**
 * Send data to peer
 *
 * Non-blocking send. If socket buffer is full, returns PT_ERR_WOULD_BLOCK.
 *
 * Returns: 0 on success, PT_ERR_WOULD_BLOCK if would block, -1 on error
 */
int pt_posix_send(struct pt_context *ctx, struct pt_peer *peer,
                  const void *data, size_t len);

/**
 * Receive data from peer
 *
 * Non-blocking receive with state machine for header/payload/CRC.
 *
 * Returns: 1 if complete message received, 0 if incomplete, -1 on error
 */
int pt_posix_recv(struct pt_context *ctx, struct pt_peer *peer);

/**
 * Send control message to peer
 *
 * Helper for sending protocol control messages (PING, PONG, etc).
 *
 * Returns: 0 on success, -1 on failure
 */
int pt_posix_send_control(struct pt_context *ctx, struct pt_peer *peer,
                          uint8_t msg_type);

/**
 * Send capability message to peer
 *
 * Called after connection established to exchange capability information.
 * Enables automatic fragmentation for constrained peers.
 *
 * Returns: PT_OK on success, error code on failure
 */
int pt_posix_send_capability(struct pt_context *ctx, struct pt_peer *peer);

/* ========================================================================== */
/* UDP Messaging (Session 4.4)                                               */
/* ========================================================================== */

/**
 * Initialize UDP messaging socket (separate from discovery)
 *
 * Returns: 0 on success, -1 on failure
 */
int pt_posix_udp_init(struct pt_context *ctx);

/**
 * Shutdown UDP messaging socket
 */
void pt_posix_udp_shutdown(struct pt_context *ctx);

/**
 * Send unreliable UDP message to peer
 *
 * Returns: 0 on success, -1 on failure
 */
int pt_posix_send_udp(struct pt_context *ctx, struct pt_peer *peer,
                      const void *data, uint16_t len);

/**
 * Receive UDP message
 *
 * Non-blocking receive of UDP messages (not discovery packets).
 *
 * Returns: 1 if message received, 0 if no data, -1 on error
 */
int pt_posix_recv_udp(struct pt_context *ctx);

/* ========================================================================== */
/* Main Poll                                                                  */
/* ========================================================================== */

/**
 * Main POSIX poll function
 *
 * Polls all sockets (discovery, listen, UDP, TCP peers) using select().
 * Delegates to appropriate handlers based on ready sockets.
 *
 * Returns: 0 on success, -1 on error
 */
int pt_posix_poll(struct pt_context *ctx);

#endif /* PT_NET_POSIX_H */
