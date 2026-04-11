/*
 * test_multi.c -- PeerTalk multi-peer test (N-way discovery + broadcast)
 *
 * Proves: Multi-peer discovery, N-way connections, PT_Broadcast to all
 *
 * All peers auto-connect on discovery. Once connections stabilize,
 * each peer broadcasts "HELLO from <name>" and collects broadcasts
 * from all others. Then disconnects cleanly.
 *
 * PASS: connected >= 1, broadcast_recv >= expected_peers, disconnects OK
 * Dynamic peer counting -- works with 2, 3, or 4 peers.
 */

#include "test_common.h"

static PT_Context *g_ctx;
static int g_num_discovered = 0;
static int g_num_connected = 0;
static int g_num_broadcast_recv = 0;
static int g_num_disconnected = 0;
static int g_broadcast_sent = 0;
static int g_expected_peers = 0;  /* set after discovery settles */

static const char *g_name = "Unnamed";

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static const char *safe_peer_name(const PT_Peer *p)
{
    const char *n;
    if (!p) return "(unknown)";
    n = PT_PeerName(p);
    if (!n || n[0] == '\0') return "(unknown)";
    return n;
}

/* ------------------------------------------------------------------ */
/* Callbacks                                                           */
/* ------------------------------------------------------------------ */

static void on_discovered(PT_Peer *peer, void *data)
{
    (void)data;
    g_num_discovered++;
    TEST_LOG("[DISCOVERED] %s @ %s (total=%d)",
             safe_peer_name(peer), PT_PeerAddress(peer),
             g_num_discovered);

    if (test_should_connect(peer)) {
        PT_Connect(g_ctx, peer);
    }
}

static void on_connected(PT_Peer *peer, void *data)
{
    (void)data;
    g_num_connected++;
    test_mark_connected();
    TEST_LOG("[CONNECTED] %s (total=%d)",
             safe_peer_name(peer), g_num_connected);
}

static void on_disconnected(PT_Peer *peer,
                             PT_DisconnectReason reason,
                             void *data)
{
    (void)data;
    g_num_disconnected++;
    TEST_LOG("[DISCONNECTED] %s (%s) (total=%d)",
             safe_peer_name(peer), test_reason_str(reason),
             g_num_disconnected);
}

static void on_broadcast_recv(PT_Peer *peer, const void *data,
                               size_t len, void *user)
{
    const char *msg = (const char *)data;
    (void)user;

    if (len >= 5 && msg[0] == 'H' && msg[1] == 'E' &&
        msg[2] == 'L' && msg[3] == 'L' && msg[4] == 'O') {
        g_num_broadcast_recv++;
        TEST_LOG("[BROADCAST] from %s: %.*s (count=%d)",
                 safe_peer_name(peer), (int)(len < 64 ? len : 64),
                 msg, g_num_broadcast_recv);
    } else {
        TEST_WARN("[BROADCAST] unexpected payload from %s (%lu bytes)",
                  safe_peer_name(peer), (unsigned long)len);
    }
}

static void on_error(PT_Peer *peer, PT_Status error, const char *desc,
                     void *data)
{
    (void)data;
    TEST_WARN("[ERROR] peer=%s code=%d: %s",
              safe_peer_name(peer), (int)error, desc ? desc : "");
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    int passed;
    int i, count;

    g_name = test_parse_name(argc, argv);

    test_install_signal_handler();
    test_init_toolbox();
    test_init_logging("test_multi");

    TEST_LOG("=== test_multi ===");
    TEST_LOG("Name: %s", g_name);

    /* Phase 1: Init */
    if (PT_Init(&g_ctx, g_name) != PT_OK) {
        TEST_WARN("PT_Init FAILED");
        TEST_LOG("*** FAIL: PT_Init failed ***");
        test_shutdown_logging();
        return 1;
    }
    TEST_LOG("PT_Init OK");

    PT_RegisterMessage(g_ctx, MSG_CHAT, PT_RELIABLE);

    PT_OnPeerDiscovered(g_ctx, on_discovered, NULL);
    PT_OnConnected(g_ctx, on_connected, NULL);
    PT_OnDisconnected(g_ctx, on_disconnected, NULL);
    PT_OnMessage(g_ctx, MSG_CHAT, on_broadcast_recv, NULL);
    PT_OnError(g_ctx, on_error, NULL);

    if (PT_StartDiscovery(g_ctx) != PT_OK) {
        TEST_WARN("PT_StartDiscovery FAILED");
        TEST_LOG("*** FAIL: Discovery failed ***");
        PT_Shutdown(g_ctx);
        test_shutdown_logging();
        return 1;
    }
    TEST_LOG("Discovery started, waiting for peers...");
    test_mark_start();

    /* Phase 2: Wait for peers to discover and connect.
     * Timer starts when the first peer connects (not at launch), so
     * fast machines (POSIX) wait for slow ones (Mac SE ~30s init).
     * After first connect, wait 30s more for additional peers. */
    {
        unsigned long connect_start = 0;
        unsigned long wait_after_first = 30;
        unsigned long last_log = 0;
        unsigned long abs_start = test_time_sec();

        TEST_LOG("Waiting for first peer, then %lus for others...",
                 (unsigned long)wait_after_first);

        while (g_running) {
            unsigned long now = test_time_sec();
            unsigned long elapsed = now - abs_start;
            PT_Poll(g_ctx);

            /* Start countdown from first connection */
            if (g_num_connected > 0 && connect_start == 0) {
                connect_start = now;
                TEST_LOG("First peer connected! Waiting %lus for more...",
                         (unsigned long)wait_after_first);
            }

            /* Log progress every 10 seconds */
            if (elapsed >= last_log + 10) {
                last_log = elapsed;
                TEST_LOG("  %lus: discovered=%d connected=%d",
                         elapsed, g_num_discovered, g_num_connected);
            }

            /* Exit after wait_after_first seconds from first connect */
            if (connect_start > 0 && now - connect_start >= wait_after_first) {
                TEST_LOG("Discovery window closed: %d discovered, %d connected",
                         g_num_discovered, g_num_connected);
                break;
            }

            /* Solo timeout: no peers at all after 60s */
            if (test_solo_timeout()) {
                TEST_LOG("Solo timeout (%ds). No peers found.",
                         TEST_SOLO_TIMEOUT_SEC);
                break;
            }

            test_sleep_ms(16);
        }
    }

    /* Count currently connected peers (not cumulative connects) */
    {
        int pc = PT_GetPeerCount(g_ctx);
        int ci;
        g_expected_peers = 0;
        for (ci = 0; ci < pc; ci++) {
            PT_Peer *p = PT_GetPeer(g_ctx, ci);
            if (p && PT_GetPeerState(p) == PT_PEER_CONNECTED) {
                g_expected_peers++;
            }
        }
    }
    TEST_LOG("Expected peers: %d (of %d discovered)",
             g_expected_peers, g_num_discovered);

    /* Phase 3: Broadcast */
    if (g_expected_peers > 0 && g_running) {
        unsigned long bcast_start;
        char hello_buf[80];
        size_t hello_len;

        /* Build hello message */
        hello_len = 0;
        {
            const char *prefix = "HELLO from ";
            size_t pi;
            for (pi = 0; prefix[pi] && hello_len < sizeof(hello_buf) - 1; pi++) {
                hello_buf[hello_len++] = prefix[pi];
            }
            for (pi = 0; g_name[pi] && hello_len < sizeof(hello_buf) - 1; pi++) {
                hello_buf[hello_len++] = g_name[pi];
            }
            hello_buf[hello_len] = '\0';
        }

        TEST_LOG("Broadcasting: \"%s\"", hello_buf);
        if (PT_Broadcast(g_ctx, MSG_CHAT, hello_buf, hello_len) == PT_OK) {
            g_broadcast_sent = 1;
            TEST_LOG("Broadcast sent OK");
        } else {
            TEST_WARN("Broadcast FAILED");
        }

        /* Poll for 10s collecting broadcasts */
        bcast_start = test_time_sec();
        while (g_running && test_time_sec() - bcast_start < 10) {
            PT_Poll(g_ctx);
            test_sleep_ms(16);
        }
    }

    /* Phase 4: Disconnect */
    TEST_LOG("Disconnecting all peers...");
    count = PT_GetPeerCount(g_ctx);
    for (i = 0; i < count; i++) {
        PT_Peer *p = PT_GetPeer(g_ctx, i);
        if (p && PT_GetPeerState(p) == PT_PEER_CONNECTED) {
            TEST_LOG("Disconnecting %s", safe_peer_name(p));
            PT_Disconnect(g_ctx, p);
        }
    }

    /* Grace period for disconnect callbacks */
    {
        unsigned long dc_start = test_time_sec();
        while (g_running && test_time_sec() - dc_start < 5) {
            PT_Poll(g_ctx);
            test_sleep_ms(16);
        }
    }

    /* Phase 5: Verdict */
    TEST_LOG("=== Summary ===");
    TEST_LOG("Discovered: %d", g_num_discovered);
    TEST_LOG("Connected: %d", g_num_connected);
    TEST_LOG("Expected peers: %d", g_expected_peers);
    TEST_LOG("Broadcasts recv: %d", g_num_broadcast_recv);
    TEST_LOG("Disconnected: %d", g_num_disconnected);
    TEST_LOG("Broadcast sent: %s", g_broadcast_sent ? "yes" : "no");

    passed = (g_expected_peers >= 1) &&
             (g_num_broadcast_recv >= g_expected_peers) &&
             g_broadcast_sent;

    if (passed) {
        TEST_LOG("*** PASS ***");
    } else if (!g_ever_connected) {
        TEST_LOG("*** FAIL: no peer connected ***");
    } else {
        TEST_LOG("*** FAIL: connected=%d bcast_recv=%d(need>=%d) dc=%d ***",
                 g_num_connected, g_num_broadcast_recv, g_expected_peers,
                 g_num_disconnected);
    }

    PT_Shutdown(g_ctx);
    test_shutdown_logging();
    return passed ? 0 : 1;
}
