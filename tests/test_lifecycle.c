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

static const char *test_state_str(PT_PeerState state)
{
    switch (state) {
        case PT_PEER_DISCOVERED:  return "DISCOVERED";
        case PT_PEER_CONNECTED:   return "CONNECTED";
        case PT_PEER_DISCONNECTED: return "DISCONNECTED";
        default: return "UNKNOWN";
    }
}

static PT_Context *g_ctx;
static int g_connect_count = 0;
static int g_disconnect_count = 0;
static unsigned long g_connect_time = 0;
static int g_stop_phase_done = 0;
static unsigned long g_stop_phase_start = 0;
static int g_peers_lost = 0;
static int g_waiting_peer_lost = 0;
static unsigned long g_peer_lost_wait_start = 0;

static int has_dot(const char *s)
{
    while (*s) { if (*s == '.') return 1; s++; }
    return 0;
}

static void on_discovered(PT_Peer *peer, void *data)
{
    const char *addr = PT_PeerAddress(peer);
    (void)data;
    TEST_LOG("[DISCOVERED] %s@%s", PT_PeerName(peer), addr);

    if (!addr[0] || !has_dot(addr)) {
        TEST_LOG("*** FAIL: PT_PeerAddress empty or invalid for discovered peer ***");
    }

    if (test_should_connect(peer)) {
        TEST_LOG("  -> Connecting to %s...", PT_PeerName(peer));
        PT_Connect(g_ctx, peer);
    }
}

static void on_peer_lost(PT_Peer *peer, void *data)
{
    (void)data;
    g_peers_lost++;
    TEST_LOG("[PEER LOST] %s (count=%d)", PT_PeerName(peer), g_peers_lost);
}

static void on_connected(PT_Peer *peer, void *data)
{
    const char *addr = PT_PeerAddress(peer);
    (void)data;
    g_connect_count++;
    g_connect_time = test_time_sec();
    test_mark_connected();

    TEST_LOG("[CONNECTED] %s@%s (#%d, state: %s)",
             PT_PeerName(peer), addr, g_connect_count,
             test_state_str(PT_GetPeerState(peer)));

    if (!addr[0] || !has_dot(addr)) {
        TEST_LOG("*** FAIL: PT_PeerAddress empty or invalid for connected peer ***");
    }
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

    /* After 2 disconnects, wait for peer-lost */
    if (g_disconnect_count >= 2) {
        TEST_LOG("All lifecycle phases complete.");
        if (!g_waiting_peer_lost) {
            g_waiting_peer_lost = 1;
            g_peer_lost_wait_start = test_time_sec();
            PT_StopDiscovery(g_ctx);
            TEST_LOG("Waiting for peer-lost timeout...");
        }
    }
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
    test_remote_log_enable(g_ctx);

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

        /* Peer-lost wait: exit after peer-lost fires or 20s timeout */
        if (g_waiting_peer_lost) {
            if (g_peers_lost > 0) {
                TEST_LOG("Peer-lost received! Count: %d", g_peers_lost);
                g_running = 0;
            } else if (g_peer_lost_wait_start > 0 &&
                       test_time_sec() - g_peer_lost_wait_start >= 20) {
                TEST_LOG("Peer-lost wait timed out (20s).");
                g_running = 0;
            }
        }

        /* Stop/start discovery phase (after 1st disconnect, before reconnect) */
        if (g_disconnect_count == 1 && !g_stop_phase_done &&
            g_connect_count == g_disconnect_count) {
            if (g_stop_phase_start == 0) {
                TEST_LOG("--- Stop discovery phase ---");
                PT_StopDiscovery(g_ctx);
                g_stop_phase_start = test_time_sec();
            } else if (test_time_sec() - g_stop_phase_start >= 5) {
                PT_SetName(g_ctx, "Renamed");
                TEST_LOG("Name changed to 'Renamed'");
                PT_StartDiscovery(g_ctx);
                g_stop_phase_done = 1;
                TEST_LOG("Discovery restarted");
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
