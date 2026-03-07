/*
 * test_lifecycle.c -- PeerTalk lifecycle test app
 *
 * Proves: Discovery, connection, disconnect reasons, reconnection (US1+US4)
 *
 * Both sides auto-connect on discovery. After connecting, wait 5s,
 * disconnect. After first disconnect, reconnect. After 2nd disconnect, done.
 *
 * PASS criteria: connect_count >= 2 AND disconnect_count >= 2
 */

#include "test_common.h"

static PT_Context *g_ctx;
static int g_connect_count = 0;
static int g_disconnect_count = 0;
static unsigned long g_connect_time = 0;

static void on_discovered(PT_Peer *peer, void *data)
{
    (void)data;
    TEST_LOG("[DISCOVERED] %s", PT_PeerName(peer));

    if (test_should_connect(peer)) {
        TEST_LOG("  -> Connecting to %s...", PT_PeerName(peer));
        PT_Connect(g_ctx, peer);
    }
}

static void on_peer_lost(PT_Peer *peer, void *data)
{
    (void)data;
    TEST_LOG("[PEER LOST] %s", PT_PeerName(peer));

    if (g_connect_count >= 2 && g_disconnect_count >= 2) {
        TEST_LOG("All phases complete, peer gone. Finishing.");
        g_running = 0;
    }
}

static void on_connected(PT_Peer *peer, void *data)
{
    (void)data;
    g_connect_count++;
    g_connect_time = test_time_sec();
    test_mark_connected();

    TEST_LOG("[CONNECTED] %s (#%d, state: %s)",
             PT_PeerName(peer), g_connect_count,
             test_state_str(PT_GetPeerState(peer)));
}

static void on_disconnected(PT_Peer *peer,
                             PT_DisconnectReason reason,
                             void *data)
{
    (void)data;
    g_disconnect_count++;
    TEST_LOG("[DISCONNECTED] %s (reason: %s, #%d)",
             PT_PeerName(peer), test_reason_str(reason),
             g_disconnect_count);

    /* Done after 2 disconnects */
    if (g_disconnect_count >= 2) {
        TEST_LOG("All lifecycle phases complete.");
        g_running = 0;
    }
}

static void on_error(PT_Status error, const char *desc, void *data)
{
    (void)data;
    TEST_WARN("[ERROR] %d: %s", (int)error, desc ? desc : "");
}

int main(int argc, char **argv)
{
    const char *name = test_parse_name(argc, argv);
    unsigned long poll_count = 0;
    int passed;

    test_install_signal_handler();
    test_init_toolbox();
    test_init_logging("test_lifecycle");

    TEST_LOG("=== test_lifecycle ===");
    TEST_LOG("Name: %s", name);

    if (PT_Init(&g_ctx, name) != PT_OK) {
        TEST_WARN("PT_Init FAILED");
        TEST_LOG("*** FAIL: PT_Init failed ***");
        test_shutdown_logging();
        return 1;
    }
    TEST_LOG("PT_Init OK");

    PT_OnPeerDiscovered(g_ctx, on_discovered, NULL);
    PT_OnPeerLost(g_ctx, on_peer_lost, NULL);
    PT_OnConnected(g_ctx, on_connected, NULL);
    PT_OnDisconnected(g_ctx, on_disconnected, NULL);
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

        /* Disconnect after 5 seconds connected */
        if (g_connect_count > 0 &&
            g_disconnect_count < g_connect_count &&
            g_connect_time > 0 &&
            test_time_sec() - g_connect_time >= 5) {

            PT_Peer *peer = PT_GetPeer(g_ctx, 0);
            if (peer && PT_GetPeerState(peer) == PT_PEER_CONNECTED) {
                TEST_LOG("--- Disconnecting (#%d) ---",
                         g_disconnect_count + 1);
                PT_Disconnect(g_ctx, peer);
            }
        }

        test_sleep_ms(16);
    }

    /* Verdict */
    TEST_LOG("=== Summary ===");
    TEST_LOG("Connects: %d, Disconnects: %d",
             g_connect_count, g_disconnect_count);

    passed = (g_connect_count >= 2) && (g_disconnect_count >= 2);

    if (passed) {
        TEST_LOG("*** PASS ***");
    } else if (!g_ever_connected) {
        TEST_LOG("*** FAIL: no peer connected ***");
    } else {
        TEST_LOG("*** FAIL: conn=%d disc=%d ***",
                 g_connect_count, g_disconnect_count);
    }

    PT_Shutdown(g_ctx);
    test_shutdown_logging();
    return passed ? 0 : 1;
}
