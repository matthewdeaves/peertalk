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

static void reset_recorders(void)
{
    g_connected = 0;
    g_disconnected = 0;
    g_messages = 0;
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
{ (void)c; pe->platform_peer.tcp_fd = -1; }

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

    test_connect_success();
    test_connect_failure();
    test_close_clean_goodbye();
    test_close_abrupt();
    test_close_message_then_goodbye();
    test_poll_drain_order();

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
