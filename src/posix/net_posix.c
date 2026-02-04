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

    /* Update statistics */
    ctx->global_stats.discovery_packets_sent++;

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

    /* Update statistics */
    ctx->global_stats.discovery_packets_received++;

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
/* TCP Message I/O (Session 4.3)                                             */
/* ========================================================================== */

/**
 * Send framed message to a connected peer
 *
 * Uses writev() for atomic transmission of header + payload + CRC in a single
 * syscall. This is more efficient than multiple send() calls and avoids TCP
 * Nagle algorithm issues.
 *
 * @param ctx PeerTalk context
 * @param peer Target peer (must be in CONNECTED state)
 * @param data Message payload
 * @param len Payload length (max PT_MAX_MESSAGE_SIZE)
 * @return PT_OK on success, error code on failure
 */
int pt_posix_send(struct pt_context *ctx, struct pt_peer *peer,
                  const void *data, size_t len) {
    pt_posix_data *pd = pt_posix_get(ctx);
    pt_message_header hdr;
    uint8_t header_buf[PT_MESSAGE_HEADER_SIZE];
    uint8_t crc_buf[2];
    struct iovec iov[3];
    ssize_t total_len;
    ssize_t sent;
    int peer_idx;
    int sock;
    uint16_t crc;

    /* Validation */
    if (!peer || peer->hot.magic != PT_PEER_MAGIC) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL, "Invalid peer");
        return PT_ERR_INVALID_PARAM;
    }

    if (peer->hot.state != PT_PEER_CONNECTED) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "Cannot send to peer %u: not connected (state=%d)",
            peer->hot.id, peer->hot.state);
        return PT_ERR_INVALID_STATE;
    }

    if (len > PT_MAX_MESSAGE_SIZE) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL,
            "Message too large: %zu bytes (max %d)",
            len, PT_MAX_MESSAGE_SIZE);
        return PT_ERR_MESSAGE_TOO_LARGE;
    }

    peer_idx = peer->hot.id - 1;
    sock = pd->tcp_socks[peer_idx];

    if (sock < 0) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL, "Invalid socket for peer %u", peer->hot.id);
        return PT_ERR_INVALID_STATE;
    }

    /* Encode header */
    hdr.type = PT_MSG_TYPE_DATA;
    hdr.flags = 0;
    hdr.sequence = peer->hot.send_seq++;
    hdr.payload_len = (uint16_t)len;
    pt_message_encode_header(&hdr, header_buf);

    /* Calculate CRC over header + payload */
    crc = pt_crc16(header_buf, PT_MESSAGE_HEADER_SIZE);
    crc = pt_crc16_update(crc, data, len);
    crc_buf[0] = (crc >> 8) & 0xFF;
    crc_buf[1] = crc & 0xFF;

    /* Prepare iovec for atomic send */
    iov[0].iov_base = header_buf;
    iov[0].iov_len = PT_MESSAGE_HEADER_SIZE;
    iov[1].iov_base = (void *)data;
    iov[1].iov_len = len;
    iov[2].iov_base = crc_buf;
    iov[2].iov_len = 2;

    total_len = PT_MESSAGE_HEADER_SIZE + len + 2;

    /* Send with writev - handles partial writes */
    sent = writev(sock, iov, 3);

    if (sent < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL,
                "Send would block for peer %u", peer->hot.id);
            return PT_ERR_WOULD_BLOCK;
        }
        PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL,
            "Send failed for peer %u: %s", peer->hot.id, strerror(errno));
        return PT_ERR_NETWORK;
    }

    if (sent < total_len) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
            "Partial send to peer %u: %zd/%zd bytes", peer->hot.id, sent, total_len);
        /* TODO: Handle partial sends with send queue */
        return PT_ERR_WOULD_BLOCK;
    }

    /* Update statistics */
    peer->cold.stats.bytes_sent += sent;
    peer->cold.stats.messages_sent++;
    ctx->global_stats.total_bytes_sent += sent;
    ctx->global_stats.total_messages_sent++;

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL,
        "Sent %zd bytes to peer %u (seq=%u)", sent, peer->hot.id, hdr.sequence);

    return PT_OK;
}

/**
 * Send control message (PING, PONG, DISCONNECT)
 *
 * Control messages have zero-length payload and use sequence=0 to distinguish
 * from user data messages.
 *
 * @param ctx PeerTalk context
 * @param peer Target peer
 * @param msg_type PT_MSG_TYPE_PING, PT_MSG_TYPE_PONG, or PT_MSG_TYPE_DISCONNECT
 * @return PT_OK on success, error code on failure
 */
int pt_posix_send_control(struct pt_context *ctx, struct pt_peer *peer,
                           uint8_t msg_type) {
    pt_posix_data *pd = pt_posix_get(ctx);
    pt_message_header hdr;
    uint8_t buf[PT_MESSAGE_HEADER_SIZE + 2];
    uint16_t crc;
    ssize_t sent;
    int peer_idx;
    int sock;

    if (!peer || peer->hot.magic != PT_PEER_MAGIC)
        return PT_ERR_INVALID_PARAM;

    if (peer->hot.state != PT_PEER_CONNECTED)
        return PT_ERR_INVALID_STATE;

    peer_idx = peer->hot.id - 1;
    sock = pd->tcp_socks[peer_idx];

    if (sock < 0)
        return PT_ERR_INVALID_STATE;

    /* Encode header (sequence=0 for control messages) */
    hdr.type = msg_type;
    hdr.flags = 0;
    hdr.sequence = 0;  /* Control messages use seq=0 intentionally */
    hdr.payload_len = 0;
    pt_message_encode_header(&hdr, buf);

    /* Calculate CRC */
    crc = pt_crc16(buf, PT_MESSAGE_HEADER_SIZE);
    buf[PT_MESSAGE_HEADER_SIZE] = (crc >> 8) & 0xFF;
    buf[PT_MESSAGE_HEADER_SIZE + 1] = crc & 0xFF;

    /* Send */
    sent = send(sock, buf, sizeof(buf), 0);

    if (sent < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return PT_ERR_WOULD_BLOCK;
        return PT_ERR_NETWORK;
    }

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL,
        "Sent control message type=%u to peer %u", msg_type, peer->hot.id);

    return PT_OK;
}

/* ========================================================================== */
/* Statistics Helpers (Session 4.5)                                          */
/* ========================================================================== */

/**
 * Calculate connection quality score from latency
 *
 * Quality score (0-100) based on LAN latency thresholds:
 * - < 5ms: 100 (excellent - typical wired LAN)
 * - < 10ms: 90 (very good - good WiFi or loaded LAN)
 * - < 20ms: 75 (good - congested WiFi or slower network)
 * - < 50ms: 50 (fair - problematic connection for LAN)
 * - >= 50ms: 25 (poor - very bad for LAN, investigate)
 *
 * Returns: Quality score (0-100)
 */
static uint8_t calculate_quality(uint16_t latency_ms) {
    if (latency_ms < 5) {
        return 100;
    } else if (latency_ms < 10) {
        return 90;
    } else if (latency_ms < 20) {
        return 75;
    } else if (latency_ms < 50) {
        return 50;
    } else {
        return 25;
    }
}

/**
 * Update peer latency from PONG response
 *
 * Called when PONG message is received. Calculates RTT, updates rolling average,
 * and calculates connection quality score.
 */
static void update_peer_latency(struct pt_context *ctx, struct pt_peer *peer) {
    pt_tick_t now;
    uint16_t rtt;

    if (peer->cold.ping_sent_time == 0) {
        return;  /* No pending ping */
    }

    now = ctx->plat->get_ticks();
    rtt = (uint16_t)(now - peer->cold.ping_sent_time);
    peer->cold.ping_sent_time = 0;

    /* Rolling average: new = (old * 3 + sample) / 4 */
    if (peer->cold.stats.latency_ms == 0) {
        peer->cold.stats.latency_ms = rtt;
    } else {
        peer->cold.stats.latency_ms = (peer->cold.stats.latency_ms * 3 + rtt) / 4;
    }

    /* Calculate quality */
    peer->cold.stats.quality = calculate_quality(peer->cold.stats.latency_ms);

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL,
                 "Peer %u latency: %u ms (quality: %u%%)",
                 peer->hot.id, peer->cold.stats.latency_ms, peer->cold.stats.quality);
}

/* ========================================================================== */
/* TCP Receive State Machine (Session 4.3)                                   */
/* ========================================================================== */

/**
 * Reset receive buffer to initial state
 *
 * @param buf Receive buffer to reset
 */
static void pt_recv_reset(pt_recv_buffer *buf) {
    buf->hot.state = PT_RECV_HEADER;
    buf->hot.bytes_needed = PT_MESSAGE_HEADER_SIZE;
    buf->hot.bytes_received = 0;
}

/**
 * Receive header bytes
 *
 * Accumulates header bytes until complete, then transitions to payload or CRC state.
 *
 * @param ctx PeerTalk context
 * @param peer Target peer
 * @param buf Receive buffer
 * @param sock TCP socket
 * @return 1 = header complete, 0 = waiting for more data, -1 = error
 */
static int pt_recv_header(struct pt_context *ctx, struct pt_peer *peer,
                           pt_recv_buffer *buf, int sock) {
    ssize_t ret;
    size_t remaining;

    remaining = buf->hot.bytes_needed - buf->hot.bytes_received;

    ret = recv(sock, buf->cold.header_buf + buf->hot.bytes_received,
               remaining, 0);

    if (ret < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;  /* No data yet */
        PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL,
            "Header recv error for peer %u: %s", peer->hot.id, strerror(errno));
        return -1;
    }

    if (ret == 0) {
        PT_CTX_INFO(ctx, PT_LOG_CAT_PROTOCOL,
            "Peer %u closed connection during header", peer->hot.id);
        return -1;
    }

    buf->hot.bytes_received += ret;

    if (buf->hot.bytes_received >= buf->hot.bytes_needed) {
        /* Header complete - decode it */
        if (pt_message_decode_header(ctx, buf->cold.header_buf,
                                      PT_MESSAGE_HEADER_SIZE,
                                      &buf->cold.hdr) != PT_OK) {
            PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL,
                "Invalid message header from peer %u", peer->hot.id);
            return -1;
        }

        /* Validate payload size */
        if (buf->cold.hdr.payload_len > PT_MAX_MESSAGE_SIZE) {
            PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL,
                "Payload too large from peer %u: %u bytes (max %d)",
                peer->hot.id, buf->cold.hdr.payload_len, PT_MAX_MESSAGE_SIZE);
            return -1;
        }

        /* Transition to next state */
        if (buf->cold.hdr.payload_len > 0) {
            buf->hot.state = PT_RECV_PAYLOAD;
            buf->hot.bytes_needed = buf->cold.hdr.payload_len;
            buf->hot.bytes_received = 0;
        } else {
            /* No payload - go straight to CRC */
            buf->hot.state = PT_RECV_CRC;
            buf->hot.bytes_needed = 2;
            buf->hot.bytes_received = 0;
        }

        return 1;  /* Header complete */
    }

    return 0;  /* Waiting for more header bytes */
}

/**
 * Receive payload bytes
 *
 * @return 1 = payload complete, 0 = waiting for more data, -1 = error
 */
static int pt_recv_payload(struct pt_context *ctx, struct pt_peer *peer,
                             pt_recv_buffer *buf, int sock) {
    ssize_t ret;
    size_t remaining;

    remaining = buf->hot.bytes_needed - buf->hot.bytes_received;

    ret = recv(sock, buf->cold.payload_buf + buf->hot.bytes_received,
               remaining, 0);

    if (ret < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;
        PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL,
            "Payload recv error for peer %u: %s", peer->hot.id, strerror(errno));
        return -1;
    }

    if (ret == 0) {
        PT_CTX_INFO(ctx, PT_LOG_CAT_PROTOCOL,
            "Peer %u closed connection during payload", peer->hot.id);
        return -1;
    }

    buf->hot.bytes_received += ret;

    if (buf->hot.bytes_received >= buf->hot.bytes_needed) {
        /* Payload complete - move to CRC */
        buf->hot.state = PT_RECV_CRC;
        buf->hot.bytes_needed = 2;
        buf->hot.bytes_received = 0;
        return 1;
    }

    return 0;  /* Waiting for more payload bytes */
}

/**
 * Receive CRC bytes
 *
 * @return 1 = CRC complete, 0 = waiting for more data, -1 = error
 */
static int pt_recv_crc(struct pt_context *ctx, struct pt_peer *peer,
                        pt_recv_buffer *buf, int sock) {
    ssize_t ret;
    size_t remaining;

    remaining = buf->hot.bytes_needed - buf->hot.bytes_received;

    ret = recv(sock, buf->cold.crc_buf + buf->hot.bytes_received,
               remaining, 0);

    if (ret < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return 0;
        PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL,
            "CRC recv error for peer %u: %s", peer->hot.id, strerror(errno));
        return -1;
    }

    if (ret == 0) {
        PT_CTX_INFO(ctx, PT_LOG_CAT_PROTOCOL,
            "Peer %u closed connection during CRC", peer->hot.id);
        return -1;
    }

    buf->hot.bytes_received += ret;

    if (buf->hot.bytes_received >= buf->hot.bytes_needed) {
        /* CRC complete - message fully received */
        return 1;
    }

    return 0;  /* Waiting for more CRC bytes */
}

/**
 * Process complete message after CRC validation
 *
 * Verifies CRC, updates statistics, and dispatches by message type.
 *
 * @return 0 on success, -1 on error or disconnect
 */
static int pt_recv_process_message(struct pt_context *ctx, struct pt_peer *peer,
                                     pt_recv_buffer *buf) {
    uint16_t expected_crc;
    uint16_t received_crc;

    /* Calculate expected CRC */
    expected_crc = pt_crc16(buf->cold.header_buf, PT_MESSAGE_HEADER_SIZE);
    if (buf->cold.hdr.payload_len > 0) {
        expected_crc = pt_crc16_update(expected_crc, buf->cold.payload_buf,
                                        buf->cold.hdr.payload_len);
    }

    /* Extract received CRC */
    received_crc = ((uint16_t)buf->cold.crc_buf[0] << 8) | buf->cold.crc_buf[1];

    if (expected_crc != received_crc) {
        PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL,
            "CRC mismatch from peer %u: expected 0x%04X, got 0x%04X",
            peer->hot.id, expected_crc, received_crc);
        return -1;
    }

    /* Update statistics */
    peer->cold.stats.bytes_received += PT_MESSAGE_HEADER_SIZE +
                                        buf->cold.hdr.payload_len + 2;
    peer->cold.stats.messages_received++;
    ctx->global_stats.total_bytes_received += PT_MESSAGE_HEADER_SIZE +
                                               buf->cold.hdr.payload_len + 2;
    ctx->global_stats.total_messages_received++;

    /* Dispatch by message type */
    switch (buf->cold.hdr.type) {
        case PT_MSG_TYPE_DATA:
            /* Fire callback */
            if (ctx->callbacks.on_message_received) {
                ctx->callbacks.on_message_received((PeerTalk_Context *)ctx,
                    peer->hot.id,
                    buf->cold.payload_buf,
                    buf->cold.hdr.payload_len,
                    ctx->callbacks.user_data);
            }
            break;

        case PT_MSG_TYPE_PING:
            /* Send PONG response */
            pt_posix_send_control(ctx, peer, PT_MSG_TYPE_PONG);
            PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL,
                "Received PING from peer %u, sent PONG", peer->hot.id);
            break;

        case PT_MSG_TYPE_PONG:
            /* Update latency with rolling average and quality calculation */
            update_peer_latency(ctx, peer);
            break;

        case PT_MSG_TYPE_DISCONNECT:
            PT_CTX_INFO(ctx, PT_LOG_CAT_PROTOCOL,
                "Received DISCONNECT from peer %u", peer->hot.id);
            return -1;  /* Trigger disconnect */

        case PT_MSG_TYPE_ACK:
            /* Handle reliable message ACK - TODO in future session */
            PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL,
                "Received ACK from peer %u", peer->hot.id);
            break;

        default:
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                "Unknown message type %u from peer %u",
                buf->cold.hdr.type, peer->hot.id);
            break;
    }

    return 0;
}

/**
 * Main receive function - drives state machine
 *
 * Non-blocking receive with state machine for partial reads.
 *
 * @param ctx PeerTalk context
 * @param peer Target peer
 * @return 0 on success, -1 on error or disconnect
 */
int pt_posix_recv(struct pt_context *ctx, struct pt_peer *peer) {
    pt_posix_data *pd = pt_posix_get(ctx);
    pt_recv_buffer *buf;
    int peer_idx;
    int sock;
    int ret;

    if (!peer || peer->hot.magic != PT_PEER_MAGIC)
        return -1;

    peer_idx = peer->hot.id - 1;
    sock = pd->tcp_socks[peer_idx];
    buf = &pd->recv_bufs[peer_idx];

    if (sock < 0)
        return -1;

    /* Drive state machine */
    while (1) {
        switch (buf->hot.state) {
            case PT_RECV_HEADER:
                ret = pt_recv_header(ctx, peer, buf, sock);
                if (ret <= 0) return ret;
                break;

            case PT_RECV_PAYLOAD:
                ret = pt_recv_payload(ctx, peer, buf, sock);
                if (ret <= 0) return ret;
                break;

            case PT_RECV_CRC:
                ret = pt_recv_crc(ctx, peer, buf, sock);
                if (ret <= 0) return ret;

                /* Message complete - process it */
                ret = pt_recv_process_message(ctx, peer, buf);
                pt_recv_reset(buf);  /* Reset for next message */
                return ret;

            default:
                PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL,
                    "Invalid recv state %u for peer %u",
                    buf->hot.state, peer->hot.id);
                pt_recv_reset(buf);
                return -1;
        }
    }
}

/* ========================================================================== */
/* UDP Messaging (Session 4.4 - Stub for now)                                */
/* ========================================================================== */

/**
 * Initialize UDP messaging socket
 *
 * Creates dedicated UDP socket for unreliable messaging (separate from discovery).
 * Binds to DEFAULT_UDP_PORT (7355) or user-configured port.
 *
 * Returns: 0 on success, -1 on failure
 */
int pt_posix_udp_init(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    struct sockaddr_in addr;
    int sock;
    uint16_t port = UDP_PORT(ctx);

    /* Create UDP socket */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                    "Failed to create UDP messaging socket: %s", strerror(errno));
        return -1;
    }

    /* Set non-blocking */
    if (set_nonblocking(ctx, sock) < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                    "Failed to set UDP messaging socket non-blocking");
        close(sock);
        return -1;
    }

    /* Set SO_REUSEADDR */
    if (set_reuseaddr(ctx, sock) < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                    "Failed to set SO_REUSEADDR on UDP messaging socket");
        close(sock);
        return -1;
    }

    /* Bind to port */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                    "Failed to bind UDP messaging socket to port %u: %s",
                    port, strerror(errno));
        close(sock);
        return -1;
    }

    pd->udp_msg_sock = sock;
    pd->udp_msg_port = port;

    /* Update max_fd for select() */
    if (sock > pd->max_fd) {
        pd->max_fd = sock;
    }
    pd->fd_dirty = 1;  /* Rebuild fd_sets */

    PT_CTX_INFO(ctx, PT_LOG_CAT_NETWORK,
                "UDP messaging socket initialized on port %u", port);
    return 0;
}

/**
 * Shutdown UDP messaging socket
 *
 * Closes dedicated UDP messaging socket and cleans up resources.
 */
void pt_posix_udp_shutdown(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);

    if (pd->udp_msg_sock >= 0) {
        close(pd->udp_msg_sock);
        pd->udp_msg_sock = -1;
        pd->fd_dirty = 1;
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK, "UDP messaging socket closed");
    }
}

/**
 * Send UDP message to peer
 *
 * Sends unreliable UDP datagram with 8-byte header (magic, sender_port, payload_len).
 * No CRC - UDP has its own checksum at transport layer.
 *
 * Returns: PT_OK on success, PT_ERR_* on failure
 */
int pt_posix_send_udp(struct pt_context *ctx, struct pt_peer *peer,
                      const void *data, uint16_t len) {
    pt_posix_data *pd = pt_posix_get(ctx);
    uint8_t packet_buf[PT_MAX_UDP_MESSAGE_SIZE];
    struct sockaddr_in dest_addr;
    int packet_len;
    ssize_t sent;

    /* Validate socket */
    if (pd->udp_msg_sock < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK, "UDP messaging socket not initialized");
        return PT_ERR_NOT_INITIALIZED;
    }

    /* Validate peer state */
    if (peer->hot.state == PT_PEER_UNUSED) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
                     "Attempted to send UDP to peer %u (not connected)", peer->hot.id);
        return PT_ERR_PEER_NOT_FOUND;
    }

    /* Validate message size (enforce 512-byte limit) */
    if (len > PT_MAX_UDP_MESSAGE_SIZE - 8) {  /* 8-byte header */
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                    "UDP message too large: %u bytes (max 504)", len);
        return PT_ERR_MESSAGE_TOO_LARGE;
    }

    /* Encode UDP packet */
    packet_len = pt_udp_encode(data, len, pd->udp_msg_port,
                               packet_buf, sizeof(packet_buf));
    if (packet_len < 0) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                    "Failed to encode UDP packet: %d", packet_len);
        return packet_len;
    }

    /* Build destination address */
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = htonl(peer->cold.addresses[0].address);
    dest_addr.sin_port = htons(pd->udp_msg_port);  /* Use same UDP port */

    /* Send datagram */
    sent = sendto(pd->udp_msg_sock, packet_buf, packet_len, 0,
                  (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    if (sent < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK, "UDP send would block");
            return PT_ERR_WOULD_BLOCK;
        }
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                    "UDP sendto failed: %s", strerror(errno));
        return PT_ERR_NETWORK;
    }

    /* Update statistics */
    peer->cold.stats.bytes_sent += sent;
    peer->cold.stats.messages_sent++;
    ctx->global_stats.total_bytes_sent += sent;
    ctx->global_stats.total_messages_sent++;

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
                 "Sent UDP message to peer %u (%u bytes payload, %zd bytes total)",
                 peer->hot.id, len, sent);

    return PT_OK;
}

/**
 * Receive UDP messages from any peer
 *
 * Non-blocking recvfrom() on UDP messaging socket. Validates magic "PTUD",
 * finds peer by source IP, and fires on_message_received callback.
 *
 * Returns: 1 if message received, 0 if no data, -1 on error
 */
int pt_posix_recv_udp(struct pt_context *ctx) {
    pt_posix_data *pd = pt_posix_get(ctx);
    uint8_t packet_buf[PT_MAX_UDP_MESSAGE_SIZE];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t received;
    uint16_t sender_port;
    const void *payload;
    uint16_t payload_len;
    uint32_t sender_ip;
    struct pt_peer *peer;
    int ret;

    /* Check socket */
    if (pd->udp_msg_sock < 0) {
        return 0;  /* Not initialized yet */
    }

    /* Non-blocking receive */
    received = recvfrom(pd->udp_msg_sock, packet_buf, sizeof(packet_buf), 0,
                        (struct sockaddr *)&from_addr, &from_len);

    if (received < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            return 0;  /* No data available */
        }
        PT_CTX_WARN(ctx, PT_LOG_CAT_NETWORK,
                    "UDP recvfrom failed: %s", strerror(errno));
        return -1;
    }

    /* Decode packet */
    ret = pt_udp_decode(ctx, packet_buf, received, &sender_port, &payload, &payload_len);
    if (ret < 0) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
                     "Failed to decode UDP packet: %d", ret);
        return 0;  /* Ignore malformed packets */
    }

    /* Find peer by source IP */
    sender_ip = ntohl(from_addr.sin_addr.s_addr);
    peer = pt_peer_find_by_addr(ctx, sender_ip, 0);  /* Match any port */

    if (!peer) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
                     "Received UDP from unknown peer %u.%u.%u.%u",
                     (sender_ip >> 24) & 0xFF,
                     (sender_ip >> 16) & 0xFF,
                     (sender_ip >> 8) & 0xFF,
                     sender_ip & 0xFF);
        return 0;  /* Ignore unknown peers */
    }

    /* Update peer statistics and timestamp */
    peer->cold.stats.bytes_received += received;
    peer->cold.stats.messages_received++;
    peer->hot.last_seen = ctx->plat->get_ticks();

    /* Update global statistics */
    ctx->global_stats.total_bytes_received += received;
    ctx->global_stats.total_messages_received++;

    PT_CTX_DEBUG(ctx, PT_LOG_CAT_NETWORK,
                 "Received UDP from peer %u (%u bytes payload, %zd bytes total)",
                 peer->hot.id, payload_len, received);

    /* Fire callback */
    if (ctx->callbacks.on_message_received) {
        ctx->callbacks.on_message_received((PeerTalk_Context *)ctx,
                                          peer->hot.id,
                                          payload,
                                          payload_len,
                                          ctx->callbacks.user_data);
    }

    return 1;  /* Message processed */
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

    /* Poll TCP peer sockets for received data (Session 4.3) */
    for (uint8_t i = 0; i < pd->active_count; i++) {
        uint8_t peer_idx = pd->active_peers[i];
        struct pt_peer *peer = &ctx->peers[peer_idx];
        int sock = pd->tcp_socks[peer_idx];

        if (peer->hot.state == PT_PEER_CONNECTED && sock >= 0) {
            /* Check if data available using select() */
            fd_set read_fds;
            struct timeval tv = {0, 0};  /* Zero timeout = non-blocking */

            FD_ZERO(&read_fds);
            FD_SET(sock, &read_fds);

            if (select(sock + 1, &read_fds, NULL, NULL, &tv) > 0) {
                if (pt_posix_recv(ctx, peer) < 0) {
                    /* Error or disconnect - close connection */
                    PT_CTX_INFO(ctx, PT_LOG_CAT_CONNECT,
                        "Closing connection to peer %u", peer->hot.id);
                    pt_posix_disconnect(ctx, peer);
                }
            }
        }
    }

    /* Poll UDP messaging socket (Session 4.4) */
    if (pd->udp_msg_sock >= 0) {
        while (pt_posix_recv_udp(ctx) > 0) {
            /* Process all pending UDP messages */
        }
    }

    return 0;
}

/* ========================================================================== */
/* Public API (Session 4.4)                                                  */
/* ========================================================================== */

/**
 * Send unreliable UDP message to peer
 *
 * Public API wrapper for pt_posix_send_udp(). Validates context and peer ID,
 * then sends UDP datagram with 8-byte header (no CRC).
 *
 * Returns: PT_OK on success, PT_ERR_* on failure
 */
PeerTalk_Error PeerTalk_SendUDP(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                const void *data, uint16_t length) {
    struct pt_context *ictx = (struct pt_context *)ctx;
    struct pt_peer *peer;

    /* Validate context */
    if (!ictx || ictx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_STATE;
    }

    /* Validate data and length */
    if (!data && length > 0) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Find peer by ID */
    peer = pt_peer_find_by_id(ictx, peer_id);
    if (!peer) {
        return PT_ERR_PEER_NOT_FOUND;
    }

    /* Call internal send function */
    return pt_posix_send_udp(ictx, peer, data, length);
}

/* ========================================================================== */
/* Statistics API (Session 4.5)                                              */
/* ========================================================================== */

/**
 * Get global network statistics
 *
 * Returns aggregate statistics for all network activity including bytes sent/received,
 * message counts, peer counts, and connection statistics.
 *
 * Returns: PT_OK on success, PT_ERR_* on failure
 */
PeerTalk_Error PeerTalk_GetGlobalStats(PeerTalk_Context *ctx, PeerTalk_GlobalStats *stats) {
    struct pt_context *ictx = (struct pt_context *)ctx;
    uint16_t connected_count = 0;

    /* Validate parameters */
    if (!ictx || ictx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_STATE;
    }
    if (!stats) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Copy global statistics */
    stats->total_bytes_sent = ictx->global_stats.total_bytes_sent;
    stats->total_bytes_received = ictx->global_stats.total_bytes_received;
    stats->total_messages_sent = ictx->global_stats.total_messages_sent;
    stats->total_messages_received = ictx->global_stats.total_messages_received;
    stats->discovery_packets_sent = ictx->global_stats.discovery_packets_sent;
    stats->discovery_packets_received = ictx->global_stats.discovery_packets_received;
    stats->connections_accepted = ictx->global_stats.connections_accepted;
    stats->connections_rejected = ictx->global_stats.connections_rejected;

    /* Count peers by state */
    stats->peers_discovered = 0;
    for (uint8_t i = 0; i < ictx->max_peers; i++) {
        if (ictx->peers[i].hot.state != PT_PEER_UNUSED) {
            stats->peers_discovered++;
            if (ictx->peers[i].hot.state == PT_PEER_CONNECTED) {
                connected_count++;
            }
        }
    }
    stats->peers_connected = connected_count;

    /* Memory and streams (not yet tracked - set to 0) */
    stats->memory_used = 0;
    stats->streams_active = connected_count;
    stats->reserved = 0;

    return PT_OK;
}

/**
 * Get per-peer statistics
 *
 * Returns statistics for a specific peer including bytes sent/received,
 * message counts, latency, and connection quality.
 *
 * Returns: PT_OK on success, PT_ERR_* on failure
 */
PeerTalk_Error PeerTalk_GetPeerStats(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                     PeerTalk_PeerStats *stats) {
    struct pt_context *ictx = (struct pt_context *)ctx;
    struct pt_peer *peer;

    /* Validate parameters */
    if (!ictx || ictx->magic != PT_CONTEXT_MAGIC) {
        return PT_ERR_INVALID_STATE;
    }
    if (!stats) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Find peer by ID */
    peer = pt_peer_find_by_id(ictx, peer_id);
    if (!peer) {
        return PT_ERR_PEER_NOT_FOUND;
    }

    /* Copy peer statistics */
    *stats = peer->cold.stats;

    return PT_OK;
}

