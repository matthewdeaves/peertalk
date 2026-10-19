/*
 * test_seam.c -- White-box unit tests for the event-driven platform seam.
 *
 * The platform backends (POSIX / MacTCP / OT) no longer own connection
 * state: they emit PT_Events and core applies every transition in one
 * place -- pt_complete_connect(), pt_drain_disconnect(), and the PT_Poll
 * drain loop.  These tests drive that core logic directly with synthetic
 * events and a mock backend.  No sockets, no discovery, no real hardware.
 *
 * They cash in the testability the seam was built for, and lock down the
 * "goodbye is never dropped on close" behaviour that otherwise can only be
 * verified by running on a real Mac.  Because the transitions under test
 * are platform-independent, a PASS here covers all three backends.
 *
 * POSIX-only (C11), white-box: includes the internal header to reach the
 * core entry points and the context/peer structs.
 */

#include "peertalk.h"
#include "clog.h"
#include "../src/core/pt_internal.h"

#include <stdio.h>
#include <stdlib.h>   /* malloc, free */
#include <string.h>
#include <unistd.h>   /* dup, close -- real fd for the no-room test */
#include <arpa/inet.h>   /* htonl */
#include <sys/socket.h>  /* socketpair -- real stream for the POSIX send test */
#include <errno.h>
#include <sys/select.h>  /* FD_SETSIZE */
#include <sys/resource.h> /* setrlimit -- allow fds beyond FD_SETSIZE */
#include <fcntl.h>    /* fcntl(F_GETFD) -- probe whether an fd is still open */

/* ------------------------------------------------------------------ */
/* Tiny assertion harness                                              */
/* ------------------------------------------------------------------ */

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg) do {                                       \
        g_checks++;                                                 \
        if (!(cond)) {                                              \
            g_failures++;                                           \
            printf("  FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__); \
        } else {                                                    \
            printf("  ok:   %s\n", (msg));                          \
        }                                                           \
    } while (0)

/* ------------------------------------------------------------------ */
/* Callback recorders                                                  */
/* ------------------------------------------------------------------ */

#define MSG_TYPE 10

static int                 g_connected;
static int                 g_disconnected;
static int                 g_messages;
static int                 g_disconnect_calls;   /* backend tcp_disconnect */
static int                 g_connect_calls;       /* backend tcp_connect */
static int                 g_errors;              /* on_error fired count */
static int                 g_tcp_sends;           /* mock tcp_send call count */
static int                 g_tcp_send_fail_at;    /* 1-based call to fail at; 0=never */
static size_t              g_tcp_last_frame_len;  /* len passed to last tcp_send */
static unsigned char       g_tcp_last_frame[8];   /* first bytes (header) of it */
static int                 g_discovered;          /* on_peer_discovered count */
static int                 g_lost;                /* on_peer_lost count */
static int                 g_udp_broadcasts;      /* mock udp_broadcast calls */
static size_t              g_udp_last_len;        /* len of the last one */
static PT_Status           g_last_error;
static PT_DisconnectReason g_last_reason;
static unsigned char       g_last_msg[64];
static size_t              g_last_msg_len;
static char                g_order[64];

static void rec_order(char c)
{
    size_t n = strlen(g_order);
    if (n + 1 < sizeof(g_order)) {
        g_order[n] = c;
        g_order[n + 1] = '\0';
    }
}

static void on_conn(PT_Peer *peer, void *ud)
{
    (void)peer; (void)ud;
    g_connected++;
    rec_order('C');
}

static void on_disc(PT_Peer *peer, PT_DisconnectReason reason, void *ud)
{
    (void)peer; (void)ud;
    g_disconnected++;
    g_last_reason = reason;
    rec_order('X');
}

static void on_msg(PT_Peer *peer, const void *data, size_t len, void *ud)
{
    (void)peer; (void)ud;
    g_messages++;
    g_last_msg_len = len;
    if (len <= sizeof(g_last_msg)) {
        memcpy(g_last_msg, data, len);
    }
    rec_order('M');
}

static void on_err(PT_Peer *peer, PT_Status error, const char *desc, void *ud)
{
    (void)peer; (void)desc; (void)ud;
    g_errors++;
    g_last_error = error;
}

static void on_disc_found(PT_Peer *peer, void *ud)
{
    (void)peer; (void)ud;
    g_discovered++;
}

static void on_lost(PT_Peer *peer, void *ud)
{
    (void)peer; (void)ud;
    g_lost++;
}

static void reset_recorders(void)
{
    g_connected = 0;
    g_disconnected = 0;
    g_messages = 0;
    g_disconnect_calls = 0;
    g_connect_calls = 0;
    g_errors = 0;
    g_tcp_sends = 0;
    g_tcp_send_fail_at = 0;
    g_tcp_last_frame_len = 0;
    memset(g_tcp_last_frame, 0, sizeof(g_tcp_last_frame));
    g_discovered = 0;
    g_lost = 0;
    g_udp_broadcasts = 0;
    g_udp_last_len = 0;
    g_last_error = PT_OK;
    g_last_reason = (PT_DisconnectReason)-1;
    g_last_msg_len = 0;
    memset(g_last_msg, 0, sizeof(g_last_msg));
    g_order[0] = '\0';
}

/* ------------------------------------------------------------------ */
/* Peer / frame helpers                                                */
/* ------------------------------------------------------------------ */

static PT_Context_Internal *g_ctx;

/* Reuse the pre-allocated slot 0 (keeps its buffer pointers), reset the
   lifecycle fields to a known starting state.  tcp_fd = -1 makes the
   POSIX tcp_disconnect a harmless no-op. */
static PT_Peer_Internal *fresh_peer(PT_PeerState state)
{
    PT_Peer_Internal *p = &g_ctx->peers[0];

    p->in_use = 1;
    p->state = state;
    p->connect_start = (state == PT_PEER_CONNECTED) ? 0 : 1;
    p->tcp_recv_len = 0;
    p->reassembly_total = 0;
    p->reassembly_received = 0;
    p->platform_peer.tcp_fd = -1;
    strcpy(p->name, "mock");
    return p;
}

/* Append a raw byte string to a peer's TCP receive buffer, exactly as a
   backend's next_event would before reporting DATA/CLOSED. */
static void stage_bytes(PT_Peer_Internal *p, const unsigned char *b, size_t n)
{
    memcpy(p->tcp_recv_buf + p->tcp_recv_len, b, n);
    p->tcp_recv_len += n;
}

/* Return every peer slot to the free/idle state (in_use=0) so a test that
   relies on pt_alloc_peer()/discovery/rank starts from an empty roster.
   Buffer pointers are preserved (pt_alloc_peer never touches them). */
static void reset_all_peers(void)
{
    int i;
    for (i = 0; i < g_ctx->max_peers; i++) {
        PT_Peer_Internal *p = &g_ctx->peers[i];
        p->in_use = 0;
        p->state = PT_PEER_DISCONNECTED;
        p->ip_addr = 0;
        p->name[0] = '\0';
        p->last_seen = 0;
        p->connect_start = 0;
        p->tcp_recv_len = 0;
        p->reassembly_total = 0;
        p->reassembly_received = 0;
        p->platform_peer.tcp_fd = -1;
    }
    g_ctx->peer_count = 0;
}

/* Build a v2 discovery packet (6-byte header + name + NUL) into buf and
   return its length.  flags is PT_DISCOVERY_FLAG_ANNOUNCE / _LEAVE. */
static size_t build_discovery(unsigned char *buf, unsigned char version,
                              unsigned char flags, const char *name)
{
    size_t namelen = strlen(name);
    buf[0] = PT_MAGIC_0;
    buf[1] = PT_MAGIC_1;
    buf[2] = PT_MAGIC_2;
    buf[3] = PT_MAGIC_3;
    buf[4] = version;
    buf[5] = flags;
    memcpy(buf + PT_DISCOVERY_HEADER, name, namelen);
    buf[PT_DISCOVERY_HEADER + namelen] = '\0';
    return PT_DISCOVERY_HEADER + namelen + 1;
}

static const unsigned char GOODBYE[4] = { 0x00, 0x00, PT_MSG_TYPE_GOODBYE, 0x00 };
/* MSG_TYPE frame, 2-byte payload "hi" */
static const unsigned char MSG_HI[6] = { 0x00, 0x02, MSG_TYPE, 0x00, 'h', 'i' };

/* ------------------------------------------------------------------ */
/* Direct transition tests (no PT_Poll)                                */
/* ------------------------------------------------------------------ */

static void test_connect_success(void)
{
    PT_Peer_Internal *p = fresh_peer(PT_PEER_DISCONNECTED);
    printf("[connect_success]\n");
    p->connect_start = 1; /* mid-connect */
    reset_recorders();

    pt_complete_connect(g_ctx, p, 1);

    CHECK(p->state == PT_PEER_CONNECTED, "peer state -> CONNECTED");
    CHECK(g_connected == 1, "on_connected fired once");
    CHECK(p->connect_start == 0, "connect_start cleared");
    CHECK(g_disconnected == 0, "no disconnect on success");
}

static void test_connect_failure(void)
{
    PT_Peer_Internal *p = fresh_peer(PT_PEER_DISCONNECTED);
    printf("[connect_failure]\n");
    p->connect_start = 1;
    reset_recorders();

    pt_complete_connect(g_ctx, p, 0);

    CHECK(p->state == PT_PEER_DISCONNECTED, "peer state -> DISCONNECTED");
    CHECK(g_connected == 0, "on_connected NOT fired on failure");
    CHECK(p->connect_start == 0, "connect_start cleared");
}

static void test_close_clean_goodbye(void)
{
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[close_clean_goodbye]\n");
    stage_bytes(p, GOODBYE, sizeof(GOODBYE));
    reset_recorders();

    pt_drain_disconnect(g_ctx, p);

    CHECK(g_disconnected == 1, "on_disconnected fired once");
    CHECK(g_last_reason == PT_QUIT, "reason is QUIT (goodbye parsed)");
    CHECK(p->state == PT_PEER_DISCONNECTED, "peer state -> DISCONNECTED");
}

static void test_close_abrupt(void)
{
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[close_abrupt]\n");
    /* no buffered goodbye */
    reset_recorders();

    pt_drain_disconnect(g_ctx, p);

    CHECK(g_disconnected == 1, "on_disconnected fired once");
    CHECK(g_last_reason == PT_DISCONNECT_ERROR,
          "reason is ERROR (no goodbye)");
    CHECK(p->state == PT_PEER_DISCONNECTED, "peer state -> DISCONNECTED");
}

/* The headline property: a message buffered together with the goodbye in
   the final read must BOTH be delivered -- message callback, then a clean
   QUIT.  This is what collapsing DATA+close into one CLOSED guarantees. */
static void test_close_message_then_goodbye(void)
{
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[close_message_then_goodbye]\n");
    stage_bytes(p, MSG_HI, sizeof(MSG_HI));
    stage_bytes(p, GOODBYE, sizeof(GOODBYE));
    reset_recorders();

    pt_drain_disconnect(g_ctx, p);

    CHECK(g_messages == 1, "buffered message delivered");
    CHECK(g_last_msg_len == 2 && memcmp(g_last_msg, "hi", 2) == 0,
          "message payload intact");
    CHECK(g_disconnected == 1, "on_disconnected fired once");
    CHECK(g_last_reason == PT_QUIT, "reason is QUIT");
    CHECK(strcmp(g_order, "MX") == 0, "order: message before disconnect");
}

/* ------------------------------------------------------------------ */
/* Mock backend -- exercises the PT_Poll next_event() drain loop        */
/* ------------------------------------------------------------------ */

typedef struct {
    PT_EventType        type;
    PT_Peer_Internal   *peer;
    int                 ok;
    const unsigned char *bytes;  /* staged into recv buf before the event */
    size_t              len;
} ScriptStep;

static ScriptStep   g_script[8];
static int          g_script_n;
static int          g_script_cursor;

static int mock_next_event(PT_Context_Internal *ctx, PT_Event *out)
{
    ScriptStep *s;
    (void)ctx;

    if (g_script_cursor >= g_script_n) {
        out->type = PT_EVT_NONE;
        out->peer = NULL;
        return 0;
    }

    s = &g_script[g_script_cursor++];
    if (s->bytes && s->len > 0) {
        stage_bytes(s->peer, s->bytes, s->len);
    }
    out->type = s->type;
    out->peer = s->peer;
    out->ok = s->ok;
    return 1;
}

static PT_Status mock_udp_broadcast(PT_Context_Internal *c, unsigned short p,
                                    const void *d, size_t l)
{ (void)c; (void)p; (void)d; g_udp_broadcasts++; g_udp_last_len = l; return PT_OK; }

static PT_Status mock_udp_send(PT_Context_Internal *c,
                               const PT_Peer_Internal *pe,
                               unsigned short p, const void *d, size_t l)
{ (void)c; (void)pe; (void)p; (void)d; (void)l; return PT_OK; }

/* Counts calls and captures each frame's header so tests can assert what
   was sent (keepalive frames, chunk seq/total).  Set g_tcp_send_fail_at to
   a 1-based call index to make that call (and any after it) fail -- used to
   test send-error propagation and chunked-send abort. */
static PT_Status mock_tcp_send(PT_Context_Internal *c, PT_Peer_Internal *pe,
                               const void *d, size_t l)
{
    size_t n = l < sizeof(g_tcp_last_frame) ? l : sizeof(g_tcp_last_frame);
    (void)c; (void)pe;
    g_tcp_sends++;
    g_tcp_last_frame_len = l;
    memcpy(g_tcp_last_frame, d, n);
    if (g_tcp_send_fail_at != 0 && g_tcp_sends >= g_tcp_send_fail_at)
        return PT_ERR_SEND_FAILED;
    return PT_OK;
}

static void mock_tcp_disconnect(PT_Context_Internal *c, PT_Peer_Internal *pe)
{ (void)c; g_disconnect_calls++; pe->platform_peer.tcp_fd = -1; }

/* Records auto-mesh / manual dials.  Returns PT_OK so PT_Connect sets
   connect_start (marking a dial in flight), exactly like a real backend.
   tcp_fd stands in for the live transport the dial opens. */
static PT_Status mock_tcp_connect(PT_Context_Internal *c, PT_Peer_Internal *pe)
{ (void)c; pe->platform_peer.tcp_fd = 78; g_connect_calls++; return PT_OK; }

/* Install a fully-wired mock backend (all no-ops) and return the ops that
   were in place, so the caller can restore them. */
static PT_PlatformOps g_mock_ops;
static PT_PlatformOps *install_mock_ops(void)
{
    PT_PlatformOps *saved = g_ctx->platform_ops;
    memset(&g_mock_ops, 0, sizeof(g_mock_ops));
    g_mock_ops.udp_broadcast  = mock_udp_broadcast;
    g_mock_ops.udp_send       = mock_udp_send;
    g_mock_ops.tcp_send       = mock_tcp_send;
    g_mock_ops.tcp_connect    = mock_tcp_connect;
    g_mock_ops.tcp_disconnect = mock_tcp_disconnect;
    g_mock_ops.next_event     = mock_next_event;
    g_ctx->platform_ops = &g_mock_ops;
    return saved;
}

static void test_poll_drain_order(void)
{
    PT_PlatformOps mock;
    PT_PlatformOps *saved;
    PT_Peer_Internal *p;

    printf("[poll_drain_order]\n");

    /* Mock ops: real init/shutdown unused during poll; everything the
       drain loop can touch is a safe no-op. */
    memset(&mock, 0, sizeof(mock));
    mock.udp_broadcast  = mock_udp_broadcast;
    mock.udp_send       = mock_udp_send;
    mock.tcp_send       = mock_tcp_send;
    mock.tcp_disconnect = mock_tcp_disconnect;
    mock.next_event     = mock_next_event;

    p = fresh_peer(PT_PEER_DISCONNECTED);
    p->connect_start = 1;
    reset_recorders();

    g_script_n = 0;
    g_script_cursor = 0;
    g_script[g_script_n].type = PT_EVT_CONNECTED;
    g_script[g_script_n].peer = p;
    g_script[g_script_n].ok = 1;
    g_script_n++;
    g_script[g_script_n].type = PT_EVT_DATA;
    g_script[g_script_n].peer = p;
    g_script[g_script_n].bytes = MSG_HI;
    g_script[g_script_n].len = sizeof(MSG_HI);
    g_script_n++;
    g_script[g_script_n].type = PT_EVT_CLOSED;
    g_script[g_script_n].peer = p;
    g_script[g_script_n].bytes = GOODBYE;
    g_script[g_script_n].len = sizeof(GOODBYE);
    g_script_n++;

    saved = g_ctx->platform_ops;
    g_ctx->platform_ops = &mock;
    PT_Poll((PT_Context *)g_ctx);
    g_ctx->platform_ops = saved;

    CHECK(g_connected == 1, "drain: connected once");
    CHECK(g_messages == 1, "drain: message delivered");
    CHECK(g_disconnected == 1, "drain: disconnected once");
    CHECK(g_last_reason == PT_QUIT, "drain: reason QUIT");
    CHECK(strcmp(g_order, "CMX") == 0,
          "drain order: connect, message, disconnect");
}

/* ------------------------------------------------------------------ */
/* Simultaneous-connect tiebreaker (pt_handle_incoming_connection)      */
/*                                                                      */
/* Deterministic rule: the connection dialed by the LOWER-IP peer wins. */
/* The classic tiebreaker only fires while our outbound is still        */
/* pending; these cover the race where our own outbound COMPLETED first */
/* (peer already CONNECTED) and the remote's incoming then arrives --   */
/* the exact window that looped the Mac SE ~16x in test_multi.          */
/* ------------------------------------------------------------------ */

/* We are the HIGHER-IP peer, already connected via our own outbound.
   The remote (lower IP) will keep its outbound and drop ours, so we
   must swap to the incoming -- silently, since the app already saw a
   connect.  Blindly rejecting here is what caused both sides to hold a
   different connection and both abort ~200ms later.

   This is the ONLY tiebreak test that FAILS on the pre-fix code (which
   returned early, rejecting the incoming).  The other two land in the
   reject branch on both old and new code -- they are regression guards
   for behaviour the fix must preserve, not validators of the fix. */
static void test_tiebreak_swap_outbound_for_incoming(void)
{
    PT_PlatformOps *saved;
    PT_PlatformPeer incoming;
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);

    printf("[tiebreak_swap_outbound_for_incoming]\n");
    p->ip_addr = 0x0A000001UL;          /* remote = lower IP */
    p->inbound = 0;                     /* connected via our own dial */
    p->platform_peer.tcp_fd = 7;        /* our outbound socket */
    g_ctx->local_ip = 0x0A000002UL;     /* us = higher IP */
    reset_recorders();

    saved = install_mock_ops();
    memset(&incoming, 0, sizeof(incoming));
    incoming.tcp_fd = 42;               /* accepted incoming socket */
    pt_handle_incoming_connection(g_ctx, p->ip_addr, &incoming);
    g_ctx->platform_ops = saved;

    CHECK(g_disconnect_calls == 1, "old outbound transport closed");
    CHECK(p->platform_peer.tcp_fd == 42, "incoming socket adopted");
    CHECK(p->inbound == 1, "peer now marked inbound");
    CHECK(p->state == PT_PEER_CONNECTED, "still CONNECTED (no flap)");
    CHECK(g_connected == 0, "on_connected NOT re-fired");
    CHECK(g_disconnected == 0, "on_disconnected NOT fired");

    /* Fabricated fds are not real sockets -- clear so PT_Shutdown's
       disconnect sweep does not close() an unrelated descriptor. */
    p->platform_peer.tcp_fd = -1;
    p->state = PT_PEER_DISCONNECTED;
}

/* We are the LOWER-IP peer, connected via our own outbound.  We keep it
   and reject the higher peer's incoming (which they will abandon). */
static void test_tiebreak_lower_keeps_outbound(void)
{
    PT_PlatformOps *saved;
    PT_PlatformPeer incoming;
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);

    printf("[tiebreak_lower_keeps_outbound]\n");
    p->ip_addr = 0x0A000002UL;          /* remote = higher IP */
    p->inbound = 0;                     /* connected via our own dial */
    p->platform_peer.tcp_fd = 7;
    g_ctx->local_ip = 0x0A000001UL;     /* us = lower IP */
    reset_recorders();

    saved = install_mock_ops();
    memset(&incoming, 0, sizeof(incoming));
    incoming.tcp_fd = 42;
    pt_handle_incoming_connection(g_ctx, p->ip_addr, &incoming);
    g_ctx->platform_ops = saved;

    CHECK(g_disconnect_calls == 0, "our outbound left intact");
    CHECK(p->platform_peer.tcp_fd == 7, "incoming rejected, fd unchanged");
    CHECK(p->inbound == 0, "still outbound");
    CHECK(p->state == PT_PEER_CONNECTED, "still CONNECTED");

    p->platform_peer.tcp_fd = -1;
    p->state = PT_PEER_DISCONNECTED;
}

/* We already hold the AGREED inbound connection.  A further incoming from
   the same peer is a true duplicate and must be rejected even though we
   are the higher IP -- no swap, no flap. */
static void test_tiebreak_true_duplicate_rejected(void)
{
    PT_PlatformOps *saved;
    PT_PlatformPeer incoming;
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);

    printf("[tiebreak_true_duplicate_rejected]\n");
    p->ip_addr = 0x0A000001UL;          /* remote = lower IP */
    p->inbound = 1;                     /* already the accepted connection */
    p->platform_peer.tcp_fd = 7;
    g_ctx->local_ip = 0x0A000002UL;     /* us = higher IP */
    reset_recorders();

    saved = install_mock_ops();
    memset(&incoming, 0, sizeof(incoming));
    incoming.tcp_fd = 42;
    pt_handle_incoming_connection(g_ctx, p->ip_addr, &incoming);
    g_ctx->platform_ops = saved;

    CHECK(g_disconnect_calls == 0, "agreed connection left intact");
    CHECK(p->platform_peer.tcp_fd == 7, "duplicate rejected, fd unchanged");
    CHECK(p->state == PT_PEER_CONNECTED, "still CONNECTED");
    CHECK(g_disconnected == 0, "no disconnect on duplicate reject");

    p->platform_peer.tcp_fd = -1;
    p->state = PT_PEER_DISCONNECTED;
}

/* ------------------------------------------------------------------ */
/* No-room incoming connection (pt_handle_incoming_connection)          */
/*                                                                      */
/* When every peer slot is full, an unknown peer's incoming transport   */
/* cannot be adopted.  Core must fire PT_ERR_NO_ROOM and return WITHOUT  */
/* closing the transport -- the platform layer that accepted it owns the */
/* teardown (POSIX's accept guard closes the fd once when it sees no     */
/* peer took it).  The old code close()d the fd here too, so the fd was  */
/* closed twice: a latent double-close / fd-reuse hazard.               */
/*                                                                      */
/* This FAILS on the pre-fix code: we hand core a REAL fd (dup of        */
/* stdout); the old core would close() it, so the post-call F_GETFD      */
/* probe would report it gone.  The fix leaves it open. */
static void test_no_room_leaves_fd_to_platform(void)
{
    PT_PlatformOps *saved;
    PT_PlatformPeer incoming;
    int realfd;
    int i;

    printf("[no_room_leaves_fd_to_platform]\n");

    realfd = dup(1);   /* a genuine open descriptor core must not touch */
    if (realfd < 0) {
        CHECK(0, "dup(1) for real-fd probe");
        return;
    }

    /* Fill every slot so pt_alloc_peer() fails for an unknown peer. */
    for (i = 0; i < g_ctx->max_peers; i++) {
        g_ctx->peers[i].in_use = 1;
        g_ctx->peers[i].ip_addr = 0;   /* none match the probe IP below */
    }
    reset_recorders();

    saved = install_mock_ops();
    memset(&incoming, 0, sizeof(incoming));
    incoming.tcp_fd = realfd;
    /* 0x0A0000FE matches no slot (all ip_addr == 0) -> unknown peer path. */
    pt_handle_incoming_connection(g_ctx, 0x0A0000FEUL, &incoming);
    g_ctx->platform_ops = saved;

    CHECK(g_errors == 1, "PT_ERR_NO_ROOM error fired");
    CHECK(g_last_error == PT_ERR_NO_ROOM, "error code is NO_ROOM");
    CHECK(g_disconnect_calls == 0, "core did not call tcp_disconnect");
    CHECK(fcntl(realfd, F_GETFD) != -1,
          "core left the fd open (no double-close)");

    close(realfd);
    /* Release the slots we commandeered so later state stays clean. */
    for (i = 0; i < g_ctx->max_peers; i++) {
        g_ctx->peers[i].in_use = 0;
        g_ctx->peers[i].platform_peer.tcp_fd = -1;
        g_ctx->peers[i].state = PT_PEER_DISCONNECTED;
    }
}

/* ------------------------------------------------------------------ */
/* Auto-mesh dial sweep (pt_mesh_dial_sweep)                            */
/*                                                                      */
/* The topology layer: when enabled, keep a dial open to every          */
/* discovered peer THIS node is the designated initiator for            */
/* (local_ip < peer ip), throttled by a retry timer, self-healing after */
/* a drop, and never dialing a pair from both sides.                    */
/* ------------------------------------------------------------------ */

static void test_auto_mesh_sweep(void)
{
    PT_PlatformOps *saved;
    PT_Peer_Internal *hi, *lo;

    printf("[auto_mesh_sweep]\n");
    reset_all_peers();
    reset_recorders();
    saved = install_mock_ops();

    g_ctx->local_ip = 0x0A000002UL;             /* us */

    hi = &g_ctx->peers[0];                      /* higher IP -> we dial it  */
    hi->in_use = 1; hi->state = PT_PEER_DISCOVERED;
    hi->ip_addr = 0x0A000003UL; hi->connect_start = 0;
    strcpy(hi->name, "hi");

    lo = &g_ctx->peers[1];                      /* lower IP -> it dials us  */
    lo->in_use = 1; lo->state = PT_PEER_DISCOVERED;
    lo->ip_addr = 0x0A000001UL; lo->connect_start = 0;
    strcpy(lo->name, "lo");

    g_ctx->peer_count = 2;
    g_ctx->current_time = 100;

    /* Disabled: the sweep is a pure no-op regardless of dialable peers. */
    g_ctx->auto_mesh = 0;
    g_ctx->mesh_dial_timer = 0;
    pt_mesh_dial_sweep(g_ctx);
    CHECK(g_connect_calls == 0, "auto-mesh off: no dials");

    /* Enabled: dials only the higher-IP peer -- each pair from one side. */
    g_ctx->auto_mesh = 1;
    g_ctx->mesh_dial_timer = 0;
    pt_mesh_dial_sweep(g_ctx);
    CHECK(g_connect_calls == 1, "auto-mesh: exactly one dial this sweep");
    CHECK(hi->connect_start != 0, "higher-IP peer dialed (in flight)");
    CHECK(lo->connect_start == 0, "lower-IP peer not dialed (it dials us)");

    /* Retry timer: a sweep before the interval elapses does nothing. */
    pt_mesh_dial_sweep(g_ctx);
    CHECK(g_connect_calls == 1, "within retry interval: no extra dials");

    /* Even once the timer elapses, a dial still in flight is not repeated. */
    g_ctx->current_time += PT_MESH_RETRY_INTERVAL;
    pt_mesh_dial_sweep(g_ctx);
    CHECK(g_connect_calls == 1, "in-flight dial not repeated");

    /* The dial fails: peer back to DISCONNECTED, connect_start cleared.
       The next sweep after the interval re-dials it (self-heal). */
    hi->connect_start = 0;
    hi->state = PT_PEER_DISCONNECTED;
    g_ctx->current_time += PT_MESH_RETRY_INTERVAL;
    pt_mesh_dial_sweep(g_ctx);
    CHECK(g_connect_calls == 2, "dropped peer re-dialed on next sweep");

    /* A connected peer is left alone. */
    hi->state = PT_PEER_CONNECTED; hi->connect_start = 0;
    g_ctx->current_time += PT_MESH_RETRY_INTERVAL;
    pt_mesh_dial_sweep(g_ctx);
    CHECK(g_connect_calls == 2, "connected peer not re-dialed");

    g_ctx->platform_ops = saved;
    reset_all_peers();
    g_ctx->auto_mesh = 0;
    g_ctx->local_ip = 0;
}

/* ------------------------------------------------------------------ */
/* TCP frame parsing / reassembly (pt_messaging_process_tcp_data)       */
/*                                                                      */
/* Pure buffer-in -> callback-out logic, the core of the wire protocol. */
/* Each test stages raw bytes exactly as a backend would and asserts    */
/* the parser's observable effects (callbacks + how much it consumed).  */
/* ------------------------------------------------------------------ */

static void test_frame_single(void)
{
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[frame_single]\n");
    stage_bytes(p, MSG_HI, sizeof(MSG_HI));
    reset_recorders();

    pt_messaging_process_tcp_data(g_ctx, p);

    CHECK(g_messages == 1, "single frame delivered once");
    CHECK(g_last_msg_len == 2 && memcmp(g_last_msg, "hi", 2) == 0,
          "payload intact");
    CHECK(p->tcp_recv_len == 0, "buffer fully consumed");
}

/* A frame split across two reads must not be delivered until complete,
   and the partial bytes must survive in the buffer for the next read. */
static void test_frame_partial_then_rest(void)
{
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[frame_partial_then_rest]\n");
    reset_recorders();

    /* Header only (declares 2-byte payload, none present yet). */
    stage_bytes(p, MSG_HI, PT_TCP_HEADER_SIZE);
    pt_messaging_process_tcp_data(g_ctx, p);
    CHECK(g_messages == 0, "incomplete frame not delivered");
    CHECK(p->tcp_recv_len == PT_TCP_HEADER_SIZE, "partial bytes retained");

    /* The rest arrives. */
    stage_bytes(p, MSG_HI + PT_TCP_HEADER_SIZE,
                sizeof(MSG_HI) - PT_TCP_HEADER_SIZE);
    pt_messaging_process_tcp_data(g_ctx, p);
    CHECK(g_messages == 1, "frame delivered once complete");
    CHECK(g_last_msg_len == 2 && memcmp(g_last_msg, "hi", 2) == 0,
          "reassembled payload intact");
    CHECK(p->tcp_recv_len == 0, "buffer drained");
}

/* Two frames concatenated in one read: the while-loop must dispatch both. */
static void test_frame_two_back_to_back(void)
{
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[frame_two_back_to_back]\n");
    stage_bytes(p, MSG_HI, sizeof(MSG_HI));
    stage_bytes(p, MSG_HI, sizeof(MSG_HI));
    reset_recorders();

    pt_messaging_process_tcp_data(g_ctx, p);

    CHECK(g_messages == 2, "both frames delivered");
    CHECK(p->tcp_recv_len == 0, "buffer fully consumed");
}

/* A keepalive (type 254, zero payload) has no callback and must be
   consumed silently -- and consuming exactly 4 bytes is what lets the
   real frame right behind it still parse. */
static void test_frame_keepalive_consumed(void)
{
    static const unsigned char KA[4] =
        { 0x00, 0x00, PT_MSG_TYPE_KEEPALIVE, 0x00 };
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[frame_keepalive_consumed]\n");
    stage_bytes(p, KA, sizeof(KA));
    stage_bytes(p, MSG_HI, sizeof(MSG_HI));
    reset_recorders();

    pt_messaging_process_tcp_data(g_ctx, p);

    CHECK(g_messages == 1, "keepalive fired no callback; only real msg did");
    CHECK(g_last_msg_len == 2 && memcmp(g_last_msg, "hi", 2) == 0,
          "trailing frame parsed after keepalive consumed");
    CHECK(p->tcp_recv_len == 0, "buffer fully consumed");
}

/* Two-chunk reassembly: stride comes from chunk 0's payload length, the
   final size is (total-1)*stride + last_chunk.  One message callback with
   the concatenated payload. */
static void test_chunked_reassembly(void)
{
    /* chunk 0: len=4 type=10 flags=CHUNK seq=0 total=2 "AAAA" */
    static const unsigned char C0[12] = {
        0x00, 0x04, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x00, 0x00, 0x02,
        'A', 'A', 'A', 'A' };
    /* chunk 1: len=2 type=10 flags=CHUNK seq=1 total=2 "BB" */
    static const unsigned char C1[10] = {
        0x00, 0x02, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x01, 0x00, 0x02,
        'B', 'B' };
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[chunked_reassembly]\n");
    stage_bytes(p, C0, sizeof(C0));
    stage_bytes(p, C1, sizeof(C1));
    reset_recorders();

    pt_messaging_process_tcp_data(g_ctx, p);

    CHECK(g_messages == 1, "reassembled message delivered once");
    CHECK(g_last_msg_len == 6, "total length = stride*(N-1)+last");
    CHECK(memcmp(g_last_msg, "AAAABB", 6) == 0, "chunks concatenated in order");
    CHECK(p->reassembly_total == 0, "reassembly state cleared after delivery");
}

/* A chunk whose offset+payload exceeds the reassembly buffer must fire
   PT_ERR_NO_ROOM and abort reassembly -- never overrun the buffer. */
static void test_chunk_oversize_rejected(void)
{
    static const unsigned char C0[12] = {
        0x00, 0x04, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x00, 0x00, 0x02,
        'A', 'A', 'A', 'A' };
    static const unsigned char C1[12] = {
        0x00, 0x04, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x01, 0x00, 0x02,
        'B', 'B', 'B', 'B' };
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    size_t saved_size = p->reassembly_buf_size;
    printf("[chunk_oversize_rejected]\n");

    /* Shrink the bound so chunk 1 (offset = stride 4) overflows. */
    p->reassembly_buf_size = 4;
    stage_bytes(p, C0, sizeof(C0));
    stage_bytes(p, C1, sizeof(C1));
    reset_recorders();

    pt_messaging_process_tcp_data(g_ctx, p);

    CHECK(g_errors == 1, "oversize chunk fired one error");
    CHECK(g_last_error == PT_ERR_NO_ROOM, "error code is NO_ROOM");
    CHECK(g_messages == 0, "no message delivered");
    CHECK(p->reassembly_total == 0, "reassembly aborted");

    p->reassembly_buf_size = saved_size;
}

/* Chunk reassembly must accept only the next expected chunk of the SAME
   message.  A forged total in a later chunk, or a duplicated chunk, used to
   deliver a message with a bad length or a hole; now it must be dropped and
   the reassembly reset. */
static void feed_bytes(PT_Peer_Internal *p, const unsigned char *b, size_t n)
{
    stage_bytes(p, b, n);
    pt_messaging_process_tcp_data(g_ctx, p);
}

static void test_chunk_forged_total(void)
{
    static const unsigned char C0[12] = {
        0x00, 0x04, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x00, 0x00, 0x02,
        'A', 'A', 'A', 'A' };
    /* seq=1 but total forged to 60000 */
    static const unsigned char C1[9] = {
        0x00, 0x01, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x01, 0xEA, 0x60, 'B' };
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[chunk_forged_total]\n");
    reset_recorders();
    feed_bytes(p, C0, sizeof(C0));
    feed_bytes(p, C1, sizeof(C1));
    CHECK(g_messages == 0, "chunk with changed total not delivered");
    CHECK(p->reassembly_total == 0, "reassembly reset on total mismatch");
}

static void test_chunk_duplicate(void)
{
    static const unsigned char C0[10] = {
        0x00, 0x02, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x00, 0x00, 0x03,
        'A', 'A' };
    static const unsigned char C1[10] = {
        0x00, 0x02, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x01, 0x00, 0x03,
        'B', 'B' };
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[chunk_duplicate]\n");
    reset_recorders();
    feed_bytes(p, C0, sizeof(C0));
    feed_bytes(p, C1, sizeof(C1));
    feed_bytes(p, C1, sizeof(C1));   /* duplicate: chunk 2 never arrived */
    CHECK(g_messages == 0, "message with a missing chunk not delivered");
    CHECK(p->reassembly_total == 0, "reassembly reset on duplicate chunk");
}

static void test_chunk_out_of_order(void)
{
    static const unsigned char C0[10] = {
        0x00, 0x02, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x00, 0x00, 0x03,
        'A', 'A' };
    static const unsigned char C2[10] = {
        0x00, 0x02, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x02, 0x00, 0x03,
        'C', 'C' };
    static const unsigned char C1[10] = {
        0x00, 0x02, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x01, 0x00, 0x03,
        'B', 'B' };
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[chunk_out_of_order]\n");
    reset_recorders();
    feed_bytes(p, C0, sizeof(C0));
    feed_bytes(p, C2, sizeof(C2));   /* skips chunk 1 */
    feed_bytes(p, C1, sizeof(C1));
    CHECK(g_messages == 0, "out-of-order chunks not delivered");
}

/* A non-final chunk whose length differs from the stride would corrupt the
   offset math; reject it. */
static void test_chunk_stride_mismatch(void)
{
    static const unsigned char C0[10] = {
        0x00, 0x02, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x00, 0x00, 0x03,
        'A', 'A' };
    static const unsigned char C1[9] = {
        0x00, 0x01, MSG_TYPE, PT_CHUNK_FLAG, 0x00, 0x01, 0x00, 0x03, 'B' };
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[chunk_stride_mismatch]\n");
    reset_recorders();
    feed_bytes(p, C0, sizeof(C0));
    feed_bytes(p, C1, sizeof(C1));   /* short, but not the last chunk */
    CHECK(p->reassembly_total == 0, "short non-final chunk resets reassembly");
    CHECK(g_messages == 0, "no message delivered");
}

/* A keepalive is defined as zero-length, but a non-zero declared length
   must still be consumed whole so the stream stays framed. */
static void test_keepalive_with_payload(void)
{
    static const unsigned char KA[6] = {
        0x00, 0x02, PT_MSG_TYPE_KEEPALIVE, 0x00, 'h', 'i' };
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    printf("[keepalive_with_payload]\n");
    reset_recorders();
    feed_bytes(p, KA, sizeof(KA));
    feed_bytes(p, MSG_HI, sizeof(MSG_HI));
    CHECK(g_messages == 1, "message after a keepalive-with-payload delivered");
    CHECK(p->tcp_recv_len == 0, "stream fully consumed");
}

/* PT_Disconnect() called from inside on_message zeroes tcp_recv_len; the
   parser must not then subtract the frame size from it (size_t underflow
   -> huge memmove) nor keep dispatching buffered frames. */
static int g_cb_disconnects;
static void on_msg_disconnect(PT_Peer *peer, const void *d, size_t l, void *ud)
{
    (void)d; (void)l; (void)ud;
    g_messages++;
    g_cb_disconnects++;
    PT_Disconnect((PT_Context *)g_ctx, peer);
}

static void test_disconnect_inside_callback(void)
{
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    PT_PlatformOps *saved = install_mock_ops();
    printf("[disconnect_inside_callback]\n");
    reset_recorders();
    g_cb_disconnects = 0;
    PT_OnMessage((PT_Context *)g_ctx, MSG_TYPE, on_msg_disconnect, NULL);
    stage_bytes(p, MSG_HI, sizeof(MSG_HI));
    stage_bytes(p, MSG_HI, sizeof(MSG_HI));   /* a second queued frame */
    pt_messaging_process_tcp_data(g_ctx, p);
    PT_OnMessage((PT_Context *)g_ctx, MSG_TYPE, on_msg, NULL);
    CHECK(g_messages == 1, "no frames dispatched after the callback disconnected");
    CHECK(p->tcp_recv_len == 0, "receive buffer stays empty (no underflow)");
    CHECK(p->state == PT_PEER_DISCONNECTED, "peer disconnected");
    g_ctx->platform_ops = saved;
}

/* ------------------------------------------------------------------ */
/* UDP fast-message processing (pt_messaging_process_udp_data)          */
/* ------------------------------------------------------------------ */

static void test_udp_message(void)
{
    /* [len=2][type=10]"hi" */
    static const unsigned char PKT[5] = { 0x00, 0x02, MSG_TYPE, 'h', 'i' };
    PT_Peer_Internal *p;
    printf("[udp_message]\n");

    reset_all_peers();
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->state = PT_PEER_CONNECTED;
    p->ip_addr = 0x0A000005UL;

    reset_recorders();
    pt_messaging_process_udp_data(g_ctx, PKT, sizeof(PKT), 0x0A000005UL);
    CHECK(g_messages == 1, "valid UDP message dispatched");
    CHECK(g_last_msg_len == 2 && memcmp(g_last_msg, "hi", 2) == 0,
          "UDP payload intact");

    reset_recorders();
    pt_messaging_process_udp_data(g_ctx, PKT, 2, 0x0A000005UL);
    CHECK(g_messages == 0, "packet shorter than header ignored");

    reset_recorders();
    pt_messaging_process_udp_data(g_ctx, PKT, sizeof(PKT), 0x0A0000FFUL);
    CHECK(g_messages == 0, "message from unknown peer ignored");

    reset_recorders();
    {   /* declares 100-byte payload but only 5 bytes present */
        static const unsigned char BAD[5] =
            { 0x00, 0x64, MSG_TYPE, 'h', 'i' };
        pt_messaging_process_udp_data(g_ctx, BAD, sizeof(BAD), 0x0A000005UL);
    }
    CHECK(g_messages == 0, "declared length past packet end ignored");

    reset_all_peers();
}

/* ------------------------------------------------------------------ */
/* Discovery parse (pt_discovery_receive) -- v2 6-byte header           */
/* ------------------------------------------------------------------ */

static void test_discovery_announce(void)
{
    unsigned char pkt[PT_DISCOVERY_MAX];
    size_t len;
    PT_Peer_Internal *found;
    printf("[discovery_announce]\n");

    reset_all_peers();
    g_ctx->local_ip = 0x0A000001UL;
    len = build_discovery(pkt, PT_WIRE_VERSION,
                          PT_DISCOVERY_FLAG_ANNOUNCE, "Bob");
    reset_recorders();

    pt_discovery_receive(g_ctx, pkt, len, 0x0A000009UL);

    CHECK(g_discovered == 1, "on_peer_discovered fired once");
    found = pt_find_peer_by_ip(g_ctx, 0x0A000009UL);
    CHECK(found != NULL, "peer slot allocated");
    CHECK(found && strcmp(found->name, "Bob") == 0, "peer name parsed");
    CHECK(found && found->state == PT_PEER_DISCOVERED, "state DISCOVERED");

    reset_all_peers();
}

/* Every malformed / non-matching packet must be dropped with no slot
   allocated and no callback -- these guard the validation ladder. */
static void test_discovery_rejects(void)
{
    unsigned char pkt[PT_DISCOVERY_MAX];
    size_t len;
    printf("[discovery_rejects]\n");

    reset_all_peers();
    g_ctx->local_ip = 0x0A000001UL;

    /* Bad magic */
    len = build_discovery(pkt, PT_WIRE_VERSION,
                          PT_DISCOVERY_FLAG_ANNOUNCE, "Bob");
    pkt[0] = 0xFF;
    reset_recorders();
    pt_discovery_receive(g_ctx, pkt, len, 0x0A000009UL);
    CHECK(g_discovered == 0 && g_ctx->peer_count == 0, "bad magic dropped");

    /* Wrong wire version */
    len = build_discovery(pkt, PT_WIRE_VERSION + 1,
                          PT_DISCOVERY_FLAG_ANNOUNCE, "Bob");
    reset_recorders();
    pt_discovery_receive(g_ctx, pkt, len, 0x0A000009UL);
    CHECK(g_discovered == 0 && g_ctx->peer_count == 0, "wrong version dropped");

    /* Too short (header only, no name/nul) */
    len = build_discovery(pkt, PT_WIRE_VERSION,
                          PT_DISCOVERY_FLAG_ANNOUNCE, "Bob");
    reset_recorders();
    pt_discovery_receive(g_ctx, pkt, PT_DISCOVERY_HEADER, 0x0A000009UL);
    CHECK(g_discovered == 0 && g_ctx->peer_count == 0, "short packet dropped");

    /* Name with no NUL terminator inside the received region */
    len = build_discovery(pkt, PT_WIRE_VERSION,
                          PT_DISCOVERY_FLAG_ANNOUNCE, "Bob");
    reset_recorders();
    pt_discovery_receive(g_ctx, pkt, len - 1, 0x0A000009UL); /* chop the NUL */
    CHECK(g_discovered == 0 && g_ctx->peer_count == 0,
          "unterminated name dropped");

    /* Our own broadcast (source_ip == local_ip) */
    len = build_discovery(pkt, PT_WIRE_VERSION,
                          PT_DISCOVERY_FLAG_ANNOUNCE, "Me");
    reset_recorders();
    pt_discovery_receive(g_ctx, pkt, len, g_ctx->local_ip);
    CHECK(g_discovered == 0 && g_ctx->peer_count == 0, "own IP filtered");

    reset_all_peers();
}

/* A LEAVE packet (v2 flag 0x01) removes a known peer immediately and
   fires on_peer_lost -- the v1.11.0 fast-quit path. */
static void test_discovery_leave(void)
{
    unsigned char pkt[PT_DISCOVERY_MAX];
    size_t len;
    printf("[discovery_leave]\n");

    reset_all_peers();
    g_ctx->local_ip = 0x0A000001UL;

    /* Announce first so there is a peer to lose. */
    len = build_discovery(pkt, PT_WIRE_VERSION,
                          PT_DISCOVERY_FLAG_ANNOUNCE, "Bob");
    reset_recorders();
    pt_discovery_receive(g_ctx, pkt, len, 0x0A000009UL);
    CHECK(g_discovered == 1 && g_ctx->peer_count == 1, "peer present");

    /* Now the leave broadcast. */
    len = build_discovery(pkt, PT_WIRE_VERSION,
                          PT_DISCOVERY_FLAG_LEAVE, "Bob");
    reset_recorders();
    pt_discovery_receive(g_ctx, pkt, len, 0x0A000009UL);
    CHECK(g_lost == 1, "on_peer_lost fired on leave");
    CHECK(pt_find_peer_by_ip(g_ctx, 0x0A000009UL) == NULL, "peer slot freed");
    CHECK(g_ctx->peer_count == 0, "peer_count decremented");

    reset_all_peers();
}

/* A LEAVE for a peer that still owns a transport must not just free the
   slot: a CONNECTED peer is disconnected (on_disconnected, ERROR -- a
   graceless close, see PT_Shutdown) and a mid-dial peer has its dial
   released.  Otherwise the socket/endpoint leaks and the app never hears
   the connection end. */
static void test_discovery_leave_connected(void)
{
    unsigned char pkt[PT_DISCOVERY_MAX];
    size_t len;
    PT_Peer_Internal *p;
    PT_PlatformOps *saved = install_mock_ops();
    printf("[discovery_leave_connected]\n");

    reset_all_peers();
    g_ctx->local_ip = 0x0A000001UL;
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->state = PT_PEER_CONNECTED;
    p->ip_addr = 0x0A000009UL;
    strcpy(p->name, "Bob");
    g_ctx->peer_count = 1;

    len = build_discovery(pkt, PT_WIRE_VERSION, PT_DISCOVERY_FLAG_LEAVE, "Bob");
    reset_recorders();
    pt_discovery_receive(g_ctx, pkt, len, 0x0A000009UL);
    CHECK(g_disconnect_calls == 1, "transport released on leave");
    CHECK(g_disconnected == 1, "on_disconnected fired");
    CHECK(g_last_reason == PT_DISCONNECT_ERROR, "reason is ERROR");
    CHECK(g_lost == 1, "on_peer_lost fired");
    CHECK(strcmp(g_order, "X") == 0, "disconnect reported before peer_lost");
    CHECK(p->in_use == 0 && g_ctx->peer_count == 0, "slot freed");

    /* Mid-dial: state DISCOVERED, connect_start set. */
    reset_all_peers();
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->state = PT_PEER_DISCOVERED;
    p->ip_addr = 0x0A000009UL;
    p->connect_start = 5;
    strcpy(p->name, "Bob");
    g_ctx->peer_count = 1;
    reset_recorders();
    pt_discovery_receive(g_ctx, pkt, len, 0x0A000009UL);
    CHECK(g_disconnect_calls == 1, "in-flight dial released on leave");
    CHECK(g_disconnected == 0, "no on_disconnected for an unconnected dial");
    CHECK(g_lost == 1 && p->in_use == 0, "peer lost, slot freed");

    g_ctx->platform_ops = saved;
    reset_all_peers();
}

/* A dial started while current_time is 0 (just after boot / a TickCount
   wrap) must still count as in flight: connect_start == 0 means "no dial",
   so PT_Connect must never store 0. */
/* An on_disconnected / on_peer_lost callback may call PT_Connect on the
   peer being removed.  That dial must not survive on the slot being freed
   (leaked fd on POSIX, stale owner pointer on MacTCP/OT). */
static int g_reconnect_on_disc;
static int g_reconnect_on_lost;

static void on_disc_reconnect(PT_Peer *peer, PT_DisconnectReason reason, void *ud)
{
    (void)reason; (void)ud;
    if (g_reconnect_on_disc) PT_Connect((PT_Context *)g_ctx, peer);
}

static void on_lost_reconnect(PT_Peer *peer, void *ud)
{
    (void)ud;
    g_lost++;
    if (g_reconnect_on_lost) PT_Connect((PT_Context *)g_ctx, peer);
}

static void test_removal_callback_reconnect(void)
{
    unsigned char pkt[PT_DISCOVERY_MAX];
    size_t len;
    PT_Peer_Internal *p;
    PT_PlatformOps *saved = install_mock_ops();
    PT_DisconnectCallback old_disc = g_ctx->callbacks.on_disconnected;
    PT_PeerCallback old_lost = g_ctx->callbacks.on_peer_lost;
    int phase;
    printf("[removal_callback_reconnect]\n");

    g_ctx->callbacks.on_disconnected = on_disc_reconnect;
    g_ctx->callbacks.on_peer_lost = on_lost_reconnect;
    g_ctx->local_ip = 0x0A000001UL;
    g_ctx->current_time = 50;
    len = build_discovery(pkt, PT_WIRE_VERSION, PT_DISCOVERY_FLAG_LEAVE, "Bob");

    /* phase 0: leave of a connected peer, reconnect from on_disconnected;
       phase 1: leave of a dialing peer, reconnect from on_peer_lost;
       phase 2: discovery timeout, reconnect from on_peer_lost. */
    for (phase = 0; phase < 3; phase++) {
        reset_all_peers();
        reset_recorders();
        g_reconnect_on_disc = (phase == 0);
        g_reconnect_on_lost = (phase != 0);
        p = &g_ctx->peers[0];
        p->in_use = 1;
        p->state = (phase == 0) ? PT_PEER_CONNECTED : PT_PEER_DISCOVERED;
        p->connect_start = (phase == 0) ? 0 : 5;
        p->ip_addr = 0x0A000009UL;
        p->last_seen = 1;
        p->platform_peer.tcp_fd = 77;      /* mock marks a live transport */
        strcpy(p->name, "Bob");
        g_ctx->peer_count = 1;

        if (phase == 2) {
            g_ctx->discovery_listening = 1;
            pt_discovery_check_timeouts(g_ctx);
            g_ctx->discovery_listening = 0;
        } else {
            pt_discovery_receive(g_ctx, pkt, len, 0x0A000009UL);
        }
        CHECK(g_connect_calls == 1, "callback's PT_Connect ran");
        CHECK(p->in_use == 0 && g_ctx->peer_count == 0, "slot freed");
        CHECK(p->platform_peer.tcp_fd == -1 && p->connect_start == 0,
              "no live transport left on the freed slot");
    }

    g_reconnect_on_disc = g_reconnect_on_lost = 0;
    g_ctx->callbacks.on_disconnected = old_disc;
    g_ctx->callbacks.on_peer_lost = old_lost;
    g_ctx->platform_ops = saved;
    reset_all_peers();
}

static void test_dial_at_time_zero(void)
{
    unsigned char pkt[PT_DISCOVERY_MAX];
    size_t len;
    PT_Peer_Internal *p;
    PT_PlatformOps *saved = install_mock_ops();
    printf("[dial_at_time_zero]\n");

    reset_all_peers();
    reset_recorders();
    g_ctx->local_ip = 0x0A000001UL;
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->state = PT_PEER_DISCOVERED;
    p->ip_addr = 0x0A000009UL;
    strcpy(p->name, "Bob");
    g_ctx->peer_count = 1;
    g_ctx->current_time = 0;

    CHECK(PT_Connect((PT_Context *)g_ctx, (PT_Peer *)p) == PT_OK,
          "connect at time 0 started");
    CHECK(p->connect_start != 0, "dial in flight is never stamped 0");

    len = build_discovery(pkt, PT_WIRE_VERSION, PT_DISCOVERY_FLAG_LEAVE, "Bob");
    pt_discovery_receive(g_ctx, pkt, len, 0x0A000009UL);
    CHECK(g_disconnect_calls == 1, "dial begun at time 0 is aborted on leave");

    g_ctx->platform_ops = saved;
    reset_all_peers();
}

static void test_discovery_timeout_dialing(void)
{
    PT_Peer_Internal *p;
    PT_PlatformOps *saved = install_mock_ops();
    printf("[discovery_timeout_dialing]\n");

    reset_all_peers();
    g_ctx->discovery_listening = 1;
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->state = PT_PEER_DISCOVERED;
    p->ip_addr = 0x0A000009UL;
    p->last_seen = 100;
    p->connect_start = 100;
    strcpy(p->name, "slow");
    g_ctx->peer_count = 1;

    reset_recorders();
    g_ctx->current_time = 100 + PT_DISCOVERY_TIMEOUT;
    pt_discovery_check_timeouts(g_ctx);
    CHECK(g_lost == 1 && p->in_use == 0, "timed-out peer freed");
    CHECK(g_disconnect_calls == 1, "in-flight dial released on timeout");

    reset_all_peers();
    g_ctx->discovery_listening = 0;
    g_ctx->platform_ops = saved;
}

/* ------------------------------------------------------------------ */
/* Peer ranking (PT_GetPeerRank) -- deterministic IP sort incl. self    */
/* ------------------------------------------------------------------ */

static void test_peer_rank(void)
{
    PT_Peer_Internal *lo, *hi;
    printf("[peer_rank]\n");

    reset_all_peers();
    g_ctx->local_ip = 0x0A000002UL;         /* us: middle IP */

    lo = &g_ctx->peers[0];
    lo->in_use = 1; lo->state = PT_PEER_CONNECTED; lo->ip_addr = 0x0A000001UL;
    hi = &g_ctx->peers[1];
    hi->in_use = 1; hi->state = PT_PEER_CONNECTED; hi->ip_addr = 0x0A000003UL;

    /* Sorted by IP: lo(.1)=0, self(.2)=1, hi(.3)=2 */
    CHECK(PT_GetPeerRank((PT_Context *)g_ctx, NULL) == 1,
          "self ranks between the two peers");
    CHECK(PT_GetPeerRank((PT_Context *)g_ctx, (PT_Peer *)lo) == 0,
          "lowest IP ranks 0");
    CHECK(PT_GetPeerRank((PT_Context *)g_ctx, (PT_Peer *)hi) == 2,
          "highest IP ranks 2");

    /* A non-connected peer has no rank. */
    hi->state = PT_PEER_DISCOVERED;
    CHECK(PT_GetPeerRank((PT_Context *)g_ctx, (PT_Peer *)hi) == -1,
          "unconnected peer ranks -1");

    reset_all_peers();
}

/* ------------------------------------------------------------------ */
/* IP ordering is byte-wise (network order), not native-integer         */
/* ------------------------------------------------------------------ */

/* Build ip_addr the way the backends do: the four octets as bytes in
   memory (POSIX s_addr / MacTCP ip_addr / OT InetHost), NOT as a native
   integer.  On a little-endian host a native compare would see the LAST
   octet as most significant, so 192.168.1.20 vs 192.168.2.10 disagrees
   with the big-endian Mac. */
static unsigned long ip_from_bytes(unsigned a, unsigned b, unsigned c,
                                   unsigned d)
{
    unsigned char o[4];
    unsigned long ip = 0;
    o[0] = (unsigned char)a; o[1] = (unsigned char)b;
    o[2] = (unsigned char)c; o[3] = (unsigned char)d;
    memcpy(&ip, o, 4);
    return ip;
}

/* From `local`'s point of view, with `remote` as the peer, record:
   *init   = PT_ShouldInitiate
   *dials  = auto-mesh sweep dialed it
   *cancel = tiebreaker (outbound pending) cancelled our outbound
   *swap   = tiebreaker (outbound already connected) adopted the incoming
   *rself, *rpeer = PT_GetPeerRank of self / of the peer */
static void ip_view(unsigned long local, unsigned long remote, int *init,
                    int *dials, int *cancel, int *swap, int *rself,
                    int *rpeer)
{
    PT_PlatformOps *saved;
    PT_PlatformPeer incoming;
    PT_Peer_Internal *p;

    reset_all_peers();
    g_ctx->local_ip = local;

    p = fresh_peer(PT_PEER_CONNECTED);
    p->ip_addr = remote;
    p->inbound = 0;
    p->platform_peer.tcp_fd = 7;
    g_ctx->peer_count = 1;
    *init = PT_ShouldInitiate((PT_Context *)g_ctx, (PT_Peer *)p) ? 1 : 0;
    *rself = PT_GetPeerRank((PT_Context *)g_ctx, NULL);
    *rpeer = PT_GetPeerRank((PT_Context *)g_ctx, (PT_Peer *)p);

    /* Tiebreaker, our outbound already completed. */
    reset_recorders();
    saved = install_mock_ops();
    memset(&incoming, 0, sizeof(incoming));
    incoming.tcp_fd = 42;
    pt_handle_incoming_connection(g_ctx, remote, &incoming);
    g_ctx->platform_ops = saved;
    *swap = (g_disconnect_calls == 1);

    /* Tiebreaker, our outbound still pending. */
    p = fresh_peer(PT_PEER_DISCOVERED);
    p->ip_addr = remote;
    p->inbound = 0;
    reset_recorders();
    saved = install_mock_ops();
    pt_handle_incoming_connection(g_ctx, remote, &incoming);
    g_ctx->platform_ops = saved;
    *cancel = (g_disconnect_calls == 1);

    /* Auto-mesh sweep. */
    p = fresh_peer(PT_PEER_DISCOVERED);
    p->ip_addr = remote;
    p->connect_start = 0;
    p->inbound = 0;
    reset_recorders();
    saved = install_mock_ops();
    g_ctx->auto_mesh = 1;
    g_ctx->mesh_dial_timer = 0;
    g_ctx->current_time = 100;
    pt_mesh_dial_sweep(g_ctx);
    g_ctx->auto_mesh = 0;
    g_ctx->platform_ops = saved;
    *dials = (g_connect_calls == 1);

    reset_all_peers();
}

static void check_ip_pair(const char *tag, unsigned long lo_ip,
                          unsigned long hi_ip)
{
    int li, ld, lc, lw, lrs, lrp;   /* view from the lower-IP host  */
    int hi, hd, hc, hw, hrs, hrp;   /* view from the higher-IP host */
    char m[96];

    ip_view(lo_ip, hi_ip, &li, &ld, &lc, &lw, &lrs, &lrp);
    ip_view(hi_ip, lo_ip, &hi, &hd, &hc, &hw, &hrs, &hrp);

    sprintf(m, "%s: lower IP is the initiator, higher is not", tag);
    CHECK(li == 1 && hi == 0, m);
    sprintf(m, "%s: exactly one side dials in auto-mesh", tag);
    CHECK(ld == 1 && hd == 0, m);
    sprintf(m, "%s: tiebreaker cancels/swaps only on the higher IP", tag);
    CHECK(lc == 0 && hc == 1 && lw == 0 && hw == 1, m);
    sprintf(m, "%s: tiebreaker agrees with PT_ShouldInitiate", tag);
    CHECK(lc == !li && hc == !hi && lw == !li && hw == !hi, m);
    sprintf(m, "%s: ranks strictly ordered (lower 0, higher 1)", tag);
    CHECK(lrs == 0 && lrp == 1 && hrs == 1 && hrp == 0, m);
    sprintf(m, "%s: both hosts agree on each address's rank", tag);
    CHECK(lrs == hrp && lrp == hrs, m);
}

static void test_ip_byte_order(void)
{
    printf("[ip_byte_order]\n");
    check_ip_pair("192.168.1.20 < 192.168.2.10",
                  ip_from_bytes(192, 168, 1, 20),
                  ip_from_bytes(192, 168, 2, 10));
    /* First differing octet (2nd: 0 < 88) orders opposite to the last
       (15 > 10): native-integer compare gets this backwards on little-endian. */
    check_ip_pair("10.0.2.15 < 10.88.0.10",
                  ip_from_bytes(10, 0, 2, 15),
                  ip_from_bytes(10, 88, 0, 10));
}

/* Identical IPs (loopback / same host): nobody initiates and the auto-mesh
   sweep must not dial -- the pre-PT_ShouldInitiate inline check was
   local_ip >= peer ip -> skip. */
static void test_ip_equal(void)
{
    int init, dials, cancel, swap, rself, rpeer;
    unsigned long ip = ip_from_bytes(10, 0, 2, 15);
    printf("[ip_equal]\n");

    ip_view(ip, ip, &init, &dials, &cancel, &swap, &rself, &rpeer);
    CHECK(init == 0, "equal IP: PT_ShouldInitiate is false");
    CHECK(dials == 0, "equal IP: auto-mesh sweep does not dial");
    CHECK(rself == rpeer, "equal IP: self and peer share a rank");
}

/* pt_alloc_peer must hand out every slot then return NULL at capacity. */
static void test_alloc_peer_exhaustion(void)
{
    int i;
    int got = 0;
    printf("[alloc_peer_exhaustion]\n");

    reset_all_peers();
    for (i = 0; i < g_ctx->max_peers; i++) {
        if (pt_alloc_peer(g_ctx) != NULL) got++;
    }
    CHECK(got == g_ctx->max_peers, "every slot allocated");
    CHECK(pt_alloc_peer(g_ctx) == NULL, "further alloc returns NULL");

    reset_all_peers();
}

/* ------------------------------------------------------------------ */
/* Timeout sweeps (driven by setting ctx->current_time directly)        */
/* ------------------------------------------------------------------ */

static void test_reassembly_timeout(void)
{
    PT_Peer_Internal *stale, *fresh;
    printf("[reassembly_timeout]\n");

    reset_all_peers();

    stale = &g_ctx->peers[0];
    stale->in_use = 1;
    stale->reassembly_total = 2;
    stale->reassembly_received = 1;
    stale->reassembly_timer = 100;

    fresh = &g_ctx->peers[1];
    fresh->in_use = 1;
    fresh->reassembly_total = 2;
    fresh->reassembly_received = 1;
    fresh->reassembly_timer = 104;

    g_ctx->current_time = 100 + PT_REASSEMBLY_TIMEOUT; /* 105 */
    pt_messaging_check_reassembly_timeouts(g_ctx);

    CHECK(stale->reassembly_total == 0, "stale reassembly aborted");
    CHECK(stale->reassembly_received == 0, "stale received cleared");
    CHECK(fresh->reassembly_total == 2, "recent reassembly untouched");

    reset_all_peers();
}

static void test_discovery_timeout(void)
{
    PT_Peer_Internal *stale, *live;
    printf("[discovery_timeout]\n");

    reset_all_peers();
    g_ctx->discovery_listening = 1;

    stale = &g_ctx->peers[0];
    stale->in_use = 1;
    stale->state = PT_PEER_DISCOVERED;
    stale->ip_addr = 0x0A000009UL;
    strcpy(stale->name, "gone");
    stale->last_seen = 100;

    /* A CONNECTED peer must never be timed out by discovery. */
    live = &g_ctx->peers[1];
    live->in_use = 1;
    live->state = PT_PEER_CONNECTED;
    live->ip_addr = 0x0A00000AUL;
    live->last_seen = 100;

    reset_recorders();
    g_ctx->current_time = 100 + PT_DISCOVERY_TIMEOUT; /* 115 */
    pt_discovery_check_timeouts(g_ctx);

    CHECK(g_lost == 1, "on_peer_lost fired once");
    CHECK(stale->in_use == 0, "timed-out peer slot freed");
    CHECK(live->in_use == 1, "connected peer not timed out");

    reset_all_peers();
    g_ctx->discovery_listening = 0;
}

/* ------------------------------------------------------------------ */
/* Per-peer timeout sweep (pt_check_peer_timeouts, extracted from       */
/* PT_Poll so it is reachable without a real backend).  Each branch is  */
/* driven by hand-setting ctx->current_time relative to the peer's      */
/* timers, exactly as PT_Poll does after pt_get_time().                 */
/* ------------------------------------------------------------------ */

static void test_peer_timeouts(void)
{
    PT_PlatformOps *saved;
    PT_Peer_Internal *p;
    printf("[peer_timeouts]\n");

    saved = install_mock_ops();

    /* --- connect timeout: a pending outbound that never completes --- */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->state = PT_PEER_DISCONNECTED;   /* connecting == connect_start > 0 */
    p->connect_start = 1000;
    p->platform_peer.tcp_fd = -1;
    strcpy(p->name, "slow");
    g_ctx->current_time = 1000 + PT_CONNECT_TIMEOUT;
    pt_check_peer_timeouts(g_ctx);
    CHECK(g_disconnect_calls == 1, "connect-timeout: backend disconnect called");
    CHECK(p->state == PT_PEER_DISCONNECTED, "connect-timeout: state stays DISCONNECTED");
    CHECK(p->connect_start == 0, "connect-timeout: connect_start cleared");
    CHECK(g_errors == 1 && g_last_error == PT_ERR_SEND_FAILED,
          "connect-timeout: on_error fired");

    /* --- keepalive: connected, TX idle >= interval, RX recent --- */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->state = PT_PEER_CONNECTED;
    p->connect_start = 0;
    p->last_tcp_send = 1000;
    p->last_tcp_activity = 1000 + PT_KEEPALIVE_INTERVAL; /* recent RX */
    g_ctx->current_time = 1000 + PT_KEEPALIVE_INTERVAL;
    pt_check_peer_timeouts(g_ctx);
    CHECK(g_tcp_sends == 1, "keepalive: exactly one frame sent");
    CHECK(g_tcp_last_frame_len == PT_TCP_HEADER_SIZE &&
          g_tcp_last_frame[2] == PT_MSG_TYPE_KEEPALIVE,
          "keepalive: zero-payload type-254 frame");
    CHECK(g_disconnected == 0, "keepalive: peer not disconnected");

    /* --- inactivity: connected, no RX for PT_TCP_TIMEOUT --- */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->state = PT_PEER_CONNECTED;
    p->connect_start = 0;
    p->last_tcp_activity = 1000;
    p->last_tcp_send = 1000 + PT_TCP_TIMEOUT; /* recent TX: no keepalive due */
    p->platform_peer.tcp_fd = -1;
    strcpy(p->name, "silent");
    g_ctx->current_time = 1000 + PT_TCP_TIMEOUT;
    pt_check_peer_timeouts(g_ctx);
    CHECK(g_disconnected == 1, "inactivity: on_disconnected fired");
    CHECK(g_last_reason == PT_TIMEOUT, "inactivity: reason is TIMEOUT");

    /* --- healthy peer: every timer recent, nothing fires --- */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->state = PT_PEER_CONNECTED;
    p->connect_start = 0;
    p->last_tcp_send = 1000;
    p->last_tcp_activity = 1000;
    g_ctx->current_time = 1001; /* 1s later -- nothing is due */
    pt_check_peer_timeouts(g_ctx);
    CHECK(g_tcp_sends == 0, "healthy: no keepalive sent");
    CHECK(g_disconnected == 0 && g_disconnect_calls == 0,
          "healthy: no disconnect");

    g_ctx->platform_ops = saved;
    reset_all_peers();
}

/* ------------------------------------------------------------------ */
/* POSIX backend: partial TCP send                                      */
/* ------------------------------------------------------------------ */

/* If send() stalls part-way through a frame the wire holds a truncated
   frame; any later frame would be mis-parsed by the receiver.  The backend
   must fail the connection (the partner sees EOF) rather than return an
   error and leave the stream open to desync. */
static void test_posix_partial_send_fails_connection(void)
{
    static unsigned char big[1 << 20];
    unsigned char sink[65536];
    PT_Peer_Internal *p = fresh_peer(PT_PEER_CONNECTED);
    PT_PlatformOps *posix = posix_get_ops();
    int sv[2];
    ssize_t n;
    PT_Status st;
    int saw_eof = 0, i;

    printf("[posix_partial_send_fails_connection]\n");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        CHECK(0, "socketpair");
        return;
    }
    fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL, 0) | O_NONBLOCK);
    fcntl(sv[1], F_SETFL, fcntl(sv[1], F_GETFL, 0) | O_NONBLOCK);
    p->platform_peer.tcp_fd = sv[0];

    st = posix->tcp_send(g_ctx, p, big, sizeof(big));
    CHECK(st == PT_ERR_SEND_FAILED, "oversized send reports failure");

    /* Drain what made it onto the wire, then look for EOF. */
    for (i = 0; i < 1000 && !saw_eof; i++) {
        n = recv(sv[1], sink, sizeof(sink), 0);
        if (n == 0) saw_eof = 1;
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;
    }
    CHECK(saw_eof, "partner sees EOF (connection failed, not left desynced)");

    close(sv[0]);
    close(sv[1]);
    p->platform_peer.tcp_fd = -1;
}

/* select() cannot watch an fd >= FD_SETSIZE: FD_SET on one is a stack
   buffer overflow.  The backend must refuse to hand such an fd to the
   select loop (fail the connect, close the socket). */
static void test_posix_fd_beyond_setsize(void)
{
    struct rlimit rl;
    int held[FD_SETSIZE + 8];
    int nheld = 0, fd, i;
    PT_Peer_Internal *p = fresh_peer(PT_PEER_DISCONNECTED);
    PT_PlatformOps *posix = posix_get_ops();
    PT_Status st;

    printf("[posix_fd_beyond_setsize]\n");
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0 ||
        (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < FD_SETSIZE + 16)) {
        printf("  skip: cannot raise RLIMIT_NOFILE above FD_SETSIZE\n");
        return;
    }
    rl.rlim_cur = FD_SETSIZE + 16;
    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
        printf("  skip: setrlimit failed\n");
        return;
    }

    /* Occupy every fd below FD_SETSIZE so the next socket() lands above. */
    while ((fd = dup(0)) >= 0 && fd < FD_SETSIZE && nheld < FD_SETSIZE)
        held[nheld++] = fd;
    if (fd >= 0) close(fd);

    p->ip_addr = htonl(0x7F000001UL);
    st = posix->tcp_connect(g_ctx, p);
    CHECK(st != PT_OK, "connect refused when the socket would exceed FD_SETSIZE");
    CHECK(p->platform_peer.tcp_fd < 0, "no fd adopted by the peer");

    for (i = 0; i < nheld; i++) close(held[i]);
}

/* PT_DebugSend clamps the message to its 256-byte buffer.  The clamp
   compared (int)len, so a size_t above INT_MAX could truncate to a small or
   negative int, skip the clamp and memcpy gigabytes into the buffer. */
static void test_debug_send_huge_len(void)
{
    PT_PlatformOps *saved = install_mock_ops();
    char *msg;
    size_t huge = (size_t)1 << 32;
    printf("[debug_send_huge_len]\n");

    if (sizeof(size_t) < 8) {
        printf("  skip: 32-bit size_t\n");
        g_ctx->platform_ops = saved;
        return;
    }
    msg = (char *)malloc(huge + 16);
    if (!msg) {
        printf("  skip: cannot reserve 4GB of address space\n");
        g_ctx->platform_ops = saved;
        return;
    }
    memset(msg, 'x', 64);
    g_ctx->debug_port = 7356;
    strcpy(g_ctx->debug_prefix, "[t@1.2.3.4] ");
    g_ctx->debug_prefix_len = (int)strlen(g_ctx->debug_prefix);
    reset_recorders();

    PT_DebugSend((PT_Context *)g_ctx, msg, huge + 10);  /* (int) cast = 10 */
    CHECK(g_udp_broadcasts == 1, "debug message sent");
    CHECK(g_udp_last_len <= 256, "length clamped to the debug buffer");

    g_ctx->debug_port = 0;
    g_ctx->debug_prefix_len = 0;
    free(msg);
    g_ctx->platform_ops = saved;
}

/* ------------------------------------------------------------------ */
/* Clock stepping backwards (POSIX time() is wall-clock)                */
/* ------------------------------------------------------------------ */

/* Every timer compares "now - stamp" in unsigned arithmetic.  If the clock
   steps back, now < stamp and the subtraction wraps to ~4e9, which would
   instantly time out every peer, drop every connection and fire keepalives.
   A backwards step must read as "no time elapsed", and timers armed in the
   future must not leave discovery / auto-mesh silent until the clock
   catches up. */
static void test_clock_step_back(void)
{
    PT_PlatformOps *saved;
    PT_Peer_Internal *p;
    printf("[clock_step_back]\n");
    saved = install_mock_ops();

    /* discovery timeout */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1; p->state = PT_PEER_DISCOVERED; p->ip_addr = 1;
    p->last_seen = 1000;
    g_ctx->peer_count = 1;
    g_ctx->discovery_listening = 1;
    g_ctx->current_time = 999;
    pt_discovery_check_timeouts(g_ctx);
    CHECK(g_lost == 0 && p->in_use == 1, "discovery: peer not lost on clock step");
    g_ctx->discovery_listening = 0;

    /* connect timeout / keepalive / inactivity timeout */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1; p->state = PT_PEER_CONNECTED;
    p->connect_start = 1000;
    p->last_tcp_send = 1000;
    p->last_tcp_activity = 1000;
    g_ctx->current_time = 990;
    pt_check_peer_timeouts(g_ctx);
    CHECK(g_disconnect_calls == 0 && g_disconnected == 0 && g_errors == 0,
          "peer sweep: no connect timeout on clock step");

    /* keepalive in isolation (no dial in flight) */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1; p->state = PT_PEER_CONNECTED;
    p->last_tcp_send = 1000;
    p->last_tcp_activity = 1000;
    g_ctx->current_time = 990;
    pt_check_peer_timeouts(g_ctx);
    CHECK(g_tcp_sends == 0, "peer sweep: no spurious keepalive");
    CHECK(g_disconnected == 0, "peer sweep: no spurious inactivity timeout");

    /* reassembly timeout */
    reset_all_peers();
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->reassembly_total = 2; p->reassembly_received = 1;
    p->reassembly_timer = 1000;
    g_ctx->current_time = 990;
    pt_messaging_check_reassembly_timeouts(g_ctx);
    CHECK(p->reassembly_total == 2, "reassembly: not aborted on clock step");

    /* auto-mesh timer armed in the future must re-arm, not stall */
    reset_all_peers();
    reset_recorders();
    g_ctx->local_ip = 0x0A000002UL;
    p = &g_ctx->peers[0];
    p->in_use = 1; p->state = PT_PEER_DISCOVERED;
    p->ip_addr = 0x0A000003UL; p->connect_start = 0;
    g_ctx->peer_count = 1;
    g_ctx->auto_mesh = 1;
    g_ctx->current_time = 100;
    g_ctx->mesh_dial_timer = 100 + 3600;
    pt_mesh_dial_sweep(g_ctx);
    CHECK(g_connect_calls == 1, "auto-mesh: dials after clock stepped back");
    g_ctx->auto_mesh = 0;

    /* discovery broadcast timer armed in the future must re-arm too */
    reset_all_peers();
    reset_recorders();
    g_script_n = 0; g_script_cursor = 0;
    g_ctx->discovery_active = 1;
    g_ctx->discovery_timer = (unsigned long)-1 / 2;   /* far ahead of time(NULL) */
    PT_Poll((PT_Context *)g_ctx);
    CHECK(g_udp_broadcasts == 1, "discovery: announce sent after clock stepped back");
    g_ctx->discovery_active = 0;

    g_ctx->platform_ops = saved;
    reset_all_peers();
}

/* After a backwards step the stored stamps are in the future.  PT_ELAPSED
   reads that as 0, so without a clamp every timer would freeze for as long
   as the step (here 100 s).  Each sweep must pull a future stamp back to
   now so a stall lasts at most one interval. */
static void test_clock_step_back_recovers(void)
{
    PT_PlatformOps *saved;
    PT_Peer_Internal *p;
    printf("[clock_step_back_recovers]\n");
    saved = install_mock_ops();

    /* idle link: keepalive within one interval, dead peer times out */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1; p->state = PT_PEER_CONNECTED;
    p->last_tcp_send = 1000;
    p->last_tcp_activity = 1000;
    g_ctx->peer_count = 1;
    g_ctx->current_time = 900;                 /* clock stepped back 100 s */
    pt_check_peer_timeouts(g_ctx);
    CHECK(g_tcp_sends == 0 && g_disconnected == 0, "step back: quiet at once");
    g_ctx->current_time = 900 + PT_KEEPALIVE_INTERVAL;
    pt_check_peer_timeouts(g_ctx);
    CHECK(g_tcp_sends == 1, "step back: keepalive within one interval");
    g_ctx->current_time = 900 + PT_TCP_TIMEOUT;
    pt_check_peer_timeouts(g_ctx);
    CHECK(g_disconnected == 1 && g_last_reason == PT_TIMEOUT,
          "step back: dead peer still times out");

    /* in-flight dial still times out */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1; p->state = PT_PEER_DISCOVERED;
    p->connect_start = 1000;
    g_ctx->current_time = 900;
    pt_check_peer_timeouts(g_ctx);
    g_ctx->current_time = 900 + PT_CONNECT_TIMEOUT;
    pt_check_peer_timeouts(g_ctx);
    CHECK(g_disconnect_calls == 1 && p->connect_start == 0,
          "step back: dial still times out");

    /* discovery: silent peer is still lost */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1; p->state = PT_PEER_DISCOVERED; p->ip_addr = 1;
    p->last_seen = 1000;
    g_ctx->peer_count = 1;
    g_ctx->discovery_listening = 1;
    g_ctx->current_time = 900;
    pt_discovery_check_timeouts(g_ctx);
    CHECK(g_lost == 0, "step back: peer kept at once");
    g_ctx->current_time = 900 + PT_DISCOVERY_TIMEOUT;
    pt_discovery_check_timeouts(g_ctx);
    CHECK(g_lost == 1 && p->in_use == 0, "step back: silent peer still lost");
    g_ctx->discovery_listening = 0;

    /* reassembly: stalled message is still aborted */
    reset_all_peers();
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->reassembly_total = 2; p->reassembly_received = 1;
    p->reassembly_timer = 1000;
    g_ctx->current_time = 900;
    pt_messaging_check_reassembly_timeouts(g_ctx);
    g_ctx->current_time = 900 + PT_REASSEMBLY_TIMEOUT;
    pt_messaging_check_reassembly_timeouts(g_ctx);
    CHECK(p->reassembly_total == 0, "step back: stalled reassembly aborted");

    g_ctx->platform_ops = saved;
    reset_all_peers();
}

/* ------------------------------------------------------------------ */
/* TCP send error paths.  The mock tcp_send is configured to fail so    */
/* PT_Send's failure propagation and chunked-send abort -- previously   */
/* reachable only via a real backend -- can be verified directly.       */
/* ------------------------------------------------------------------ */

#define TCP_TYPE 11  /* a PT_RELIABLE type registered just for these tests */

static void test_send_error_paths(void)
{
    PT_PlatformOps *saved;
    PT_Peer_Internal *p;
    unsigned char big[64];
    size_t orig_size;
    PT_Status st;
    int i;
    printf("[send_error_paths]\n");

    for (i = 0; i < (int)sizeof(big); i++) big[i] = (unsigned char)i;

    saved = install_mock_ops();
    g_ctx->message_types[TCP_TYPE] = PT_RELIABLE;
    orig_size = g_ctx->peers[0].tcp_send_size;

    /* --- single-frame send failure propagates, timer untouched --- */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->state = PT_PEER_CONNECTED;
    p->last_tcp_send = 5;
    g_tcp_send_fail_at = 1;             /* first (only) send fails */
    st = PT_Send((PT_Context *)g_ctx, (PT_Peer *)p, TCP_TYPE, "hi", 2);
    CHECK(st == PT_ERR_SEND_FAILED, "single send: failure propagated");
    CHECK(g_tcp_sends == 1, "single send: exactly one attempt");
    CHECK(p->last_tcp_send == 5,
          "single send: last_tcp_send NOT advanced on failure");

    /* --- chunked send aborts at the failing chunk --- */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->state = PT_PEER_CONNECTED;
    p->tcp_send_size = 20;             /* max_chunk_payload = 12 -> 3 chunks/30B */
    g_tcp_send_fail_at = 2;            /* second chunk fails */
    st = PT_Send((PT_Context *)g_ctx, (PT_Peer *)p, TCP_TYPE, big, 30);
    CHECK(st == PT_ERR_SEND_FAILED, "chunked: failure propagated");
    CHECK(g_tcp_sends == 2, "chunked: aborted at failing chunk (no 3rd send)");

    /* --- chunked send success: all chunks sent, CHUNK flag + total set --- */
    reset_all_peers();
    reset_recorders();
    p = &g_ctx->peers[0];
    p->in_use = 1;
    p->state = PT_PEER_CONNECTED;
    p->tcp_send_size = 20;
    st = PT_Send((PT_Context *)g_ctx, (PT_Peer *)p, TCP_TYPE, big, 30);
    CHECK(st == PT_OK, "chunked ok: PT_Send succeeded");
    CHECK(g_tcp_sends == 3, "chunked ok: 3 chunks for 30 bytes @ 12/chunk");
    CHECK((g_tcp_last_frame[3] & PT_CHUNK_FLAG) != 0,
          "chunked ok: last frame carries the CHUNK flag");
    CHECK(g_tcp_last_frame[7] == 3, "chunked ok: total-chunks field == 3");

    g_ctx->peers[0].tcp_send_size = orig_size;
    g_ctx->message_types[TCP_TYPE] = PT_FAST;
    g_ctx->platform_ops = saved;
    reset_all_peers();
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(void)
{
    PT_Context *pub;

    clog_init("test_seam", CLOG_LVL_WARN);

    printf("=== test_seam (event-seam unit tests) ===\n");

    if (PT_Init(&pub, "seam-test") != PT_OK) {
        printf("*** FAIL: PT_Init failed ***\n");
        return 1;
    }
    g_ctx = (PT_Context_Internal *)pub;

    PT_OnConnected(pub, on_conn, NULL);
    PT_OnDisconnected(pub, on_disc, NULL);
    PT_OnMessage(pub, MSG_TYPE, on_msg, NULL);
    PT_OnError(pub, on_err, NULL);
    PT_OnPeerDiscovered(pub, on_disc_found, NULL);
    PT_OnPeerLost(pub, on_lost, NULL);

    /* Event-seam transitions */
    test_connect_success();
    test_connect_failure();
    test_close_clean_goodbye();
    test_close_abrupt();
    test_close_message_then_goodbye();
    test_poll_drain_order();

    /* Simultaneous-connect tiebreaker + no-room */
    test_tiebreak_swap_outbound_for_incoming();
    test_tiebreak_lower_keeps_outbound();
    test_tiebreak_true_duplicate_rejected();
    test_no_room_leaves_fd_to_platform();

    /* Auto-mesh topology maintenance */
    test_auto_mesh_sweep();

    /* Wire protocol: TCP framing / reassembly */
    test_frame_single();
    test_frame_partial_then_rest();
    test_frame_two_back_to_back();
    test_frame_keepalive_consumed();
    test_chunked_reassembly();
    test_chunk_oversize_rejected();
    test_chunk_forged_total();
    test_chunk_duplicate();
    test_chunk_out_of_order();
    test_chunk_stride_mismatch();
    test_keepalive_with_payload();
    test_disconnect_inside_callback();

    /* Wire protocol: UDP fast messages */
    test_udp_message();

    /* Discovery v2 parse */
    test_discovery_announce();
    test_discovery_rejects();
    test_discovery_leave();
    test_discovery_leave_connected();
    test_discovery_timeout_dialing();
    test_dial_at_time_zero();
    test_removal_callback_reconnect();

    /* Peer management + ranking */
    test_peer_rank();
    test_ip_byte_order();
    test_ip_equal();
    test_alloc_peer_exhaustion();

    /* Timeout sweeps */
    test_reassembly_timeout();
    test_discovery_timeout();
    test_peer_timeouts();

    /* POSIX backend */
    test_posix_partial_send_fails_connection();
    test_posix_fd_beyond_setsize();
    test_debug_send_huge_len();

    /* Clock stepping backwards */
    test_clock_step_back();
    test_clock_step_back_recovers();

    /* TCP send error paths */
    test_send_error_paths();

    PT_Shutdown(pub);

    printf("=== Summary ===\n");
    printf("Checks: %d, Failures: %d\n", g_checks, g_failures);
    if (g_failures == 0) {
        printf("*** PASS ***\n");
        return 0;
    }
    printf("*** FAIL ***\n");
    return 1;
}
