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
#include "status_window.h"
#include "table_ui.h"

/* Log streaming helper - implementation in this file */
#define LOG_STREAM_IMPLEMENTATION
#include "log_stream.h"
/* #include "test_cleanup.h" */

/* ========================================================================== */
/* Configuration                                                               */
/* ========================================================================== */

#define TEST_DURATION_TICKS  (60 * 60)    /* 60 seconds */
#define PING_INTERVAL_TICKS  6            /* ~100ms between pings */
#define MAX_SAMPLES          1000         /* Maximum RTT samples to collect */
#define DISCOVERY_TIMEOUT_TICKS (60 * 60) /* 60 seconds to find a peer */
#define MAX_CONNECT_RETRIES  5            /* Give up after this many failures */

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
static PeerTalk_BufferPool *g_buffer_pool = NULL;
static PeerTalk_PeerID g_connected_peer = 0;
static PeerTalk_PeerID g_target_peer = 0;
static LatencyTest g_test;
static unsigned long g_test_start = 0;
static unsigned long g_last_ping = 0;
static unsigned long g_discovery_start = 0;
static int g_running = 1;
static int g_connect_failures = 0;
static TableUI g_table;

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
    int total_sent = 0, total_recv = 0, total_lost = 0;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "LATENCY TEST RESULTS");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    /* Human-readable summary */
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Size   Min  Avg  Max  Sent Recv Lost");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "----  ---- ---- ---- ---- ---- ----");

    for (i = 0; i < (int)NUM_TEST_SIZES; i++) {
        stats = &g_test.stats[i];
        total_sent += stats->sent_count;
        total_recv += stats->recv_count;
        total_lost += stats->lost_count;

        if (stats->recv_count > 0) {
            unsigned long avg_ticks = stats->total_ticks / stats->recv_count;
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "%4dB %4lu %4lu %4lu %4d %4d %4d",
                stats->message_size,
                ticks_to_ms(stats->min_ticks),
                ticks_to_ms(avg_ticks),
                ticks_to_ms(stats->max_ticks),
                stats->sent_count, stats->recv_count, stats->lost_count);
        } else {
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "%4dB   --   --   -- %4d %4d %4d",
                stats->message_size,
                stats->sent_count, stats->recv_count, stats->lost_count);
        }
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "----  ---- ---- ---- ---- ---- ----");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Total              %4d %4d %4d", total_sent, total_recv, total_lost);

    /* Machine-parseable section for perf_partner metrics extraction */
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "======== LATENCY METRICS ========");
    for (i = 0; i < (int)NUM_TEST_SIZES; i++) {
        stats = &g_test.stats[i];
        if (stats->recv_count > 0) {
            unsigned long avg_ticks = stats->total_ticks / stats->recv_count;
            /* Format: SIZE <n>: min=<x> max=<y> avg=<z> ms (sent=<a> recv=<b> lost=<c>) */
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "SIZE %d: min=%lu max=%lu avg=%lu ms (sent=%d recv=%d lost=%d)",
                stats->message_size,
                ticks_to_ms(stats->min_ticks),
                ticks_to_ms(stats->max_ticks),
                ticks_to_ms(avg_ticks),
                stats->sent_count, stats->recv_count, stats->lost_count);
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

    /* Track target peer and auto-connect */
    if (g_target_peer == 0) {
        g_target_peer = peer->id;
    }

    if (g_connected_peer == 0 && g_target_peer == peer->id) {
        PeerTalk_Error err;
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Connecting to peer...");
        err = PeerTalk_Connect(ctx, peer->id);
        if (err != PT_OK) {
            g_connect_failures++;
            PT_LOG_ERR(g_log, PT_LOG_CAT_APP1,
                "Connect failed: %d (attempt %d/%d)",
                (int)err, g_connect_failures, MAX_CONNECT_RETRIES);
            if (g_connect_failures >= MAX_CONNECT_RETRIES) {
                PT_LOG_ERR(g_log, PT_LOG_CAT_APP1,
                    "Too many connection failures, giving up");
                g_test.test_complete = 1;
            }
        }
    }
}

static void on_peer_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                               void *user_data)
{
    PeerTalk_Capabilities caps;
    (void)user_data;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "CONNECTED to peer %u", (unsigned)peer_id);
    g_connected_peer = peer_id;

    /* Log peer capabilities */
    if (PeerTalk_GetPeerCapabilities(ctx, peer_id, &caps) == PT_OK) {
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "Peer capabilities: max=%u chunk=%u recv_buf=%u optimal=%u",
            (unsigned)caps.max_message_size,
            (unsigned)caps.preferred_chunk,
            (unsigned)caps.recv_buffer_size,
            (unsigned)caps.optimal_chunk);
    }

    /* Start first test */
    g_test_start = TickCount();
    g_last_ping = 0;
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Starting latency test with size %d", g_test_sizes[0]);

    /* Update status window */
    status_clear();
    status_linef("Connected to peer %u", (unsigned)peer_id);
    status_linef("Starting test: %d bytes", g_test_sizes[0]);
    status_line("");
}

static void on_peer_disconnected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                  int reason, void *user_data)
{
    (void)user_data;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "DISCONNECTED from peer %u (reason=%d)", (unsigned)peer_id, reason);
    g_connected_peer = 0;

    /* If test not complete, try to reconnect */
    if (!g_test.test_complete && g_target_peer != 0) {
        PeerTalk_Error err = PeerTalk_Connect(ctx, g_target_peer);
        if (err != PT_OK) {
            g_connect_failures++;
            PT_LOG_ERR(g_log, PT_LOG_CAT_APP1,
                "Reconnect failed: %d (attempt %d/%d)",
                (int)err, g_connect_failures, MAX_CONNECT_RETRIES);
            if (g_connect_failures >= MAX_CONNECT_RETRIES) {
                PT_LOG_ERR(g_log, PT_LOG_CAT_APP1,
                    "Too many connection failures, giving up");
                g_test.test_complete = 1;
            }
        }
    }
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

    /**
     * CRITICAL: PeerTalk_Bootstrap() MUST be called BEFORE init_toolbox().
     *
     * PeerTalk_Bootstrap() does three things in order:
     *   1. MaxApplZone() - extends heap to maximum size
     *   2. MoreMasters() - pre-allocates master pointer blocks
     *   3. Allocates TCP receive buffers while heap is contiguous
     *
     * This is the key optimization for MacTCP throughput. By allocating
     * before InitGraf/InitFonts/etc., we get much larger contiguous blocks:
     *   - Before toolbox: typically 16-32KB buffers on 8MB Mac
     *   - After toolbox: often only 4KB buffers due to fragmentation
     *
     * Larger buffers improve receive throughput via MacTCP's 25% threshold:
     *   - 4KB buffer = 1KB threshold (slow)
     *   - 16KB buffer = 4KB threshold (4x better)
     *   - 32KB buffer = 8KB threshold (8x better)
     */
    g_buffer_pool = PeerTalk_Bootstrap(4);  /* 4 peers, auto-size */

    /* NOW safe to initialize Toolbox */
    init_toolbox();

    /* Initialize status window for user feedback */
    status_init("PeerTalk Latency Test");
    status_line("Initializing...");

    /* Create PT_Log */
    g_log = PT_LogCreate();
    if (g_log) {
        PT_LogSetLevel(g_log, PT_LOG_DEBUG);
        PT_LogSetCategories(g_log, 0xFFFF);
        PT_LogSetFile(g_log, "PT_Latency");
        PT_LogClearFile(g_log);  /* Fresh log each run */

        /* Initialize log streaming (captures logs for sending to partner) */
        log_stream_init(g_log);
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "PeerTalk Latency Test");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Version: %s", PeerTalk_Version());
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    /* Initialize test state */
    init_test();

    /* Log buffer pool info */
    {
        uint16_t pool_count = 0;
        uint32_t pool_size = 0;
        PeerTalk_GetBufferPoolInfo(g_buffer_pool, &pool_count, &pool_size);
        if (g_buffer_pool) {
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "Buffer pool: %u buffers × %luKB (25%% threshold=%luB)",
                (unsigned)pool_count, (unsigned long)(pool_size/1024),
                (unsigned long)(pool_size/4));
        } else {
            PT_LOG_WARN(g_log, PT_LOG_CAT_APP1,
                "Buffer pool allocation failed - using on-demand (may be slower)");
        }
    }

    /* Configure PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "MacLatency", PT_MAX_PEER_NAME);
    config.max_peers = 4;
    config.discovery_port = 7353;
    config.tcp_port = 7354;
    config.buffer_pool = g_buffer_pool;

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

    /* Show test description while waiting */
    status_clear();
    status_line("LATENCY TEST");
    status_line("------------------------");
    status_line("Measures round-trip time");
    status_line("(RTT) for 100 pings at");
    status_line("each message size.");
    status_line("");
    status_line("0ms = faster than 1 tick");
    status_line("(Mac resolution: 16.7ms)");
    status_line("");
    status_line("Waiting for peer...");

    g_discovery_start = TickCount();

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

        /* Check for discovery timeout */
        if (g_target_peer == 0 && (now - g_discovery_start) >= DISCOVERY_TIMEOUT_TICKS) {
            PT_LOG_ERR(g_log, PT_LOG_CAT_APP1,
                "No peer discovered after %d seconds, giving up",
                DISCOVERY_TIMEOUT_TICKS / 60);
            g_test.test_complete = 1;
            g_running = 0;
            break;
        }

        /* Test logic */
        if (g_connected_peer && !g_test.test_complete) {
            LatencyStats *stats = &g_test.stats[g_test.current_size_idx];
            static int last_update_count = -1;

            /* Check for pending timeout */
            check_pending_timeout();

            /* Time to send next ping? */
            if (!g_test.pending && (now - g_last_ping) >= PING_INTERVAL_TICKS) {
                send_ping();
                g_last_ping = now;
            }

            /* Update status window every 10 samples - show results table */
            if (stats->recv_count != last_update_count &&
                stats->recv_count % 10 == 0) {
                int row;
                int total_lost = 0;
                last_update_count = stats->recv_count;

                /* Build table with latency results */
                table_init(&g_table, "RTT in ms (0 = <17ms)", NUM_TEST_SIZES, 5);
                table_set_header(&g_table, 0, "Size", 5, TABLE_ALIGN_RIGHT);
                table_set_header(&g_table, 1, "Min", 4, TABLE_ALIGN_RIGHT);
                table_set_header(&g_table, 2, "Avg", 4, TABLE_ALIGN_RIGHT);
                table_set_header(&g_table, 3, "Max", 4, TABLE_ALIGN_RIGHT);
                table_set_header(&g_table, 4, "N", 4, TABLE_ALIGN_RIGHT);

                /* Populate rows with results */
                for (row = 0; row < (int)NUM_TEST_SIZES; row++) {
                    LatencyStats *s = &g_test.stats[row];

                    table_set_cell_int(&g_table, row, 0, s->message_size);

                    if (s->recv_count > 0) {
                        unsigned long avg = s->total_ticks / s->recv_count;
                        table_set_cell_ms(&g_table, row, 1, s->min_ticks);
                        table_set_cell_ms(&g_table, row, 2, avg);
                        table_set_cell_ms(&g_table, row, 3, s->max_ticks);
                        table_set_cell_int(&g_table, row, 4, s->recv_count);
                    } else {
                        table_clear_cell(&g_table, row, 1);
                        table_clear_cell(&g_table, row, 2);
                        table_clear_cell(&g_table, row, 3);
                        table_clear_cell(&g_table, row, 4);
                    }
                }

                /* Mark current row and render */
                table_set_current_row(&g_table, g_test.current_size_idx);
                table_render(&g_table);

                /* Show lost packets footer if any */
                for (row = 0; row <= g_test.current_size_idx; row++) {
                    total_lost += g_test.stats[row].lost_count;
                }
                if (total_lost > 0) {
                    status_line("");
                    status_linef("Lost: %d packets", total_lost);
                }
            }

            /* Check if we have enough samples for this size */
            if (stats->recv_count >= samples_per_size) {
                advance_test();
                last_update_count = -1;  /* Reset for next size */
            }
        }

        /* Check for overall test completion */
        if (g_test.test_complete && !g_log_stream.streaming && !g_log_stream.complete) {
            print_results();

            /* Update status for completion */
            status_clear();
            status_line("Test complete!");
            status_line("");

            /* Stream logs to partner before exiting */
            if (g_connected_peer) {
                status_line("Streaming logs to partner...");
                PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                    "Streaming %lu bytes of logs to partner...",
                    (unsigned long)g_log_stream.length);
                log_stream_send(g_ctx, g_connected_peer);
            } else {
                status_line("No peer - cannot stream logs");
                g_running = 0;
            }
        }

        /* Wait for log streaming to complete, or exit if peer disconnected */
        if (g_test.test_complete) {
            if (g_log_stream.complete) {
                if (log_stream_bytes_sent() > 0) {
                    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                        "Log streaming complete: %lu bytes sent",
                        (unsigned long)log_stream_bytes_sent());
                }
                g_running = 0;
            } else if (g_log_stream.streaming && g_connected_peer == 0) {
                /* Peer disconnected during streaming - abort and exit */
                PT_LOG_WARN(g_log, PT_LOG_CAT_APP1,
                    "Peer disconnected during log streaming - exiting");
                g_running = 0;
            }
        }
    }

cleanup:
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "TEST EXITING - cleaning up...");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    status_clear();
    status_line("Done. Press any key to exit.");

    log_stream_cleanup();
    if (g_ctx) {
        PeerTalk_Shutdown(g_ctx);
    }
    if (g_buffer_pool) {
        PeerTalk_FreeBuffers(g_buffer_pool);
    }
    if (g_log) {
        PT_LogDestroy(g_log);
    }

    /* Clean up log files AFTER logging is complete */
    /* test_cleanup_files("PT_Latency"); */

    status_cleanup();
    return 0;
}
