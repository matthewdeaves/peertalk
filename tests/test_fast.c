/*
 * test_fast.c -- PeerTalk fast messaging test (Bomberman pattern)
 *
 * Proves: High-frequency UDP messaging, oversized rejection (US3)
 *
 * Flow: Init -> discover -> connect -> test oversize rejection ->
 *       BOTH sides send at SEND_HZ for TEST_SECS while also receiving
 *       -> show stats -> exit
 *
 * PASS: Oversized rejected, both sides sent > 0, both sides
 *       received >= 10, payload valid. (T121: bidirectional)
 * Auto-exit: After TEST_SECS elapsed, or solo timeout
 */

#include "test_common.h"

#define SEND_HZ     60
#define SEND_MS     (1000 / SEND_HZ)
#define TEST_SECS   5

typedef struct {
    unsigned short x;
    unsigned short y;
    unsigned char  direction;
    unsigned char  action;
    unsigned short seq;
    unsigned short timestamp_hi;
    unsigned short timestamp_lo;
} PositionMsg;

static PT_Context *g_ctx;
static int g_connected = 0;
static unsigned long g_sent = 0;
static unsigned long g_received = 0;
static unsigned long g_start_time = 0;
static PT_Peer *g_peer = NULL;
static int g_oversize_rejected = 0;

/* T120: inter-arrival timing for latency measurement */
static unsigned long g_first_recv_ms = 0;
static unsigned long g_last_recv_ms = 0;

static unsigned long test_time_ms_now(void)
{
#ifdef PT_PLATFORM_POSIX
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#else
    /* TickCount = 1/60s ticks. Convert to ms: ticks * 1000 / 60 */
    return (unsigned long)(TickCount() * 1000UL / 60UL);
#endif
}

static void on_discovered(PT_Peer *peer, void *data)
{
    (void)data;
    TEST_LOG("[DISCOVERED] %s", PT_PeerName(peer));
    if (g_connected) return;
    PT_Connect(g_ctx, peer);
}

static void on_connected(PT_Peer *peer, void *data)
{
    (void)data;
    g_connected = 1;
    g_peer = peer;
    g_start_time = test_time_sec();
    test_mark_connected();
    TEST_LOG("[CONNECTED] %s", PT_PeerName(peer));

    /* Test oversized message rejection */
    {
        char big[1500];
        memset(big, 0, sizeof(big));
        if (PT_Send(g_ctx, peer, MSG_POSITION, big, sizeof(big))
                == PT_ERR_SEND_FAILED) {
            TEST_LOG("Oversized UDP correctly rejected");
            g_oversize_rejected = 1;
        } else {
            TEST_WARN("FAIL: oversized not rejected");
        }
    }

    /* T153: 1s post-connect delay on 68k to let MacTCP stabilize */
#if defined(PT_PLATFORM_MACTCP) && defined(__m68k__)
    {
        long dummy;
        TEST_LOG("68k: 1s post-connect stabilization delay...");
        Delay(60, &dummy);
    }
#endif

    /* T121: both sides send and receive */
    TEST_LOG("Sending+receiving at %dHz for %ds...",
             SEND_HZ, TEST_SECS);
}

static int g_payload_valid = 1;

static void on_position(PT_Peer *peer, const void *data,
                         size_t len, void *user)
{
    const PositionMsg *pos;
    (void)peer; (void)user;

    if (len < sizeof(PositionMsg)) return;

    pos = (const PositionMsg *)data;

    /* Validate single-byte fields (T074). Multi-byte fields (x, y, seq)
       have endian issues on cross-platform tests, so only validate
       direction (single byte, always safe) and action. */
    if (pos->direction >= 4 || pos->action != 0) {
        if (g_payload_valid) {
            TEST_WARN("Invalid payload: dir=%d action=%d",
                      pos->direction, pos->action);
        }
        g_payload_valid = 0;
    }

    g_received++;
    {
        unsigned long now_ms = test_time_ms_now();
        if (g_first_recv_ms == 0) g_first_recv_ms = now_ms;
        g_last_recv_ms = now_ms;
    }

    if (g_received % 60 == 0) {
        TEST_LOG("[RECV] #%lu seq=%d", g_received, pos->seq);
    }
}

static void on_disconnected(PT_Peer *peer,
                             PT_DisconnectReason reason,
                             void *data)
{
    (void)data;
    TEST_LOG("[DISCONNECTED] %s (%s)",
             PT_PeerName(peer), test_reason_str(reason));
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
#if !(defined(PT_PLATFORM_MACTCP) && defined(__m68k__))
    unsigned long last_send = 0;
#endif
    unsigned long poll_count = 0;
    int passed;

    test_install_signal_handler();
    test_init_toolbox();
    test_init_logging("test_fast");

    TEST_LOG("=== test_fast ===");
    TEST_LOG("Name: %s", name);

    if (PT_Init(&g_ctx, name) != PT_OK) {
        TEST_WARN("PT_Init FAILED");
        TEST_LOG("*** FAIL: PT_Init failed ***");
        test_shutdown_logging();
        return 1;
    }
    TEST_LOG("PT_Init OK");

    PT_RegisterMessage(g_ctx, MSG_POSITION, PT_FAST);

    PT_OnPeerDiscovered(g_ctx, on_discovered, NULL);
    PT_OnConnected(g_ctx, on_connected, NULL);
    PT_OnDisconnected(g_ctx, on_disconnected, NULL);
    PT_OnMessage(g_ctx, MSG_POSITION, on_position, NULL);
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
        unsigned long now;

        PT_Poll(g_ctx);
        poll_count++;

        if (test_solo_timeout()) {
            TEST_LOG("Solo timeout (%ds). Exiting.",
                     TEST_SOLO_TIMEOUT_SEC);
            break;
        }

        now = test_time_sec();

        /* Check if test duration elapsed */
        if (g_connected && g_start_time > 0 &&
            now - g_start_time >= TEST_SECS) {
            TEST_LOG("Test duration elapsed.");
            break;
        }

        /* T121: BOTH sides send position at SEND_HZ */
        /* T128: 68k MacTCP crashes on 12-msg burst (R40). Throttle
           to 1 send per poll cycle on 68k; keep full rate elsewhere. */
        if (g_connected && g_peer) {
#if defined(PT_PLATFORM_MACTCP) && defined(__m68k__)
            /* 68k MacTCP: 1 send per poll cycle (~60/s at SEND_MS) */
            if (g_running) {
                PositionMsg pos;
                pos.x = (unsigned short)(g_sent % 320);
                pos.y = (unsigned short)(g_sent % 200);
                pos.direction = (unsigned char)(g_sent % 4);
                pos.action = 0;
                pos.seq = (unsigned short)(g_sent & 0xFFFF);
                pos.timestamp_hi = (unsigned short)((now >> 16) & 0xFFFF);
                pos.timestamp_lo = (unsigned short)(now & 0xFFFF);

                /* T153: diagnostic log on first send */
                if (g_sent == 0) {
                    TEST_LOG("68k: first PT_Send (R48 stack fix)...");
                }
                PT_Send(g_ctx, g_peer, MSG_POSITION,
                        &pos, sizeof(pos));
                g_sent++;
                if (g_sent == 1) {
                    TEST_LOG("68k: first send OK, g_sent=1");
                }
            }
#else
            if (now != last_send || g_sent == 0) {
                unsigned long target = (unsigned long)SEND_HZ;
                unsigned long i;
                for (i = 0; i < target / TEST_SECS && g_running; i++) {
                    PositionMsg pos;
                    pos.x = (unsigned short)(g_sent % 320);
                    pos.y = (unsigned short)(g_sent % 200);
                    pos.direction = (unsigned char)(g_sent % 4);
                    pos.action = 0;
                    pos.seq = (unsigned short)(g_sent & 0xFFFF);
                    pos.timestamp_hi = (unsigned short)((now >> 16) & 0xFFFF);
                    pos.timestamp_lo = (unsigned short)(now & 0xFFFF);

                    PT_Send(g_ctx, g_peer, MSG_POSITION,
                            &pos, sizeof(pos));
                    g_sent++;
                }
                last_send = now;
            }
#endif
        }

        test_sleep_ms(SEND_MS);
    }

    /* T153: diagnostic — total sends at loop exit */
    TEST_LOG("Loop exit: g_sent=%lu g_received=%lu polls=%lu",
             g_sent, g_received, poll_count);

    /* Verdict */
    TEST_LOG("=== Summary ===");
    TEST_LOG("Sent: %lu, Received: %lu", g_sent, g_received);
    TEST_LOG("Oversize rejected: %s",
             g_oversize_rejected ? "yes" : "no");
    if (g_sent > 0) {
        unsigned long pct = (g_received * 100) / g_sent;
        TEST_LOG("Delivery: %lu%%", pct);
    }

    TEST_LOG("Payload valid: %s", g_payload_valid ? "yes" : "no");

    /* T120: latency measurement */
    if (g_received > 1 && g_last_recv_ms > g_first_recv_ms) {
        unsigned long span_ms = g_last_recv_ms - g_first_recv_ms;
        unsigned long avg_ms = span_ms / (g_received - 1);
        TEST_LOG("Inter-arrival: %lums total, %lums avg over %lu msgs",
                 span_ms, avg_ms, g_received);
    }

    /* T121: bidirectional — both sides must send AND receive */
    passed = g_oversize_rejected && g_payload_valid &&
             (g_sent > 0) && (g_received >= 10);

    if (passed) {
        TEST_LOG("*** PASS ***");
    } else if (!g_ever_connected) {
        TEST_LOG("*** FAIL: no peer connected ***");
    } else {
        TEST_LOG("*** FAIL: sent=%lu recv=%lu oversize=%s valid=%s ***",
                 g_sent, g_received,
                 g_oversize_rejected ? "y" : "n",
                 g_payload_valid ? "y" : "n");
    }

    PT_Shutdown(g_ctx);
    test_shutdown_logging();
    return passed ? 0 : 1;
}
