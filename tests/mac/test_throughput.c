/**
 * @file test_throughput.c
 * @brief MacTCP Throughput Test Application
 *
 * Measures sustained data transfer rate to/from a POSIX peer.
 *
 * Test modes:
 *   SEND - Mac streams data to POSIX, POSIX counts bytes
 *   RECV - POSIX streams data to Mac, Mac counts bytes
 *   BOTH - Bidirectional streaming
 *
 * Results are logged via PT_Log.
 *
 * Build with Retro68:
 *   make -f Makefile.retro68 PLATFORM=mactcp test_throughput
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
#include "test_cleanup.h"

/* ========================================================================== */
/* Configuration                                                               */
/* ========================================================================== */

#define TEST_DURATION_SEC    30       /* Test duration per configuration */
#define REPORT_INTERVAL_SEC  5        /* Report progress every N seconds */
#define DISCOVERY_TIMEOUT_TICKS (60 * 60) /* 60 seconds to find a peer */
#define MAX_CONNECT_RETRIES  5        /* Give up after this many failures */

/**
 * Flow control window size.
 *
 * This limits how many messages can be "in flight" (sent but not yet echoed).
 * Without flow control, the Mac floods the network faster than it can process
 * incoming echoes, causing severe asymmetry at large message sizes.
 *
 * Window sizing:
 *   - Too small (1-2): Underutilizes network, low throughput
 *   - Too large (10+): Overwhelms Mac receive processing
 *   - Sweet spot (4-6): Balances throughput with receive capacity
 *
 * At 4096B messages with window=4: 16KB in flight, matches 25% threshold well.
 */
#define FLOW_CONTROL_WINDOW  4

/* Buffer sizes to test */
static const int g_buffer_sizes[] = { 256, 512, 1024, 2048, 4096 };
#define NUM_BUFFER_SIZES  (sizeof(g_buffer_sizes) / sizeof(g_buffer_sizes[0]))

/* ========================================================================== */
/* Throughput Statistics                                                       */
/* ========================================================================== */

typedef struct {
    int             buffer_size;
    unsigned long   bytes_sent;
    unsigned long   bytes_received;
    unsigned long   messages_sent;
    unsigned long   messages_received;
    unsigned long   start_ticks;
    unsigned long   end_ticks;
    unsigned long   send_errors;
} ThroughputStats;

typedef struct {
    int             current_size_idx;
    int             test_complete;
    int             phase;              /* 0=send, 1=recv */
    ThroughputStats stats[NUM_BUFFER_SIZES];
} ThroughputTest;

/* ========================================================================== */
/* Globals                                                                     */
/* ========================================================================== */

static PeerTalk_Context *g_ctx = NULL;
static PT_Log *g_log = NULL;
static PeerTalk_BufferPool *g_buffer_pool = NULL;
static PeerTalk_PeerID g_connected_peer = 0;
static PeerTalk_PeerID g_target_peer = 0;
static ThroughputTest g_test;
static unsigned long g_test_start = 0;
static unsigned long g_last_report = 0;
static unsigned long g_discovery_start = 0;
static int g_running = 1;
static int g_connect_failures = 0;
static uint8_t g_send_buffer[4096];
static unsigned long g_in_flight = 0;   /* Messages sent but not yet echoed */
static TableUI g_table;

/* ========================================================================== */
/* Utility Functions                                                           */
/* ========================================================================== */

static unsigned long ticks_to_ms(unsigned long ticks)
{
    return (ticks * 1000UL) / 60UL;
}

static void init_test(void)
{
    int i;

    memset(&g_test, 0, sizeof(g_test));

    for (i = 0; i < (int)NUM_BUFFER_SIZES; i++) {
        g_test.stats[i].buffer_size = g_buffer_sizes[i];
    }

    /* Initialize send buffer with pattern */
    for (i = 0; i < (int)sizeof(g_send_buffer); i++) {
        g_send_buffer[i] = (uint8_t)(i & 0xFF);
    }
}

/* ========================================================================== */
/* Throughput Test Logic                                                       */
/* ========================================================================== */

static void send_data_burst(void)
{
    ThroughputStats *stats;
    int size;
    PeerTalk_Error err;

    if (g_connected_peer == 0 || g_test.test_complete)
        return;

    stats = &g_test.stats[g_test.current_size_idx];
    size = stats->buffer_size;

    /*
     * Window-based flow control: only send if we have room in our window.
     * This prevents flooding the network faster than we can process echoes.
     */
    while (g_in_flight < FLOW_CONTROL_WINDOW) {
        /* Add sequence number to first 4 bytes */
        uint32_t seq = (uint32_t)stats->messages_sent;
        memcpy(g_send_buffer, &seq, sizeof(seq));

        err = PeerTalk_Send(g_ctx, g_connected_peer, g_send_buffer, size);
        if (err == PT_OK) {
            stats->bytes_sent += size;
            stats->messages_sent++;
            g_in_flight++;
        } else if (err == PT_ERR_WOULD_BLOCK || err == PT_ERR_BUFFER_FULL) {
            /* Backpressure - buffer/queue busy, let poll drain */
            break;
        } else {
            /* Real error */
            stats->send_errors++;
            break;
        }
    }
}

static void report_progress(void)
{
    ThroughputStats *stats = &g_test.stats[g_test.current_size_idx];
    unsigned long elapsed_ticks = TickCount() - stats->start_ticks;
    unsigned long elapsed_ms = ticks_to_ms(elapsed_ticks);
    int row;

    if (elapsed_ms == 0) elapsed_ms = 1;

    unsigned long send_kbps = (stats->bytes_sent * 1000UL) / elapsed_ms;
    unsigned long recv_kbps = (stats->bytes_received * 1000UL) / elapsed_ms;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "BUF %d: sent=%lu KB/s (%lu msgs) recv=%lu KB/s (%lu msgs) inflight=%lu",
        stats->buffer_size,
        send_kbps / 1024UL, stats->messages_sent,
        recv_kbps / 1024UL, stats->messages_received,
        g_in_flight);

    /* Build results table showing all buffer sizes */
    table_init(&g_table, "Throughput (KB/s)", NUM_BUFFER_SIZES, 5);
    table_set_header(&g_table, 0, "Size", 5, TABLE_ALIGN_RIGHT);
    table_set_header(&g_table, 1, "Send", 5, TABLE_ALIGN_RIGHT);
    table_set_header(&g_table, 2, "Recv", 5, TABLE_ALIGN_RIGHT);
    table_set_header(&g_table, 3, "Msgs", 6, TABLE_ALIGN_RIGHT);
    table_set_header(&g_table, 4, "Err", 4, TABLE_ALIGN_RIGHT);

    for (row = 0; row < (int)NUM_BUFFER_SIZES; row++) {
        ThroughputStats *s = &g_test.stats[row];
        unsigned long row_elapsed_ticks, row_elapsed_ms;
        unsigned long row_send_kbps, row_recv_kbps;

        table_set_cell_int(&g_table, row, 0, s->buffer_size);

        if (row < g_test.current_size_idx && s->end_ticks > 0) {
            /* Completed test - use final stats */
            row_elapsed_ticks = s->end_ticks - s->start_ticks;
            row_elapsed_ms = ticks_to_ms(row_elapsed_ticks);
            if (row_elapsed_ms == 0) row_elapsed_ms = 1;
            row_send_kbps = (s->bytes_sent * 1000UL) / row_elapsed_ms;
            row_recv_kbps = (s->bytes_received * 1000UL) / row_elapsed_ms;
            table_set_cell_kbps(&g_table, row, 1, row_send_kbps);
            table_set_cell_kbps(&g_table, row, 2, row_recv_kbps);
            table_set_cell_uint(&g_table, row, 3, s->messages_sent);
            table_set_cell_uint(&g_table, row, 4, s->send_errors);
        } else if (row == g_test.current_size_idx && s->messages_sent > 0) {
            /* Current test - use live stats */
            table_set_cell_kbps(&g_table, row, 1, send_kbps);
            table_set_cell_kbps(&g_table, row, 2, recv_kbps);
            table_set_cell_uint(&g_table, row, 3, s->messages_sent);
            table_set_cell_uint(&g_table, row, 4, s->send_errors);
        } else {
            /* Not yet started */
            table_clear_cell(&g_table, row, 1);
            table_clear_cell(&g_table, row, 2);
            table_clear_cell(&g_table, row, 3);
            table_clear_cell(&g_table, row, 4);
        }
    }

    table_set_current_row(&g_table, g_test.current_size_idx);
    table_render(&g_table);

    /* Show elapsed time below table */
    status_linef("Elapsed: %lu/%d sec", elapsed_ms / 1000UL, TEST_DURATION_SEC);
}

static void finish_current_test(void)
{
    ThroughputStats *stats = &g_test.stats[g_test.current_size_idx];
    unsigned long elapsed_ticks, elapsed_ms;
    unsigned long send_kbps, recv_kbps;

    stats->end_ticks = TickCount();
    elapsed_ticks = stats->end_ticks - stats->start_ticks;
    elapsed_ms = ticks_to_ms(elapsed_ticks);
    if (elapsed_ms == 0) elapsed_ms = 1;

    send_kbps = (stats->bytes_sent * 1000UL) / elapsed_ms;
    recv_kbps = (stats->bytes_received * 1000UL) / elapsed_ms;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "COMPLETE %d bytes: SEND=%lu KB/s (%lu msgs) RECV=%lu KB/s (%lu msgs)",
        stats->buffer_size,
        send_kbps / 1024UL, stats->messages_sent,
        recv_kbps / 1024UL, stats->messages_received);

    /* Move to next buffer size */
    g_test.current_size_idx++;
    if (g_test.current_size_idx >= (int)NUM_BUFFER_SIZES) {
        g_test.test_complete = 1;
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "All throughput tests complete");
    } else {
        ThroughputStats *next = &g_test.stats[g_test.current_size_idx];
        next->start_ticks = TickCount();
        g_in_flight = 0;  /* Reset flow control for new buffer size */
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "Starting buffer size %d test", g_buffer_sizes[g_test.current_size_idx]);
    }
}

/* ========================================================================== */
/* Results                                                                     */
/* ========================================================================== */

static void print_results(void)
{
    int i;
    ThroughputStats *stats;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "THROUGHPUT TEST RESULTS");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    for (i = 0; i < (int)NUM_BUFFER_SIZES; i++) {
        stats = &g_test.stats[i];

        if (stats->messages_sent > 0 || stats->messages_received > 0) {
            unsigned long elapsed_ms = ticks_to_ms(stats->end_ticks - stats->start_ticks);
            if (elapsed_ms == 0) elapsed_ms = 1;

            unsigned long send_kbps = (stats->bytes_sent * 1000UL) / elapsed_ms / 1024UL;
            unsigned long recv_kbps = (stats->bytes_received * 1000UL) / elapsed_ms / 1024UL;

            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "%4d bytes: SEND %4lu KB/s  RECV %4lu KB/s  (errs=%lu)",
                stats->buffer_size, send_kbps, recv_kbps, stats->send_errors);
        } else {
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "%4d bytes: NO DATA", stats->buffer_size);
        }
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    /* Memory check */
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Memory: FreeMem=%ld MaxBlock=%ld",
        (long)FreeMem(), (long)MaxBlock());
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
    uint16_t effective_max;
    (void)user_data;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "CONNECTED to peer %u", (unsigned)peer_id);
    g_connected_peer = peer_id;

    /* Log peer capabilities (may take a poll cycle to negotiate) */
    if (PeerTalk_GetPeerCapabilities(ctx, peer_id, &caps) == PT_OK) {
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "Peer capabilities: max_msg=%u chunk=%u pressure=%u",
            (unsigned)caps.max_message_size,
            (unsigned)caps.preferred_chunk,
            (unsigned)caps.buffer_pressure);
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "Peer buffer info: recv_buf=%u optimal_chunk=%u",
            (unsigned)caps.recv_buffer_size,
            (unsigned)caps.optimal_chunk);
    }

    effective_max = PeerTalk_GetPeerMaxMessage(ctx, peer_id);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Effective max message size: %u bytes", (unsigned)effective_max);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Optimal chunk for peer: %u bytes",
        (unsigned)PeerTalk_GetPeerOptimalChunk(ctx, peer_id));

    /* Start first test */
    g_test.stats[0].start_ticks = TickCount();
    g_test_start = TickCount();
    g_last_report = g_test_start;
    g_in_flight = 0;  /* Initialize flow control window */

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Starting throughput test with buffer size %d (window=%d)",
        g_buffer_sizes[0], FLOW_CONTROL_WINDOW);

    /* Update status window */
    status_clear();
    status_linef("Connected to peer %u", (unsigned)peer_id);
    status_linef("Starting test: %d bytes", g_buffer_sizes[0]);
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
    ThroughputStats *stats;
    (void)ctx;
    (void)peer_id;
    (void)data;
    (void)user_data;

    if (g_test.current_size_idx < (int)NUM_BUFFER_SIZES) {
        stats = &g_test.stats[g_test.current_size_idx];
        stats->bytes_received += len;
        stats->messages_received++;

        /* Flow control: echo received, allow another send */
        if (g_in_flight > 0) {
            g_in_flight--;
        }
    }
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
    unsigned long test_duration_ticks = TEST_DURATION_SEC * 60UL;
    unsigned long report_interval_ticks = REPORT_INTERVAL_SEC * 60UL;

    /**
     * CRITICAL: Bootstrap PeerTalk FIRST, before ANY Toolbox initialization!
     *
     * PeerTalk_Bootstrap() does three things:
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
     *
     * PeerTalk_Bootstrap() automatically scales peer count based on available
     * memory, so the same code works on both Mac SE (4MB) and larger systems.
     */
    g_buffer_pool = PeerTalk_Bootstrap(4);  /* SDK auto-scales for low-memory */

    /* NOW safe to initialize Toolbox */
    init_toolbox();

    /* Create PT_Log FIRST so diagnostic logging works */
    g_log = PT_LogCreate();
    if (g_log) {
        PT_LogSetLevel(g_log, PT_LOG_DEBUG);
        PT_LogSetCategories(g_log, 0xFFFF);
        PT_LogSetFile(g_log, "PT_Throughput");
        PT_LogClearFile(g_log);  /* Fresh log each run */

        /* Initialize log streaming (captures logs for sending to partner) */
        log_stream_init(g_log);
    }

    /* Initialize status window for user feedback (after log so diagnostics work) */
    status_init("PeerTalk Throughput Test");
    status_line("Initializing...");

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "PeerTalk Throughput Test");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Version: %s", PeerTalk_Version());
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    /* Log initial memory state and buffer pool info */
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Memory after pool alloc: FreeMem=%ld MaxBlock=%ld",
        (long)FreeMem(), (long)MaxBlock());
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

    /* Initialize test state */
    init_test();

    /* Configure PeerTalk
     *
     * Buffer allocation options:
     *
     * OPTION 1 (Best performance - used here): Manual early allocation
     *   g_buffer_pool = PeerTalk_AllocateBuffersAuto(4);  // At start of main()
     *   config.buffer_pool = g_buffer_pool;
     *
     * OPTION 2 (Simplest - SDK manages everything):
     *   config.auto_buffers = 1;  // SDK allocates optimal buffers automatically
     *
     * Option 1 is better because heap is less fragmented at start of main().
     * Option 2 is simpler but may get smaller buffers due to fragmentation.
     */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "MacThroughput", PT_MAX_PEER_NAME);
    config.max_peers = 4;
    config.discovery_port = 7353;
    config.tcp_port = 7354;
    config.buffer_pool = g_buffer_pool;  /* Option 1: Pre-allocated buffers */
    /* config.auto_buffers = 1; */       /* Option 2: SDK auto-allocates */

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Initializing PeerTalk...");
    g_ctx = PeerTalk_Init(&config);
    if (!g_ctx) {
        PT_LOG_ERR(g_log, PT_LOG_CAT_APP1, "FAILED to initialize PeerTalk!");
        goto cleanup;
    }

    /* Configure library logging to write to file for debugging */
    {
        PT_Log *lib_log = PeerTalk_GetLog(g_ctx);
        if (lib_log) {
            PT_LogSetLevel(lib_log, PT_LOG_DEBUG);
            PT_LogSetFile(lib_log, "PT_LibDebug");
            PT_LogClearFile(lib_log);  /* Fresh log each run */
        }
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
    status_line("THROUGHPUT TEST");
    status_line("------------------------");
    status_line("Measures sustained data");
    status_line("transfer rate (KB/s).");
    status_line("");
    status_line("Sends for 30s per size,");
    status_line("partner echoes back.");
    status_line("Flow control window = 4.");
    status_line("");
    status_line("Waiting for peer...");

    g_discovery_start = TickCount();

    /* Main loop */
    while (g_running) {
        /* Check for user input */
        if (WaitNextEvent(everyEvent, &event, 0, NULL)) {
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

        /* Throughput test logic */
        if (g_connected_peer && !g_test.test_complete) {
            ThroughputStats *stats = &g_test.stats[g_test.current_size_idx];

            /* Send data as fast as possible */
            send_data_burst();

            /* Progress report */
            if ((now - g_last_report) >= report_interval_ticks) {
                report_progress();
                g_last_report = now;
            }

            /* Check if test duration elapsed */
            if ((now - stats->start_ticks) >= test_duration_ticks) {
                finish_current_test();
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
    /* Free buffer pool AFTER PeerTalk_Shutdown (buffers returned to pool during shutdown) */
    if (g_buffer_pool) {
        PeerTalk_FreeBuffers(g_buffer_pool);
    }
    if (g_log) {
        PT_LogDestroy(g_log);
    }

    /* Clean up log files AFTER logging is complete */
    /* test_cleanup_files("PT_Throughput"); */

    status_cleanup();
    return 0;
}
