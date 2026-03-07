/*
 * test_chat.c -- PeerTalk chat messaging test (variable-size messages)
 *
 * Proves: Variable-size messages, chunking/reassembly (US2 chunking)
 *
 * Both sides auto-connect on discovery. After connecting, both send
 * messages of increasing sizes (10B to 4KB on Mac, 10B to 65KB on POSIX)
 * with 1s spacing. Both validate integrity of received messages.
 *
 * PASS: sent > 0, received >= 1, integrity OK
 * Auto-exit: After all sizes sent + grace period, or solo timeout
 */

#include "test_common.h"

static PT_Context *g_ctx;
static int g_connected = 0;
static int g_msgs_sent = 0;
static int g_msgs_received = 0;
static int g_integrity_ok = 1;
static PT_Peer *g_peer = NULL;

/* Test messages of increasing sizes */
#ifdef PT_PLATFORM_POSIX
static const size_t test_sizes[] = {
    10, 100, 500, 1000, 2000, 4000, 8000, 16000, 32000, 65000
};
#define NUM_TEST_SIZES 10
#else
/* Classic Mac: limited reassembly buffer, no malloc after init */
static const size_t test_sizes[] = {
    10, 100, 500, 1000, 2000, 4000
};
#define NUM_TEST_SIZES 6
#define CHAT_SEND_BUF_SIZE 4096
#endif

static int g_size_index = 0;
static unsigned long g_last_send_time = 0;
static int g_all_sent = 0;
static unsigned long g_all_sent_time = 0;

static void send_chat_msg(void)
{
    size_t msg_len;
    size_t i;
#ifdef PT_PLATFORM_POSIX
    char *buf;
#else
    static char send_buf[CHAT_SEND_BUF_SIZE];
#endif

    if (g_size_index >= NUM_TEST_SIZES || !g_peer) {
        return;
    }

    msg_len = test_sizes[g_size_index];

#ifdef PT_PLATFORM_POSIX
    buf = (char *)malloc(msg_len);
    if (!buf) return;

    for (i = 0; i < msg_len; i++) {
        buf[i] = (char)('A' + (i % 26));
    }

    TEST_LOG("[SEND] %lu bytes", (unsigned long)msg_len);

    if (PT_Send(g_ctx, g_peer, MSG_CHAT, buf, msg_len) != PT_OK) {
        TEST_WARN("Send failed (%lu bytes)",
                  (unsigned long)msg_len);
    } else {
        g_msgs_sent++;
    }

    free(buf);
#else
    /* Classic Mac: use static buffer, skip messages that don't fit */
    if (msg_len > CHAT_SEND_BUF_SIZE) {
        TEST_LOG("[SKIP] %lu bytes > buffer",
                 (unsigned long)msg_len);
        g_size_index++;
        return;
    }

    for (i = 0; i < msg_len; i++) {
        send_buf[i] = (char)('A' + (i % 26));
    }

    TEST_LOG("[SEND] %lu bytes", (unsigned long)msg_len);

    if (PT_Send(g_ctx, g_peer, MSG_CHAT, send_buf, msg_len) != PT_OK) {
        TEST_WARN("Send failed (%lu bytes)",
                  (unsigned long)msg_len);
    } else {
        g_msgs_sent++;
    }
#endif

    g_size_index++;
    g_last_send_time = test_time_sec();
}

static void on_discovered(PT_Peer *peer, void *data)
{
    (void)data;
    TEST_LOG("[DISCOVERED] %s", PT_PeerName(peer));

    if (test_should_connect(peer)) {
        PT_Connect(g_ctx, peer);
    }
}

static void on_connected(PT_Peer *peer, void *data)
{
    (void)data;
    g_connected = 1;
    g_peer = peer;
    test_mark_connected();
    TEST_LOG("[CONNECTED] %s", PT_PeerName(peer));
    TEST_LOG("Sending %d test messages...", NUM_TEST_SIZES);
}

static void on_chat(PT_Peer *peer, const void *data,
                     size_t len, void *user)
{
    const char *msg = (const char *)data;
    int valid = 1;
    size_t i;
    (void)user;
    (void)peer;

    for (i = 0; i < len; i++) {
        if (msg[i] != (char)('A' + (i % 26))) {
            valid = 0;
            break;
        }
    }

    if (!valid) g_integrity_ok = 0;

    g_msgs_received++;
    TEST_LOG("[RECV] %lu bytes (%s)",
             (unsigned long)len, valid ? "VALID" : "CORRUPT");
}

static void on_disconnected(PT_Peer *peer,
                             PT_DisconnectReason reason,
                             void *data)
{
    (void)data;
    TEST_LOG("[DISCONNECTED] %s (%s)",
             PT_PeerName(peer), test_reason_str(reason));
    g_connected = 0;
    g_peer = NULL;
    g_running = 0;
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
    test_init_logging("test_chat");

    TEST_LOG("=== test_chat ===");
    TEST_LOG("Name: %s", name);

    if (PT_Init(&g_ctx, name) != PT_OK) {
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
    PT_OnMessage(g_ctx, MSG_CHAT, on_chat, NULL);
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

        /* Send messages with 1s spacing */
        if (g_connected && !g_all_sent) {
            unsigned long now = test_time_sec();
            if (g_msgs_sent == 0 || now > g_last_send_time) {
                send_chat_msg();
                if (g_size_index >= NUM_TEST_SIZES) {
                    g_all_sent = 1;
                    g_all_sent_time = test_time_sec();
                    TEST_LOG("All %d messages sent.", g_msgs_sent);
                }
            }
        }

        /* Exit after all sent + 5s grace (for receiving peer's msgs) */
        if (g_all_sent && g_all_sent_time > 0 &&
            test_time_sec() - g_all_sent_time >= 5) {
            TEST_LOG("Grace period elapsed.");
            break;
        }

        test_sleep_ms(16);
    }

    /* Verdict */
    TEST_LOG("=== Summary ===");
    TEST_LOG("Sent: %d, Received: %d", g_msgs_sent, g_msgs_received);
    TEST_LOG("Integrity: %s", g_integrity_ok ? "ok" : "CORRUPT");

    passed = g_integrity_ok && (g_msgs_sent > 0) &&
             (g_msgs_received >= 1);

    if (passed) {
        TEST_LOG("*** PASS ***");
    } else if (!g_ever_connected) {
        TEST_LOG("*** FAIL: no peer connected ***");
    } else {
        TEST_LOG("*** FAIL: sent=%d recv=%d integrity=%s ***",
                 g_msgs_sent, g_msgs_received,
                 g_integrity_ok ? "ok" : "bad");
    }

    PT_Shutdown(g_ctx);
    test_shutdown_logging();
    return passed ? 0 : 1;
}
