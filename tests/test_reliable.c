/*
 * test_reliable.c -- PeerTalk reliable messaging test (Chess pattern)
 *
 * Proves: Ordered TCP delivery, request/response pattern (US2)
 *
 * Flow: Init -> discover -> connect -> alternate 10 moves ->
 *       show results -> exit
 *
 * PASS: All 10 moves sent and received in order
 * Auto-exit: After TOTAL_TURNS complete, or solo timeout
 */

#include "test_common.h"

/* Byte-order helpers for cross-platform wire data (mirrors SDK pt_htons) */
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
#define TEST_HTONS(x) (x)
#define TEST_NTOHS(x) (x)
#else
#include <arpa/inet.h>
#define TEST_HTONS(x) htons(x)
#define TEST_NTOHS(x) ntohs(x)
#endif

#define TOTAL_TURNS 10

typedef struct {
    unsigned char from_row;
    unsigned char from_col;
    unsigned char to_row;
    unsigned char to_col;
    unsigned short move_num;
    unsigned char padding[14];
} MoveMsg;

static PT_Context *g_ctx;
static int g_connected = 0;
static int g_my_turn = 0;
static int g_moves_sent = 0;
static int g_moves_received = 0;
static int g_initiated = 0;
static int g_order_valid = 1;
static int g_payload_valid = 1;
static int g_last_recv_num = 0;
static int g_broadcast_sent = 0;
static int g_broadcast_received = 0;
static int g_moves_done = 0;
static unsigned long g_moves_done_time = 0;

static const char *safe_peer_name(PT_Peer *peer)
{
    const char *n = PT_PeerName(peer);
    return (n && n[0]) ? n : "(unknown)";
}

static void send_move(PT_Peer *peer)
{
    MoveMsg move;
    memset(&move, 0, sizeof(move));
    move.from_row = (unsigned char)(g_moves_sent % 8);
    move.from_col = (unsigned char)((g_moves_sent + 1) % 8);
    move.to_row = (unsigned char)((g_moves_sent + 2) % 8);
    move.to_col = (unsigned char)((g_moves_sent + 3) % 8);
    move.move_num = TEST_HTONS((unsigned short)(g_moves_sent + 1));

    TEST_LOG("[SEND] Move %d: (%d,%d)->(%d,%d)",
             g_moves_sent + 1,
             move.from_row, move.from_col,
             move.to_row, move.to_col);

    if (PT_Send(g_ctx, peer, MSG_MOVE, &move, sizeof(move)) != PT_OK) {
        TEST_WARN("Failed to send move %d", move.move_num);
    }
    g_moves_sent++;
    g_my_turn = 0;
}

static void on_discovered(PT_Peer *peer, void *data)
{
    (void)data;
    TEST_LOG("[DISCOVERED] %s", safe_peer_name(peer));
    if (!g_connected && !g_moves_done) {
        g_initiated = 1;
        PT_Connect(g_ctx, peer);
    }
}

static void on_connected(PT_Peer *peer, void *data)
{
    (void)data;
    g_connected = 1;
    test_mark_connected();
    TEST_LOG("[CONNECTED] %s", safe_peer_name(peer));

    if (g_initiated) {
        g_my_turn = 1;
        TEST_LOG("I go first!");
        send_move(peer);
    } else {
        TEST_LOG("Waiting for opponent...");
    }
}

static void on_move(PT_Peer *peer, const void *data,
                     size_t len, void *user)
{
    const MoveMsg *move;
    (void)user;

    if (len < sizeof(MoveMsg)) {
        TEST_WARN("Invalid move size: %lu", (unsigned long)len);
        return;
    }

    move = (const MoveMsg *)data;
    g_moves_received++;

    /* Decode network byte order move number */
    {
        int recv_num = (int)TEST_NTOHS(move->move_num);

        /* Check ordering */
        if (recv_num <= g_last_recv_num) {
            TEST_WARN("Out of order: got %d after %d",
                      recv_num, g_last_recv_num);
            g_order_valid = 0;
        }
        g_last_recv_num = recv_num;
    }

    /* Validate payload field relationships (T075).
       Sender uses: from_col = (from_row+1)%8, to_row = (from_row+2)%8,
       to_col = (from_row+3)%8. Validate self-consistency without
       depending on move_num (which has endian issues cross-platform). */
    {
        unsigned char r = move->from_row;
        unsigned char exp_col  = (unsigned char)((r + 1) % 8);
        unsigned char exp_to_r = (unsigned char)((r + 2) % 8);
        unsigned char exp_to_c = (unsigned char)((r + 3) % 8);
        if (move->from_col != exp_col ||
            move->to_row != exp_to_r ||
            move->to_col != exp_to_c) {
            TEST_WARN("Payload mismatch: got (%d,%d)->(%d,%d) "
                      "expected (%d,%d)->(%d,%d)",
                      move->from_row, move->from_col,
                      move->to_row, move->to_col,
                      r, exp_col, exp_to_r, exp_to_c);
            g_payload_valid = 0;
        }
    }

    TEST_LOG("[RECV] Move %d from %s: (%d,%d)->(%d,%d)",
             g_last_recv_num, safe_peer_name(peer),
             move->from_row, move->from_col,
             move->to_row, move->to_col);

    if (g_moves_sent < TOTAL_TURNS) {
        g_my_turn = 1;
        send_move(peer);
    } else {
        TEST_LOG("All %d turns complete!", TOTAL_TURNS);
        g_moves_done = 1;
        g_moves_done_time = test_time_sec();
    }
}

/* T076: Game-over broadcast callback */
static void on_game_over(PT_Peer *peer, const void *data,
                          size_t len, void *user)
{
    (void)peer; (void)user;
    if (len >= 9 && memcmp(data, "GAME_OVER", 9) == 0) {
        TEST_LOG("[RECV] Broadcast: GAME_OVER from %s",
                 safe_peer_name(peer));
        g_broadcast_received = 1;
    }
}

static void on_disconnected(PT_Peer *peer,
                             PT_DisconnectReason reason,
                             void *data)
{
    (void)data;
    TEST_LOG("[DISCONNECTED] %s (%s)",
             safe_peer_name(peer), test_reason_str(reason));
    g_connected = 0;
    g_running = 0;
}

static void on_error(PT_Peer *peer, PT_Status error, const char *desc,
                     void *data)
{
    (void)peer; (void)data;
    TEST_WARN("[ERROR] %d: %s", (int)error, desc ? desc : "");
}

int main(int argc, char **argv)
{
    const char *name = test_parse_name(argc, argv);
    unsigned long poll_count = 0;
    int passed;

    test_install_signal_handler();
    test_init_toolbox();
    test_init_logging("test_reliable");

    TEST_LOG("=== test_reliable ===");
    TEST_LOG("Name: %s", name);

    if (PT_Init(&g_ctx, name) != PT_OK) {
        TEST_WARN("PT_Init FAILED");
        TEST_LOG("*** FAIL: PT_Init failed ***");
        test_shutdown_logging();
        return 1;
    }
    TEST_LOG("PT_Init OK");

    PT_RegisterMessage(g_ctx, MSG_MOVE, PT_RELIABLE);
    PT_RegisterMessage(g_ctx, MSG_GAME_OVER, PT_RELIABLE);

    PT_OnPeerDiscovered(g_ctx, on_discovered, NULL);
    PT_OnConnected(g_ctx, on_connected, NULL);
    PT_OnDisconnected(g_ctx, on_disconnected, NULL);
    PT_OnMessage(g_ctx, MSG_MOVE, on_move, NULL);
    PT_OnMessage(g_ctx, MSG_GAME_OVER, on_game_over, NULL);
    PT_OnError(g_ctx, on_error, NULL);

    if (PT_StartDiscovery(g_ctx) != PT_OK) {
        TEST_WARN("PT_StartDiscovery FAILED");
        TEST_LOG("*** FAIL: Discovery failed ***");
        PT_Shutdown(g_ctx);
        test_shutdown_logging();
        return 1;
    }
    TEST_LOG("Discovery started, waiting...");
    test_mark_start();

    while (g_running) {
        PT_Poll(g_ctx);
        poll_count++;

        if (test_solo_timeout()) {
            TEST_LOG("Solo timeout (%ds). Exiting.",
                     TEST_SOLO_TIMEOUT_SEC);
            break;
        }

        /* T076: Broadcast phase — first mover broadcasts game-over
           after moves are done, then wait for receipt or timeout */
        if (g_moves_done && g_connected) {
            if (g_initiated && !g_broadcast_sent) {
                PT_Status st = PT_Broadcast(g_ctx, MSG_GAME_OVER,
                                            "GAME_OVER", 9);
                if (st == PT_OK) {
                    TEST_LOG("[BCAST] Sent GAME_OVER");
                    g_broadcast_sent = 1;
                }
            }
            /* Exit after broadcast sent/received + 2s grace */
            if (g_moves_done_time > 0 &&
                test_time_sec() - g_moves_done_time >= 3) {
                break;
            }
        }

        test_sleep_ms(16);
    }

    /* Verdict */
    TEST_LOG("=== Summary ===");
    TEST_LOG("Sent: %d, Received: %d", g_moves_sent, g_moves_received);
    TEST_LOG("Order valid: %s", g_order_valid ? "yes" : "no");
    TEST_LOG("Payload valid: %s", g_payload_valid ? "yes" : "no");
    TEST_LOG("Broadcast: sent=%d received=%d",
             g_broadcast_sent, g_broadcast_received);

    /* T075+T076: require payload validity + broadcast works */
    {
        int broadcast_ok = g_initiated ? g_broadcast_sent
                                       : g_broadcast_received;
        passed = (g_moves_sent == TOTAL_TURNS &&
                  g_moves_received == TOTAL_TURNS &&
                  g_order_valid && g_payload_valid && broadcast_ok);
    }

    if (passed) {
        TEST_LOG("*** PASS ***");
    } else if (!g_ever_connected) {
        TEST_LOG("*** FAIL: no peer connected ***");
    } else {
        TEST_LOG("*** FAIL: sent=%d recv=%d order=%s ***",
                 g_moves_sent, g_moves_received,
                 g_order_valid ? "ok" : "bad");
    }

    PT_Shutdown(g_ctx);
    test_shutdown_logging();
    return passed ? 0 : 1;
}
