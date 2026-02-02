/*
 * PeerTalk POSIX Networking Implementation
 *
 * Session 4.1: UDP Discovery
 * - Non-blocking UDP broadcast for peer discovery
 * - Local IP detection with fallback strategies
 * - Discovery packet handling (ANNOUNCE, QUERY, GOODBYE)
 */

#include "net_posix.h"
#include "protocol.h"
#include "peer.h"
#include "pt_compat.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ifaddrs.h>

/* ========================================================================== */
/* Port Configuration                                                         */
/* ========================================================================== */

#define DEFAULT_DISCOVERY_PORT 7353
#define DEFAULT_TCP_PORT 7354
#define DEFAULT_UDP_PORT 7355

/* Port accessor macros - use config if set, otherwise defaults */
#define DISCOVERY_PORT(ctx) \
    ((ctx)->config.discovery_port > 0 ? (ctx)->config.discovery_port : DEFAULT_DISCOVERY_PORT)
#define TCP_PORT(ctx) \
    ((ctx)->config.tcp_port > 0 ? (ctx)->config.tcp_port : DEFAULT_TCP_PORT)
#define UDP_PORT(ctx) \
    ((ctx)->config.udp_port > 0 ? (ctx)->config.udp_port : DEFAULT_UDP_PORT)

/* ========================================================================== */
/* Helper Functions                                                           */
/* ========================================================================== */

/**
 * Set socket to non-blocking mode
 *
 * Returns: 0 on success, -1 on failure
 */
static int set_nonblocking(struct pt_context *ctx, int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
                   "Failed to get socket flags: %s", strerror(errno));
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
                   "Failed to set non-blocking: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * Enable SO_BROADCAST on socket
 *
 * Returns: 0 on success, -1 on failure
 */
static int set_broadcast(struct pt_context *ctx, int fd) {
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
                   "Failed to enable broadcast: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/**
 * Enable SO_REUSEADDR on socket
 *
 * Allows fast restart without "Address already in use" error.
 *
 * Returns: 0 on success, -1 on failure
 */
static int set_reuseaddr(struct pt_context *ctx, int fd) {
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
                   "Failed to set SO_REUSEADDR: %s", strerror(errno));
        return -1;
    }
    return 0;
}

/**
 * Get local IP address using three-tier fallback strategy
 *
 * 1. Try getifaddrs() - works on air-gapped networks and normal LANs
 * 2. Fall back to "connect to 8.8.8.8" trick - works in containers with NAT
 * 3. Return loopback (127.0.0.1) as last resort
 *
 * Returns: Local IP in host byte order
 */
static uint32_t get_local_ip(struct pt_context *ctx) {
    struct ifaddrs *ifaddr, *ifa;
    uint32_t local_ip = 0;

    /* Strategy 1: getifaddrs() - preferred method */
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL)
                continue;

            if (ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                uint32_t ip = ntohl(addr->sin_addr.s_addr);

                /* Skip loopback */
                if ((ip >> 24) == 127)
                    continue;

                /* Found valid interface */
                local_ip = ip;
                PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK,
                            "Local IP detected via getifaddrs: %u.%u.%u.%u",
                            (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                            (ip >> 8) & 0xFF, ip & 0xFF);
                break;
            }
        }
        freeifaddrs(ifaddr);
    }

    /* Strategy 2: Connect to 8.8.8.8 (Google DNS) - works in containers */
    if (local_ip == 0) {
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock >= 0) {
            struct sockaddr_in addr;
            pt_memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(0x08080808);  /* 8.8.8.8 */
            addr.sin_port = htons(53);

            if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0) {
                struct sockaddr_in local_addr;
                socklen_t len = sizeof(local_addr);
                if (getsockname(sock, (struct sockaddr *)&local_addr, &len) == 0) {
                    local_ip = ntohl(local_addr.sin_addr.s_addr);
                    PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK,
                                "Local IP detected via 8.8.8.8 trick: %u.%u.%u.%u",
                                (local_ip >> 24) & 0xFF, (local_ip >> 16) & 0xFF,
                                (local_ip >> 8) & 0xFF, local_ip & 0xFF);
                }
            }
            close(sock);
        }
    }

    /* Strategy 3: Fallback to loopback */
    if (local_ip == 0) {
        local_ip = 0x7F000001;  /* 127.0.0.1 */
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                   "Could not detect local IP, using loopback 127.0.0.1");
    }

    return local_ip;
}

/* ========================================================================== */
/* Platform Size/Init                                                         */
/* ========================================================================== */

size_t pt_posix_extra_size(void) {
    return sizeof(pt_posix_data);
}

int pt_posix_net_init(struct pt_context *ctx) {
    pt_posix_data *pd;
    size_t i;

    if (!ctx) {
        return -1;
    }

    pd = pt_posix_get(ctx);

    /* Initialize HOT fields */
    pd->max_fd = -1;
    pd->active_count = 0;
    pd->fd_dirty = 1;  /* Initial build needed */
    pd->batch_count = 0;
    pd->last_announce = 0;
    pd->local_ip = get_local_ip(ctx);

    /* Clear active peers tracking */
    pt_memset(pd->active_peers, 0, sizeof(pd->active_peers));
    pt_memset(pd->active_position, 0xFF, sizeof(pd->active_position));  /* -1 */

    /* Initialize WARM fields - sockets to -1 */
    pd->discovery_sock = -1;
    pd->listen_sock = -1;
    pd->udp_msg_sock = -1;
    pd->broadcast_addr = INADDR_BROADCAST;  /* 255.255.255.255 */
    pd->discovery_port = DISCOVERY_PORT(ctx);
    pd->listen_port = TCP_PORT(ctx);
    pd->udp_msg_port = UDP_PORT(ctx);

    /* Initialize fd_set cache */
    FD_ZERO(&pd->cached_read_fds);
    FD_ZERO(&pd->cached_write_fds);

    /* Initialize COLD fields - TCP sockets */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        pd->tcp_socks[i] = -1;
    }

    /* CRITICAL: Allocate recv_bufs separately for cache efficiency */
    pd->recv_bufs = (pt_recv_buffer *)pt_alloc_clear(
        sizeof(pt_recv_buffer) * PT_MAX_PEERS);
    if (!pd->recv_bufs) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
                   "Failed to allocate receive buffers");
        return -1;
    }

    /* Initialize receive buffer states */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        pd->recv_bufs[i].hot.state = PT_RECV_HEADER;
        pd->recv_bufs[i].hot.bytes_needed = PT_MESSAGE_HEADER_SIZE;
        pd->recv_bufs[i].hot.bytes_received = 0;
    }

    /* Initialize UDP messaging socket (Session 4.4) */
    if (pt_posix_udp_init(ctx) < 0) {
        pt_free(pd->recv_bufs);
        pd->recv_bufs = NULL;
        return -1;
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK,
                "POSIX networking initialized (discovery=%u, tcp=%u, udp=%u)",
                pd->discovery_port, pd->listen_port, pd->udp_msg_port);

    return 0;
}

void pt_posix_net_shutdown(struct pt_context *ctx) {
    pt_posix_data *pd;
    size_t i;

    if (!ctx) {
        return;
    }

    pd = pt_posix_get(ctx);

    /* Close server sockets */
    if (pd->discovery_sock >= 0) {
        close(pd->discovery_sock);
        pd->discovery_sock = -1;
    }

    if (pd->listen_sock >= 0) {
        close(pd->listen_sock);
        pd->listen_sock = -1;
    }

    if (pd->udp_msg_sock >= 0) {
        close(pd->udp_msg_sock);
        pd->udp_msg_sock = -1;
    }

    /* Close peer TCP sockets */
    for (i = 0; i < PT_MAX_PEERS; i++) {
        if (pd->tcp_socks[i] >= 0) {
            close(pd->tcp_socks[i]);
            pd->tcp_socks[i] = -1;
        }
    }

    /* Free receive buffers */
    if (pd->recv_bufs) {
        pt_free(pd->recv_bufs);
        pd->recv_bufs = NULL;
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK, "POSIX networking shut down");
}

/* ========================================================================== */
/* Discovery                                                                  */
/* ========================================================================== */

int pt_posix_discovery_start(struct pt_context *ctx) {
    pt_posix_data *pd;
    int sock;
    struct sockaddr_in addr;

    if (!ctx) {
        return -1;
    }

    pd = pt_posix_get(ctx);

    /* Create UDP socket */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
                   "Failed to create discovery socket: %s", strerror(errno));
        return -1;
    }

    /* Set non-blocking mode */
    if (set_nonblocking(ctx, sock) < 0) {
        close(sock);
        return -1;
    }

    /* Enable SO_BROADCAST */
    if (set_broadcast(ctx, sock) < 0) {
        close(sock);
        return -1;
    }

    /* Enable SO_REUSEADDR for fast restart */
    if (set_reuseaddr(ctx, sock) < 0) {
        close(sock);
        return -1;
    }

    /* Bind to INADDR_ANY on discovery port */
    pt_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(pd->discovery_port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK,
                   "Failed to bind discovery socket to port %u: %s",
                   pd->discovery_port, strerror(errno));
        close(sock);
        return -1;
    }

    pd->discovery_sock = sock;

    /* Update max_fd for select() */
    if (sock > pd->max_fd) {
        pd->max_fd = sock;
    }
    pd->fd_dirty = 1;  /* Rebuild fd_sets */

    PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY,
                "Discovery started on UDP port %u", pd->discovery_port);

    /* Send initial announcement */
    pt_posix_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);
    pd->last_announce = ctx->plat->get_ticks();

    return 0;
}

void pt_posix_discovery_stop(struct pt_context *ctx) {
    pt_posix_data *pd;

    if (!ctx) {
        return;
    }

    pd = pt_posix_get(ctx);

    if (pd->discovery_sock >= 0) {
        /* Send goodbye before closing */
        pt_posix_discovery_send(ctx, PT_DISC_TYPE_GOODBYE);

        close(pd->discovery_sock);
        pd->discovery_sock = -1;
        pd->fd_dirty = 1;

        PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY, "Discovery stopped");
    }
}

int pt_posix_discovery_send(struct pt_context *ctx, uint8_t type) {
    pt_posix_data *pd;
    pt_discovery_packet pkt;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    ssize_t encoded_len;
    struct sockaddr_in dest;
    ssize_t sent;

    if (!ctx) {
        return -1;
    }

    pd = pt_posix_get(ctx);

    if (pd->discovery_sock < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_DISCOVERY,
                   "Discovery socket not initialized");
        return -1;
    }

    /* Build discovery packet */
    pt_memset(&pkt, 0, sizeof(pkt));
    pkt.version = PT_PROTOCOL_VERSION;
    pkt.type = type;
    pkt.flags = 0;
    pkt.sender_port = pd->listen_port;
    pkt.transports = PT_TRANSPORT_TCP | PT_TRANSPORT_UDP;  /* POSIX supports both */

    /* Copy local name */
    if (ctx->config.local_name[0] != '\0') {
        pt_strncpy(pkt.name, ctx->config.local_name, PT_MAX_PEER_NAME);
    } else {
        pt_strncpy(pkt.name, "PeerTalk", PT_MAX_PEER_NAME);
    }

    /* Set name length */
    pkt.name_len = pt_strlen(pkt.name);

    /* Encode packet */
    encoded_len = pt_discovery_encode(&pkt, buf, sizeof(buf));
    if (encoded_len < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_DISCOVERY,
                   "Failed to encode discovery packet");
        return -1;
    }

    /* Send to broadcast address */
    pt_memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = htonl(pd->broadcast_addr);
    dest.sin_port = htons(pd->discovery_port);

    sent = sendto(pd->discovery_sock, buf, encoded_len, 0,
                  (struct sockaddr *)&dest, sizeof(dest));

    if (sent < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_DISCOVERY,
                   "Discovery send failed: %s", strerror(errno));
        return -1;
    }

    if (sent != encoded_len) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_DISCOVERY,
                   "Discovery send incomplete: %zd/%zd bytes", sent, encoded_len);
        return -1;
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY,
                "Discovery %s sent to %u.%u.%u.%u:%u (%zd bytes)",
                type == PT_DISC_TYPE_ANNOUNCE ? "ANNOUNCE" :
                type == PT_DISC_TYPE_QUERY ? "QUERY" : "GOODBYE",
                (pd->broadcast_addr >> 24) & 0xFF,
                (pd->broadcast_addr >> 16) & 0xFF,
                (pd->broadcast_addr >> 8) & 0xFF,
                pd->broadcast_addr & 0xFF,
                pd->discovery_port,
                sent);

    return 0;
}

int pt_posix_discovery_poll(struct pt_context *ctx) {
    pt_posix_data *pd;
    uint8_t buf[PT_DISCOVERY_MAX_SIZE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t ret;
    uint32_t sender_ip;
    pt_discovery_packet pkt;
    struct pt_peer *peer;

    if (!ctx) {
        return -1;
    }

    pd = pt_posix_get(ctx);

    if (pd->discovery_sock < 0) {
        return 0;  /* Not initialized yet */
    }

    /* Non-blocking receive */
    ret = recvfrom(pd->discovery_sock, buf, sizeof(buf), 0,
                   (struct sockaddr *)&from_addr, &from_len);

    if (ret < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return 0;  /* No data available - not an error */
        }
        PT_CTX_ERR(ctx, PT_LOG_CAT_DISCOVERY,
                   "Discovery recv error: %s", strerror(errno));
        return -1;
    }

    /* Extract sender IP */
    sender_ip = ntohl(from_addr.sin_addr.s_addr);

    /* CRITICAL: Ignore broadcasts from our own IP */
    if (sender_ip == pd->local_ip) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_DISCOVERY,
                    "Ignoring packet from our own IP %u.%u.%u.%u",
                    (sender_ip >> 24) & 0xFF, (sender_ip >> 16) & 0xFF,
                    (sender_ip >> 8) & 0xFF, sender_ip & 0xFF);
        return 0;  /* Our own broadcast, ignore */
    }

    /* Decode packet */
    if (pt_discovery_decode(ctx, buf, ret, &pkt) < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_DISCOVERY,
                   "Failed to decode discovery packet from %u.%u.%u.%u",
                   (sender_ip >> 24) & 0xFF, (sender_ip >> 16) & 0xFF,
                   (sender_ip >> 8) & 0xFF, sender_ip & 0xFF);
        return 0;  /* Ignore invalid packets */
    }

    PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY,
                "Discovery %s received from %u.%u.%u.%u:%u (%s)",
                pkt.type == PT_DISC_TYPE_ANNOUNCE ? "ANNOUNCE" :
                pkt.type == PT_DISC_TYPE_QUERY ? "QUERY" : "GOODBYE",
                (sender_ip >> 24) & 0xFF, (sender_ip >> 16) & 0xFF,
                (sender_ip >> 8) & 0xFF, sender_ip & 0xFF,
                pkt.sender_port, pkt.name);

    /* Handle packet type */
    PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY,
               "Processing packet type %u", pkt.type);
    switch (pkt.type) {
    case PT_DISC_TYPE_ANNOUNCE:
        PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY,
                   "Handling ANNOUNCE packet");
        /* Find or create peer */
        peer = pt_peer_find_by_addr(ctx, sender_ip, pkt.sender_port);
        PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY,
                   "pt_peer_find_by_addr returned: %p", (void*)peer);
        if (!peer) {
            /* Create new peer */
            PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY,
                       "Creating new peer: %s at %u.%u.%u.%u:%u",
                       pkt.name,
                       (sender_ip >> 24) & 0xFF, (sender_ip >> 16) & 0xFF,
                       (sender_ip >> 8) & 0xFF, sender_ip & 0xFF,
                       pkt.sender_port);
            peer = pt_peer_create(ctx, pkt.name, sender_ip, pkt.sender_port);
            if (!peer) {
                PT_CTX_WARN(ctx, PT_LOG_CAT_DISCOVERY,
                           "Failed to create peer for %s", pkt.name);
                return 0;
            }

            PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY,
                       "Peer created successfully, firing callback");

            /* Fire on_peer_discovered callback */
            if (ctx->callbacks.on_peer_discovered) {
                PeerTalk_PeerInfo info;
                pt_peer_get_info(peer, &info);
                PT_CTX_INFO(ctx, PT_LOG_CAT_DISCOVERY,
                           "Calling on_peer_discovered callback");
                ctx->callbacks.on_peer_discovered((PeerTalk_Context *)ctx,
                                                  &info, ctx->callbacks.user_data);
            } else {
                PT_CTX_WARN(ctx, PT_LOG_CAT_DISCOVERY,
                           "No on_peer_discovered callback registered!");
            }
        } else {
            /* Update existing peer */
            peer->hot.last_seen = ctx->plat->get_ticks();
        }
        break;

    case PT_DISC_TYPE_QUERY:
        /* Respond with ANNOUNCE */
        pt_posix_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);
        break;

    case PT_DISC_TYPE_GOODBYE:
        /* Remove peer */
        peer = pt_peer_find_by_addr(ctx, sender_ip, pkt.sender_port);
        if (peer) {
            PeerTalk_PeerID peer_id = peer->hot.id;

            /* Fire on_peer_lost callback before destroying */
            if (ctx->callbacks.on_peer_lost) {
                ctx->callbacks.on_peer_lost((PeerTalk_Context *)ctx,
                                           peer_id, ctx->callbacks.user_data);
            }

            pt_peer_destroy(ctx, peer);
        }
        break;

    default:
        PT_CTX_WARN(ctx, PT_LOG_CAT_DISCOVERY,
                   "Unknown discovery packet type: %u", pkt.type);
        break;
    }

    return 1;  /* Packet processed */
}

/* ========================================================================== */
/* Active Peer Tracking                                                      */
/* ========================================================================== */

/**
 * Add peer to active tracking list (O(1))
 *
 * Used when accepting connection or initiating connect.
 * Marks fd_sets dirty for rebuild.
 */
static void pt_posix_add_active_peer(pt_posix_data *pd, uint8_t peer_idx) {
    /* Skip if already in list */
    if (pd->active_position[peer_idx] != 0xFF) {
        return;
    }

    /* Add to end of active list */
    pd->active_peers[pd->active_count] = peer_idx;
    pd->active_position[peer_idx] = pd->active_count;
    pd->active_count++;
    pd->fd_dirty = 1;
}

/**
 * Remove peer from active tracking list (O(1) swap-back)
 *
 * Used when closing connection. Swaps last element into removed position.
 * Marks fd_sets dirty for rebuild.
 */
static void pt_posix_remove_active_peer(pt_posix_data *pd, uint8_t peer_idx) {
    uint8_t pos = pd->active_position[peer_idx];

    /* Not in list */
    if (pos == 0xFF) {
        return;
    }

    /* Swap last element into this position */
    if (pos < pd->active_count - 1) {
        uint8_t last_idx = pd->active_peers[pd->active_count - 1];
        pd->active_peers[pos] = last_idx;
        pd->active_position[last_idx] = pos;
    }

    /* Clear removed peer's position */
    pd->active_position[peer_idx] = 0xFF;
    pd->active_count--;
    pd->fd_dirty = 1;
}

/* ========================================================================== */
/* TCP Server                                                                 */
/* ========================================================================== */

int pt_posix_listen_start(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    struct sockaddr_in addr;
    int sock;
    uint16_t port;

    port = ctx->config.tcp_port > 0 ?
           ctx->config.tcp_port : TCP_PORT(ctx);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
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
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "Failed to bind listen socket: %s", strerror(errno));
        close(sock);
        return -1;
    }

    if (listen(sock, 8) < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "Listen failed: %s", strerror(errno));
        close(sock);
        return -1;
    }

    pd->listen_sock = sock;
    pd->listen_port = port;

    if (sock > pd->max_fd)
        pd->max_fd = sock;

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Listening on port %u", port);

    return 0;
}

void pt_posix_listen_stop(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);

    if (pd->listen_sock >= 0) {
        close(pd->listen_sock);
        pd->listen_sock = -1;
        PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT, "Listen stopped");
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
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "Accept error: %s", strerror(errno));
        return -1;
    }

    set_nonblocking(ctx, client_sock);

    client_ip = ntohl(addr.sin_addr.s_addr);

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Incoming connection from 0x%08X", client_ip);

    /* Find or create peer */
    peer = pt_peer_find_by_addr(ctx, client_ip, 0);
    if (!peer) {
        /* Unknown peer - create with empty name */
        peer = pt_peer_create(ctx, "", client_ip, ntohs(addr.sin_port));
    }

    if (!peer) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
            "No peer slot for incoming connection");
        close(client_sock);
        return 0;
    }

    /* Store socket and reset receive state */
    int peer_idx = peer->hot.id - 1;
    pd->tcp_socks[peer_idx] = client_sock;
    pd->recv_bufs[peer_idx].hot.state = PT_RECV_HEADER;
    pd->recv_bufs[peer_idx].hot.bytes_needed = PT_MESSAGE_HEADER_SIZE;
    pd->recv_bufs[peer_idx].hot.bytes_received = 0;

    /* Add to active peers list and mark fd_sets dirty */
    pt_posix_add_active_peer(pd, peer_idx);

    /* Update state with logging */
    pt_peer_set_state(ctx, peer, PT_PEER_CONNECTED);
    peer->hot.last_seen = ctx->plat->get_ticks();

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Accepted connection from peer %u at 0x%08X (assigned to slot %u)",
        peer->hot.id, client_ip, peer_idx);

    /* Fire callback */
    if (ctx->callbacks.on_peer_connected) {
        ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                         peer->hot.id, ctx->callbacks.user_data);
    }

    return 1;
}

/* ========================================================================== */
/* TCP Client                                                                 */
/* ========================================================================== */

int pt_posix_connect(struct pt_context *ctx, struct pt_peer *peer) {
    pt_posix_data *pd = pt_posix_get(ctx);
    struct sockaddr_in addr;
    int sock;
    int result;
    uint32_t peer_ip;
    uint16_t peer_port;

    if (!peer || peer->hot.magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    if (peer->hot.state != PT_PEER_DISCOVERED)
        return PT_ERR_INVALID_STATE;

    /* Get peer address from first address entry */
    if (peer->hot.address_count == 0)
        return PT_ERR_INVALID_STATE;

    peer_ip = peer->cold.addresses[0].address;
    peer_port = peer->cold.addresses[0].port;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "Failed to create socket: %s", strerror(errno));
        return PT_ERR_NETWORK;
    }

    set_nonblocking(ctx, sock);

    pt_memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(peer_ip);
    addr.sin_port = htons(peer_port);

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Connecting to peer %u (%s) at 0x%08X:%u",
        peer->hot.id, peer->cold.name, peer_ip, peer_port);

    result = connect(sock, (struct sockaddr *)&addr, sizeof(addr));

    if (result < 0) {
        if (errno == EINPROGRESS) {
            /* Connection in progress - this is expected for non-blocking */
            int peer_idx = peer->hot.id - 1;
            pd->tcp_socks[peer_idx] = sock;
            pd->recv_bufs[peer_idx].hot.state = PT_RECV_HEADER;
            pd->recv_bufs[peer_idx].hot.bytes_needed = PT_MESSAGE_HEADER_SIZE;
            pd->recv_bufs[peer_idx].hot.bytes_received = 0;

            /* Add to active peers list and mark fd_sets dirty */
            pt_posix_add_active_peer(pd, peer_idx);

            pt_peer_set_state(ctx, peer, PT_PEER_CONNECTING);
            peer->cold.ping_sent_time = ctx->plat->get_ticks();
            return PT_OK;
        }

        PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
            "Connect failed to peer %u (%s) at 0x%08X:%u: %s",
            peer->hot.id, peer->cold.name, peer_ip, peer_port, strerror(errno));
        close(sock);
        pt_peer_set_state(ctx, peer, PT_PEER_FAILED);
        return PT_ERR_NETWORK;
    }

    /* Immediate connection (unlikely but possible on localhost) */
    int peer_idx = peer->hot.id - 1;
    pd->tcp_socks[peer_idx] = sock;
    pd->recv_bufs[peer_idx].hot.state = PT_RECV_HEADER;
    pd->recv_bufs[peer_idx].hot.bytes_needed = PT_MESSAGE_HEADER_SIZE;
    pd->recv_bufs[peer_idx].hot.bytes_received = 0;

    /* Add to active peers list and mark fd_sets dirty */
    pt_posix_add_active_peer(pd, peer_idx);

    pt_peer_set_state(ctx, peer, PT_PEER_CONNECTED);
    peer->hot.last_seen = ctx->plat->get_ticks();

    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
        "Connected to peer %u (%s) - immediate connect on localhost",
        peer->hot.id, peer->cold.name);

    if (ctx->callbacks.on_peer_connected) {
        ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                         peer->hot.id, ctx->callbacks.user_data);
    }

    return PT_OK;
}

int pt_posix_disconnect(struct pt_context *ctx, struct pt_peer *peer) {
    pt_posix_data *pd = pt_posix_get(ctx);
    int peer_idx;
    int sock;

    if (!peer || peer->hot.magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    peer_idx = peer->hot.id - 1;
    sock = pd->tcp_socks[peer_idx];

    if (sock >= 0) {
        PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
            "Disconnecting peer %u (%s)", peer->hot.id, peer->cold.name);

        /* Send disconnect message if connected */
        if (peer->hot.state == PT_PEER_CONNECTED) {
            pt_message_header hdr;
            uint8_t buf[PT_MESSAGE_HEADER_SIZE + 2];

            hdr.type = PT_MSG_TYPE_DISCONNECT;
            hdr.flags = 0;
            hdr.sequence = peer->hot.send_seq++;
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

    pt_peer_set_state(ctx, peer, PT_PEER_DISCONNECTING);

    if (ctx->callbacks.on_peer_disconnected) {
        ctx->callbacks.on_peer_disconnected((PeerTalk_Context *)ctx,
                                            peer->hot.id, PT_OK,
                                            ctx->callbacks.user_data);
    }

    pt_peer_set_state(ctx, peer, PT_PEER_UNUSED);

    return PT_OK;
}

/* ========================================================================== */
/* UDP Messaging (Session 4.4 - Stub for now)                                */
/* ========================================================================== */

int pt_posix_udp_init(struct pt_context *ctx) {
    /* TODO: Session 4.4 will implement UDP messaging socket */
    (void)ctx;
    return 0;
}

void pt_posix_udp_shutdown(struct pt_context *ctx) {
    /* TODO: Session 4.4 */
    (void)ctx;
}

int pt_posix_send_udp(struct pt_context *ctx, struct pt_peer *peer,
                      const void *data, uint16_t len) {
    /* TODO: Session 4.4 */
    (void)ctx;
    (void)peer;
    (void)data;
    (void)len;
    return -1;
}

int pt_posix_recv_udp(struct pt_context *ctx) {
    /* TODO: Session 4.4 */
    (void)ctx;
    return 0;
}

/* ========================================================================== */
/* Connection Completion Checking (Session 4.2)                              */
/* ========================================================================== */

/**
 * Check for async connection completion
 *
 * Polls sockets in CONNECTING state for writability (indicates connection complete).
 * Uses select() with zero timeout for non-blocking check.
 *
 * Returns: Number of connections completed
 */
static int pt_posix_connect_poll(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    struct pt_peer *peer;
    fd_set write_fds;
    struct timeval tv;
    int completed = 0;
    size_t i;

    FD_ZERO(&write_fds);
    int max_fd = -1;

    /* Build fd_set of CONNECTING sockets */
    for (i = 0; i < ctx->max_peers; i++) {
        peer = &ctx->peers[i];
        if (peer->hot.state == PT_PEER_CONNECTING) {
            int sock = pd->tcp_socks[i];
            if (sock >= 0) {
                FD_SET(sock, &write_fds);
                if (sock > max_fd)
                    max_fd = sock;
            }
        }
    }

    if (max_fd < 0)
        return 0;  /* No connecting sockets */

    /* Zero timeout for non-blocking check */
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    if (select(max_fd + 1, NULL, &write_fds, NULL, &tv) < 0) {
        if (errno != EINTR) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
                "Select failed in connect poll: %s", strerror(errno));
        }
        return 0;
    }

    /* Check which sockets are now writable */
    for (i = 0; i < ctx->max_peers; i++) {
        peer = &ctx->peers[i];
        if (peer->hot.state == PT_PEER_CONNECTING) {
            int sock = pd->tcp_socks[i];
            if (sock >= 0 && FD_ISSET(sock, &write_fds)) {
                /* Socket is writable - connection complete or failed */
                int error = 0;
                socklen_t len = sizeof(error);

                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
                    PT_CTX_ERR(ctx, PT_LOG_CAT_CONNECT,
                        "getsockopt failed for peer %u: %s",
                        peer->hot.id, strerror(errno));
                    pt_peer_set_state(ctx, peer, PT_PEER_FAILED);
                    close(sock);
                    pd->tcp_socks[i] = -1;
                    pt_posix_remove_active_peer(pd, i);
                    continue;
                }

                if (error != 0) {
                    /* Connection failed */
                    PT_CTX_WARN(ctx, PT_LOG_CAT_CONNECT,
                        "Connection failed for peer %u (%s): %s",
                        peer->hot.id, peer->cold.name, strerror(error));
                    pt_peer_set_state(ctx, peer, PT_PEER_FAILED);
                    close(sock);
                    pd->tcp_socks[i] = -1;
                    pt_posix_remove_active_peer(pd, i);
                    continue;
                }

                /* Connection successful */
                pt_peer_set_state(ctx, peer, PT_PEER_CONNECTED);
                peer->hot.last_seen = ctx->plat->get_ticks();

                PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
                    "Connection established to peer %u (%s)",
                    peer->hot.id, peer->cold.name);

                /* Fire callback */
                if (ctx->callbacks.on_peer_connected) {
                    ctx->callbacks.on_peer_connected((PeerTalk_Context *)ctx,
                                                     peer->hot.id,
                                                     ctx->callbacks.user_data);
                }

                completed++;
            }
        }
    }

    return completed;
}

/* ========================================================================== */
/* Main Poll (Session 4.1 - Discovery only)                                  */
/* ========================================================================== */

int pt_posix_poll(struct pt_context *ctx) {
    pt_posix_data *pd;
    pt_tick_t now;

    if (!ctx) {
        return -1;
    }

    pd = pt_posix_get(ctx);
    now = ctx->plat->get_ticks();

    /* Poll discovery socket */
    if (pd->discovery_sock >= 0) {
        while (pt_posix_discovery_poll(ctx) > 0) {
            /* Process all pending discovery packets */
        }

        /* Send periodic announcements (every 5 seconds) */
        if (now - pd->last_announce >= 5000) {
            pt_posix_discovery_send(ctx, PT_DISC_TYPE_ANNOUNCE);
            pd->last_announce = now;
        }
    }

    /* Poll TCP listen socket */
    if (pd->listen_sock >= 0) {
        while (pt_posix_listen_poll(ctx) > 0) {
            /* Process all pending incoming connections */
        }
    }

    /* Check for connection completion (CONNECTING -> CONNECTED) */
    pt_posix_connect_poll(ctx);

    /* TODO: Session 4.3 - TCP peer socket I/O */
    /* TODO: Session 4.4 - UDP messaging socket polling */

    return 0;
}
