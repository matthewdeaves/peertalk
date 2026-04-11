/*
 * pt_posix.c -- POSIX platform backend (BSD sockets + select)
 */

#include "pt_internal.h"

#ifdef PT_PLATFORM_POSIX

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Platform state                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    int discovery_fd;   /* UDP socket, port 7353 */
    int tcp_listen_fd;  /* TCP listener, port 7354 */
    int udp_msg_fd;     /* UDP socket, port 7355 */
    int listening;       /* tcp_listen called? */
} PosixState;

static PosixState g_posix;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int make_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int create_udp_socket(unsigned short port, int broadcast)
{
    int fd;
    int opt = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (broadcast) {
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    make_nonblocking(fd);
    return fd;
}

static int create_tcp_listener(unsigned short port)
{
    int fd;
    int opt = 1;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    make_nonblocking(fd);
    return fd;
}

static unsigned long get_local_ip(void)
{
    int fd;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    /* Connect a UDP socket to a public IP to determine local IP */
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return htonl(INADDR_LOOPBACK);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = htonl(0x08080808); /* 8.8.8.8 */

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return htonl(INADDR_LOOPBACK);
    }

    if (getsockname(fd, (struct sockaddr *)&addr, &len) < 0) {
        close(fd);
        return htonl(INADDR_LOOPBACK);
    }

    close(fd);
    return addr.sin_addr.s_addr;
}

/* ------------------------------------------------------------------ */
/* PT_PlatformOps implementation                                       */
/* ------------------------------------------------------------------ */

static PT_Status posix_init(PT_Context_Internal *ctx)
{
    memset(&g_posix, 0, sizeof(g_posix));
    g_posix.discovery_fd = -1;
    g_posix.tcp_listen_fd = -1;
    g_posix.udp_msg_fd = -1;
    g_posix.listening = 0;

    /* Discovery UDP socket (port 7353) */
    g_posix.discovery_fd = create_udp_socket(PT_DISCOVERY_PORT, 1);
    if (g_posix.discovery_fd < 0) {
        CLOG_ERR("Failed to create discovery socket");
        return PT_ERR_INIT;
    }

    /* TCP listener socket (port 7354) */
    g_posix.tcp_listen_fd = create_tcp_listener(PT_TCP_PORT);
    if (g_posix.tcp_listen_fd < 0) {
        CLOG_ERR("Failed to create TCP listener socket");
        close(g_posix.discovery_fd);
        return PT_ERR_INIT;
    }

    /* UDP message socket (port 7355).
     * Needs SO_BROADCAST because posix_udp_broadcast may send through
     * this socket for non-discovery ports (e.g. clog on port 7356). */
    g_posix.udp_msg_fd = create_udp_socket(PT_UDP_MSG_PORT, 1);
    if (g_posix.udp_msg_fd < 0) {
        CLOG_ERR("Failed to create UDP message socket");
        close(g_posix.discovery_fd);
        close(g_posix.tcp_listen_fd);
        return PT_ERR_INIT;
    }

    /* Get local IP */
    ctx->local_ip = get_local_ip();
    ctx->platform_state = &g_posix;

    CLOG_INFO("Initialized (IP: %s)",
              inet_ntoa(*(struct in_addr *)&ctx->local_ip));

    return PT_OK;
}

static void posix_shutdown(PT_Context_Internal *ctx)
{
    int i;

    /* Close per-peer TCP connections */
    for (i = 0; i < ctx->max_peers; i++) {
        if (ctx->peers[i].in_use &&
            ctx->peers[i].platform_peer.tcp_fd >= 0) {
            close(ctx->peers[i].platform_peer.tcp_fd);
            ctx->peers[i].platform_peer.tcp_fd = -1;
        }
    }

    if (g_posix.discovery_fd >= 0) {
        close(g_posix.discovery_fd);
        g_posix.discovery_fd = -1;
    }
    if (g_posix.tcp_listen_fd >= 0) {
        close(g_posix.tcp_listen_fd);
        g_posix.tcp_listen_fd = -1;
    }
    if (g_posix.udp_msg_fd >= 0) {
        close(g_posix.udp_msg_fd);
        g_posix.udp_msg_fd = -1;
    }

    ctx->platform_state = NULL;
    (void)ctx;
}

static PT_Status posix_udp_broadcast(PT_Context_Internal *ctx,
                                     unsigned short port,
                                     const void *data, size_t len)
{
    struct sockaddr_in addr;
    ssize_t sent;
    int fd;

    (void)ctx;

    if (port == PT_DISCOVERY_PORT) {
        fd = g_posix.discovery_fd;
    } else {
        fd = g_posix.udp_msg_fd;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    sent = sendto(fd, data, len, 0,
                  (struct sockaddr *)&addr, sizeof(addr));
    return (sent >= 0) ? PT_OK : PT_ERR_SEND_FAILED;
}

static PT_Status posix_udp_send(PT_Context_Internal *ctx,
                                const PT_Peer_Internal *peer,
                                unsigned short port,
                                const void *data, size_t len)
{
    struct sockaddr_in addr;
    ssize_t sent;

    (void)ctx;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = peer->ip_addr;

    sent = sendto(g_posix.udp_msg_fd, data, len, 0,
                  (struct sockaddr *)&addr, sizeof(addr));
    return (sent >= 0) ? PT_OK : PT_ERR_SEND_FAILED;
}

static PT_Status posix_udp_listen(PT_Context_Internal *ctx,
                                  unsigned short port)
{
    /* Already bound in init, just mark as listening */
    (void)ctx;
    (void)port;
    return PT_OK;
}

static PT_Status posix_tcp_listen(PT_Context_Internal *ctx)
{
    (void)ctx;

    if (g_posix.listening) return PT_OK;

    if (listen(g_posix.tcp_listen_fd, 8) < 0) {
        CLOG_ERR("listen() failed: %s", strerror(errno));
        return PT_ERR_INIT;
    }

    g_posix.listening = 1;
    CLOG_INFO("Listening on TCP port %d", PT_TCP_PORT);
    return PT_OK;
}

static PT_Status posix_tcp_connect(PT_Context_Internal *ctx,
                                   PT_Peer_Internal *peer)
{
    int fd;
    struct sockaddr_in addr;
    int opt = 1;

    (void)ctx;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return PT_ERR_SEND_FAILED;

    make_nonblocking(fd);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PT_TCP_PORT);
    addr.sin_addr.s_addr = peer->ip_addr;

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            close(fd);
            return PT_ERR_SEND_FAILED;
        }
    }

    peer->platform_peer.tcp_fd = fd;
    return PT_OK;
}

static PT_Status posix_tcp_send(PT_Context_Internal *ctx,
                                PT_Peer_Internal *peer,
                                const void *data, size_t len)
{
    size_t total = 0;

    (void)ctx;

    if (peer->platform_peer.tcp_fd < 0) return PT_ERR_NOT_CONNECTED;

    while (total < len) {
        ssize_t sent = send(peer->platform_peer.tcp_fd,
                    (const char *)data + total,
                    len - total, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return PT_ERR_SEND_FAILED;
        }
        total += (size_t)sent;
    }

    /* Partial send corrupts TCP framing — report failure */
    return (total == len) ? PT_OK : PT_ERR_SEND_FAILED;
}

static void posix_tcp_disconnect(PT_Context_Internal *ctx,
                                 PT_Peer_Internal *peer)
{
    (void)ctx;

    if (peer->platform_peer.tcp_fd >= 0) {
        close(peer->platform_peer.tcp_fd);
        peer->platform_peer.tcp_fd = -1;
    }
}

static void posix_poll(PT_Context_Internal *ctx)
{
    fd_set readfds, writefds;
    int maxfd = -1;
    int i, ret;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);

    /* Discovery socket */
    if (ctx->discovery_listening && g_posix.discovery_fd >= 0) {
        FD_SET(g_posix.discovery_fd, &readfds);
        if (g_posix.discovery_fd > maxfd) maxfd = g_posix.discovery_fd;
    }

    /* TCP listener */
    if (g_posix.listening && g_posix.tcp_listen_fd >= 0) {
        FD_SET(g_posix.tcp_listen_fd, &readfds);
        if (g_posix.tcp_listen_fd > maxfd) maxfd = g_posix.tcp_listen_fd;
    }

    /* UDP message socket */
    if (g_posix.udp_msg_fd >= 0) {
        FD_SET(g_posix.udp_msg_fd, &readfds);
        if (g_posix.udp_msg_fd > maxfd) maxfd = g_posix.udp_msg_fd;
    }

    /* Per-peer TCP fds */
    for (i = 0; i < ctx->max_peers; i++) {
        int fd = ctx->peers[i].platform_peer.tcp_fd;
        if (ctx->peers[i].in_use && fd >= 0) {
            if (ctx->peers[i].state == PT_PEER_CONNECTED) {
                FD_SET(fd, &readfds);
                if (fd > maxfd) maxfd = fd;
            } else if (ctx->peers[i].connect_start > 0) {
                /* Connecting -- watch for writability */
                FD_SET(fd, &writefds);
                if (fd > maxfd) maxfd = fd;
            }
        }
    }

    if (maxfd < 0) return;

    tv.tv_sec = 0;
    tv.tv_usec = 0;
    ret = select(maxfd + 1, &readfds, &writefds, NULL, &tv);
    if (ret <= 0) return;

    /* Check discovery socket -- drain all queued datagrams */
    if (g_posix.discovery_fd >= 0 &&
        FD_ISSET(g_posix.discovery_fd, &readfds)) {
        for (;;) {
            unsigned char buf[PT_DISCOVERY_MAX + 16];
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            ssize_t n;

            n = recvfrom(g_posix.discovery_fd, buf, sizeof(buf), 0,
                         (struct sockaddr *)&from, &fromlen);
            if (n <= 0) break;
            pt_discovery_receive(ctx, buf, (size_t)n,
                                 from.sin_addr.s_addr);
        }
    }

    /* Check TCP listener for new connections */
    if (g_posix.listening && g_posix.tcp_listen_fd >= 0 &&
        FD_ISSET(g_posix.tcp_listen_fd, &readfds)) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int newfd;
        int opt = 1;

        newfd = accept(g_posix.tcp_listen_fd,
                       (struct sockaddr *)&from, &fromlen);
        if (newfd >= 0) {
            PT_PlatformPeer ppeer;
            const PT_Peer_Internal *accepted;

            make_nonblocking(newfd);
            setsockopt(newfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

            memset(&ppeer, 0, sizeof(ppeer));
            ppeer.tcp_fd = newfd;

            pt_handle_incoming_connection(ctx, from.sin_addr.s_addr,
                                          &ppeer);

            /* Verify the fd was adopted by a peer; if not, close it
               to avoid fd leak when connection is rejected (T069). */
            accepted = pt_find_peer_by_ip(ctx, from.sin_addr.s_addr);
            if (!accepted ||
                accepted->platform_peer.tcp_fd != newfd) {
                close(newfd);
            }
        }
    }

    /* Check UDP message socket -- drain all queued datagrams */
    if (g_posix.udp_msg_fd >= 0 &&
        FD_ISSET(g_posix.udp_msg_fd, &readfds)) {
        for (;;) {
            unsigned char buf[2048];
            struct sockaddr_in from;
            socklen_t fromlen = sizeof(from);
            ssize_t n;

            n = recvfrom(g_posix.udp_msg_fd, buf, sizeof(buf), 0,
                         (struct sockaddr *)&from, &fromlen);
            if (n <= 0) break;
            pt_messaging_process_udp_data(ctx, buf, (size_t)n,
                                          from.sin_addr.s_addr);
        }
    }

    /* Check per-peer TCP fds */
    for (i = 0; i < ctx->max_peers; i++) {
        int fd = ctx->peers[i].platform_peer.tcp_fd;
        if (!ctx->peers[i].in_use || fd < 0) continue;

        /* Check connecting peers for writability (connect complete) */
        if (ctx->peers[i].connect_start > 0 &&
            FD_ISSET(fd, &writefds)) {
            int err = 0;
            socklen_t errlen = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);

            if (err == 0) {
                /* Connection succeeded */
                ctx->peers[i].connect_start = 0;
                ctx->peers[i].state = PT_PEER_CONNECTED;
                ctx->peers[i].last_tcp_activity = ctx->current_time;
                ctx->peers[i].last_tcp_send = ctx->current_time;
                CLOG_INFO("TCP connected to %s",
                          ctx->peers[i].name);
                if (ctx->callbacks.on_connected) {
                    ctx->callbacks.on_connected(
                        (PT_Peer *)&ctx->peers[i],
                        ctx->callbacks.on_connected_data);
                }
            } else {
                /* Connection failed */
                ctx->peers[i].connect_start = 0;
                close(fd);
                ctx->peers[i].platform_peer.tcp_fd = -1;
                ctx->peers[i].state = PT_PEER_DISCONNECTED;
                pt_fire_error(ctx, &ctx->peers[i],
                              PT_ERR_SEND_FAILED,
                              "TCP connect failed");
            }
        }

        /* Check connected peers for readable data */
        if (ctx->peers[i].state == PT_PEER_CONNECTED &&
            FD_ISSET(fd, &readfds)) {
            size_t space;

            space = ctx->peers[i].tcp_recv_size -
                    ctx->peers[i].tcp_recv_len;
            if (space > 0) {
                ssize_t n = recv(fd,
                         (char *)ctx->peers[i].tcp_recv_buf +
                             ctx->peers[i].tcp_recv_len,
                         space, 0);
                if (n > 0) {
                    ctx->peers[i].tcp_recv_len += (size_t)n;
                    ctx->peers[i].last_tcp_activity = ctx->current_time;
                    pt_messaging_process_tcp_data(ctx, &ctx->peers[i]);
                } else if (n == 0) {
                    /* Peer closed connection */
                    pt_handle_peer_disconnect(ctx, &ctx->peers[i],
                                              PT_DISCONNECT_ERROR);
                } else {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        pt_handle_peer_disconnect(ctx, &ctx->peers[i],
                                                  PT_DISCONNECT_ERROR);
                    }
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Ops table                                                           */
/* ------------------------------------------------------------------ */

static PT_PlatformOps posix_ops = {
    posix_init,
    posix_shutdown,
    posix_udp_broadcast,
    posix_udp_send,
    posix_udp_listen,
    posix_tcp_listen,
    posix_tcp_connect,
    posix_tcp_send,
    posix_tcp_disconnect,
    posix_poll
};

PT_PlatformOps *posix_get_ops(void)
{
    return &posix_ops;
}

#endif /* PT_PLATFORM_POSIX */
