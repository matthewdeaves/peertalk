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
#include <string.h>
#include <unistd.h>   /* dup, close -- real fd for the no-room test */
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
static int                 g_errors;              /* on_error fired count */
static int                 g_discovered;          /* on_peer_discovered count */
static int                 g_lost;                /* on_peer_lost count */
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
    g_errors = 0;
    g_discovered = 0;
    g_lost = 0;
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
{ (void)c; (void)p; (void)d; (void)l; return PT_OK; }

static PT_Status mock_udp_send(PT_Context_Internal *c,
                               const PT_Peer_Internal *pe,
                               unsigned short p, const void *d, size_t l)
{ (void)c; (void)pe; (void)p; (void)d; (void)l; return PT_OK; }

static PT_Status mock_tcp_send(PT_Context_Internal *c, PT_Peer_Internal *pe,
                               const void *d, size_t l)
{ (void)c; (void)pe; (void)d; (void)l; return PT_OK; }

static void mock_tcp_disconnect(PT_Context_Internal *c, PT_Peer_Internal *pe)
{ (void)c; g_disconnect_calls++; pe->platform_peer.tcp_fd = -1; }

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
    g_mock_ops.tcp_disconnect = mock_tcp_disconnect;
    g_mock_ops.poll           = NULL;
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
    mock.poll           = NULL;
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

    /* Wire protocol: TCP framing / reassembly */
    test_frame_single();
    test_frame_partial_then_rest();
    test_frame_two_back_to_back();
    test_frame_keepalive_consumed();
    test_chunked_reassembly();
    test_chunk_oversize_rejected();

    /* Wire protocol: UDP fast messages */
    test_udp_message();

    /* Discovery v2 parse */
    test_discovery_announce();
    test_discovery_rejects();
    test_discovery_leave();

    /* Peer management + ranking */
    test_peer_rank();
    test_alloc_peer_exhaustion();

    /* Timeout sweeps */
    test_reassembly_timeout();
    test_discovery_timeout();

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
