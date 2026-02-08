/**
 * @file test_latency.c
 * @brief MacTCP Latency Test Application
 *
 * Measures round-trip time (RTT) to a POSIX peer.
 *
 * Protocol:
 *   Mac sends:  [4-byte sequence][4-byte timestamp_ticks][payload]
 *   POSIX echoes the message back unchanged
 *   Mac calculates: RTT = current_ticks - timestamp_ticks
 *
 * Results are logged via PT_Log and displayed in summary.
 *
 * Build with Retro68:
 *   make -f Makefile.retro68 PLATFORM=mactcp test_latency
 */

#include <stdio.h>
#include <string.h>

/* Retro68 / Classic Mac includes */
#include <Quickdraw.h>
#include <Windows.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <OSUtils.h>

#include "peertalk.h"
#include "pt_log.h"

/* Log streaming helper - implementation in this file */
#define LOG_STREAM_IMPLEMENTATION
#include "log_stream.h"

/* ========================================================================== */
/* Configuration                                                               */
/* ========================================================================== */

#define TEST_DURATION_TICKS  (60 * 60)    /* 60 seconds */
#define PING_INTERVAL_TICKS  6            /* ~100ms between pings */
#define MAX_SAMPLES          1000         /* Maximum RTT samples to collect */

/* Message sizes to test (in bytes, including 8-byte header) */
static const int g_test_sizes[] = { 16, 64, 256, 1024, 4096 };
#define NUM_TEST_SIZES  (sizeof(g_test_sizes) / sizeof(g_test_sizes[0]))

/* ========================================================================== */
/* Latency Statistics                                                          */
/* ========================================================================== */

typedef struct {
    int             message_size;
    unsigned long   samples[MAX_SAMPLES];
    int             sample_count;
    unsigned long   min_ticks;
    unsigned long   max_ticks;
    unsigned long   total_ticks;
    int             sent_count;
    int             recv_count;
    int             lost_count;
} LatencyStats;

typedef struct {
    /* Current test state */
    int             current_size_idx;
    int             test_complete;

    /* Pending ping */
    uint32_t        pending_seq;
    unsigned long   pending_sent_time;
    int             pending;

    /* Per-size statistics */
    LatencyStats    stats[NUM_TEST_SIZES];

} LatencyTest;

/* ========================================================================== */
/* Globals                                                                     */
/* ========================================================================== */

static PeerTalk_Context *g_ctx = NULL;
static PT_Log *g_log = NULL;
static PeerTalk_PeerID g_connected_peer = 0;
static LatencyTest g_test;
static unsigned long g_test_start = 0;
static unsigned long g_last_ping = 0;
static int g_running = 1;

/* ========================================================================== */
/* Utility Functions                                                           */
/* ========================================================================== */

/**
 * Convert ticks to milliseconds (60 ticks/sec on Mac)
 */
static unsigned long ticks_to_ms(unsigned long ticks)
{
    return (ticks * 1000UL) / 60UL;
}

/**
 * Initialize test state
 */
static void init_test(void)
{
    int i;

    memset(&g_test, 0, sizeof(g_test));

    for (i = 0; i < (int)NUM_TEST_SIZES; i++) {
        g_test.stats[i].message_size = g_test_sizes[i];
        g_test.stats[i].min_ticks = 0xFFFFFFFF;
    }

    g_test.current_size_idx = 0;
}

/* ========================================================================== */
/* Ping/Pong Protocol                                                          */
/* ========================================================================== */

/**
 * Ping message format:
 *   [0-3]  uint32_t sequence number
 *   [4-7]  uint32_t timestamp (TickCount when sent)
 *   [8+]   payload (filled with pattern)
 */
#define PING_HEADER_SIZE  8

static void send_ping(void)
{
    LatencyStats *stats;
    uint8_t buffer[4096];
    int size;
    unsigned long now;
    uint32_t seq;
    int i;

    if (g_connected_peer == 0 || g_test.pending || g_test.test_complete)
        return;

    stats = &g_test.stats[g_test.current_size_idx];
    size = stats->message_size;

    if (size > (int)sizeof(buffer))
        size = sizeof(buffer);

    now = TickCount();
    seq = stats->sent_count;

    /* Build ping message */
    memcpy(&buffer[0], &seq, sizeof(seq));
    memcpy(&buffer[4], &now, sizeof(now));

    /* Fill payload with pattern */
    for (i = PING_HEADER_SIZE; i < size; i++) {
        buffer[i] = (uint8_t)(i & 0xFF);
    }

    if (PeerTalk_Send(g_ctx, g_connected_peer, buffer, size) == PT_OK) {
        g_test.pending_seq = seq;
        g_test.pending_sent_time = now;
        g_test.pending = 1;
        stats->sent_count++;

        PT_LOG_DEBUG(g_log, PT_LOG_CAT_APP1,
            "PING seq=%lu size=%d sent", (unsigned long)seq, size);
    }
}

static void handle_pong(const void *data, uint16_t len)
{
    LatencyStats *stats;
    uint32_t recv_seq;
    uint32_t recv_time;
    unsigned long now, rtt;

    if (len < PING_HEADER_SIZE)
        return;

    /* Parse pong */
    memcpy(&recv_seq, data, sizeof(recv_seq));
    memcpy(&recv_time, (const uint8_t *)data + 4, sizeof(recv_time));

    now = TickCount();
    rtt = now - recv_time;

    stats = &g_test.stats[g_test.current_size_idx];

    /* Check if this is our pending ping */
    if (g_test.pending && recv_seq == g_test.pending_seq) {
        g_test.pending = 0;

        /* Record sample */
        if (stats->sample_count < MAX_SAMPLES) {
            stats->samples[stats->sample_count++] = rtt;
        }
        stats->recv_count++;
        stats->total_ticks += rtt;

        if (rtt < stats->min_ticks) stats->min_ticks = rtt;
        if (rtt > stats->max_ticks) stats->max_ticks = rtt;

        PT_LOG_DEBUG(g_log, PT_LOG_CAT_APP1,
            "PONG seq=%lu rtt=%lu ticks (%lu ms)",
            (unsigned long)recv_seq, rtt, ticks_to_ms(rtt));
    } else {
        PT_LOG_WARN(g_log, PT_LOG_CAT_APP1,
            "PONG seq mismatch: got %lu expected %lu",
            (unsigned long)recv_seq, (unsigned long)g_test.pending_seq);
    }
}

/* ========================================================================== */
/* Test Control                                                                */
/* ========================================================================== */

static void advance_test(void)
{
    LatencyStats *stats = &g_test.stats[g_test.current_size_idx];

    /* Log results for this size */
    if (stats->recv_count > 0) {
        unsigned long avg_ticks = stats->total_ticks / stats->recv_count;

        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "SIZE %d: min=%lu max=%lu avg=%lu ms (sent=%d recv=%d lost=%d)",
            stats->message_size,
            ticks_to_ms(stats->min_ticks),
            ticks_to_ms(stats->max_ticks),
            ticks_to_ms(avg_ticks),
            stats->sent_count, stats->recv_count, stats->lost_count);
    }

    /* Move to next size */
    g_test.current_size_idx++;
    if (g_test.current_size_idx >= (int)NUM_TEST_SIZES) {
        g_test.test_complete = 1;
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "All latency tests complete");
    } else {
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "Starting size %d test",
            g_test_sizes[g_test.current_size_idx]);
    }

    g_test.pending = 0;
    g_test_start = TickCount();
}

static void check_pending_timeout(void)
{
    unsigned long now = TickCount();

    if (g_test.pending && (now - g_test.pending_sent_time) > 180) {
        /* 3 second timeout - mark as lost */
        LatencyStats *stats = &g_test.stats[g_test.current_size_idx];
        stats->lost_count++;
        g_test.pending = 0;

        PT_LOG_WARN(g_log, PT_LOG_CAT_APP1,
            "PING seq=%lu timeout", (unsigned long)g_test.pending_seq);
    }
}

/* ========================================================================== */
/* Results                                                                     */
/* ========================================================================== */

static void print_results(void)
{
    int i;
    LatencyStats *stats;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "LATENCY TEST RESULTS");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    for (i = 0; i < (int)NUM_TEST_SIZES; i++) {
        stats = &g_test.stats[i];

        if (stats->recv_count > 0) {
            unsigned long avg_ticks = stats->total_ticks / stats->recv_count;
            int loss_pct = (stats->sent_count > 0) ?
                          ((stats->lost_count * 100) / stats->sent_count) : 0;

            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "%4d bytes: min=%3lu avg=%3lu max=%3lu ms (loss=%d%%)",
                stats->message_size,
                ticks_to_ms(stats->min_ticks),
                ticks_to_ms(avg_ticks),
                ticks_to_ms(stats->max_ticks),
                loss_pct);
        } else {
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "%4d bytes: NO DATA (sent=%d lost=%d)",
                stats->message_size,
                stats->sent_count, stats->lost_count);
        }
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
}

/* ========================================================================== */
/* Callbacks                                                                   */
/* ========================================================================== */

static void on_peer_discovered(PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer,
                                void *user_data)
{
    const char *name;
    (void)user_data;

    name = PeerTalk_GetPeerName(ctx, peer->name_idx);
    if (!name) name = "(unknown)";

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "DISCOVERED: \"%s\" at %lu.%lu.%lu.%lu:%u",
        name,
        (peer->address >> 24) & 0xFF,
        (peer->address >> 16) & 0xFF,
        (peer->address >> 8) & 0xFF,
        peer->address & 0xFF,
        (unsigned)peer->port);

    /* Auto-connect to first discovered peer */
    if (g_connected_peer == 0) {
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Connecting to peer...");
        PeerTalk_Connect(ctx, peer->id);
    }
}

static void on_peer_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                               void *user_data)
{
    (void)ctx;
    (void)user_data;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "CONNECTED to peer %u", (unsigned)peer_id);
    g_connected_peer = peer_id;

    /* Start first test */
    g_test_start = TickCount();
    g_last_ping = 0;
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Starting latency test with size %d", g_test_sizes[0]);
}

static void on_peer_disconnected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                  int reason, void *user_data)
{
    (void)ctx;
    (void)user_data;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "DISCONNECTED from peer %u (reason=%d)", (unsigned)peer_id, reason);
    g_connected_peer = 0;
}

static void on_message_received(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                 const void *data, uint16_t len, void *user_data)
{
    (void)ctx;
    (void)peer_id;
    (void)user_data;

    handle_pong(data, len);
}

/* ========================================================================== */
/* Toolbox Initialization                                                      */
/* ========================================================================== */

static void init_toolbox(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
}

/* ========================================================================== */
/* Main                                                                        */
/* ========================================================================== */

int main(void)
{
    PeerTalk_Config config;
    PeerTalk_Callbacks callbacks;
    EventRecord event;
    unsigned long now;
    int samples_per_size = 100;  /* Collect 100 samples per message size */

    init_toolbox();

    /* Create PT_Log */
    g_log = PT_LogCreate();
    if (g_log) {
        PT_LogSetLevel(g_log, PT_LOG_DEBUG);
        PT_LogSetCategories(g_log, 0xFFFF);
        PT_LogSetFile(g_log, "PT_Latency");

        /* Initialize log streaming (captures logs for sending to partner) */
        log_stream_init(g_log);
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "PeerTalk Latency Test");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Version: %s", PeerTalk_Version());
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    /* Initialize test state */
    init_test();

    /* Configure PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "MacLatency", PT_MAX_PEER_NAME);
    config.max_peers = 4;
    config.discovery_port = 7353;
    config.tcp_port = 7354;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Initializing PeerTalk...");
    g_ctx = PeerTalk_Init(&config);
    if (!g_ctx) {
        PT_LOG_ERR(g_log, PT_LOG_CAT_APP1, "FAILED to initialize PeerTalk!");
        goto cleanup;
    }

    /* Set callbacks */
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_connected = on_peer_connected;
    callbacks.on_peer_disconnected = on_peer_disconnected;
    callbacks.on_message_received = on_message_received;
    PeerTalk_SetCallbacks(g_ctx, &callbacks);

    /* Start discovery */
    if (PeerTalk_StartDiscovery(g_ctx) != 0) {
        PT_LOG_ERR(g_log, PT_LOG_CAT_APP1, "FAILED to start discovery!");
        goto cleanup;
    }

    /* Start listening */
    if (PeerTalk_StartListening(g_ctx) != 0) {
        PT_LOG_ERR(g_log, PT_LOG_CAT_APP1, "FAILED to start listening!");
        goto cleanup;
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Waiting for peer...");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Press any key to exit.");

    /* Main loop */
    while (g_running) {
        /* Check for user input */
        if (WaitNextEvent(everyEvent, &event, 1, NULL)) {
            if (event.what == keyDown || event.what == mouseDown) {
                g_running = 0;
                break;
            }
        }

        PeerTalk_Poll(g_ctx);

        now = TickCount();

        /* Test logic */
        if (g_connected_peer && !g_test.test_complete) {
            LatencyStats *stats = &g_test.stats[g_test.current_size_idx];

            /* Check for pending timeout */
            check_pending_timeout();

            /* Time to send next ping? */
            if (!g_test.pending && (now - g_last_ping) >= PING_INTERVAL_TICKS) {
                send_ping();
                g_last_ping = now;
            }

            /* Check if we have enough samples for this size */
            if (stats->recv_count >= samples_per_size) {
                advance_test();
            }
        }

        /* Check for overall test completion */
        if (g_test.test_complete && !g_log_stream.streaming && !g_log_stream.complete) {
            print_results();

            /* Stream logs to partner before exiting */
            if (g_connected_peer) {
                PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                    "Streaming %lu bytes of logs to partner...",
                    (unsigned long)g_log_stream.length);
                log_stream_send(g_ctx, g_connected_peer);
            } else {
                g_running = 0;
            }
        }

        /* Wait for log streaming to complete */
        if (g_log_stream.complete && g_test.test_complete) {
            if (log_stream_bytes_sent() > 0) {
                PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                    "Log streaming complete: %lu bytes sent",
                    (unsigned long)log_stream_bytes_sent());
            }
            g_running = 0;
        }
    }

cleanup:
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "TEST EXITING - cleaning up...");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    log_stream_cleanup();
    if (g_ctx) {
        PeerTalk_Shutdown(g_ctx);
    }
    if (g_log) {
        PT_LogDestroy(g_log);
    }

    return 0;
}
