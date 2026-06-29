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

    /* next_event() iterator state (one select() per poll round, then
       one lifecycle event handed back per call). */
    fd_set ev_readfds;
    fd_set ev_writefds;
    int    ev_have_select;  /* select() done for this round? */
    int    ev_cursor;       /* next peer index to examine */
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

/* Build fd sets, run one non-blocking select(), and handle everything
   that is NOT a per-peer lifecycle transition inline: discovery and UDP
   datagrams (handed straight to core helpers, no duplicated state) and
   inbound accepts (core's pt_handle_incoming_connection owns that). The
   per-peer connect/data/close transitions are left for the iterator. */
static void posix_select_round(PT_Context_Internal *ctx)
{
    int maxfd = -1;
    int i, ret;
    struct timeval tv;

    FD_ZERO(&g_posix.ev_readfds);
    FD_ZERO(&g_posix.ev_writefds);
    g_posix.ev_cursor = 0;

    if (ctx->discovery_listening && g_posix.discovery_fd >= 0) {
        FD_SET(g_posix.discovery_fd, &g_posix.ev_readfds);
        if (g_posix.discovery_fd > maxfd) maxfd = g_posix.discovery_fd;
    }
    if (g_posix.listening && g_posix.tcp_listen_fd >= 0) {
        FD_SET(g_posix.tcp_listen_fd, &g_posix.ev_readfds);
        if (g_posix.tcp_listen_fd > maxfd) maxfd = g_posix.tcp_listen_fd;
    }
    if (g_posix.udp_msg_fd >= 0) {
        FD_SET(g_posix.udp_msg_fd, &g_posix.ev_readfds);
        if (g_posix.udp_msg_fd > maxfd) maxfd = g_posix.udp_msg_fd;
    }
    for (i = 0; i < ctx->max_peers; i++) {
        int fd = ctx->peers[i].platform_peer.tcp_fd;
        if (ctx->peers[i].in_use && fd >= 0) {
            if (ctx->peers[i].state == PT_PEER_CONNECTED) {
                FD_SET(fd, &g_posix.ev_readfds);
                if (fd > maxfd) maxfd = fd;
            } else if (ctx->peers[i].connect_start > 0) {
                FD_SET(fd, &g_posix.ev_writefds);
                if (fd > maxfd) maxfd = fd;
            }
        }
    }

    if (maxfd < 0) return;

    tv.tv_sec = 0;
    tv.tv_usec = 0;
    ret = select(maxfd + 1, &g_posix.ev_readfds, &g_posix.ev_writefds,
                 NULL, &tv);
    if (ret <= 0) {
        FD_ZERO(&g_posix.ev_readfds);
        FD_ZERO(&g_posix.ev_writefds);
        return;
    }

    /* Discovery datagrams -- drain all */
    if (g_posix.discovery_fd >= 0 &&
        FD_ISSET(g_posix.discovery_fd, &g_posix.ev_readfds)) {
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

    /* Inbound TCP connection */
    if (g_posix.listening && g_posix.tcp_listen_fd >= 0 &&
        FD_ISSET(g_posix.tcp_listen_fd, &g_posix.ev_readfds)) {
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

    /* UDP message datagrams -- drain all */
    if (g_posix.udp_msg_fd >= 0 &&
        FD_ISSET(g_posix.udp_msg_fd, &g_posix.ev_readfds)) {
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
}

/* Event iterator: on the first call of a round, run the select() and
   handle non-lifecycle I/O; then return one per-peer lifecycle event
   per call (CONNECTED / DATA / CLOSED).  The recv() that fills the
   peer buffer stays here (it is irreducibly platform); the resulting
   state transition is core's job via pt_apply_platform_event(). */
static int posix_next_event(PT_Context_Internal *ctx, PT_Event *out)
{
    int i;

    if (!g_posix.ev_have_select) {
        posix_select_round(ctx);
        g_posix.ev_have_select = 1;
    }

    for (i = g_posix.ev_cursor; i < ctx->max_peers; i++) {
        int fd = ctx->peers[i].platform_peer.tcp_fd;
        if (!ctx->peers[i].in_use || fd < 0) continue;

        /* Outgoing connect completed (writable)? */
        if (ctx->peers[i].connect_start > 0 &&
            FD_ISSET(fd, &g_posix.ev_writefds)) {
            int err = 0;
            socklen_t errlen = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);

            g_posix.ev_cursor = i + 1;
            out->type = PT_EVT_CONNECTED;
            out->peer = &ctx->peers[i];
            out->ok = (err == 0);
            return 1;
        }

        /* Readable data on a connected peer? */
        if (ctx->peers[i].state == PT_PEER_CONNECTED &&
            FD_ISSET(fd, &g_posix.ev_readfds)) {
            size_t space = ctx->peers[i].tcp_recv_size -
                           ctx->peers[i].tcp_recv_len;
            if (space > 0) {
                ssize_t n = recv(fd,
                         (char *)ctx->peers[i].tcp_recv_buf +
                             ctx->peers[i].tcp_recv_len,
                         space, 0);
                if (n > 0) {
                    ctx->peers[i].tcp_recv_len += (size_t)n;
                    g_posix.ev_cursor = i + 1;
                    out->type = PT_EVT_DATA;
                    out->peer = &ctx->peers[i];
                    return 1;
                } else if (n == 0) {
                    g_posix.ev_cursor = i + 1;
                    out->type = PT_EVT_CLOSED;
                    out->peer = &ctx->peers[i];
                    return 1;
                } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    g_posix.ev_cursor = i + 1;
                    out->type = PT_EVT_CLOSED;
                    out->peer = &ctx->peers[i];
                    return 1;
                }
                /* EAGAIN: spurious readiness, no event */
            }
        }
    }

    /* Round drained */
    g_posix.ev_have_select = 0;
    out->type = PT_EVT_NONE;
    out->peer = NULL;
    return 0;
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
    NULL,   /* poll: POSIX is event-driven via next_event */
    NULL,   /* cleanup_streams: POSIX sockets don't need explicit cleanup */
    posix_next_event
};

PT_PlatformOps *posix_get_ops(void)
{
    return &posix_ops;
}

#endif /* PT_PLATFORM_POSIX */
