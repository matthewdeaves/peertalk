# PHASE 4: POSIX Networking

> **Status:** OPEN
> **Depends on:** Phase 1 (Foundation), Phase 2 (Protocol), Phase 3 (Advanced Queues), Phase 3.5 (SendEx API)
> **Produces:** Fully functional PeerTalk on Linux/macOS with complete message queuing
> **Risk Level:** Low (POSIX sockets are well-understood)
> **Estimated Sessions:** 8 (originally 7, added Session 4.3.5 for queue integration)
> **Implementation Complete:** 2026-02-04 - All 8 sessions implemented and tested. Queue integration (Session 4.3.5) verified in net_posix.c with queue allocation on connection establishment, cleanup on disconnection, and pt_queue_pop_priority_direct() in poll loop. All POSIX integration tests passing (21/21).
> **Review Applied:** 2026-01-29 (second pass: 68k alignment fixes, DOD reorganization, new common pitfalls)

> **Build Order:** Complete Phase 1 → Phase 2 → Phase 4. This phase cannot compile without Phase 1 and Phase 2.

## Overview

Phase 4 implements the complete networking layer for POSIX systems (Linux, macOS, BSD). This serves as the **reference implementation** and test bed for validating protocol correctness before tackling the more complex Classic Mac implementations.

**Key Principle:** Use non-blocking I/O with select() for a single-threaded event loop. This matches the polling model required for Classic Mac compatibility.

**Data-Oriented Design:** Struct layouts are optimized for cache efficiency. While POSIX runs on modern CPUs, these patterns will be replicated for Classic Mac where the 68030 has only a 256-byte L1 data cache. Hot fields (accessed every poll) are grouped first, followed by warm fields (accessed per-operation), then cold fields (large buffers).

---

## Prerequisites from Earlier Phases

### Phase 1 Prerequisites (Foundation)

Phase 4 requires these Phase 1 deliverables:

**Core structures:**
- `struct pt_context` - Main context with `magic`, `config`, `callbacks`, `peers[]`, `global_stats`
- `struct pt_peer` - Peer tracking with `id`, `name`, `state`, `ip_addr`, `port`
- `pt_platform_ops` - Function pointer table for platform dispatch

**Compatibility layer (`pt_compat.h`):**
- `pt_memcpy()`, `pt_memset()`, `pt_memcmp()`
- `pt_strlen()`, `pt_strncpy()`, `pt_snprintf()`
- `pt_htonl()`, `pt_ntohl()`, `pt_htons()`, `pt_ntohs()`
- `pt_alloc()`, `pt_free()`

**Logging (`pt_log.h` from Phase 0):**
- `PT_LOG_INFO()`, `PT_LOG_WARN()`, `PT_LOG_ERR()`, `PT_LOG_DEBUG()`
- `PT_LOG_CAT_GENERAL`, `PT_LOG_CAT_DISCOVERY`, `PT_LOG_CAT_CONNECT`, etc.

**Public API stubs (`context.c`):**
- `PeerTalk_Init()`, `PeerTalk_Shutdown()`, `PeerTalk_Poll()`
- `PeerTalk_StartDiscovery()`, `PeerTalk_StopDiscovery()`
- `PeerTalk_Connect()`, `PeerTalk_Disconnect()`
- `PeerTalk_Send()`, `PeerTalk_SetCallbacks()`, `PeerTalk_GetPeers()`

### Phase 2 Prerequisites (Protocol)

**Protocol encoding/decoding:**
```c
/* Discovery packets */
int pt_discovery_encode(const pt_discovery_packet *pkt, uint8_t *buf, size_t len);
int pt_discovery_decode(const uint8_t *buf, size_t len, pt_discovery_packet *pkt);

/* Message framing */
int pt_message_encode_header(const pt_message_header *hdr, uint8_t *buf);
int pt_message_decode_header(const uint8_t *buf, size_t len, pt_message_header *hdr);

/* CRC validation */
uint16_t pt_crc16(const uint8_t *data, size_t len);
uint16_t pt_crc16_update(uint16_t crc, const uint8_t *data, size_t len);
```

**Peer management:**
```c
struct pt_peer* pt_peer_create(struct pt_context *ctx, const char *name,
                               uint32_t ip, uint16_t port);
struct pt_peer* pt_peer_find_by_id(struct pt_context *ctx, PeerTalk_PeerID id);
struct pt_peer* pt_peer_find_by_addr(struct pt_context *ctx, uint32_t ip, uint16_t port);
void pt_peer_destroy(struct pt_context *ctx, struct pt_peer *peer);
void pt_peer_get_info(struct pt_peer *peer, PeerTalk_PeerInfo *info);
void pt_peer_set_state(struct pt_peer *peer, pt_peer_state state);
int pt_peer_is_timed_out(struct pt_peer *peer, pt_tick_t now, pt_tick_t timeout_ms);
void pt_peer_check_canaries(struct pt_peer *peer);
```

**Protocol constants (from Phase 2):**
```c
#define PT_DISCOVERY_MAX_SIZE    128   /* Max discovery packet size */
#define PT_MESSAGE_HEADER_SIZE   10    /* Magic(4)+Ver(1)+Type(1)+Flags(1)+Seq(1)+Len(2) */
#define PT_MAX_MESSAGE_SIZE      4096  /* Max payload size (configurable) */
#define PT_PEER_NAME_MAX         31    /* Max peer name length */

/* Discovery packet types (defined in protocol.h from Phase 2) */
/* #define PT_DISC_TYPE_ANNOUNCE   0x01 */
/* #define PT_DISC_TYPE_QUERY      0x02 */
/* #define PT_DISC_TYPE_GOODBYE    0x03 */

/* Discovery flags */
#define PT_DISC_FLAG_ACCEPTING   0x0001

/* Message types */
#define PT_MSG_DATA              0x01
#define PT_MSG_PING              0x02
#define PT_MSG_PONG              0x03
#define PT_MSG_DISCONNECT        0x04
#define PT_MSG_ACK               0x05
```

### Struct Field Requirements

**In `struct pt_peer` (Phase 2):**
```c
uint16_t udp_port;          /* Peer's UDP messaging port (learned from traffic) */
uint16_t send_seq;          /* Next sequence number to send */
uint16_t recv_seq;          /* Last received sequence number */
pt_tick_t last_seen;        /* Tick when last heard from peer */
pt_tick_t connect_start;    /* Tick when connect initiated (for timeout) */

struct {
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t messages_sent;
    uint32_t messages_received;
    uint16_t latency_ms;        /* Rolling average RTT */
    uint8_t  quality;           /* 0-100 connection quality */
    uint32_t ping_sent_at;      /* Tick when last ping sent */
} stats;
```

**In `struct pt_context` (Phase 1):**
```c
PeerTalk_GlobalStats global_stats;  /* Accumulates network statistics */
int discovery_active;               /* Non-zero if discovery running */
int max_peers;                      /* From config, for array bounds */
pt_platform_ops *plat;              /* Platform function pointers */
```

---

## Testing Strategy

**POSIX = Automated Testing Baseline**

Unlike MacTCP and Open Transport (which require manual testing on real hardware),
POSIX implementations can be fully validated with automated tests:

- Unit tests run in CI/CD pipeline
- Integration tests use fork() to simulate multiple peers
- Memory leak detection via valgrind
- Protocol validation via packet capture

This automated test suite serves as the protocol reference:
- If POSIX<->POSIX tests pass, the protocol is correct
- MacTCP and OT implementations must match POSIX behavior
- Cross-platform tests (POSIX<->Mac) validate interoperability

---

## Goals

1. Implement UDP broadcast discovery with select()-based polling
2. Implement TCP connection management with non-blocking connect
3. Integrate with protocol layer for message framing
4. Full test coverage with multiple POSIX clients

## Session Scope Table

| Session | Focus | Status | Files Created/Modified | Tests | Verify |
|---------|-------|--------|------------------------|-------|--------|
| 4.1 | UDP Discovery | [DONE] | `src/posix/net_posix.c`, `src/posix/net_posix.h` | `tests/test_discovery_posix.c` | Peers discover each other via broadcast |
| 4.2 | TCP Connections | [DONE] | `src/posix/net_posix.c`, `src/core/pt_init.c` | `tests/test_connect_posix.c` | Connect/disconnect lifecycle works |
| 4.3 | Message I/O | [DONE] | `src/posix/net_posix.c` | `tests/test_messaging_posix.c` | Send/receive messages correctly |
| 4.3.5 | Queue Integration | [DONE] | `src/posix/net_posix.c` | `tests/test_integration_full.c` (enhanced) | Queues init on connect, messages flow end-to-end |
| 4.4 | UDP Messaging | [DONE] | `src/posix/udp_posix.c` | `tests/test_udp_posix.c` | PeerTalk_SendUDP works for unreliable messages |
| 4.5 | Network Statistics | [DONE] | `src/posix/stats_posix.c` | `tests/test_stats_posix.c` | Latency, bytes, quality tracking |
| 4.6 | Integration | [DONE] | `src/posix/net_posix.c` | `tests/test_integration_posix.c` | Poll loop with cached fd_sets works |
| 4.7 | CI Setup | [DONE] | `.github/workflows/ci.yml` | CI passes | `make test` runs on push/PR |

> **Session 4.3.5 Added (2026-02-04):** Queue integration session added to complete POSIX reference implementation. Integration test discovered queues are never initialized when peers connect, causing `PeerTalk_SendEx()` to fail with "Peer has no send queue" even though connections work. This session completes the integration by:
> - Allocating send/recv queues on connection establishment
> - Draining send queue in poll loop
> - Freeing queues on disconnection
> - Full 3-peer Docker validation with end-to-end messaging
>
> **Session 4.6 Status:**
>
> **Initial Implementation (2026-02-04):** The integration test was simplified from the original 3-peer Docker scenario to a basic poll loop test due to missing dependencies. The test verified: initialization, discovery start, listening start, optimized poll loop with cached fd_sets, statistics tracking, and shutdown.
>
> **Updated (2026-02-04):** Phase 3.5 dependencies are now complete! The following APIs have been implemented:
> - ✅ `PeerTalk_Send()` / `PeerTalk_SendEx()` - Phase 3.5 (full implementation with priority, coalescing, unreliable routing)
> - ✅ `PeerTalk_GetPeers()` - Phase 1 helper (implemented in `src/core/pt_init.c`)
> - ✅ `PeerTalk_Broadcast()` - Phase 1 helper (implemented in `src/core/pt_init.c`)
>
> **Superseded by Session 4.3.5:** The full 3-peer Docker integration test is now part of Session 4.3.5 (Queue Integration), which provides comprehensive validation of discovery, connection, AND end-to-end messaging between multiple peers.

### Status Key
- **[OPEN]** - Not started
- **[IN PROGRESS]** - Currently being worked on
- **[READY TO TEST]** - Implementation complete, needs verification
- **[DONE]** - Verified and complete

---

## Session 4.1: UDP Discovery

### Objective
Implement UDP broadcast for peer discovery using non-blocking sockets and select().

### Implementation Notes

**⚠️ CRITICAL TESTING LIMITATION**: UDP broadcast discovery **CANNOT** be tested with two processes
on the same host due to kernel loopback filtering. The OS filters out broadcast packets sent from
your own IP address. Use Docker Compose with bridge networking for multi-peer testing (see Task 4.1.4 below).

**Critical Bugs Found During Implementation:**
1. **Missing `name_len` initialization** - Must set `pkt.name_len = pt_strlen(pkt.name)` before encoding
2. **Missing `pt_peer_list_init()` call** - Required in `PeerTalk_Init()` before any peer operations
3. **Socket helpers need context** - Pass `struct pt_context *ctx` for proper error logging
4. **Missing `address_count` initialization** (2026-02-04) - `pt_peer_create()` never set `peer->hot.address_count`, causing `PeerTalk_Connect()` to fail with PT_ERR_INVALID_STATE. Must initialize addresses array when creating peers from discovery. Found by 3-peer Docker integration test.

**Protocol Constant Naming:**
- Use `PT_DISC_TYPE_ANNOUNCE` (not `PT_DISC_TYPE_ANNOUNCE`)
- Use `PT_DISC_TYPE_QUERY` (not `PT_DISC_TYPE_QUERY`)
- Use `PT_DISC_TYPE_GOODBYE` (not `PT_DISC_TYPE_GOODBYE`)

**Logging Levels:**
- Discovery events should use `PT_CTX_INFO` (not `PT_CTX_DEBUG`) for visibility
- Users need to see peer discovery working without enabling full debug output

### Tasks

#### Task 4.1.1: Create `src/posix/net_posix.h`

```c
#ifndef PT_NET_POSIX_H
#define PT_NET_POSIX_H

#include "pt_internal.h"
#include <sys/uio.h>  /* For struct iovec, writev() */

/* Receive state machine for handling partial TCP reads.
 * DOD: Use uint8_t instead of enum to reduce from 4 bytes to 1 byte.
 * On 68030, this shrinks the HOT section from 8 bytes to 5 bytes.
 */
typedef uint8_t pt_recv_state;
#define PT_RECV_HEADER   0   /* Waiting for header bytes */
#define PT_RECV_PAYLOAD  1   /* Waiting for payload bytes */
#define PT_RECV_CRC      2   /* Waiting for CRC bytes */

/*
 * Per-peer receive buffer - SPLIT into hot/cold for cache efficiency.
 *
 * DOD: The hot section is checked every poll cycle when scanning for readable
 * sockets. The cold section contains large buffers that are only accessed when
 * actual I/O happens. By keeping these separate, we avoid polluting the cache
 * when iterating receive states.
 *
 * With 16 peers, the hot scan touches 128 bytes (fits in 68030 L1 cache)
 * instead of 131KB if payload buffers were inline.
 */

/* HOT: Checked every poll cycle per active connection (8 bytes)
 *
 * 68K ALIGNMENT: uint16_t fields MUST start at even byte offsets on 68000/68020/68030.
 * Placing bytes_needed at offset 1 would cause unaligned access penalties or crashes.
 * The _pad0 byte ensures proper alignment.
 */
typedef struct {
    pt_recv_state state;           /* 1 byte at offset 0: current state machine state */
    uint8_t       _pad0;           /* 1 byte at offset 1: CRITICAL for 68k alignment */
    uint16_t      bytes_needed;    /* 2 bytes at offset 2: remaining bytes for current state */
    uint16_t      bytes_received;  /* 2 bytes at offset 4: bytes received so far in current state */
    uint16_t      _pad1;           /* 2 bytes at offset 6: align to 8 bytes for array iteration */
} pt_recv_hot;  /* Total: 8 bytes, all uint16_t at even offsets */

/* COLD: Only accessed when actual I/O happens */
typedef struct {
    pt_message_header hdr;         /* 6 bytes: parsed header (valid after header complete) */
    uint8_t       header_buf[PT_MESSAGE_HEADER_SIZE];  /* 10 bytes: raw header bytes */
    uint8_t       crc_buf[2];      /* 2 bytes: CRC bytes (persists across partial reads) */
    uint8_t       payload_buf[PT_MAX_MESSAGE_SIZE];  /* Payload receive buffer */
} pt_recv_cold;

/* Combined receive buffer - used when hot/cold separation is not needed.
 *
 * 68K ALIGNMENT: All uint16_t fields start at even offsets.
 * Field ordering follows largest-to-smallest within each section.
 */
typedef struct {
    /* HOT - accessed every poll cycle (8 bytes, all aligned) */
    pt_recv_state state;           /* 1 byte at offset 0: current state machine state */
    uint8_t       _pad0;           /* 1 byte at offset 1: 68k alignment padding */
    uint16_t      bytes_needed;    /* 2 bytes at offset 2: remaining bytes for current state */
    uint16_t      bytes_received;  /* 2 bytes at offset 4: bytes received so far in current state */
    uint16_t      _pad1;           /* 2 bytes at offset 6: align to 8-byte boundary */

    /* WARM - accessed when processing messages (20 bytes) */
    pt_message_header hdr;         /* 6 bytes: parsed header (valid after header complete) */
    uint8_t       header_buf[PT_MESSAGE_HEADER_SIZE];  /* 10 bytes: raw header bytes */
    uint8_t       crc_buf[2];      /* 2 bytes: CRC bytes (persists across partial reads) */
    uint8_t       _pad2[2];        /* 2 bytes: padding to maintain alignment */

    /* COLD - only accessed when message payload arrives */
    uint8_t       payload_buf[PT_MAX_MESSAGE_SIZE];  /* Payload receive buffer */
} pt_recv_buffer;

/*
 * Platform-specific context extension for POSIX.
 *
 * DOD: Fields ordered by access frequency. Hot fields first (accessed every poll),
 * then warm fields (accessed per-operation), then cold data (per-peer arrays).
 *
 * IMPORTANT: recv_bufs is a POINTER, not inline array. This prevents the massive
 * payload buffers from polluting the cache when accessing hot fields. The recv_bufs
 * array is allocated separately in pt_posix_net_init().
 */
typedef struct {
    /* HOT - accessed every poll cycle (20 bytes on 32-bit)
     * Fields ordered by access frequency within the hot section.
     * All integer types sized to minimize padding on both 32-bit and 64-bit. */
    int max_fd;                    /* 4 bytes: highest fd for select() */
    uint8_t active_count;          /* 1 byte: number of active peer connections */
    uint8_t fd_dirty;              /* 1 byte: non-zero when fd_sets need rebuild */
    uint16_t batch_count;          /* 2 bytes: messages in current batch (checked each poll) */
    pt_tick_t last_announce;       /* 4 bytes: tick of last discovery announce */
    uint32_t local_ip;             /* 4 bytes: our IP (for filtering own broadcasts) */

    /* Active peer tracking - avoids iterating all PT_MAX_PEERS slots
     * active_peers[i] = peer_idx at position i
     * active_position[peer_idx] = position in active_peers (0xFF if inactive)
     * This enables O(1) removal via swap-back without linear search */
    uint8_t active_peers[PT_MAX_PEERS];     /* Indices of peers with active sockets */
    uint8_t active_position[PT_MAX_PEERS];  /* Reverse mapping for O(1) removal */

    /* WARM - accessed on socket operations (24 bytes) */
    int discovery_sock;            /* 4 bytes */
    int listen_sock;               /* 4 bytes */
    int udp_msg_sock;              /* 4 bytes */
    uint32_t broadcast_addr;       /* 4 bytes: INADDR_BROADCAST */
    uint16_t discovery_port;       /* 2 bytes */
    uint16_t listen_port;          /* 2 bytes */
    uint16_t udp_msg_port;         /* 2 bytes */
    uint16_t _pad1;                /* 2 bytes: alignment padding */

    /* Cached fd_set - rebuilt only when fd_dirty is set */
    fd_set cached_read_fds;        /* Cached read fd_set */
    fd_set cached_write_fds;       /* Cached write fd_set (for connecting sockets) */

    /* COLD - per-peer data (accessed per specific peer) */
    int tcp_socks[PT_MAX_PEERS];   /* TCP socket per peer, -1 if not connected */

    /* Batch message accumulation - in COLD section because msg_batch[] array is large.
     * Only batch_count (in HOT) is checked each poll; the actual messages are only
     * accessed when messages arrive and when firing the batch callback. */
    PeerTalk_MessageBatch msg_batch[PT_MAX_BATCH_SIZE];

    /* SEPARATE ALLOCATION - do NOT embed inline */
    pt_recv_buffer *recv_bufs;     /* Pointer to allocated array of recv buffers */
} pt_posix_data;

/* Get platform data from context */
static inline pt_posix_data *pt_posix_get(struct pt_context *ctx) {
    return (pt_posix_data *)((char *)ctx + sizeof(struct pt_context));
}

/* Platform extra size */
size_t pt_posix_extra_size(void);

/* Network initialization */
int pt_posix_net_init(struct pt_context *ctx);
void pt_posix_net_shutdown(struct pt_context *ctx);

/* Discovery */
int pt_posix_discovery_start(struct pt_context *ctx);
void pt_posix_discovery_stop(struct pt_context *ctx);
int pt_posix_discovery_poll(struct pt_context *ctx);
int pt_posix_discovery_send(struct pt_context *ctx, uint8_t type);

/* TCP server */
int pt_posix_listen_start(struct pt_context *ctx);
void pt_posix_listen_stop(struct pt_context *ctx);
int pt_posix_listen_poll(struct pt_context *ctx);

/* TCP client */
int pt_posix_connect(struct pt_context *ctx, struct pt_peer *peer);
int pt_posix_disconnect(struct pt_context *ctx, struct pt_peer *peer);

/* I/O */
int pt_posix_send(struct pt_context *ctx, struct pt_peer *peer,
                  const void *data, size_t len);
int pt_posix_recv(struct pt_context *ctx, struct pt_peer *peer);

/* Control messages (ping/pong/disconnect) */
int pt_posix_send_control(struct pt_context *ctx, struct pt_peer *peer,
                          uint8_t msg_type);

/* UDP messaging (separate from discovery) - Session 4.4 */
int pt_posix_udp_init(struct pt_context *ctx);
void pt_posix_udp_shutdown(struct pt_context *ctx);
int pt_posix_send_udp(struct pt_context *ctx, struct pt_peer *peer,
                       const void *data, uint16_t len);
int pt_posix_recv_udp(struct pt_context *ctx);

/* Main poll function */
int pt_posix_poll(struct pt_context *ctx);

#endif /* PT_NET_POSIX_H */
```

#### Task 4.1.2: Create UDP discovery implementation

```c
/* src/posix/net_posix.c */

#include "net_posix.h"
#include "protocol.h"
#include "peer.h"
#include "pt_log.h"
#include "pt_compat.h"

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* Forward declarations */
static const char *pt_discovery_type_str(uint8_t type);
static const char *pt_peer_state_str(pt_peer_state state);

/*
 * Helper: Log peer state transitions for debugging.
 * This provides visibility into connection lifecycle for troubleshooting.
 */
static void pt_log_state_transition(struct pt_context *ctx, struct pt_peer *peer,
                                     pt_peer_state old_state, pt_peer_state new_state) {
    PT_LOG_DEBUG(ctx, PT_LOG_CAT_CONNECT,
        "Peer %u (%s) state: %s -> %s",
        peer->id, peer->name,
        pt_peer_state_str(old_state), pt_peer_state_str(new_state));
}

static const char *pt_peer_state_str(pt_peer_state state) {
    switch (state) {
    case PT_PEER_UNUSED:       return "UNUSED";
    case PT_PEER_DISCOVERED:   return "DISCOVERED";
    case PT_PEER_CONNECTING:   return "CONNECTING";
    case PT_PEER_CONNECTED:    return "CONNECTED";
    case PT_PEER_DISCONNECTING: return "DISCONNECTING";
    case PT_PEER_FAILED:       return "FAILED";
    default:                   return "UNKNOWN";
    }
}

/* Port defaults - used when ctx->config values are 0 */
#define DEFAULT_DISCOVERY_PORT 7353
#define DEFAULT_TCP_PORT 7354
#define DEFAULT_UDP_PORT 7355

/* Port accessor macros - use config value if set, otherwise default */
#define DISCOVERY_PORT(ctx) \
    ((ctx)->config.discovery_port > 0 ? (ctx)->config.discovery_port : DEFAULT_DISCOVERY_PORT)
#define TCP_PORT(ctx) \
    ((ctx)->config.tcp_port > 0 ? (ctx)->config.tcp_port : DEFAULT_TCP_PORT)

size_t pt_posix_extra_size(void) {
    return sizeof(pt_posix_data);
}

/*
 * Socket configuration helpers with error logging.
 * Accept context parameter for proper error logging with category.
 */
static int set_nonblocking(struct pt_context *ctx, int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
                   "Failed to get socket flags: %s", strerror(errno));
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
                   "Failed to set non-blocking: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int set_broadcast(struct pt_context *ctx, int fd) {
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
                   "Failed to enable broadcast: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int set_reuseaddr(struct pt_context *ctx, int fd) {
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
                   "Failed to set SO_REUSEADDR: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/*
 * Get local IP address.
 *
 * Strategy (order matters for different network environments):
 * 1. Try getifaddrs() first - enumerates local interfaces directly
 *    WORKS: Air-gapped networks, normal LANs, any environment with configured interfaces
 *    FAILS: Some container environments where interface enumeration is restricted
 *
 * 2. Fall back to connect-to-8.8.8.8 trick
 *    WORKS: Containers with NAT, environments where getifaddrs is restricted
 *    FAILS: Air-gapped networks (no route to 8.8.8.8), firewalled environments
 *    NOTE: Does NOT actually send packets - just asks kernel which interface would be used
 *
 * 3. Return loopback (127.0.0.1) as last resort
 *    Allows local testing but peer discovery won't work across machines
 */
static uint32_t get_local_ip(void) {
    struct ifaddrs *ifaddr, *ifa;
    uint32_t result = INADDR_LOOPBACK;

    /* Method 1: getifaddrs - works on air-gapped networks and normal LANs */
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL)
                continue;

            /* Skip non-IPv4 */
            if (ifa->ifa_addr->sa_family != AF_INET)
                continue;

            /* Skip loopback */
            if (ifa->ifa_flags & IFF_LOOPBACK)
                continue;

            /* Skip interfaces that are down */
            if (!(ifa->ifa_flags & IFF_UP))
                continue;

            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            result = ntohl(addr->sin_addr.s_addr);
            freeifaddrs(ifaddr);
            /* Note: Logging not available here (no ctx), but pt_posix_net_init logs result */
            return result;
        }
        freeifaddrs(ifaddr);
    }

    /* Method 2: Connect to public IP to determine local interface.
     * This works even when getifaddrs fails (e.g., in some containers).
     * NOTE: Does NOT send packets - connect() on UDP just sets routing info. */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);

        pt_memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("8.8.8.8");
        addr.sin_port = htons(53);

        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
            getsockname(sock, (struct sockaddr *)&addr, &len);
            result = ntohl(addr.sin_addr.s_addr);
        }
        close(sock);
    }

    /* If we get here with loopback, both methods failed - peer discovery
     * will only work for local testing, not across machines */
    return result;
}

int pt_posix_net_init(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    int i;

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_PLATFORM,
        "Initializing POSIX network layer");

    /* Initialize HOT fields */
    pd->max_fd = 0;
    pd->active_count = 0;
    pd->last_announce = 0;
    pd->batch_count = 0;
    pd->local_ip = get_local_ip();

    /* Clear active peers tracking and reverse mapping */
    pt_memset(pd->active_peers, 0, sizeof(pd->active_peers));
    pt_memset(pd->active_position, 0xFF, sizeof(pd->active_position));  /* 0xFF = inactive */

    /* Initialize WARM fields */
    pd->discovery_sock = -1;
    pd->listen_sock = -1;
    pd->udp_msg_sock = -1;
    pd->udp_msg_port = 0;
    pd->discovery_port = 0;
    pd->listen_port = 0;

    /*
     * Use INADDR_BROADCAST (255.255.255.255) for maximum compatibility.
     * This works regardless of subnet size (/8, /16, /24, /25, etc.)
     * and is the standard approach for LAN discovery.
     */
    pd->broadcast_addr = INADDR_BROADCAST;  /* 0xFFFFFFFF */

    /* Initialize fd_set cache */
    FD_ZERO(&pd->cached_read_fds);
    FD_ZERO(&pd->cached_write_fds);
    pd->fd_dirty = 1;  /* Force rebuild on first poll */

    /* Initialize COLD per-peer data */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        pd->tcp_socks[i] = -1;
    }

    /*
     * Allocate recv_bufs array SEPARATELY (not inline).
     * This is critical for cache efficiency - the massive payload buffers
     * would otherwise pollute the cache when accessing pt_posix_data hot fields.
     */
    pd->recv_bufs = (pt_recv_buffer *)pt_alloc(
        PT_MAX_PEERS * sizeof(pt_recv_buffer));
    if (!pd->recv_bufs) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to allocate recv buffers: %u peers x %u bytes = %u total",
            PT_MAX_PEERS, (unsigned)sizeof(pt_recv_buffer),
            PT_MAX_PEERS * (unsigned)sizeof(pt_recv_buffer));
        return -1;
    }

    /* Initialize receive buffer states */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        pd->recv_bufs[i].state = PT_RECV_HEADER;
        pd->recv_bufs[i].bytes_needed = PT_MESSAGE_HEADER_SIZE;
        pd->recv_bufs[i].bytes_received = 0;
    }

    PT_LOG_INFO(ctx, PT_LOG_CAT_NETWORK,
        "POSIX network init: local_ip=0x%08X broadcast=0x%08X",
        pd->local_ip, pd->broadcast_addr);

    /* Initialize UDP messaging socket (separate from discovery) */
    if (pt_posix_udp_init(ctx) < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_NETWORK,
            "UDP messaging init failed - PeerTalk_SendUDP will return PT_ERR_NOT_AVAILABLE");
        /* Non-fatal: TCP still works */
    }

    return 0;
}

void pt_posix_net_shutdown(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    int i;

    if (pd->discovery_sock >= 0) {
        close(pd->discovery_sock);
        pd->discovery_sock = -1;
    }

    if (pd->listen_sock >= 0) {
        close(pd->listen_sock);
        pd->listen_sock = -1;
    }

    /* Close UDP messaging socket */
    if (pd->udp_msg_sock >= 0) {
        close(pd->udp_msg_sock);
        pd->udp_msg_sock = -1;
    }

    /* Close all TCP connections */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        if (pd->tcp_socks[i] >= 0) {
            close(pd->tcp_socks[i]);
            pd->tcp_socks[i] = -1;
        }
    }

    /* Free separately allocated recv_bufs */
    if (pd->recv_bufs) {
        pt_free(pd->recv_bufs);
        pd->recv_bufs = NULL;
    }

    pd->active_count = 0;
}

int pt_posix_discovery_start(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    struct sockaddr_in addr;
    int sock;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_DISCOVERY,
            "Failed to create discovery socket: %s", strerror(errno));
        return -1;
    }

    if (set_nonblocking(ctx, sock) < 0) {
        /* Error already logged by helper */
        close(sock);
        return -1;
    }

    if (set_broadcast(ctx, sock) < 0) {
        /* Error already logged by helper */
        close(sock);
        return -1;
    }

    if (set_reuseaddr(ctx, sock) < 0) {
        /* Error already logged by helper */
        close(sock);
        return -1;
    }

    pt_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(DISCOVERY_PORT(ctx));

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_DISCOVERY,
            "Failed to bind discovery socket to port %u: %s",
            DISCOVERY_PORT(ctx), strerror(errno));
        close(sock);
        return -1;
    }

    pd->discovery_sock = sock;
    pd->discovery_port = DISCOVERY_PORT(ctx);

    if (sock > pd->max_fd)
        pd->max_fd = sock;

    PT_LOG_INFO(ctx, PT_LOG_CAT_DISCOVERY,
        "Discovery started on port %u", DISCOVERY_PORT(ctx));

    /* Send initial announcement */
    pt_posix_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);

    return 0;
}

void pt_posix_discovery_stop(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);

    if (pd->discovery_sock >= 0) {
        /* Send goodbye */
        pt_posix_discovery_send(ctx, PT_DISC_TYPE_GOODBYE);

        close(pd->discovery_sock);
        pd->discovery_sock = -1;

        PT_LOG_INFO(ctx, PT_LOG_CAT_DISCOVERY, "Discovery stopped");
    }
}

int pt_posix_discovery_send(struct pt_context *ctx, uint8_t type) {
    pt_posix_data *pd = pt_posix_get(ctx);
    pt_discovery_packet pkt;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    struct sockaddr_in addr;
    int len;

    if (pd->discovery_sock < 0)
        return -1;

    /* Build packet */
    pkt.type = type;
    pkt.flags = PT_DISC_FLAG_ACCEPTING;  /* TODO: configurable */
    pkt.sender_port = ctx->config.listen_port > 0 ?
                      ctx->config.listen_port : TCP_PORT(ctx);

    pt_strncpy(pkt.name, ctx->local_name, PT_PEER_NAME_MAX + 1);
    pkt.name_len = pt_strlen(pkt.name);

    len = pt_discovery_encode(&pkt, buf, sizeof(buf));
    if (len < 0) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_DISCOVERY, "Failed to encode discovery packet");
        return -1;
    }

    /* Send to broadcast address */
    pt_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(pd->broadcast_addr);
    addr.sin_port = htons(DISCOVERY_PORT(ctx));

    if (sendto(pd->discovery_sock, buf, len, 0,
               (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_DISCOVERY,
            "Discovery broadcast failed: %s", strerror(errno));
        return -1;
    }

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_DISCOVERY,
        "Sent discovery %s", pt_discovery_type_str(type));

    return 0;
}

int pt_posix_discovery_poll(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t recv_len;
    pt_discovery_packet pkt;
    uint32_t from_ip;
    struct pt_peer *peer;

    if (pd->discovery_sock < 0)
        return 0;

    /* Non-blocking receive */
    recv_len = recvfrom(pd->discovery_sock, buf, sizeof(buf), 0,
                        (struct sockaddr *)&from_addr, &from_len);

    if (recv_len < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;  /* No data */
        PT_LOG_WARN(ctx, PT_LOG_CAT_DISCOVERY,
            "Discovery recv error: %s", strerror(errno));
        return -1;
    }

    from_ip = ntohl(from_addr.sin_addr.s_addr);

    /* Ignore our own broadcasts */
    if (from_ip == pd->local_ip)
        return 0;

    /* Decode packet */
    if (pt_discovery_decode(buf, recv_len, &pkt) != 0) {
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_DISCOVERY,
            "Invalid discovery packet from %08X", from_ip);
        return 0;
    }

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_DISCOVERY,
        "Discovery %s from \"%s\" at %08X:%u",
        pt_discovery_type_str(pkt.type), pkt.name, from_ip, pkt.sender_port);

    switch (pkt.type) {
    case PT_DISC_TYPE_ANNOUNCE:
        /* Create or update peer */
        peer = pt_peer_create(ctx, pkt.name, from_ip, pkt.sender_port);
        if (peer && ctx->callbacks.on_peer_discovered) {
            PeerTalk_PeerInfo info;
            pt_peer_get_info(peer, &info);
            ctx->callbacks.on_peer_discovered((PeerTalk_Context *)ctx,
                                              &info, ctx->callbacks.user_data);
        }
        break;

    case PT_DISC_TYPE_QUERY:
        /* Respond with announce */
        pt_posix_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);
        break;

    case PT_DISC_TYPE_GOODBYE:
        /* Find and remove peer */
        peer = pt_peer_find_by_addr(ctx, from_ip, pkt.sender_port);
        if (peer) {
            if (ctx->callbacks.on_peer_lost) {
                ctx->callbacks.on_peer_lost((PeerTalk_Context *)ctx,
                                            peer->id, ctx->callbacks.user_data);
            }
            pt_peer_destroy(ctx, peer);
        }
        break;
    }

    return 1;  /* Processed a packet */
}

/* Helper for logging */
static const char *pt_discovery_type_str(uint8_t type) {
    switch (type) {
    case PT_DISC_TYPE_ANNOUNCE: return "ANNOUNCE";
    case PT_DISC_TYPE_QUERY:    return "QUERY";
    case PT_DISC_TYPE_GOODBYE:  return "GOODBYE";
    default:               return "UNKNOWN";
    }
}
```

#### Task 4.1.3: Create `tests/test_discovery_posix.c`

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "peertalk.h"

static int peers_discovered = 0;
static volatile int running = 1;

void on_peer_discovered(PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer,
                        void *user_data) {
    (void)ctx; (void)user_data;
    char ip_str[16];
    uint32_t ip = peer->address;
    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
             (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
             (ip >> 8) & 0xFF, ip & 0xFF);
    printf("DISCOVERED: %s at %s:%u\n", peer->name, ip_str, peer->port);
    peers_discovered++;
}

void on_peer_lost(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                  void *user_data) {
    (void)ctx; (void)user_data;
    printf("LOST: peer %u\n", peer_id);
}

void sigint_handler(int sig) {
    (void)sig;
    running = 0;
}

int main(int argc, char **argv) {
    PeerTalk_Config config = {0};
    PeerTalk_Callbacks callbacks = {0};
    PeerTalk_Context *ctx;
    const char *name = argc > 1 ? argv[1] : "TestPeer";

    signal(SIGINT, sigint_handler);

    config.local_name = name;
    config.max_peers = 16;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        fprintf(stderr, "Failed to init PeerTalk\n");
        return 1;
    }

    /* Register callbacks via PeerTalk_SetCallbacks */
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_lost = on_peer_lost;
    PeerTalk_SetCallbacks(ctx, &callbacks);

    printf("Starting discovery as \"%s\"...\n", name);
    PeerTalk_StartDiscovery(ctx);

    /* Run for 30 seconds or until Ctrl-C */
    int iterations = 0;
    while (running && iterations < 300) {  /* 300 * 100ms = 30 seconds */
        PeerTalk_Poll(ctx);
        usleep(100000);  /* 100ms */

        /* Print status every 100 iterations (10 seconds) */
        if ((iterations % 100) == 0) {
            printf("... %d peers discovered so far\n", peers_discovered);
        }
        iterations++;
    }

    printf("\nStopping...\n");
    PeerTalk_StopDiscovery(ctx);
    PeerTalk_Shutdown(ctx);

    printf("Total peers discovered: %d\n", peers_discovered);
    return 0;
}
```

#### Task 4.1.4: Docker Testing Infrastructure

**Problem**: UDP broadcast discovery **cannot work** with two processes on the same host because
the kernel filters out broadcast packets sent from your own IP address. This is a fundamental
limitation of UDP broadcast on loopback.

**Solution**: Use Docker Compose with bridge networking to create isolated network namespaces
where each peer has a unique IP address on a virtual network segment that properly forwards
broadcasts between containers.

**Files to Create:**

1. **Dockerfile.test.build** - Self-contained build environment:

```dockerfile
FROM debian:bookworm-slim

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    net-tools \
    iputils-ping \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy entire project (build happens inside container)
COPY Makefile ./
COPY include ./include/
COPY src ./src/
COPY tests ./tests/

# Build the discovery test binary
RUN make test_discovery_posix

# Default command (overridden by docker-compose)
CMD ["/app/test_discovery_posix", "default"]
```

2. **docker-compose.test.yml** - Multi-peer test network:

```yaml
services:
  alice:
    build:
      context: .
      dockerfile: Dockerfile.test.build
    container_name: peertalk-alice
    command: ["/app/test_discovery_posix", "Alice"]
    tty: true
    stdin_open: true
    networks:
      peertalk_test_net:
        ipv4_address: 192.168.200.2

  bob:
    build:
      context: .
      dockerfile: Dockerfile.test.build
    container_name: peertalk-bob
    command: ["/app/test_discovery_posix", "Bob"]
    tty: true
    stdin_open: true
    networks:
      peertalk_test_net:
        ipv4_address: 192.168.200.3

  charlie:
    build:
      context: .
      dockerfile: Dockerfile.test.build
    container_name: peertalk-charlie
    command: ["/app/test_discovery_posix", "Charlie"]
    tty: true
    stdin_open: true
    networks:
      peertalk_test_net:
        ipv4_address: 192.168.200.4

networks:
  peertalk_test_net:
    driver: bridge
    ipam:
      config:
        - subnet: 192.168.200.0/24
```

3. **scripts/test-discovery-docker.sh** - Helper script for managing tests:

```bash
#!/bin/bash
# Test PeerTalk UDP discovery using Docker containers

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

# Commands
start_test() {
    echo "Building containers (this will compile inside Docker)..."
    docker-compose -f docker-compose.test.yml up --build -d

    echo ""
    echo "Waiting 5 seconds for discovery to occur..."
    sleep 5

    echo ""
    echo "========================================"
    echo "Alice's output:"
    echo "========================================"
    docker logs peertalk-alice 2>&1 | tail -20

    echo ""
    echo "========================================"
    echo "Bob's output:"
    echo "========================================"
    docker logs peertalk-bob 2>&1 | tail -20

    echo ""
    echo "========================================"
    echo "Charlie's output:"
    echo "========================================"
    docker logs peertalk-charlie 2>&1 | tail -20

    echo ""
    echo "Containers are still running. Use '$0 logs' to see updates."
    echo "Use '$0 stop' to stop the test."
}

stop_test() {
    echo "Stopping peer containers..."
    docker-compose -f docker-compose.test.yml down
}

show_logs() {
    local peer=${1:-all}

    if [ "$peer" = "all" ]; then
        echo "=== Alice ==="
        docker logs peertalk-alice 2>&1 | tail -30
        echo ""
        echo "=== Bob ==="
        docker logs peertalk-bob 2>&1 | tail -30
        echo ""
        echo "=== Charlie ==="
        docker logs peertalk-charlie 2>&1 | tail -30
    else
        docker logs "peertalk-$peer" 2>&1
    fi
}

follow_logs() {
    local peer=${1:-alice}
    docker logs -f "peertalk-$peer" 2>&1
}

show_status() {
    docker-compose -f docker-compose.test.yml ps
}

case "$1" in
    start)
        start_test
        ;;
    stop)
        stop_test
        ;;
    logs)
        show_logs "$2"
        ;;
    follow)
        follow_logs "$2"
        ;;
    status)
        show_status
        ;;
    *)
        echo "Usage: $0 {start|stop|status|logs [peer]|follow [peer]}"
        echo ""
        echo "Commands:"
        echo "  start           - Build and start 3 peer containers"
        echo "  stop            - Stop all containers"
        echo "  status          - Show container status"
        echo "  logs [peer]     - Show logs (alice/bob/charlie/all)"
        echo "  follow [peer]   - Follow logs for a peer (default: alice)"
        echo ""
        echo "Example:"
        echo "  $0 start        # Start the test"
        echo "  $0 logs bob     # Check Bob's logs"
        echo "  $0 follow alice # Follow Alice's output"
        echo "  $0 stop         # Stop the test"
        ;;
esac
```

**Usage:**

```bash
chmod +x scripts/test-discovery-docker.sh
./scripts/test-discovery-docker.sh start    # Build and launch 3 peers
./scripts/test-discovery-docker.sh logs     # Check all logs
./scripts/test-discovery-docker.sh logs bob # Check Bob's logs only
./scripts/test-discovery-docker.sh follow alice # Follow Alice's logs in real-time
./scripts/test-discovery-docker.sh stop     # Clean up
```

**Expected Results:**

All 3 peers should discover each other:
- Alice discovers Bob & Charlie (2 peers)
- Bob discovers Alice & Charlie (2 peers)
- Charlie discovers Alice & Bob (2 peers)

Each peer should log:
- Local IP detection (192.168.200.x)
- Discovery ANNOUNCE sent to broadcast address
- Discovery packets received from other peers
- Peer creation with full address details
- Callbacks firing correctly

**Architecture Benefits:**

1. **Network Isolation** - Each container has its own network namespace with unique IP
2. **Broadcast Works** - Docker bridge network properly forwards broadcasts between containers
3. **Reproducible** - Same environment every time, no host network interference
4. **Self-Contained** - Builds inside Docker, no host build tools required (~30s rebuild)
5. **CI/CD Ready** - Can run in automated testing pipelines
6. **Multi-Platform** - Works on Linux, macOS (Docker Desktop), Windows (WSL2)

### Acceptance Criteria
1. UDP socket binds successfully to broadcast port
2. Announce packets are sent on start
3. Packets from other peers are received and parsed
4. Peer list is updated on ANNOUNCE
5. Own broadcasts are ignored
6. GOODBYE removes peer from list
7. QUERY triggers ANNOUNCE response
8. Callbacks fire correctly
9. **Docker-based multi-peer testing works** (3 containers discover each other)
10. **Peer list initialization** is called in `PeerTalk_Init()` before peer operations
11. **Discovery packets include `name_len`** field properly set
12. **Socket helpers log errors** using context parameter

---

## Session 4.2: TCP Connections

### Objective
Implement TCP connection management with non-blocking connect and accept.

### Tasks

#### Task 4.2.1: Implement TCP listener

```c
/* Add to src/posix/net_posix.c */

int pt_posix_listen_start(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    struct sockaddr_in addr;
    int sock;
    uint16_t port;

    port = ctx->config.listen_port > 0 ?
           ctx->config.listen_port : TCP_PORT(ctx);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "Failed to create listen socket: %s", strerror(errno));
        return -1;
    }

    if (set_nonblocking(ctx, sock) < 0) {
        /* Error already logged by helper */
        close(sock);
        return -1;
    }

    if (set_reuseaddr(ctx, sock) < 0) {
        /* Error already logged by helper */
        close(sock);
        return -1;
    }

    pt_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "Failed to bind listen socket: %s", strerror(errno));
        close(sock);
        return -1;
    }

    if (listen(sock, 8) < 0) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "Listen failed: %s", strerror(errno));
        close(sock);
        return -1;
    }

    pd->listen_sock = sock;
    pd->listen_port = port;

    if (sock > pd->max_fd)
        pd->max_fd = sock;

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Listening on port %u", port);

    return 0;
}

void pt_posix_listen_stop(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);

    if (pd->listen_sock >= 0) {
        close(pd->listen_sock);
        pd->listen_sock = -1;
        PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT, "Listen stopped");
    }
}

int pt_posix_listen_poll(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int client_sock;
    uint32_t client_ip;
    struct pt_peer *peer;

    if (pd->listen_sock < 0)
        return 0;

    /* Non-blocking accept */
    client_sock = accept(pd->listen_sock, (struct sockaddr *)&addr, &addr_len);
    if (client_sock < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "Accept error: %s", strerror(errno));
        return -1;
    }

    set_nonblocking(client_sock);

    client_ip = ntohl(addr.sin_addr.s_addr);

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Incoming connection from 0x%08X", client_ip);

    /* Find or create peer */
    peer = pt_peer_find_by_addr(ctx, client_ip, 0);
    if (!peer) {
        /* Unknown peer - create with empty name */
        peer = pt_peer_create(ctx, "", client_ip, ntohs(addr.sin_port));
    }

    if (!peer) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No peer slot for incoming connection");
        close(client_sock);
        return 0;
    }

    /* Store socket and reset receive state */
    int peer_idx = peer->id - 1;
    pd->tcp_socks[peer_idx] = client_sock;
    pd->recv_bufs[peer_idx].state = PT_RECV_HEADER;
    pd->recv_bufs[peer_idx].bytes_needed = PT_MESSAGE_HEADER_SIZE;
    pd->recv_bufs[peer_idx].bytes_received = 0;

    /* Add to active peers list and mark fd_sets dirty */
    pt_posix_add_active_peer(pd, peer_idx);

    /* Update state with logging */
    pt_log_state_transition(ctx, peer, peer->state, PT_PEER_CONNECTED);
    pt_peer_set_state(peer, PT_PEER_CONNECTED);
    peer->last_seen = ctx->plat->get_ticks();

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Accepted connection from peer %u at 0x%08X (assigned to slot %u)",
        peer->id, client_ip, peer_idx);

    /* Fire callback */
    if (ctx->callbacks.on_peer_connected) {
        ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                         peer->id, ctx->callbacks.user_data);
    }

    return 1;
}
```

#### Task 4.2.2: Implement TCP connect (non-blocking)

```c
int pt_posix_connect(struct pt_context *ctx, struct pt_peer *peer) {
    pt_posix_data *pd = pt_posix_get(ctx);
    struct sockaddr_in addr;
    int sock;
    int result;

    if (!peer || peer->magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    if (peer->state != PT_PEER_DISCOVERED)
        return PT_ERR_INVALID_STATE;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "Failed to create socket: %s", strerror(errno));
        return PT_ERR_NETWORK;
    }

    set_nonblocking(ctx, sock);

    pt_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(peer->ip_addr);
    addr.sin_port = htons(peer->port);

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Connecting to peer %u (%s) at 0x%08X:%u",
        peer->id, peer->name, peer->ip_addr, peer->port);

    result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));

    if (result < 0) {
        if (errno == EINPROGRESS) {
            /* Connection in progress - this is expected for non-blocking */
            int peer_idx = peer->id - 1;
            pd->tcp_socks[peer_idx] = sock;
            pd->recv_bufs[peer_idx].state = PT_RECV_HEADER;
            pd->recv_bufs[peer_idx].bytes_needed = PT_MESSAGE_HEADER_SIZE;
            pd->recv_bufs[peer_idx].bytes_received = 0;

            /* Add to active peers list and mark fd_sets dirty */
            pt_posix_add_active_peer(pd, peer_idx);

            pt_log_state_transition(ctx, peer, peer->state, PT_PEER_CONNECTING);
            pt_peer_set_state(peer, PT_PEER_CONNECTING);
            peer->connect_start = ctx->plat->get_ticks();
            return PT_OK;
        }

        PT_LOG_ERR(ctx, PT_LOG_CAT_CONNECT,
            "Connect failed to peer %u (%s) at 0x%08X:%u: %s",
            peer->id, peer->name, peer->ip_addr, peer->port, strerror(errno));
        close(sock);
        pt_log_state_transition(ctx, peer, peer->state, PT_PEER_FAILED);
        pt_peer_set_state(peer, PT_PEER_FAILED);
        return PT_ERR_NETWORK;
    }

    /* Immediate connection (unlikely but possible on localhost) */
    int peer_idx = peer->id - 1;
    pd->tcp_socks[peer_idx] = sock;
    pd->recv_bufs[peer_idx].state = PT_RECV_HEADER;
    pd->recv_bufs[peer_idx].bytes_needed = PT_MESSAGE_HEADER_SIZE;
    pd->recv_bufs[peer_idx].bytes_received = 0;

    /* Add to active peers list and mark fd_sets dirty */
    pt_posix_add_active_peer(pd, peer_idx);

    pt_log_state_transition(ctx, peer, peer->state, PT_PEER_CONNECTED);
    pt_peer_set_state(peer, PT_PEER_CONNECTED);
    peer->last_seen = ctx->plat->get_ticks();

    PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Connected to peer %u (%s) - immediate connect on localhost",
        peer->id, peer->name);

    if (ctx->callbacks.on_peer_connected) {
        ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                         peer->id, ctx->callbacks.user_data);
    }

    return PT_OK;
}

int pt_posix_disconnect(struct pt_context *ctx, struct pt_peer *peer) {
    pt_posix_data *pd = pt_posix_get(ctx);
    int peer_idx;
    int sock;

    if (!peer || peer->magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    peer_idx = peer->id - 1;
    sock = pd->tcp_socks[peer_idx];

    if (sock >= 0) {
        PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
            "Disconnecting peer %u (%s)", peer->id, peer->name);

        /* Send disconnect message if connected */
        if (peer->state == PT_PEER_CONNECTED) {
            pt_message_header hdr;
            uint8_t buf[PT_MESSAGE_HEADER_SIZE + 2];

            hdr.type = PT_MSG_DISCONNECT;
            hdr.flags = 0;
            hdr.sequence = peer->send_seq++;
            hdr.payload_len = 0;

            pt_message_encode_header(&hdr, buf);
            /* Add CRC for empty payload */
            uint16_t crc = pt_crc16(buf, PT_MESSAGE_HEADER_SIZE);
            buf[PT_MESSAGE_HEADER_SIZE] = (crc >> 8) & 0xFF;
            buf[PT_MESSAGE_HEADER_SIZE + 1] = crc & 0xFF;

            send(sock, buf, sizeof(buf), 0);
        }

        close(sock);
        pd->tcp_socks[peer_idx] = -1;

        /* Remove from active peers list */
        pt_posix_remove_active_peer(pd, peer_idx);
    }

    pt_log_state_transition(ctx, peer, peer->state, PT_PEER_DISCONNECTING);
    pt_peer_set_state(peer, PT_PEER_DISCONNECTING);

    if (ctx->callbacks.on_peer_disconnected) {
        ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                            peer->id, PT_OK,
                                            ctx->callbacks.user_data);
    }

    pt_log_state_transition(ctx, peer, PT_PEER_DISCONNECTING, PT_PEER_UNUSED);
    pt_peer_set_state(peer, PT_PEER_UNUSED);

    return PT_OK;
}
```

#### Task 4.2.3: Create `tests/test_connect_posix.c`

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "peertalk.h"

static PeerTalk_PeerInfo target_peer;
static int connected = 0;
static int is_server = 0;

void on_peer_discovered(PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer,
                        void *user_data) {
    (void)ctx; (void)user_data;
    printf("Discovered: %s\n", peer->name);
    if (!is_server && !connected) {
        pt_memcpy(&target_peer, peer, sizeof(target_peer));
    }
}

void on_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                  void *user_data) {
    (void)ctx; (void)user_data;
    printf("CONNECTED to peer %u\n", peer_id);
    connected = 1;
}

void on_disconnected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                     PeerTalk_Error reason, void *user_data) {
    (void)ctx; (void)reason; (void)user_data;
    printf("DISCONNECTED from peer %u\n", peer_id);
    connected = 0;
}

int main(int argc, char **argv) {
    PeerTalk_Config config = {0};
    PeerTalk_Callbacks callbacks = {0};
    PeerTalk_Context *ctx;

    is_server = (argc > 1 && strcmp(argv[1], "server") == 0);

    config.local_name = is_server ? "Server" : "Client";
    config.max_peers = 16;

    /* Use different ports for server vs client to avoid conflicts */
    if (is_server) {
        config.discovery_port = 7353;
        config.tcp_port = 7354;
    } else {
        config.discovery_port = 7353;  /* Same discovery port to find each other */
        config.tcp_port = 7355;        /* Different TCP port */
    }

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        fprintf(stderr, "Init failed\n");
        return 1;
    }

    /* Register callbacks via PeerTalk_SetCallbacks */
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_connected = on_connected;
    callbacks.on_peer_disconnected = on_disconnected;
    PeerTalk_SetCallbacks(ctx, &callbacks);

    PeerTalk_StartDiscovery(ctx);

    printf("Running as %s...\n", config.local_name);

    for (int i = 0; i < 100; i++) {
        PeerTalk_Poll(ctx);

        /* Client: try to connect to first discovered peer */
        if (!is_server && !connected && target_peer.id != 0) {
            printf("Attempting connection to %s...\n", target_peer.name);
            int result = PeerTalk_Connect(ctx, target_peer.id);
            printf("Connect result: %d\n", result);
        }

        usleep(100000);
    }

    if (connected) {
        printf("Test PASSED - connection established\n");
    } else {
        printf("Test INCOMPLETE - no connection\n");
    }

    PeerTalk_Shutdown(ctx);
    return connected ? 0 : 1;
}
```

### Acceptance Criteria
1. Listen socket binds and accepts connections
2. Non-blocking connect works (returns immediately)
3. CONNECTING state is tracked during connection
4. CONNECTED state is set when connection completes
5. Callbacks fire on connect/disconnect
6. Graceful disconnect sends DISCONNECT message
7. Socket cleanup on disconnect

---

## Session 4.3: Message I/O

### Objective
Implement message send/receive with proper framing and queue integration.

### Tasks

#### Task 4.3.1: Implement send function

```c
/*
 * Send a message to a connected peer using writev() for atomic transmission.
 *
 * Uses writev() to send header + payload + CRC in a single syscall.
 * This is more efficient than multiple send() calls and avoids potential
 * issues with partial sends fragmenting the message frame.
 *
 * For large payloads that exceed socket buffer, writev() may return partial
 * writes. This function handles that by looping until all data is sent.
 */
int pt_posix_send(struct pt_context *ctx, struct pt_peer *peer,
                  const void *data, size_t len) {
    pt_posix_data *pd = pt_posix_get(ctx);
    pt_message_header hdr;
    uint8_t header_buf[PT_MESSAGE_HEADER_SIZE];
    uint8_t crc_buf[2];
    uint16_t crc;
    int sock;
    struct iovec iov[3];
    int iov_count;
    ssize_t total_len, sent, total_sent;

    if (!peer || peer->magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    if (peer->state != PT_PEER_CONNECTED)
        return PT_ERR_INVALID_STATE;

    if (len > PT_MAX_MESSAGE_SIZE)
        return PT_ERR_MESSAGE_TOO_LARGE;

    sock = pd->tcp_socks[peer->id - 1];
    if (sock < 0)
        return PT_ERR_INVALID_STATE;

    /* Build header */
    hdr.type = PT_MSG_DATA;
    hdr.flags = 0;
    hdr.sequence = peer->send_seq++;
    hdr.payload_len = len;

    pt_message_encode_header(&hdr, header_buf);

    /* Calculate CRC over header + payload using incremental function */
    crc = pt_crc16(header_buf, PT_MESSAGE_HEADER_SIZE);
    if (len > 0) {
        crc = pt_crc16_update(crc, data, len);
    }
    crc_buf[0] = (crc >> 8) & 0xFF;
    crc_buf[1] = crc & 0xFF;

    /*
     * Build iovec for writev() - sends header + payload + CRC atomically.
     * This avoids the TCP Nagle algorithm issues with multiple small sends
     * and reduces syscall overhead from 3 calls to 1.
     */
    iov[0].iov_base = header_buf;
    iov[0].iov_len = PT_MESSAGE_HEADER_SIZE;

    if (len > 0) {
        iov[1].iov_base = (void *)data;  /* Cast away const for iovec */
        iov[1].iov_len = len;
        iov[2].iov_base = crc_buf;
        iov[2].iov_len = 2;
        iov_count = 3;
        total_len = PT_MESSAGE_HEADER_SIZE + len + 2;
    } else {
        /* Zero-payload message: header + CRC only */
        iov[1].iov_base = crc_buf;
        iov[1].iov_len = 2;
        iov_count = 2;
        total_len = PT_MESSAGE_HEADER_SIZE + 2;
    }

    /* Send with writev(), handling partial writes */
    total_sent = 0;
    while (total_sent < total_len) {
        sent = writev(sock, iov, iov_count);

        if (sent < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                /* Socket buffer full - would need to buffer and retry later.
                 * For simplicity, we fail here. A production implementation
                 * might queue the remaining data and retry on next poll. */
                PT_LOG_WARN(ctx, PT_LOG_CAT_SEND,
                    "Send would block after %zd/%zd bytes", total_sent, total_len);
                return PT_ERR_WOULD_BLOCK;
            }
            PT_LOG_WARN(ctx, PT_LOG_CAT_SEND,
                "Send failed: %s", strerror(errno));
            return PT_ERR_NETWORK;
        }

        if (sent == 0) {
            PT_LOG_WARN(ctx, PT_LOG_CAT_SEND, "Send returned 0");
            return PT_ERR_NETWORK;
        }

        total_sent += sent;

        /* Adjust iovec for partial write */
        if (total_sent < total_len) {
            ssize_t remaining = sent;
            for (int i = 0; i < iov_count; i++) {
                if ((size_t)remaining < iov[i].iov_len) {
                    iov[i].iov_base = (char *)iov[i].iov_base + remaining;
                    iov[i].iov_len -= remaining;
                    break;
                }
                remaining -= iov[i].iov_len;
                iov[i].iov_len = 0;
            }
        }
    }

    /* Update statistics */
    peer->stats.bytes_sent += len;
    peer->stats.messages_sent++;
    ctx->global_stats.bytes_sent += len;
    ctx->global_stats.messages_sent++;

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_SEND,
        "Sent %zu bytes to peer %u (seq=%u)",
        len, peer->id, hdr.sequence);

    return PT_OK;
}
```

#### Task 4.3.2: Implement non-blocking receive function

```c
/*
 * Non-blocking receive with state machine for partial reads.
 *
 * TCP is a stream protocol - we may receive partial messages.
 * These functions use a state machine to reassemble messages
 * across multiple poll cycles without blocking.
 *
 * DOD: Split into small helper functions for better instruction cache
 * behavior on 68k. Each handler is ~30 lines, fits in 68030 I-cache.
 *
 * State handlers return:
 *   1  = State complete, continue to next state
 *   0  = Waiting for more data (call again later)
 *  -1  = Error or connection closed
 */

/* Reset receive buffer to initial state */
static inline void pt_recv_reset(pt_recv_buffer *rb) {
    rb->state = PT_RECV_HEADER;
    rb->bytes_needed = PT_MESSAGE_HEADER_SIZE;
    rb->bytes_received = 0;
}

/* State handler: receive header bytes */
static int pt_recv_header(struct pt_context *ctx, struct pt_peer *peer,
                          pt_recv_buffer *rb, int sock) {
    ssize_t received;

    received = recv(sock,
                    rb->header_buf + rb->bytes_received,
                    rb->bytes_needed,
                    0);

    if (received < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;  /* No data yet */
        PT_LOG_WARN(ctx, PT_LOG_CAT_RECV,
            "Header recv error: %s", strerror(errno));
        return -1;
    }

    if (received == 0) {
        PT_LOG_INFO(ctx, PT_LOG_CAT_RECV,
            "Peer %u closed connection", peer->id);
        return -1;
    }

    rb->bytes_received += received;
    rb->bytes_needed -= received;

    if (rb->bytes_needed > 0)
        return 0;  /* Header incomplete, wait for more */

    /* Header complete - parse it */
    if (pt_message_decode_header(rb->header_buf, PT_MESSAGE_HEADER_SIZE,
                                 &rb->hdr) < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_RECV,
            "Invalid header from peer %u: decode failed", peer->id);
        pt_recv_reset(rb);
        return -1;
    }

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_RECV,
        "Header complete from peer %u: type=%u seq=%u payload_len=%u",
        peer->id, rb->hdr.type, rb->hdr.sequence, rb->hdr.payload_len);

    /* Validate payload size against our buffer limit */
    if (rb->hdr.payload_len > PT_MAX_MESSAGE_SIZE) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_RECV,
            "Buffer overflow attempt from peer %u: payload_len=%u max=%u",
            peer->id, rb->hdr.payload_len, PT_MAX_MESSAGE_SIZE);
        pt_recv_reset(rb);
        return -1;
    }

    /* Transition to next state */
    if (rb->hdr.payload_len > 0) {
        rb->state = PT_RECV_PAYLOAD;
        rb->bytes_needed = rb->hdr.payload_len;
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_RECV,
            "State: HEADER -> PAYLOAD (%u bytes needed)", rb->hdr.payload_len);
    } else {
        rb->state = PT_RECV_CRC;
        rb->bytes_needed = 2;
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_RECV,
            "State: HEADER -> CRC (zero-payload message)");
    }
    rb->bytes_received = 0;
    return 1;  /* Continue to next state */
}

/* State handler: receive payload bytes */
static int pt_recv_payload(struct pt_context *ctx, struct pt_peer *peer,
                           pt_recv_buffer *rb, int sock) {
    ssize_t received;

    received = recv(sock,
                    rb->payload_buf + rb->bytes_received,
                    rb->bytes_needed,
                    0);

    if (received < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;
        PT_LOG_WARN(ctx, PT_LOG_CAT_RECV,
            "Payload recv error: %s", strerror(errno));
        return -1;
    }

    if (received == 0) {
        PT_LOG_INFO(ctx, PT_LOG_CAT_RECV,
            "Peer %u closed connection during payload", peer->id);
        return -1;
    }

    rb->bytes_received += received;
    rb->bytes_needed -= received;

    if (rb->bytes_needed > 0)
        return 0;  /* Payload incomplete */

    /* Payload complete - move to CRC */
    rb->state = PT_RECV_CRC;
    rb->bytes_needed = 2;
    rb->bytes_received = 0;
    PT_LOG_DEBUG(ctx, PT_LOG_CAT_RECV,
        "State: PAYLOAD -> CRC (payload received)");
    return 1;
}

/* State handler: receive CRC bytes */
static int pt_recv_crc(struct pt_context *ctx, struct pt_peer *peer,
                       pt_recv_buffer *rb, int sock) {
    ssize_t received;

    received = recv(sock,
                    rb->crc_buf + rb->bytes_received,
                    rb->bytes_needed,
                    0);

    if (received < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;
        PT_LOG_WARN(ctx, PT_LOG_CAT_RECV,
            "CRC recv error: %s", strerror(errno));
        return -1;
    }

    if (received == 0) {
        PT_LOG_INFO(ctx, PT_LOG_CAT_RECV,
            "Peer %u closed connection during CRC", peer->id);
        return -1;
    }

    rb->bytes_received += received;
    rb->bytes_needed -= received;

    if (rb->bytes_needed > 0)
        return 0;  /* CRC incomplete */

    return 1;  /* CRC complete */
}

/* Process a complete message after CRC validation */
static int pt_recv_process_message(struct pt_context *ctx, struct pt_peer *peer,
                                   pt_recv_buffer *rb) {
    uint16_t crc_expected, crc_actual;

    /* Verify CRC */
    crc_expected = (rb->crc_buf[0] << 8) | rb->crc_buf[1];
    crc_actual = pt_crc16(rb->header_buf, PT_MESSAGE_HEADER_SIZE);
    if (rb->hdr.payload_len > 0) {
        crc_actual = pt_crc16_update(crc_actual, rb->payload_buf, rb->hdr.payload_len);
    }

    if (crc_actual != crc_expected) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_PROTOCOL,
            "CRC mismatch from peer %u: expected=%04X actual=%04X "
            "type=%u seq=%u len=%u",
            peer->id, crc_expected, crc_actual,
            rb->hdr.type, rb->hdr.sequence, rb->hdr.payload_len);
        return -1;
    }

    /* Update peer state */
    peer->last_seen = ctx->plat->get_ticks();
    peer->recv_seq = rb->hdr.sequence;
    pt_peer_check_canaries(peer);

    /* Update statistics */
    peer->stats.bytes_received += rb->hdr.payload_len;
    peer->stats.messages_received++;
    ctx->global_stats.bytes_received += rb->hdr.payload_len;
    ctx->global_stats.messages_received++;

    /* Handle message by type */
    switch (rb->hdr.type) {
    case PT_MSG_DATA:
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_RECV,
            "Received %u bytes from peer %u (seq=%u)",
            rb->hdr.payload_len, peer->id, rb->hdr.sequence);

        /*
         * BATCH CALLBACK SUPPORT:
         * If batch callback is set, accumulate messages for batch processing.
         * Otherwise, fire per-message callback immediately.
         *
         * This allows applications to choose between:
         * - Per-message callbacks (simpler, immediate processing)
         * - Batch callbacks (more efficient, processes multiple messages at once)
         */
        if (ctx->callbacks.on_message_batch) {
            pt_posix_data *pd = pt_posix_get(ctx);
            if (pd->batch_count < PT_MAX_BATCH_SIZE) {
                pd->msg_batch[pd->batch_count].from_peer = peer->id;
                pd->msg_batch[pd->batch_count].data = rb->payload_buf;
                pd->msg_batch[pd->batch_count].length = rb->hdr.payload_len;
                pd->msg_batch[pd->batch_count].reserved = 0;
                pd->batch_count++;
                PT_LOG_DEBUG(ctx, PT_LOG_CAT_RECV,
                    "Batch accumulated: %u messages pending", pd->batch_count);
            } else {
                PT_LOG_WARN(ctx, PT_LOG_CAT_RECV,
                    "Message batch full (%u messages), dropping message from peer %u",
                    PT_MAX_BATCH_SIZE, peer->id);
            }
        } else if (ctx->callbacks.on_message_received) {
            ctx->callbacks.on_message_received(
                (PeerTalk_Context *)ctx,
                peer->id, rb->payload_buf, rb->hdr.payload_len,
                ctx->callbacks.user_data);
        }
        break;

    case PT_MSG_PING:
        pt_posix_send_control(ctx, peer, PT_MSG_PONG);
        break;

    case PT_MSG_PONG:
        pt_posix_pong_received(ctx, peer);
        break;

    case PT_MSG_DISCONNECT:
        PT_LOG_INFO(ctx, PT_LOG_CAT_RECV,
            "Received DISCONNECT from peer %u", peer->id);
        return -1;

    case PT_MSG_ACK:
        /* Handle ACK for reliable messages */
        break;
    }

    return 1;  /* Message processed */
}

/*
 * Main receive function - dispatches to state handlers.
 *
 * Returns:
 *   1  = Complete message received and processed
 *   0  = No data or partial data (call again later)
 *  -1  = Error or connection closed
 */
int pt_posix_recv(struct pt_context *ctx, struct pt_peer *peer) {
    pt_posix_data *pd = pt_posix_get(ctx);
    pt_recv_buffer *rb;
    int sock;
    int result;

    if (!peer || peer->magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    sock = pd->tcp_socks[peer->id - 1];
    if (sock < 0)
        return PT_ERR_INVALID_STATE;

    rb = &pd->recv_bufs[peer->id - 1];

    /* Dispatch to current state handler */
    switch (rb->state) {
    case PT_RECV_HEADER:
        result = pt_recv_header(ctx, peer, rb, sock);
        if (result <= 0)
            return result;
        /* Fall through if header complete and more data available */
        if (rb->state == PT_RECV_CRC)
            goto try_crc;
        return 0;

    case PT_RECV_PAYLOAD:
        result = pt_recv_payload(ctx, peer, rb, sock);
        if (result <= 0)
            return result;
        /* Fall through to CRC */

    try_crc:
    case PT_RECV_CRC:
        result = pt_recv_crc(ctx, peer, rb, sock);
        if (result <= 0)
            return result;
        break;

    default:
        /* Invalid state - reset */
        pt_recv_reset(rb);
        return 0;
    }

    /* Message complete - reset state BEFORE processing */
    pt_recv_reset(rb);

    /* Process the complete message */
    return pt_recv_process_message(ctx, peer, rb);
}

/*
 * Send control message (ping/pong/disconnect)
 * These are zero-payload messages used for connection management.
 */
int pt_posix_send_control(struct pt_context *ctx, struct pt_peer *peer,
                          uint8_t msg_type) {
    pt_posix_data *pd = pt_posix_get(ctx);
    pt_message_header hdr;
    uint8_t buf[PT_MESSAGE_HEADER_SIZE + 2];
    uint16_t crc;
    int sock;
    ssize_t sent;

    if (!peer || peer->magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    sock = pd->tcp_socks[peer->id - 1];
    if (sock < 0)
        return PT_ERR_INVALID_STATE;

    /* Build header for zero-payload control message.
     *
     * PROTOCOL NOTE: Control messages (PING, PONG, DISCONNECT) use sequence=0
     * intentionally. This is protocol-compliant because:
     * 1. Control messages are connection management, not user data
     * 2. They don't require ordering guarantees (idempotent operations)
     * 3. Using sequence=0 distinguishes them from user messages in logs/debugging
     * 4. The receiver should NOT update recv_seq from control messages
     */
    hdr.type = msg_type;
    hdr.flags = 0;
    hdr.sequence = 0;  /* Control messages: sequence=0 is intentional */
    hdr.payload_len = 0;

    pt_message_encode_header(&hdr, buf);

    /* CRC covers header only (no payload) */
    crc = pt_crc16(buf, PT_MESSAGE_HEADER_SIZE);
    buf[PT_MESSAGE_HEADER_SIZE] = (crc >> 8) & 0xFF;
    buf[PT_MESSAGE_HEADER_SIZE + 1] = crc & 0xFF;

    /* Send header + CRC */
    sent = send(sock, buf, sizeof(buf), 0);
    if (sent != sizeof(buf)) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_SEND,
            "Failed to send control message: %s", strerror(errno));
        return PT_ERR_NETWORK;
    }

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_SEND,
        "Sent control message type=%u to peer %u", msg_type, peer->id);

    return PT_OK;
}
```

#### Task 4.3.3: Create `tests/test_messaging_posix.c`

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "peertalk.h"
#include "pt_compat.h"

static int messages_received = 0;
static char last_message[256];

void on_message(PeerTalk_Context *ctx, PeerTalk_PeerID from_peer,
                const void *data, uint16_t length, void *user_data) {
    (void)ctx; (void)user_data;
    messages_received++;
    uint16_t copy_len = length < 255 ? length : 255;
    pt_memcpy(last_message, data, copy_len);
    last_message[copy_len] = '\0';
    printf("Received message from peer %u: \"%s\"\n", from_peer, last_message);
}

int main(int argc, char **argv) {
    PeerTalk_Config config = {0};
    PeerTalk_Callbacks callbacks = {0};
    PeerTalk_Context *ctx1, *ctx2;

    (void)argc;
    (void)argv;

    config.max_peers = 4;

    /* Create two contexts with DIFFERENT ports to avoid bind conflicts */
    config.local_name = "Peer1";
    config.discovery_port = 7360;  /* Unique discovery port for Peer1 */
    config.tcp_port = 7361;
    ctx1 = PeerTalk_Init(&config);

    config.local_name = "Peer2";
    config.discovery_port = 7360;  /* Same discovery port to find each other */
    config.tcp_port = 7362;        /* Different TCP port */
    ctx2 = PeerTalk_Init(&config);

    /* Register callbacks on both contexts */
    callbacks.on_message_received = on_message;
    PeerTalk_SetCallbacks(ctx1, &callbacks);
    PeerTalk_SetCallbacks(ctx2, &callbacks);

    PeerTalk_StartDiscovery(ctx1);
    PeerTalk_StartDiscovery(ctx2);

    /* Wait for discovery */
    for (int i = 0; i < 20; i++) {
        PeerTalk_Poll(ctx1);
        PeerTalk_Poll(ctx2);
        usleep(100000);
    }

    /* Get peer list */
    PeerTalk_PeerInfo peers[4];
    uint16_t count;
    PeerTalk_GetPeers(ctx1, peers, 4, &count);
    printf("Peer1 sees %u peers\n", count);

    if (count > 0) {
        /* Connect */
        printf("Connecting to %s...\n", peers[0].name);
        PeerTalk_Connect(ctx1, peers[0].id);

        /* Wait for connection */
        for (int i = 0; i < 20; i++) {
            PeerTalk_Poll(ctx1);
            PeerTalk_Poll(ctx2);
            usleep(100000);
        }

        /* Send message */
        char msg[] = "Hello from Peer1!";
        int result = PeerTalk_Send(ctx1, peers[0].id, msg, strlen(msg) + 1);
        printf("Send result: %d\n", result);

        /* Receive */
        for (int i = 0; i < 20; i++) {
            PeerTalk_Poll(ctx1);
            PeerTalk_Poll(ctx2);
            usleep(100000);
        }
    }

    PeerTalk_Shutdown(ctx1);
    PeerTalk_Shutdown(ctx2);

    if (messages_received > 0) {
        printf("TEST PASSED - %d messages received\n", messages_received);
        return 0;
    } else {
        printf("TEST FAILED - no messages received\n");
        return 1;
    }
}
```

### Acceptance Criteria
1. Messages are correctly framed with header and CRC
2. Send completes successfully for connected peers
3. Receive handles partial reads without blocking
4. CRC validation catches corruption
5. PING/PONG messages work
6. DISCONNECT message triggers clean disconnect
7. Callback fires with correct data

---

## Session 4.3.5: Queue Integration with Connection Lifecycle

> **Implementation Status (2026-02-04):** ✅ COMPLETE
>
> **Code Review Findings:** Queue integration is fully implemented in `src/posix/net_posix.c`:
> - **Lines 895-908:** Queue allocation on TCP connection establishment
> - **Lines 1015-1027:** Queue allocation on UDP connection
> - **Lines 1102-1105:** Queue cleanup on peer disconnection
> - **Line 2221:** `pt_queue_pop_priority_direct()` being used in poll loop for batch send
>
> **Test Status:** Integration tests passing (test_integration_full.c, test_integration_posix.c)
>
> **Reference Implementation:** This session's patterns are ready for replication in Phase 5 (MacTCP) and Phase 6 (Open Transport).

### Objective
Integrate Phase 3 queue infrastructure with POSIX connection lifecycle to enable reliable message queuing and delivery. This completes the POSIX reference implementation by connecting the low-level I/O (Session 4.3) with the queue-based SendEx API (Phase 3.5).

### Problem Statement (Original Design Document)

**Original Analysis (Pre-Implementation):**
- ✅ `PeerTalk_SendEx()` API implemented (Phase 3.5)
- ✅ `pt_posix_send()` low-level send function implemented (Session 4.3)
- ✅ `pt_posix_recv()` low-level receive function implemented (Session 4.3)
- ✅ `pt_drain_send_queue()` batch send helper implemented (Phase 3)
- ❌ **Queues are NEVER initialized when peers connect** (Now: ✅ IMPLEMENTED)
- ❌ **`pt_drain_send_queue()` is NEVER called in poll loop** (Now: ✅ IMPLEMENTED)

**Original Expected Failure:** `PeerTalk_SendEx()` fails with `PT_ERR_INVALID_STATE` ("Peer has no send queue") even though peers are connected.

**Actual Status:** This issue has been resolved by implementing queue allocation in the connection lifecycle.

**Integration Test Evidence:**
```
[00002085][ERR] SendEx failed: Peer 1 has no send queue
Messages sent: 0
Messages received: 0
```

### Architecture Overview

**Queue Lifecycle:**
```
Connection Established (accept or connect)
    ↓
Initialize send_queue and recv_queue
    ↓
Poll Loop: drain send_queue → pt_posix_send()
    ↓
Poll Loop: pt_posix_recv() → fire callback (or enqueue to recv_queue)
    ↓
Disconnection/Failure
    ↓
Free send_queue and recv_queue
```

**Memory Management:**
- Queues are dynamically allocated pointers in `struct pt_peer`
- Capacity: 16 slots per queue (good balance for POSIX, adequate for MacTCP)
- Each slot: 268 bytes (12-byte metadata + 256-byte payload)
- Total per peer: ~8.6 KB for both queues

**DOD Consideration:**
16-slot queues were chosen to balance:
- POSIX: Can handle bursts without backpressure
- MacTCP: Fits comfortably in 68k memory constraints
- Cache efficiency: 16 * 268 = 4.3 KB per queue (reasonable for 68030's 256-byte cache)

### Tasks

#### Task 4.3.5.1: Add queue allocation helper

Create a helper function to allocate and initialize a queue for a peer. This centralizes error handling and ensures consistent capacity across the codebase.

**Add to `src/posix/net_posix.c`:**

```c
/*
 * Allocate and initialize a queue for a peer
 *
 * Capacity is fixed at 16 slots per queue (adequate for POSIX burst handling,
 * conservative for MacTCP memory constraints).
 *
 * MEMORY: 16 slots * 268 bytes/slot = 4,288 bytes per queue
 *         Both send + recv = ~8.6 KB per peer
 *
 * Returns: Initialized queue pointer, or NULL on allocation failure
 */
static pt_queue *pt_alloc_peer_queue(struct pt_context *ctx) {
    pt_queue *q;
    int result;

    /* Allocate queue structure */
    q = (pt_queue *)pt_alloc(sizeof(pt_queue));
    if (!q) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to allocate queue structure (%zu bytes)", sizeof(pt_queue));
        return NULL;
    }

    /* Initialize queue with 16-slot capacity (power of 2 for fast wrap) */
    result = pt_queue_init(ctx, q, 16);
    if (result != 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to initialize queue: error %d", result);
        pt_free(q);
        return NULL;
    }

    /* Initialize Phase 3 extensions (priority free-lists, coalesce hash) */
    pt_queue_ext_init(q);

    return q;
}

/*
 * Free a peer's queue
 *
 * Handles NULL pointers gracefully (idempotent cleanup).
 */
static void pt_free_peer_queue(pt_queue *q) {
    if (!q)
        return;

    pt_queue_free(q);
    pt_free(q);
}
```

#### Task 4.3.5.2: Initialize queues on connection establishment

Modify both connection paths (accept and connect) to allocate queues when peers transition to CONNECTED state.

**Location 1: `pt_posix_listen_poll()` (accept path)**

Find this code block (around line 818):
```c
    /* Update state with logging */
    pt_peer_set_state(ctx, peer, PT_PEER_CONNECTED);
    peer->hot.last_seen = ctx->plat->get_ticks();

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Accepted connection from peer %u at 0x%08X (assigned to slot %u)",
        peer->hot.id, client_ip, peer_idx);
```

**Replace with:**
```c
    /* Allocate send and receive queues */
    peer->send_queue = pt_alloc_peer_queue(ctx);
    peer->recv_queue = pt_alloc_peer_queue(ctx);

    if (!peer->send_queue || !peer->recv_queue) {
        /* Allocation failed - clean up and reject connection */
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
            "Failed to allocate queues for peer %u, rejecting connection",
            peer->hot.id);

        /* Free any partially allocated queues */
        pt_free_peer_queue(peer->send_queue);
        pt_free_peer_queue(peer->recv_queue);
        peer->send_queue = NULL;
        peer->recv_queue = NULL;

        /* Close socket and clean up peer */
        close(client_sock);
        pt_peer_destroy(ctx, peer);

        /* Update global stats */
        ctx->global_stats.connections_rejected++;

        return -1;
    }

    /* Update state with logging */
    pt_peer_set_state(ctx, peer, PT_PEER_CONNECTED);
    peer->hot.last_seen = ctx->plat->get_ticks();

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Accepted connection from peer %u at 0x%08X (assigned to slot %u)",
        peer->hot.id, client_ip, peer_idx);

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_MEMORY,
        "Allocated queues for peer %u (send: %p, recv: %p)",
        peer->hot.id, (void*)peer->send_queue, (void*)peer->recv_queue);
```

**Location 2: `pt_posix_poll()` (async connect completion)**

Find this code block (around line 2033):
```c
            /* Connection successful - transition to CONNECTED */
            pt_peer_set_state(ctx, peer, PT_PEER_CONNECTED);
            peer->hot.last_seen = poll_time;

            PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
                "Connection established to peer %u (%s)",
                peer->hot.id, peer->cold.name);
```

**Replace with:**
```c
            /* Allocate send and receive queues */
            peer->send_queue = pt_alloc_peer_queue(ctx);
            peer->recv_queue = pt_alloc_peer_queue(ctx);

            if (!peer->send_queue || !peer->recv_queue) {
                /* Allocation failed - close connection */
                PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
                    "Failed to allocate queues for peer %u, closing connection",
                    peer->hot.id);

                /* Free any partially allocated queues */
                pt_free_peer_queue(peer->send_queue);
                pt_free_peer_queue(peer->recv_queue);
                peer->send_queue = NULL;
                peer->recv_queue = NULL;

                /* Close socket and mark failed */
                close(sock);
                pd->tcp_socks[peer_idx] = -1;
                pt_posix_remove_active_peer(pd, peer_idx);
                pt_peer_set_state(ctx, peer, PT_PEER_FAILED);

                continue;
            }

            /* Connection successful - transition to CONNECTED */
            pt_peer_set_state(ctx, peer, PT_PEER_CONNECTED);
            peer->hot.last_seen = poll_time;

            PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
                "Connection established to peer %u (%s)",
                peer->hot.id, peer->cold.name);

            PT_CTX_DEBUG(ctx, PT_LOG_CAT_MEMORY,
                "Allocated queues for peer %u (send: %p, recv: %p)",
                peer->hot.id, (void*)peer->send_queue, (void*)peer->recv_queue);
```

#### Task 4.3.5.3: Free queues on disconnection

Ensure queues are properly deallocated when peers disconnect to prevent memory leaks.

**Location 1: `pt_posix_poll()` (disconnect handling)**

Find this code block (around line 2078):
```c
                /* Destroy peer */
                pt_peer_destroy(ctx, peer);
```

**Insert BEFORE `pt_peer_destroy`:**
```c
                /* Free queues before destroying peer */
                PT_CTX_DEBUG(ctx, PT_LOG_CAT_MEMORY,
                    "Freeing queues for peer %u", peer->hot.id);
                pt_free_peer_queue(peer->send_queue);
                pt_free_peer_queue(peer->recv_queue);
                peer->send_queue = NULL;
                peer->recv_queue = NULL;

                /* Destroy peer */
                pt_peer_destroy(ctx, peer);
```

**Location 2: `pt_posix_disconnect()` (explicit disconnect)**

Add queue cleanup at the end of the function (after close() but before return):

```c
    /* Free queues */
    PT_CTX_DEBUG(ctx, PT_LOG_CAT_MEMORY,
        "Freeing queues for peer %u during explicit disconnect", peer->hot.id);
    pt_free_peer_queue(peer->send_queue);
    pt_free_peer_queue(peer->recv_queue);
    peer->send_queue = NULL;
    peer->recv_queue = NULL;

    return PT_OK;
```

#### Task 4.3.5.4: Integrate send queue draining in poll loop

Call `pt_drain_send_queue()` for connected peers in the main poll loop to flush queued messages.

**Location: `pt_posix_poll()` after receive processing**

Find the code block (around line 2080) that ends with:
```c
            }
        }
    }

periodic_work:
```

**Insert BEFORE `periodic_work:`:**

```c
        }

        /* Drain send queue if peer is connected and queue is not empty */
        if (peer->hot.state == PT_PEER_CONNECTED &&
            peer->send_queue &&
            !pt_queue_is_empty(peer->send_queue)) {

            /* Drain send queue using batching */
            int batches_sent = pt_drain_send_queue(ctx, peer, pt_posix_send_batch);

            if (batches_sent < 0) {
                /* Send error - log and continue (peer may disconnect later) */
                PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
                    "Send queue drain failed for peer %u", peer->hot.id);
            } else if (batches_sent > 0) {
                /* Track batch count for diagnostics */
                pd->batch_count += batches_sent;
            }
        }
    }

periodic_work:
```

#### Task 4.3.5.5: Implement batch send adapter

`pt_drain_send_queue()` expects a callback with signature `int (*)(ctx, peer, pt_batch*)`. Create an adapter that sends batches using `pt_posix_send()`.

**Add to `src/posix/net_posix.c`:**

```c
/*
 * Batch send adapter for pt_drain_send_queue()
 *
 * Converts a pt_batch structure into a framed message and sends it via
 * pt_posix_send(). This allows the platform-independent queue draining
 * code to work with the POSIX-specific send function.
 *
 * NOTE: The batch buffer contains multiple messages with length prefixes.
 * We send the entire batch as a single framed message with PT_MSG_FLAG_BATCH.
 *
 * Returns: 0 on success, negative error code on failure
 */
static int pt_posix_send_batch(struct pt_context *ctx, struct pt_peer *peer,
                                pt_batch *batch) {
    if (!batch || batch->count == 0 || batch->used == 0)
        return 0;

    /* Send batch buffer as payload */
    return pt_posix_send(ctx, peer, batch->buffer, batch->used);
}
```

**IMPORTANT:** Check if `pt_posix_send()` already handles batched messages. Looking at the Session 4.3 plan, `pt_posix_send()` sends raw payloads with framing. The batch structure pre-pends length headers to each message, so we need to ensure the protocol layer understands batched vs non-batched messages.

**Alternative approach:** If batching is not yet supported in the protocol layer, use a simpler non-batching approach:

```c
/*
 * Send individual messages from queue without batching
 *
 * Temporary implementation until batch protocol support is added.
 * Pops one message from queue and sends it directly.
 *
 * Returns: 0 on success, negative error code on failure
 */
static int pt_posix_send_from_queue(struct pt_context *ctx, struct pt_peer *peer) {
    uint8_t buffer[PT_QUEUE_SLOT_SIZE];
    uint16_t length;
    int result;

    if (!peer->send_queue || pt_queue_is_empty(peer->send_queue))
        return 0;

    /* Pop message from queue (priority order) */
    result = pt_queue_pop_priority(peer->send_queue, buffer, &length);
    if (result != 0) {
        /* Queue empty or error */
        return 0;
    }

    /* Send message */
    result = pt_posix_send(ctx, peer, buffer, length);
    if (result != PT_OK) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_SEND,
            "Failed to send queued message to peer %u: error %d",
            peer->hot.id, result);
        return result;
    }

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_SEND,
        "Sent queued message to peer %u (%u bytes)", peer->hot.id, length);

    return 0;
}
```

**And update the poll loop to call the simpler version:**

```c
        /* Drain send queue if peer is connected and queue is not empty */
        if (peer->hot.state == PT_PEER_CONNECTED &&
            peer->send_queue &&
            !pt_queue_is_empty(peer->send_queue)) {

            /* Send one message per poll cycle (simple, non-batching approach) */
            pt_posix_send_from_queue(ctx, peer);
        }
```

**DECISION:** Use the simpler non-batching approach initially. Batch protocol support can be added in a future session if needed for performance optimization.

#### Task 4.3.5.6: Update integration test

Enhance `tests/test_integration_full.c` to verify end-to-end messaging works.

**Modify the test to add validation:**

```c
/* After test completion, validate results */
printf("\n[VALIDATION] Checking test criteria:\n");

int validation_passed = 1;

/* Check discovery */
if (g_state.peers_discovered >= 2) {
    printf("  ✓ Discovery: %d peers found\n", g_state.peers_discovered);
} else {
    printf("  ✗ Discovery: Only %d peers found (expected 2+)\n",
           g_state.peers_discovered);
    validation_passed = 0;
}

/* Check connection */
PeerTalk_GlobalStats final_stats;
PeerTalk_GetGlobalStats(g_state.ctx, &final_stats);

if (final_stats.peers_connected >= 2) {
    printf("  ✓ Connection: %d peers connected\n", final_stats.peers_connected);
} else {
    printf("  ✗ Connection: Only %d peers connected (expected 2+)\n",
           final_stats.peers_connected);
    validation_passed = 0;
}

/* Check messaging */
if (g_state.mode == MODE_SENDER || g_state.mode == MODE_BOTH) {
    if (g_state.messages_sent > 0) {
        printf("  ✓ Sending: %d messages sent\n", g_state.messages_sent);
    } else {
        printf("  ✗ FAIL: Expected messages to be sent in sender mode\n");
        validation_passed = 0;
    }
}

if (g_state.mode == MODE_RECEIVER || g_state.mode == MODE_BOTH) {
    if (g_state.messages_received > 0) {
        printf("  ✓ Receiving: %d messages received\n", g_state.messages_received);
    } else {
        printf("  ✗ FAIL: Expected messages to be received in receiver mode\n");
        validation_passed = 0;
    }
}

/* Performance metrics validation */
printf("\n[PERFORMANCE] Validating metrics:\n");

if (final_stats.total_messages_sent == g_state.messages_sent) {
    printf("  ✓ Global stats match: sent counters consistent\n");
} else {
    printf("  ✗ Stats mismatch: global=%u, test=%d\n",
           final_stats.total_messages_sent, g_state.messages_sent);
}

if (final_stats.total_messages_received == g_state.messages_received) {
    printf("  ✓ Global stats match: recv counters consistent\n");
} else {
    printf("  ✗ Stats mismatch: global=%u, test=%d\n",
           final_stats.total_messages_received, g_state.messages_received);
}

printf("\n%s\n", validation_passed ? "✓ TEST PASSED" : "✗ TEST FAILED");
return validation_passed ? 0 : 1;
```

**Update Dockerfile.test.build and docker-compose.test.yml if needed** to ensure proper cleanup and error reporting.

#### Task 4.3.5.7: Add queue status logging

Add diagnostic logging to help debug queue-related issues.

**Add helper function to `src/posix/net_posix.c`:**

```c
/*
 * Log queue status for a peer (diagnostic helper)
 *
 * Used during connection establishment and periodic diagnostics.
 */
static void pt_log_peer_queue_status(struct pt_context *ctx, struct pt_peer *peer) {
    if (!peer->send_queue || !peer->recv_queue)
        return;

    float send_pressure = pt_queue_pressure(peer->send_queue);
    float recv_pressure = pt_queue_pressure(peer->recv_queue);

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL,
        "Peer %u queues: send=%.0f%% (%u/%u), recv=%.0f%% (%u/%u)",
        peer->hot.id,
        send_pressure * 100.0f,
        peer->send_queue->count,
        peer->send_queue->capacity,
        recv_pressure * 100.0f,
        peer->recv_queue->count,
        peer->recv_queue->capacity);
}
```

**Call after queue allocation:**
```c
    PT_CTX_DEBUG(ctx, PT_LOG_CAT_MEMORY,
        "Allocated queues for peer %u (send: %p, recv: %p)",
        peer->hot.id, (void*)peer->send_queue, (void*)peer->recv_queue);

    /* Log initial queue status */
    pt_log_peer_queue_status(ctx, peer);
```

### Acceptance Criteria

1. **Queue Lifecycle:**
   - ✓ Queues allocated when peers connect (both accept and connect paths)
   - ✓ Queues freed when peers disconnect (all disconnect paths)
   - ✓ No memory leaks (verified with valgrind)

2. **Message Flow:**
   - ✓ `PeerTalk_SendEx()` succeeds when peer is connected
   - ✓ Messages are queued and eventually sent
   - ✓ Send queue drains in poll loop
   - ✓ Messages arrive at destination peer
   - ✓ Callbacks fire with correct data

3. **Error Handling:**
   - ✓ Queue allocation failure rejects connection gracefully
   - ✓ Send errors don't crash (logged and peer can recover)
   - ✓ Queue overflow triggers backpressure (messages rejected, not queued)

4. **Integration Test:**
   - ✓ 3-peer Docker scenario discovers all peers
   - ✓ All peers successfully connect
   - ✓ Messages sent: >0
   - ✓ Messages received: >0
   - ✓ Global stats match test counters
   - ✓ No errors in logs except expected backpressure

5. **Memory Validation:**
   - ✓ Valgrind reports no leaks
   - ✓ Valgrind reports no invalid reads/writes
   - ✓ Queue memory is properly freed on disconnect

6. **Logging Quality:**
   - ✓ Connection: INFO level (operational visibility)
   - ✓ Queue allocation: DEBUG level (diagnostic)
   - ✓ Queue drain: DEBUG level (diagnostic)
   - ✓ Queue failures: ERR level (problems)

### Verification Steps

```bash
# 1. Build with clean slate
make clean && make

# 2. Run unit tests
make test

# 3. Run integration test
docker build -f Dockerfile.test.build -t peertalk-test .
docker-compose -f docker-compose.test.yml up --abort-on-container-exit

# 4. Check for expected output
# Should see:
#   - "Allocated queues for peer X"
#   - "DISCOVERED → CONNECTED" state transitions
#   - "Messages sent: X" where X > 0
#   - "Messages received: X" where X > 0
#   - "✓ TEST PASSED"

# 5. Memory leak check (run integration test under valgrind)
valgrind --leak-check=full --show-leak-kinds=all \
    ./build/test_integration_full Alice sender 10

# Should report:
#   - "All heap blocks were freed -- no leaks are possible"
#   - No invalid reads/writes
```

### Common Issues and Solutions

**Issue:** `SendEx failed: Peer X has no send queue`
- **Cause:** Queue initialization not called after connection
- **Fix:** Verify `pt_alloc_peer_queue()` is called in both connection paths

**Issue:** Messages queued but never sent
- **Cause:** `pt_drain_send_queue()` not called in poll loop
- **Fix:** Verify poll loop drains send queues for connected peers

**Issue:** Memory leak on disconnect
- **Cause:** Queues not freed when peer disconnects
- **Fix:** Ensure all disconnect paths call `pt_free_peer_queue()`

**Issue:** Valgrind reports "Invalid read" in queue operations
- **Cause:** Accessing queue after it's been freed
- **Fix:** Set queue pointers to NULL after freeing

**Issue:** Test fails with "No messages received"
- **Cause:** Receive callback not firing, or messages not being sent
- **Fix:** Check send queue is being drained, verify socket is writable

### Notes for MacTCP/OT/AppleTalk Ports

This session establishes the queue lifecycle pattern that will be replicated in MacTCP (Phase 5), Open Transport (Phase 6), and AppleTalk (Phase 7):

**Pattern to replicate:**
1. Allocate queues immediately after connection state transition
2. Check for allocation failure and reject connection if queues can't be allocated
3. Drain send queue in poll/notifier callback
4. Free queues before destroying peer

**MacTCP-specific considerations:**
- Queue allocation happens in ASR context (CANNOT allocate there - must pre-allocate)
- Solution: Allocate queues in main thread before calling MacTCPPassiveOpen/ActiveOpen
- ASR just sets a flag, main loop completes connection and assigns pre-allocated queues

**Open Transport-specific considerations:**
- Similar to MacTCP, but notifier runs at deferred task time
- May be able to allocate with OTAllocMem (check if safe in notifier context)
- Conservative approach: pre-allocate like MacTCP

**AppleTalk-specific considerations:**
- ADSP completion routines run at interrupt time
- MUST pre-allocate queues before initiating connection
- Use same pattern as MacTCP

---

## Related Sessions

- **Depends on:** Phase 3 (Queue infrastructure), Phase 3.5 (SendEx API), Session 4.3 (Message I/O)
- **Enables:** Complete POSIX reference implementation for validating protocol before MacTCP/OT/AppleTalk
- **Future work:** Batch protocol optimization (optional performance enhancement)
## Session 4.4: UDP Messaging

### Objective
Implement UDP messaging for low-latency, unreliable data transfer (game state, position updates).

### Tasks

#### Task 4.4.1: Add UDP messaging socket to net_posix.h

```c
/* Add to pt_posix_data structure in net_posix.h */

/* UDP messaging socket (separate from discovery) */
int udp_msg_sock;
uint16_t udp_msg_port;
```

#### Task 4.4.2: Implement UDP send/receive

```c
/* Add to src/posix/udp_posix.c */

#include "net_posix.h"
#include "protocol.h"
#include "pt_log.h"

#define DEFAULT_UDP_MSG_PORT 7355

/*
 * Initialize UDP messaging socket (separate from discovery).
 * Called during context initialization.
 */
int pt_posix_udp_init(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    struct sockaddr_in addr;
    int sock;
    uint16_t port;

    port = ctx->config.udp_port > 0 ? ctx->config.udp_port : DEFAULT_UDP_MSG_PORT;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_NETWORK,
            "Failed to create UDP messaging socket: %s", strerror(errno));
        return -1;
    }

    if (set_nonblocking(ctx, sock) < 0) {
        /* Error already logged by helper */
        close(sock);
        return -1;
    }

    if (set_reuseaddr(ctx, sock) < 0) {
        /* Error already logged by helper */
        close(sock);
        return -1;
    }

    pt_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        PT_LOG_ERR(ctx, PT_LOG_CAT_NETWORK,
            "Failed to bind UDP messaging socket to port %u: %s", port, strerror(errno));
        close(sock);
        return -1;
    }

    pd->udp_msg_sock = sock;
    pd->udp_msg_port = port;

    if (sock > pd->max_fd)
        pd->max_fd = sock;

    PT_LOG_INFO(ctx, PT_LOG_CAT_NETWORK,
        "UDP messaging on port %u", port);

    return 0;
}

void pt_posix_udp_shutdown(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);

    if (pd->udp_msg_sock >= 0) {
        close(pd->udp_msg_sock);
        pd->udp_msg_sock = -1;
    }
}

/*
 * Send data via UDP (unreliable, low-latency).
 *
 * Message is framed with a simple header:
 *   [4 bytes: magic "PTUD"]
 *   [2 bytes: sender port (for reply)]
 *   [2 bytes: payload length]
 *   [N bytes: payload]
 */
int pt_posix_send_udp(struct pt_context *ctx, struct pt_peer *peer,
                       const void *data, uint16_t len) {
    pt_posix_data *pd = pt_posix_get(ctx);
    uint8_t buf[PT_MAX_MESSAGE_SIZE + 8];
    struct sockaddr_in addr;
    ssize_t sent;

    if (pd->udp_msg_sock < 0)
        return PT_ERR_NOT_SUPPORTED;

    if (!peer || peer->magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    if (len > PT_MAX_MESSAGE_SIZE)
        return PT_ERR_MESSAGE_TOO_LARGE;

    /* Build UDP message header */
    buf[0] = 'P'; buf[1] = 'T'; buf[2] = 'U'; buf[3] = 'D';
    buf[4] = (pd->udp_msg_port >> 8) & 0xFF;
    buf[5] = pd->udp_msg_port & 0xFF;
    buf[6] = (len >> 8) & 0xFF;
    buf[7] = len & 0xFF;

    /* Copy payload */
    pt_memcpy(buf + 8, data, len);

    /* Send to peer's UDP port (use same port as us, or peer's configured port) */
    pt_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(peer->ip_addr);
    addr.sin_port = htons(peer->udp_port > 0 ? peer->udp_port : pd->udp_msg_port);

    sent = sendto(pd->udp_msg_sock, buf, 8 + len, 0,
                  (struct sockaddr *)&addr, sizeof(addr));

    if (sent < 0) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_SEND,
            "UDP send to peer %u (%s) at 0x%08X:%u failed: %s",
            peer->id, peer->name, peer->ip_addr,
            peer->udp_port > 0 ? peer->udp_port : pd->udp_msg_port,
            strerror(errno));
        return PT_ERR_NETWORK;
    }

    /* Update stats */
    ctx->global_stats.bytes_sent += len;
    ctx->global_stats.messages_sent++;
    peer->stats.bytes_sent += len;
    peer->stats.messages_sent++;

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_SEND,
        "UDP sent %u bytes to peer %u", len, peer->id);

    return PT_OK;
}

/*
 * Receive UDP messages (called from poll loop).
 */
int pt_posix_recv_udp(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    uint8_t buf[PT_MAX_MESSAGE_SIZE + 8];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t recv_len;
    uint32_t from_ip;
    uint16_t payload_len;
    struct pt_peer *peer;

    if (pd->udp_msg_sock < 0)
        return 0;

    recv_len = recvfrom(pd->udp_msg_sock, buf, sizeof(buf), 0,
                        (struct sockaddr *)&from_addr, &from_len);

    if (recv_len < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;
        return -1;
    }

    /* Validate minimum size and magic */
    if (recv_len < 8) {
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_RECV,
            "UDP packet too short: %zd bytes (minimum 8)", recv_len);
        return 0;
    }

    if (buf[0] != 'P' || buf[1] != 'T' || buf[2] != 'U' || buf[3] != 'D') {
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_RECV,
            "UDP packet magic mismatch: expected PTUD, got %c%c%c%c",
            buf[0], buf[1], buf[2], buf[3]);
        return 0;  /* Not a PeerTalk UDP message */
    }

    payload_len = (buf[6] << 8) | buf[7];
    if (recv_len < 8 + payload_len) {
        PT_LOG_WARN(ctx, PT_LOG_CAT_RECV, "UDP message truncated");
        return 0;
    }

    from_ip = ntohl(from_addr.sin_addr.s_addr);

    /* Find peer by IP */
    peer = pt_peer_find_by_addr(ctx, from_ip, 0);
    if (!peer) {
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_RECV,
            "UDP from unknown peer 0x%08X", from_ip);
        return 0;
    }

    /* Update peer's UDP port if needed */
    if (peer->udp_port == 0) {
        peer->udp_port = ntohs(from_addr.sin_port);
    }

    /* Update stats */
    ctx->global_stats.bytes_received += payload_len;
    ctx->global_stats.messages_received++;
    peer->stats.bytes_received += payload_len;
    peer->stats.messages_received++;
    peer->last_seen = ctx->plat->get_ticks();

    PT_LOG_DEBUG(ctx, PT_LOG_CAT_RECV,
        "UDP received %u bytes from peer %u (%s)",
        payload_len, peer->id, peer->name);

    /* Fire callback */
    if (ctx->callbacks.on_message_received) {
        ctx->callbacks.on_message_received((PeerTalk_Context *)ctx,
                                           peer->id, buf + 8, payload_len,
                                           ctx->callbacks.user_data);
    }

    return 1;
}

/*
 * Public API wrapper for UDP send.
 */
PeerTalk_Error PeerTalk_SendUDP(PeerTalk_Context *ctx_pub,
                                 PeerTalk_PeerID peer_id,
                                 const void *data, uint16_t length) {
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    struct pt_peer *peer;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC)
        return PT_ERR_INVALID_PARAM;

    peer = pt_peer_find_by_id(ctx, peer_id);
    if (!peer)
        return PT_ERR_NOT_FOUND;

    return pt_posix_send_udp(ctx, peer, data, length);
}
```

#### Task 4.4.3: Create tests/test_udp_posix.c

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "peertalk.h"

static int udp_messages = 0;

void on_message(PeerTalk_Context *ctx, PeerTalk_PeerID from,
                const void *data, uint16_t len, void *ud) {
    (void)ctx; (void)ud;
    printf("UDP message from %u: %.*s\n", from, len, (char *)data);
    udp_messages++;
}

int main(void) {
    PeerTalk_Config cfg1 = {0}, cfg2 = {0};
    PeerTalk_Callbacks cb = {0};
    PeerTalk_Context *ctx1, *ctx2;

    cfg1.local_name = "UDP-Peer1";
    cfg1.discovery_port = 7380;
    cfg1.tcp_port = 7381;
    cfg1.udp_port = 7382;
    cfg1.max_peers = 4;

    cfg2.local_name = "UDP-Peer2";
    cfg2.discovery_port = 7380;
    cfg2.tcp_port = 7383;
    cfg2.udp_port = 7384;
    cfg2.max_peers = 4;

    ctx1 = PeerTalk_Init(&cfg1);
    ctx2 = PeerTalk_Init(&cfg2);

    cb.on_message_received = on_message;
    PeerTalk_SetCallbacks(ctx1, &cb);
    PeerTalk_SetCallbacks(ctx2, &cb);

    PeerTalk_StartDiscovery(ctx1);
    PeerTalk_StartDiscovery(ctx2);

    /* Wait for discovery */
    for (int i = 0; i < 30; i++) {
        PeerTalk_Poll(ctx1);
        PeerTalk_Poll(ctx2);
        usleep(100000);
    }

    /* Get peers and send UDP */
    PeerTalk_PeerInfo peers[4];
    uint16_t count;
    PeerTalk_GetPeers(ctx1, peers, 4, &count);

    if (count > 0) {
        printf("Sending UDP to %s...\n", peers[0].name);
        PeerTalk_SendUDP(ctx1, peers[0].id, "Hello UDP!", 10);

        for (int i = 0; i < 20; i++) {
            PeerTalk_Poll(ctx1);
            PeerTalk_Poll(ctx2);
            usleep(100000);
        }
    }

    PeerTalk_Shutdown(ctx1);
    PeerTalk_Shutdown(ctx2);

    if (udp_messages > 0) {
        printf("TEST PASSED - %d UDP messages\n", udp_messages);
        return 0;
    }
    printf("TEST FAILED - no UDP messages\n");
    return 1;
}
```

### Acceptance Criteria
1. UDP messaging socket binds successfully
2. PeerTalk_SendUDP() sends data to peer
3. UDP messages are received and callback fires
4. UDP works independently of TCP connection state
5. Stats are updated for UDP messages

---

## Session 4.5: Network Statistics

### Objective
Implement network statistics tracking for latency, bytes, and connection quality.

### Tasks

#### Task 4.5.1: Add statistics structures to internal headers

```c
/* Add to src/core/pt_internal.h (in pt_context struct) */

/* Global statistics */
struct {
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t connections_total;
    uint32_t connections_failed;
} stats;

/* Add to pt_peer struct */
struct {
    uint32_t bytes_sent;
    uint32_t bytes_received;
    uint32_t messages_sent;
    uint32_t messages_received;
    uint16_t latency_ms;        /* Rolling average latency */
    uint8_t  quality;           /* 0-100 connection quality */
    uint32_t ping_sent_at;      /* Tick when last ping sent */
} stats;
```

#### Task 4.5.2: Implement statistics API

```c
/* Add to src/posix/stats_posix.c */

#include "pt_internal.h"
#include "pt_log.h"

/*
 * Get global network statistics.
 */
PeerTalk_Error PeerTalk_GetStats(PeerTalk_Context *ctx_pub,
                                  PeerTalk_Stats *stats) {
    struct pt_context *ctx = (struct pt_context *)ctx_pub;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC || !stats)
        return PT_ERR_INVALID_PARAM;

    stats->bytes_sent = ctx->global_stats.bytes_sent;
    stats->bytes_received = ctx->global_stats.bytes_received;
    stats->messages_sent = ctx->global_stats.messages_sent;
    stats->messages_received = ctx->global_stats.messages_received;
    stats->peers_discovered = ctx->peer_count;
    stats->peers_connected = 0;

    /* Count connected peers */
    for (int i = 0; i < ctx->max_peers; i++) {
        if (ctx->peers[i].state == PT_PEER_CONNECTED)
            stats->peers_connected++;
    }

    return PT_OK;
}

/*
 * Get per-peer statistics.
 */
PeerTalk_Error PeerTalk_GetPeerStats(PeerTalk_Context *ctx_pub,
                                      PeerTalk_PeerID peer_id,
                                      PeerTalk_PeerStats *stats) {
    struct pt_context *ctx = (struct pt_context *)ctx_pub;
    struct pt_peer *peer;

    if (!ctx || ctx->magic != PT_CONTEXT_MAGIC || !stats)
        return PT_ERR_INVALID_PARAM;

    peer = pt_peer_find_by_id(ctx, peer_id);
    if (!peer)
        return PT_ERR_NOT_FOUND;

    stats->bytes_sent = peer->stats.bytes_sent;
    stats->bytes_received = peer->stats.bytes_received;
    stats->messages_sent = peer->stats.messages_sent;
    stats->messages_received = peer->stats.messages_received;
    stats->latency_ms = peer->stats.latency_ms;
    stats->quality = peer->stats.quality;

    return PT_OK;
}

/*
 * Send ping to measure latency.
 * Called periodically from poll loop for connected peers.
 */
void pt_posix_ping_peer(struct pt_context *ctx, struct pt_peer *peer) {
    if (peer->state != PT_PEER_CONNECTED)
        return;

    /* Record when ping was sent */
    peer->stats.ping_sent_at = ctx->plat->get_ticks();

    /* Send ping message */
    pt_posix_send_control(ctx, peer, PT_MSG_PING);
}

/*
 * Handle pong response - update latency.
 * Called when PONG message is received.
 */
void pt_posix_pong_received(struct pt_context *ctx, struct pt_peer *peer) {
    pt_tick_t now = ctx->plat->get_ticks();
    uint16_t rtt;

    if (peer->stats.ping_sent_at == 0)
        return;  /* No pending ping */

    rtt = (uint16_t)(now - peer->stats.ping_sent_at);
    peer->stats.ping_sent_at = 0;

    /* Rolling average: new = (old * 3 + sample) / 4 */
    if (peer->stats.latency_ms == 0) {
        peer->stats.latency_ms = rtt;
    } else {
        peer->stats.latency_ms = (peer->stats.latency_ms * 3 + rtt) / 4;
    }

    /* Calculate quality (100 = excellent, 0 = terrible) */
    if (peer->stats.latency_ms < 50) {
        peer->stats.quality = 100;
    } else if (peer->stats.latency_ms < 100) {
        peer->stats.quality = 90;
    } else if (peer->stats.latency_ms < 200) {
        peer->stats.quality = 75;
    } else if (peer->stats.latency_ms < 500) {
        peer->stats.quality = 50;
    } else {
        peer->stats.quality = 25;
    }

    PT_LOG_INFO(ctx, PT_LOG_CAT_PERF,
        "Peer %u latency: %u ms (quality: %u%%)",
        peer->id, peer->stats.latency_ms, peer->stats.quality);
}
```

#### Task 4.5.3: Create tests/test_stats_posix.c

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "peertalk.h"

int main(void) {
    PeerTalk_Config cfg = {0};
    PeerTalk_Context *ctx;
    PeerTalk_Stats stats;

    cfg.local_name = "StatsTest";
    cfg.max_peers = 4;
    cfg.discovery_port = 7390;
    cfg.tcp_port = 7391;

    ctx = PeerTalk_Init(&cfg);
    if (!ctx) {
        fprintf(stderr, "Init failed\n");
        return 1;
    }

    /* Initial stats should be zero */
    PeerTalk_GetStats(ctx, &stats);
    printf("Initial stats:\n");
    printf("  bytes_sent: %u\n", stats.bytes_sent);
    printf("  bytes_received: %u\n", stats.bytes_received);
    printf("  peers_discovered: %u\n", stats.peers_discovered);

    if (stats.bytes_sent != 0 || stats.bytes_received != 0) {
        printf("TEST FAILED - initial stats not zero\n");
        PeerTalk_Shutdown(ctx);
        return 1;
    }

    /* Start discovery - this will send broadcasts */
    PeerTalk_StartDiscovery(ctx);

    for (int i = 0; i < 10; i++) {
        PeerTalk_Poll(ctx);
        usleep(100000);
    }

    /* Stats should show broadcast sends */
    PeerTalk_GetStats(ctx, &stats);
    printf("\nAfter discovery:\n");
    printf("  bytes_sent: %u\n", stats.bytes_sent);
    printf("  messages_sent: %u\n", stats.messages_sent);

    PeerTalk_Shutdown(ctx);
    printf("\nTEST PASSED\n");
    return 0;
}
```

### Acceptance Criteria
1. PeerTalk_GetStats() returns global statistics
2. PeerTalk_GetPeerStats() returns per-peer statistics
3. Bytes sent/received are tracked correctly
4. Latency is measured via ping/pong
5. Quality is calculated from latency
6. Stats update on both TCP and UDP messages

---

## Session 4.6: Integration

### Objective
Integrate all POSIX components and verify end-to-end functionality.

> **IMPLEMENTATION STATUS (2026-02-04):** ✅ **DONE** - Poll loop optimization completed
>
> **What was implemented:**
> - `pt_posix_rebuild_fd_sets()` - Rebuilds cached fd_sets when `fd_dirty` flag is set
> - Enhanced `pt_posix_poll()` - Optimized select()-based multiplexing with cached fd_sets
> - Connection completion handling integrated into main poll loop
> - Periodic discovery announcements (every 10 seconds)
> - Peer timeout checking (30 second threshold)
> - Basic integration test (`tests/test_integration_posix.c`)
>
> **What was simplified:**
> - Integration test does NOT include 3-peer Docker scenario or messaging tests
> - Test verifies: init, discovery start, listening, poll loop, statistics, shutdown
> - Full messaging test deferred until Phase 3.5 (`PeerTalk_Send()` API) is implemented
>
> **Files modified:**
> - `src/posix/net_posix.c` - Added `pt_posix_rebuild_fd_sets()`, enhanced `pt_posix_poll()`
> - `tests/test_integration_posix.c` - Basic poll loop test
> - `Makefile` - Added integration test build rules
> - `docker/docker-compose.test.yml` - Created (for future 3-peer test)

### Tasks

#### Task 4.6.1: Implement main poll function

```c
/*
 * Helper: Rebuild cached fd_sets when connections change.
 * Called only when pd->fd_dirty is set, avoiding the cost of
 * FD_ZERO and rebuilding every poll cycle.
 */
static void pt_posix_rebuild_fd_sets(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    int i;

    FD_ZERO(&pd->cached_read_fds);
    FD_ZERO(&pd->cached_write_fds);
    pd->max_fd = 0;

    /* Add server sockets */
    if (pd->discovery_sock >= 0) {
        FD_SET(pd->discovery_sock, &pd->cached_read_fds);
        if (pd->discovery_sock > pd->max_fd)
            pd->max_fd = pd->discovery_sock;
    }

    if (pd->listen_sock >= 0) {
        FD_SET(pd->listen_sock, &pd->cached_read_fds);
        if (pd->listen_sock > pd->max_fd)
            pd->max_fd = pd->listen_sock;
    }

    if (pd->udp_msg_sock >= 0) {
        FD_SET(pd->udp_msg_sock, &pd->cached_read_fds);
        if (pd->udp_msg_sock > pd->max_fd)
            pd->max_fd = pd->udp_msg_sock;
    }

    /* Add only active peer sockets (uses active_peers list) */
    for (i = 0; i < pd->active_count; i++) {
        int peer_idx = pd->active_peers[i];
        int sock = pd->tcp_socks[peer_idx];

        if (sock >= 0) {
            FD_SET(sock, &pd->cached_read_fds);

            /* Check for pending connect */
            struct pt_peer *peer = &ctx->peers[peer_idx];
            if (peer->state == PT_PEER_CONNECTING) {
                FD_SET(sock, &pd->cached_write_fds);
            }

            if (sock > pd->max_fd)
                pd->max_fd = sock;
        }
    }

    pd->fd_dirty = 0;
}

/*
 * Helper: Add peer to active list and mark fd_sets dirty.
 * Call when a new connection is established.
 * O(1) operation using reverse mapping.
 */
static void pt_posix_add_active_peer(pt_posix_data *pd, uint8_t peer_idx) {
    if (pd->active_count < PT_MAX_PEERS && pd->active_position[peer_idx] == 0xFF) {
        uint8_t pos = pd->active_count;
        pd->active_peers[pos] = peer_idx;
        pd->active_position[peer_idx] = pos;
        pd->active_count++;
        pd->fd_dirty = 1;
    }
}

/*
 * Helper: Remove peer from active list and mark fd_sets dirty.
 * Uses swap-back removal for O(1) operation without preserving order.
 * The active_position reverse mapping enables O(1) lookup of position.
 */
static void pt_posix_remove_active_peer(pt_posix_data *pd, uint8_t peer_idx) {
    uint8_t pos = pd->active_position[peer_idx];
    if (pos == 0xFF) return;  /* Not active - nothing to do */

    /* Swap-back: move last element to this position */
    uint8_t last_pos = pd->active_count - 1;
    if (pos != last_pos) {
        uint8_t last_peer = pd->active_peers[last_pos];
        pd->active_peers[pos] = last_peer;
        pd->active_position[last_peer] = pos;
    }

    pd->active_position[peer_idx] = 0xFF;  /* Mark as inactive */
    pd->active_count--;
    pd->fd_dirty = 1;
}

/*
 * Main poll function.
 *
 * DOD optimizations:
 * 1. Uses cached fd_sets - only rebuilt when connections change
 * 2. Iterates only active peers via active_peers[] list
 * 3. Hot path (no events) is very fast - just select() and return
 * 4. Caches tick value at poll start to avoid repeated get_ticks() calls
 */
int pt_posix_poll(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    fd_set read_fds, write_fds;
    struct timeval tv;
    int result;
    int i;

    /* Cache tick value at start of poll - avoids repeated get_ticks() calls.
     * On Classic Mac, TickCount() requires a trap instruction; caching reduces overhead. */
    pt_tick_t poll_time = ctx->plat->get_ticks();

    /* Reset batch count for this poll cycle */
    pd->batch_count = 0;

    /* Rebuild fd_sets only if connections changed */
    if (pd->fd_dirty) {
        pt_posix_rebuild_fd_sets(ctx);
    }

    /* Copy cached fd_sets (select modifies them) */
    pt_memcpy(&read_fds, &pd->cached_read_fds, sizeof(fd_set));
    pt_memcpy(&write_fds, &pd->cached_write_fds, sizeof(fd_set));

    /* Short timeout for polling (10ms) */
    tv.tv_sec = 0;
    tv.tv_usec = 10000;

    result = select(pd->max_fd + 1, &read_fds, &write_fds, NULL, &tv);
    if (result < 0) {
        if (errno != EINTR) {
            PT_LOG_ERR(ctx, PT_LOG_CAT_PLATFORM,
                "select() error: %s", strerror(errno));
        }
        return -1;
    }

    /* Process discovery */
    if (pd->discovery_sock >= 0 && FD_ISSET(pd->discovery_sock, &read_fds)) {
        while (pt_posix_discovery_poll(ctx) > 0)
            ;  /* Process all pending discovery packets */
    }

    /* Process UDP messages (unreliable messaging) */
    if (pd->udp_msg_sock >= 0 && FD_ISSET(pd->udp_msg_sock, &read_fds)) {
        while (pt_posix_recv_udp(ctx) > 0)
            ;  /* Process all pending UDP messages */
    }

    /* Process incoming connections */
    if (pd->listen_sock >= 0 && FD_ISSET(pd->listen_sock, &read_fds)) {
        pt_posix_listen_poll(ctx);
    }

    /* Process TCP sockets - iterate only active peers */
    for (i = 0; i < pd->active_count; i++) {
        int peer_idx = pd->active_peers[i];
        int sock = pd->tcp_socks[peer_idx];

        if (sock < 0)
            continue;

        struct pt_peer *peer = &ctx->peers[peer_idx];

        /* Check for connect completion */
        if (peer->state == PT_PEER_CONNECTING &&
            FD_ISSET(sock, &write_fds)) {
            int error;
            socklen_t len = sizeof(error);
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);

            if (error == 0) {
                pt_log_state_transition(ctx, peer, PT_PEER_CONNECTING, PT_PEER_CONNECTED);
                pt_peer_set_state(peer, PT_PEER_CONNECTED);
                peer->last_seen = poll_time;
                pd->fd_dirty = 1;  /* Rebuild to remove from write_fds */

                PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
                    "Connected to peer %u (%s) at 0x%08X:%u",
                    peer->id, peer->name, peer->ip_addr, peer->port);

                if (ctx->callbacks.on_peer_connected) {
                    ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                                     peer->id,
                                                     ctx->callbacks.user_data);
                }
            } else {
                PT_LOG_WARN(ctx, PT_LOG_CAT_CONNECT,
                    "Connect to peer %u (%s) failed: %s",
                    peer->id, peer->name, strerror(error));
                close(sock);
                pd->tcp_socks[peer_idx] = -1;
                pt_posix_remove_active_peer(pd, peer_idx);
                pt_log_state_transition(ctx, peer, PT_PEER_CONNECTING, PT_PEER_FAILED);
                pt_peer_set_state(peer, PT_PEER_FAILED);
            }
        }

        /* Check for incoming data */
        if (FD_ISSET(sock, &read_fds)) {
            int recv_result;
            /* Process all complete messages available */
            while ((recv_result = pt_posix_recv(ctx, peer)) > 0)
                ;

            if (recv_result < 0) {
                /* Connection error or closed */
                PT_LOG_INFO(ctx, PT_LOG_CAT_CONNECT,
                    "Connection to peer %u (%s) closed or errored",
                    peer->id, peer->name);

                close(sock);
                pd->tcp_socks[peer_idx] = -1;
                pt_posix_remove_active_peer(pd, peer_idx);

                if (ctx->callbacks.on_peer_disconnected) {
                    ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                                        peer->id,
                                                        PT_ERR_CONNECTION_CLOSED,
                                                        ctx->callbacks.user_data);
                }

                pt_peer_destroy(ctx, peer);
            }
        }
    }

    /* Periodic discovery announce (every 10 seconds) */
    if (ctx->discovery_active && (poll_time - pd->last_announce) > 10000) {
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_DISCOVERY,
            "Sending periodic discovery announce (interval: 10s)");
        pt_posix_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);
        pd->last_announce = poll_time;
    }

    /*
     * Check for peer timeouts - iterate all peers (infrequent).
     *
     * DISCOVERY TIMEOUT BEHAVIOR:
     * - Only peers in PT_PEER_DISCOVERED state are subject to discovery timeout
     * - When a peer times out (30s without discovery announce), on_peer_lost fires
     * - pt_peer_destroy() is called which:
     *   - Does NOT close TCP connections (they operate independently)
     *   - Frees the peer slot so it can be reused
     *   - If peer had active TCP connection, that connection remains open
     *
     * TCP CONNECTION TIMEOUT BEHAVIOR (separate from discovery):
     * - Connected peers have their own timeout checked via peer->last_seen
     * - last_seen is updated on every received TCP message
     * - TCP timeout fires on_peer_disconnected (not on_peer_lost)
     * - TCP timeout DOES close the socket and remove from active_peers
     *
     * This separation allows:
     * - Discovery to stop while TCP connections remain open
     * - Peers to reconnect via TCP without re-discovering
     */
    for (i = 0; i < ctx->max_peers; i++) {
        struct pt_peer *peer = &ctx->peers[i];
        if (peer->state == PT_PEER_DISCOVERED &&
            pt_peer_is_timed_out(peer, poll_time, 30000)) {  /* 30 second timeout */

            pt_tick_t time_since_seen = poll_time - peer->last_seen;
            PT_LOG_WARN(ctx, PT_LOG_CAT_DISCOVERY,
                "Peer %u (%s) discovery timed out: no announce for %u ms (threshold: 30000 ms)",
                peer->id, peer->name, (unsigned)time_since_seen);

            if (ctx->callbacks.on_peer_lost) {
                ctx->callbacks.on_peer_lost((PeerTalk_Context *)ctx,
                                            peer->id,
                                            ctx->callbacks.user_data);
            }

            pt_peer_destroy(ctx, peer);
        }
    }

    /*
     * Fire batch callback if messages were accumulated.
     * This is called AFTER all message processing is complete for this poll cycle.
     */
    if (pd->batch_count > 0 && ctx->callbacks.on_message_batch) {
        PT_LOG_DEBUG(ctx, PT_LOG_CAT_RECV,
            "Firing batch callback with %u messages", pd->batch_count);
        ctx->callbacks.on_message_batch(
            (PeerTalk_Context *)ctx,
            pd->msg_batch,
            pd->batch_count,
            ctx->callbacks.user_data);
        pd->batch_count = 0;  /* Reset for next poll */
    }

    return result;
}
```

#### Task 4.6.2: Create `tests/test_integration_posix.c`

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "peertalk.h"
#include "pt_compat.h"

#define TEST_MSG "Integration test message!"

static int received_count = 0;
static int connected_count = 0;
static const char *test_name = NULL;

void on_message(PeerTalk_Context *ctx, PeerTalk_PeerID from_peer,
                const void *data, uint16_t length, void *user_data) {
    (void)ctx;
    (void)length;
    (void)user_data;
    if (pt_memcmp(data, TEST_MSG, pt_strlen(TEST_MSG)) == 0) {
        received_count++;
        printf("[%s] Received expected message from peer %u\n",
               test_name, from_peer);
    }
}

void on_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                  void *user_data) {
    (void)ctx;
    (void)user_data;
    connected_count++;
    printf("[%s] Connected to peer %u\n", test_name, peer_id);
}

int run_peer(const char *name, int is_initiator, uint16_t tcp_port,
             uint16_t discovery_port) {
    PeerTalk_Config config = {0};
    PeerTalk_Callbacks callbacks = {0};
    PeerTalk_Context *ctx;
    int success = 0;

    test_name = name;

    config.local_name = name;
    config.tcp_port = tcp_port;
    config.discovery_port = discovery_port;
    config.max_peers = 8;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        fprintf(stderr, "[%s] Init failed\n", name);
        return 0;
    }

    /* Register callbacks via PeerTalk_SetCallbacks */
    callbacks.on_message_received = on_message;
    callbacks.on_peer_connected = on_connected;
    callbacks.user_data = (void *)name;
    PeerTalk_SetCallbacks(ctx, &callbacks);

    PeerTalk_StartDiscovery(ctx);

    /* Phase 1: Discovery */
    printf("[%s] Waiting for discovery...\n", name);
    for (int i = 0; i < 50; i++) {
        PeerTalk_Poll(ctx);
        usleep(100000);
    }

    /* Phase 2: Connect (initiator only) */
    if (is_initiator) {
        PeerTalk_PeerInfo peers[8];
        uint16_t count;
        PeerTalk_GetPeers(ctx, peers, 8, &count);
        printf("[%s] Found %u peers\n", name, count);

        if (count > 0) {
            printf("[%s] Connecting to %s...\n", name, peers[0].name);
            PeerTalk_Connect(ctx, peers[0].id);
        }
    }

    /* Phase 3: Wait for connection */
    for (int i = 0; i < 30; i++) {
        PeerTalk_Poll(ctx);
        if (connected_count > 0) break;
        usleep(100000);
    }

    /* Phase 4: Exchange messages */
    if (connected_count > 0) {
        PeerTalk_PeerInfo peers[8];
        uint16_t count;
        PeerTalk_GetPeers(ctx, peers, 8, &count);

        for (uint16_t i = 0; i < count; i++) {
            if (peers[i].connected) {
                printf("[%s] Sending message to %s\n", name, peers[i].name);
                PeerTalk_Send(ctx, peers[i].id, TEST_MSG, pt_strlen(TEST_MSG) + 1);
            }
        }
    }

    /* Phase 5: Receive */
    for (int i = 0; i < 30; i++) {
        PeerTalk_Poll(ctx);
        usleep(100000);
    }

    success = (connected_count > 0 && received_count > 0);
    printf("[%s] Result: connected=%d received=%d success=%d\n",
           name, connected_count, received_count, success);

    PeerTalk_Shutdown(ctx);
    return success;
}

int main(void) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* Child: Peer2 */
        sleep(1);  /* Let Peer1 start first */
        return run_peer("Peer2", 0, 7372, 7370) ? 0 : 1;
    } else {
        /* Parent: Peer1 */
        int peer1_result = run_peer("Peer1", 1, 7371, 7370);

        int status;
        waitpid(pid, &status, 0);
        int peer2_result = WIFEXITED(status) && WEXITSTATUS(status) == 0;

        if (peer1_result && peer2_result) {
            printf("\n=== INTEGRATION TEST PASSED ===\n");
            return 0;
        } else {
            printf("\n=== INTEGRATION TEST FAILED ===\n");
            return 1;
        }
    }
}
```

### Acceptance Criteria
1. Two processes discover each other
2. Connection is established
3. Messages are exchanged bidirectionally
4. Disconnection is handled gracefully
5. Peer timeout removes stale peers
6. Periodic announcements work
7. All callbacks fire correctly

---

## Building Tests

Create `tests/Makefile` to build the POSIX test suite:

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -g -I../include -I../src/core -I../src/posix
CFLAGS += -DPT_PLATFORM_POSIX

# Source files
CORE_SRC = ../src/core/protocol.c ../src/core/peer.c ../src/core/queue.c \
           ../src/core/pt_log.c ../src/core/pt_compat.c ../src/core/context.c
POSIX_SRC = ../src/posix/net_posix.c ../src/posix/platform_posix.c

# All library sources
LIB_SRC = $(CORE_SRC) $(POSIX_SRC)

# Test targets
TESTS = test_discovery_posix test_connect_posix test_messaging_posix test_integration_posix

.PHONY: all clean test

all: $(TESTS)

test_discovery_posix: test_discovery_posix.c $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $^

test_connect_posix: test_connect_posix.c $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $^

test_messaging_posix: test_messaging_posix.c $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $^

test_integration_posix: test_integration_posix.c $(LIB_SRC)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TESTS)
	@echo "Running discovery test..."
	./test_discovery_posix TestPeer &
	sleep 2 && kill $$! 2>/dev/null || true
	@echo ""
	@echo "Running connect test..."
	./test_connect_posix server &
	sleep 1 && ./test_connect_posix client
	@echo ""
	@echo "Running messaging test..."
	./test_messaging_posix
	@echo ""
	@echo "Running integration test..."
	./test_integration_posix

clean:
	rm -f $(TESTS)

# Memory leak check with valgrind
valgrind: test_integration_posix
	valgrind --leak-check=full --show-leak-kinds=all ./test_integration_posix
```

**Usage:**
```bash
cd tests
make              # Build all tests
make test         # Run all tests
make valgrind     # Check for memory leaks
make clean        # Remove binaries
```

---

## Phase 4 Complete Checklist

**Prerequisites (verify before starting):**
- [ ] Phase 1 complete: `pt_context`, `pt_peer` structs exist
- [ ] Phase 1 complete: `pt_compat.h` functions available (pt_memcpy, pt_alloc, etc.)
- [ ] Phase 1 complete: `PT_LOG_*` macros work
- [ ] Phase 1 complete: Public API stubs in `context.c`
- [ ] Phase 2 complete: `pt_discovery_encode/decode()` functions
- [ ] Phase 2 complete: `pt_message_encode_header/decode_header()` functions
- [ ] Phase 2 complete: `pt_crc16()` and `pt_crc16_update()` functions
- [ ] Phase 2 complete: `pt_peer_*` functions (create, find, destroy, etc.)
- [ ] Phase 2 `pt_peer` has `udp_port`, `send_seq`, `recv_seq`, `last_seen` fields
- [ ] Phase 2 `pt_peer` has `stats` struct with latency/quality fields
- [ ] Phase 1 `pt_context` has `PeerTalk_GlobalStats global_stats`

**Data-Oriented Design (cache efficiency):**
- [ ] `pt_recv_hot` and `pt_recv_cold` structs separate polling data from I/O buffers
- [ ] `pt_recv_hot` has uint16_t fields at even offsets (68k alignment)
- [ ] `pt_recv_buffer` has HOT fields first (state, bytes_needed, bytes_received)
- [ ] `pt_recv_buffer` payload_buf is at end (COLD)
- [ ] `pt_posix_data.recv_bufs` is pointer to separate allocation (not inline)
- [ ] `pt_posix_data` has `active_peers[]` list for sparse iteration
- [ ] `pt_posix_data` has `active_position[]` reverse mapping for O(1) removal
- [ ] `pt_posix_data` has `batch_count` in HOT section, `msg_batch[]` in COLD section
- [ ] `pt_posix_data` has `fd_dirty` as uint8_t (not int) in HOT section
- [ ] `pt_posix_data` has cached fd_sets
- [ ] Poll loop iterates only `active_count` peers, not all `PT_MAX_PEERS`
- [ ] `pt_posix_add_active_peer()` uses reverse mapping for O(1) add
- [ ] `pt_posix_remove_active_peer()` uses reverse mapping for O(1) removal
- [ ] Receive function split into small helpers (pt_recv_header, pt_recv_payload, etc.)
- [ ] Send function uses `writev()` for atomic header+payload+CRC

**Implementation:**
- [ ] `src/posix/net_posix.h` created with receive state machine
- [ ] UDP discovery socket implementation
- [ ] Broadcast send/receive works (uses INADDR_BROADCAST)
- [ ] Local IP detection (getifaddrs + fallback)
- [ ] TCP listener implementation with active peer tracking
- [ ] Non-blocking connect works with active peer tracking
- [ ] Message send with framing using `writev()`
- [ ] Message receive handles partial reads (state machine with helpers)
- [ ] CRC validation using `pt_crc16_update()`
- [ ] Main poll loop with cached fd_sets
- [ ] Connect completion detection
- [ ] Peer timeout handling
- [ ] Discovery timing per-context (not static)
- [ ] `recv_bufs` allocated separately in `pt_posix_net_init()`
- [ ] `recv_bufs` freed in `pt_posix_net_shutdown()`
- [ ] UDP messaging socket separate from discovery
- [ ] UDP socket initialized in `pt_posix_net_init()`
- [ ] UDP socket included in poll loop fd_set
- [ ] UDP messages polled in `pt_posix_poll()`
- [ ] PeerTalk_SendUDP() works for unreliable messages
- [ ] UDP messages received and callback fires
- [ ] PeerTalk_GetStats() returns global statistics
- [ ] PeerTalk_GetPeerStats() returns per-peer statistics
- [ ] Latency measured via ping/pong
- [ ] Quality calculated from latency
- [ ] Stats update on both TCP and UDP
- [ ] All tests use unique ports to avoid conflicts
- [ ] Uses `pt_*` functions consistently (no raw stdlib)

**Logging & Debugging:**
- [ ] Socket creation failures logged with PT_LOG_ERR
- [ ] Socket configuration failures logged with strerror(errno)
- [ ] Connection state transitions logged with pt_log_state_transition()
- [ ] Receive state machine transitions logged at DEBUG level
- [ ] CRC validation errors logged with expected vs actual values
- [ ] Peer timeout warnings include duration and threshold
- [ ] Batch callback accumulation logged at DEBUG level
- [ ] UDP send/receive success logged at DEBUG level
- [ ] Control message sequence=0 documented as intentional
- [ ] Parameter validation failures (NULL, magic, state) logged at WARN level
- [ ] Peer slot exhaustion logged at ERR level
- [ ] SIGPIPE ignored at init (signal(SIGPIPE, SIG_IGN))
- [ ] EINTR from select() handled by retry, not logged as error

**Tests:**
- [ ] `tests/test_discovery_posix.c` passes
- [ ] `tests/test_connect_posix.c` passes
- [ ] `tests/test_messaging_posix.c` passes
- [ ] `tests/test_udp_posix.c` passes
- [ ] `tests/test_stats_posix.c` passes
- [ ] `tests/test_integration_posix.c` passes
- [ ] Memory leak check (valgrind)

---

## Session 4.7: CI Setup

### Objective
Set up GitHub Actions to run POSIX tests automatically on every push and pull request.

### Tasks

#### Task 4.7.1: Create `.github/workflows/ci.yml`

```yaml
name: CI

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build-and-test:
    runs-on: ubuntu-latest

    steps:
    - uses: actions/checkout@v4

    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y gcc make valgrind lcov

    - name: Build
      run: make

    - name: Run tests
      run: make test

    - name: Memory leak check
      run: |
        for test in tests/test_*; do
          if [ -x "$test" ]; then
            valgrind --leak-check=full --error-exitcode=1 "$test"
          fi
        done

    - name: Coverage report
      run: |
        make clean
        make CFLAGS="-Wall -Werror -O0 -g --coverage -I include -I src/core -DPT_LOG_ENABLED -DPT_PLATFORM_POSIX"
        make test
        lcov --capture --directory . --output-file coverage.info
        lcov --remove coverage.info '/usr/*' --output-file coverage.info
        lcov --list coverage.info
```

#### Task 4.7.2: Update Makefile for coverage support

Add to `Makefile`:
```makefile
# Coverage target
coverage:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -O0 -g --coverage"
	$(MAKE) test
	lcov --capture --directory . --output-file coverage.info
	lcov --remove coverage.info '/usr/*' --output-file coverage.info
	genhtml coverage.info --output-directory coverage_html
	@echo "Coverage report: coverage_html/index.html"

.PHONY: coverage
```

#### Task 4.7.3: Verify CI Workflow Executes Successfully

After committing the workflow file:

1. **Push to GitHub:**
   ```bash
   git add .github/workflows/ci.yml Makefile
   git commit -m "feat(ci): add GitHub Actions workflow for POSIX tests"
   git push origin main
   ```

2. **Check Actions tab:**
   - Go to the repository on GitHub
   - Click "Actions" tab
   - Watch the workflow run
   - All steps should show green checkmarks

3. **If workflow fails:**
   - Click the failed job to see logs
   - Common issues:
     - Missing dependencies: update `apt-get install` list
     - Test failures: fix tests locally first, then push
     - Valgrind errors: address memory leaks before CI will pass
   - Fix issues locally, commit, push again

4. **Add CI badge to README (optional):**
   ```markdown
   ![CI](https://github.com/USERNAME/peertalk/actions/workflows/ci.yml/badge.svg)
   ```

### Acceptance Criteria
1. Push to main triggers CI workflow
2. Pull requests trigger CI workflow
3. `make test` runs all POSIX tests
4. Valgrind reports no memory leaks
5. Coverage report generates successfully
6. CI badge can be added to README
7. **CI verified working**: Workflow executes successfully in GitHub Actions tab (green checkmark on all steps)

---

## Common Pitfalls

1. **Discovery port conflicts** - When running multiple peers in tests, use the SAME discovery port (so they can find each other) but DIFFERENT TCP ports.

2. **Partial TCP reads** - TCP is a stream protocol. Never assume recv() returns a complete message. Use the state machine pattern implemented here.

3. **Blocking after non-blocking** - Don't call blocking recv() after detecting data with select(). Keep everything non-blocking.

4. **Subnet assumptions** - Don't compute broadcast from /24 assumption. Use INADDR_BROADCAST (255.255.255.255) for maximum compatibility.

5. **Static timing variables** - Don't use `static pt_tick_t last_announce`. Put timing state in the context so multiple contexts work correctly.

6. **CRC over non-contiguous data** - Use `pt_crc16_update()` to compute CRC incrementally over header + payload.

7. **Air-gapped networks** - The 8.8.8.8 trick for local IP doesn't work without internet. Use getifaddrs() as primary method.

8. **UDP socket in poll loop** - Remember to add `udp_msg_sock` to both the fd_set AND process it after select(). Missing either causes silent failure.

9. **Inline large buffers** - Don't embed `recv_bufs[PT_MAX_PEERS]` inline in pt_posix_data. With 64KB payloads × 16 peers = 1MB of inline data that pollutes the cache. Use pointer to separately allocated array.

10. **Sparse array iteration** - Don't iterate all PT_MAX_PEERS slots to find active connections. Maintain an `active_peers[]` list and only iterate `active_count` entries.

11. **Forgetting active peer tracking** - When adding/removing connections, always call `pt_posix_add_active_peer()` / `pt_posix_remove_active_peer()` and set `fd_dirty = 1`.

12. **Multiple send() calls** - Don't use separate send() calls for header, payload, CRC. Use `writev()` for atomic transmission and to avoid Nagle algorithm delays.

13. **Phase 1 dependency** - Phase 4 cannot compile without Phase 1 (Foundation) and Phase 2 (Protocol) complete. Don't start Phase 4 until prerequisites are done.

14. **SIGPIPE on closed socket** - When writing to a socket closed by the peer, POSIX sends SIGPIPE which terminates the process by default. Either `signal(SIGPIPE, SIG_IGN)` at startup or use `MSG_NOSIGNAL` flag with send(). Note: writev() doesn't support MSG_NOSIGNAL, so ignoring SIGPIPE globally is recommended.

15. **EINTR during select()** - When a signal arrives during select(), it returns -1 with errno=EINTR. This is not an error - retry the select() call. The poll loop should check `if (errno != EINTR)` before logging errors.

16. **68k struct alignment** - All uint16_t fields must start at even byte offsets on 68000/68020/68030 processors. Use explicit padding bytes after single-byte fields to maintain alignment. Misaligned access causes performance penalties or crashes depending on CPU model.

17. **Validation failures should log** - When returning early due to invalid parameters (NULL pointers, wrong magic, bad state), always log at PT_LOG_WARN before returning. Silent failures are hard to debug, especially cross-platform.

18. **UDP broadcast on single host** - You **CANNOT** test UDP discovery with two processes on the same machine. The kernel filters out broadcast packets from your own IP address. Use Docker Compose with bridge networking (see Task 4.1.4) to give each peer a unique IP in an isolated network namespace. This is not optional - single-host testing will silently fail.

19. **Forgetting pt_peer_list_init()** - The peer list **MUST** be initialized in `PeerTalk_Init()` before any peer operations (discovery, connections, etc.). Missing this call causes segfaults when creating peers. Always verify `ctx->peers` is not NULL before using.

20. **Missing name_len in discovery packets** - After copying the peer name into `pkt.name`, you **MUST** set `pkt.name_len = pt_strlen(pkt.name)` before calling `pt_discovery_encode()`. The protocol requires this field to properly decode names. Without it, peers will have empty names.

21. **Socket helpers without context** - Pass `struct pt_context *ctx` to socket helper functions (`set_nonblocking`, `set_broadcast`, `set_reuseaddr`) so they can log errors properly with category tags. Silent socket setup failures (wrong signature taking just `int fd`) are extremely difficult to debug, especially when testing on different platforms.

22. **Wrong discovery constant names** - Use `PT_DISC_TYPE_ANNOUNCE` (not `PT_DISC_TYPE_ANNOUNCE`), `PT_DISC_TYPE_QUERY` (not `PT_DISC_TYPE_QUERY`), `PT_DISC_TYPE_GOODBYE` (not `PT_DISC_TYPE_GOODBYE`). The constants include `_TYPE_` in their names for consistency with message type naming.

23. **Config field confusion** - Use `ctx->config.local_name` (embedded char array, not pointer), `ctx->config.tcp_port` (not `listen_port`), and remember to use `strncpy` for the name since it's a char array, not a pointer assignment. Code like `config.local_name = name;` will compile but cause memory corruption.

24. **INFO vs DEBUG logging for discovery** - Discovery events (peer found, packet sent/received, local IP detected) should use `PT_CTX_INFO` (not `PT_CTX_DEBUG`) so users can see peer discovery working without enabling full debug output which floods logs with packet hex dumps. Save DEBUG for low-level packet parsing details.

---

## References

- Berkeley Sockets: select(), non-blocking I/O, writev()
- getifaddrs(3) - enumerate network interfaces
- TCP state machine for connection handling
- Protocol layer from Phase 2
- CSEND-LESSONS.md Part C for cross-platform considerations
- Data-Oriented Design principles for cache efficiency on Classic Mac

---

## Review Changelog

### 2026-02-02: Phase 4.1 Implementation Corrections

**Critical Bug Fixes:**
- Added missing `name_len` field initialization in `pt_posix_discovery_send()` (must set after copying name, before encoding)
- Added `pt_peer_list_init()` call requirement in `PeerTalk_Init()` (missing this causes segfaults on peer creation)
- Updated socket helper functions to accept `ctx` parameter for proper error logging with categories

**Protocol Constant Corrections:**
- Changed all `PT_DISC_TYPE_ANNOUNCE` → `PT_DISC_TYPE_ANNOUNCE` throughout Session 4.1
- Changed all `PT_DISC_TYPE_QUERY` → `PT_DISC_TYPE_QUERY` throughout Session 4.1
- Changed all `PT_DISC_TYPE_GOODBYE` → `PT_DISC_TYPE_GOODBYE` throughout Session 4.1
- Added missing `pkt.transports` field initialization (must set to `PT_TRANSPORT_TCP | PT_TRANSPORT_UDP`)

**Configuration Field Corrections:**
- Changed `ctx->local_name` → `ctx->config.local_name` (embedded array, not pointer)
- Changed `config.local_name = name` → `strncpy(config.local_name, name, PT_MAX_PEER_NAME)` in test code
- Clarified `ctx->config.tcp_port` vs `pd->listen_port` naming (from platform data, not config)

**Docker Testing Infrastructure (NEW - Task 4.1.4):**
- Added `Dockerfile.test.build` for containerized builds (self-contained, ~30s rebuild)
- Added `docker-compose.test.yml` with 3-peer network (Alice, Bob, Charlie on 192.168.200.0/24)
- Added `scripts/test-discovery-docker.sh` helper script (start/stop/logs/follow/status commands)
- **Critical Discovery**: UDP broadcast **DOES NOT WORK** on single host - requires Docker bridge networking
- Documented that kernel filters out broadcast packets from your own IP (loopback limitation)
- This is not optional - single-host testing will silently fail

**Logging Level Changes:**
- Upgraded discovery events from DEBUG to INFO for better visibility:
  - Local IP detection (`PT_CTX_INFO` with detected address)
  - Packet send/receive (`PT_CTX_INFO` with full address details and peer names)
  - Peer creation (`PT_CTX_INFO` with address and name)
- Added detailed address formatting in log messages (dotted-quad notation)
- Rationale: Users need to see discovery working without enabling full debug spam

**Socket Helper Updates:**
- Changed signatures from `static int set_nonblocking(int fd)` to `static int set_nonblocking(struct pt_context *ctx, int fd)`
- Added error logging with `PT_CTX_ERR` and category tags inside helpers
- Removed redundant error logging at call sites (helpers now log)
- Applies to: `set_nonblocking()`, `set_broadcast()`, `set_reuseaddr()`

**Test Program Updates:**
- Fixed config initialization to use `strncpy` for `local_name` (array, not pointer)
- Changed poll interval from 100ms to 1 second (usleep→sleep)
- Improved output formatting and status reporting

**New Common Pitfalls:**
- #18: UDP broadcast on single host limitation (requires Docker)
- #19: Forgetting `pt_peer_list_init()` (causes segfaults)
- #20: Missing `name_len` in discovery packets (silent protocol failure)
- #21: Socket helpers without context (poor debugging)
- #22: Wrong discovery constant names (`_TYPE_` prefix required)
- #23: Config field confusion (`local_name` as array, `tcp_port` naming)
- #24: INFO vs DEBUG logging for discovery events (visibility vs spam)

**Acceptance Criteria Added:**
- #9: Docker-based multi-peer testing works (3 containers discover each other)
- #10: Peer list initialization called before peer operations
- #11: Discovery packets include `name_len` field properly set
- #12: Socket helpers log errors using context parameter

**Implementation Notes Section Added:**
- Warning about UDP broadcast testing limitation (cannot use single host)
- Summary of critical bugs found during implementation
- Protocol constant naming guidance
- Logging level guidance (INFO for discovery, not DEBUG)

**No Breaking API Changes** - All changes are corrections to match actual working implementation.

Fixes identified by implementation review of commits 7fec197 (initial implementation) and b441433 (bug fixes).

---

### 2026-01-29: Plan Review Applied (Second Pass)

**68k Alignment Fixes (CRITICAL):**
- Fixed `pt_recv_hot` struct: added `_pad0` byte after `state` so `bytes_needed` starts at even offset 2
- Fixed `pt_recv_buffer` struct: same alignment fix for 68000/68020/68030 compatibility
- Added explicit comments documenting 68k alignment requirements

**DOD Reorganization:**
- Moved `msg_batch[]` from HOT to COLD section in `pt_posix_data` - only `batch_count` is hot
- Changed `fd_dirty` from `int` to `uint8_t` for better packing (saves 3 bytes)
- Moved `fd_dirty` and `batch_count` into HOT section alongside other frequently-accessed fields
- Removed duplicate `fd_dirty` declaration (was in both HOT and after cached fd_sets)

**New Common Pitfalls Added:**
- #14: SIGPIPE handling for closed socket writes
- #15: EINTR during select() - must retry, not treat as error
- #16: 68k struct alignment requirements
- #17: Validation failures should log before returning

**No Breaking API Changes:** All changes are internal struct layout and documentation.

---

### 2026-01-29: Plan Review Applied (First Pass)

**Architectural Changes (DOD):**
- Split `pt_recv_buffer` into `pt_recv_hot` (8 bytes) and `pt_recv_cold` (large buffers) for better cache efficiency
- Moved `batch_count` and `msg_batch[]` into HOT section of `pt_posix_data`
- Added `active_position[]` reverse mapping for O(1) `pt_posix_remove_active_peer()`
- Updated `pt_posix_add_active_peer()` and `pt_posix_remove_active_peer()` to use reverse mapping

**Logging Improvements:**
- Added `pt_log_state_transition()` helper and `pt_peer_state_str()` for connection lifecycle logging
- Added PT_LOG_ERR for all socket creation failures with strerror(errno)
- Added PT_LOG_DEBUG for receive state machine transitions (HEADER→PAYLOAD→CRC)
- Added PT_LOG_DEBUG for batch accumulation visibility
- Added PT_LOG_WARN for timeout events with duration and threshold
- Added PT_LOG_DEBUG for UDP send/receive success
- Added PT_LOG_DEBUG for periodic discovery announcement
- Added state transition logging to connect completion and failure paths

**Documentation Fixes:**
- Fixed air-gapped network comment to clarify that getifaddrs() works on air-gapped networks, not the 8.8.8.8 fallback
- Added explicit documentation for control message sequence=0 being intentional (not a protocol violation)
- Updated checklist with new DOD and logging requirements

**No Breaking API Changes:** All changes are internal implementation improvements.

---

## Phase 4 Completion (2026-02-04)


All 7 sessions of Phase 4 have been implemented and tested:
- Sessions 4.1-4.6: POSIX networking stack (discovery, TCP, messaging, UDP, stats, integration)
- Session 4.7: CI/CD setup with GitHub Actions

**Additional Implementations:**

Beyond the original phase plan, the following helper functions were implemented to
complete the foundation before MacTCP:

**Phase 1 Peer Query Helpers:**
- `PeerTalk_GetPeersVersion()` - Version counter for change detection
- `PeerTalk_GetPeerByID()` - Get peer info by ID (returns pointer)
- `PeerTalk_GetPeer()` - Get peer info by ID (copies to buffer)
- `PeerTalk_FindPeerByName()` - Find peer by name string
- `PeerTalk_FindPeerByAddress()` - Find peer by IP:port

**Phase 4 Enhancements:**
- `PeerTalk_GetQueueStatus()` - Get pending/available queue slots per peer
- `PeerTalk_ResetStats()` - Reset peer or global statistics

**Testing:**
- All helper functions have comprehensive unit tests (`tests/test_helpers.c`)
- Full test suite passes: 21 tests total
- CI pipeline validates builds, tests, and code quality on every push/PR
- Coverage maintained above 10% threshold

**Quality Metrics:**
- Zero compiler warnings (treat warnings as errors)
- All files under 500-line limit
- Data-oriented design principles maintained throughout
- Hot/cold data separation preserved in all implementations

**Next Phase:** Phase 5 (MacTCP for 68k Macs)

The POSIX implementation provides the reference implementation and automated
test baseline. All MacTCP implementations must match POSIX behavior for
cross-platform compatibility.
