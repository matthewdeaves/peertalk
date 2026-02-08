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

/* ========================================================================== */
/* Configuration                                                               */
/* ========================================================================== */

#define TEST_DURATION_SEC    30       /* Test duration per configuration */
#define REPORT_INTERVAL_SEC  5        /* Report progress every N seconds */

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
static PeerTalk_PeerID g_connected_peer = 0;
static ThroughputTest g_test;
static unsigned long g_test_start = 0;
static unsigned long g_last_report = 0;
static int g_running = 1;
static uint8_t g_send_buffer[4096];

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
    int i;
    int burst_count = 10;  /* Send 10 messages per poll to maximize throughput */
    PeerTalk_Error err;

    if (g_connected_peer == 0 || g_test.test_complete)
        return;

    stats = &g_test.stats[g_test.current_size_idx];
    size = stats->buffer_size;

    for (i = 0; i < burst_count; i++) {
        /* Add sequence number to first 4 bytes */
        uint32_t seq = stats->messages_sent;
        memcpy(g_send_buffer, &seq, sizeof(seq));

        err = PeerTalk_Send(g_ctx, g_connected_peer, g_send_buffer, size);
        if (err == PT_OK) {
            stats->bytes_sent += size;
            stats->messages_sent++;
        } else if (err == PT_ERR_WOULD_BLOCK || err == PT_ERR_BUFFER_FULL) {
            /* Backpressure - buffer/queue busy, stop burst and let poll drain */
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

    if (elapsed_ms == 0) elapsed_ms = 1;

    unsigned long send_kbps = (stats->bytes_sent * 1000UL) / elapsed_ms;
    unsigned long recv_kbps = (stats->bytes_received * 1000UL) / elapsed_ms;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "BUF %d: sent=%lu KB/s (%lu msgs) recv=%lu KB/s (%lu msgs) errs=%lu",
        stats->buffer_size,
        send_kbps / 1024UL, stats->messages_sent,
        recv_kbps / 1024UL, stats->messages_received,
        stats->send_errors);
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
    g_test.stats[0].start_ticks = TickCount();
    g_test_start = TickCount();
    g_last_report = g_test_start;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Starting throughput test with buffer size %d", g_buffer_sizes[0]);
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
    ThroughputStats *stats;
    (void)ctx;
    (void)peer_id;
    (void)data;
    (void)user_data;

    if (g_test.current_size_idx < (int)NUM_BUFFER_SIZES) {
        stats = &g_test.stats[g_test.current_size_idx];
        stats->bytes_received += len;
        stats->messages_received++;
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

    init_toolbox();

    /* Create PT_Log */
    g_log = PT_LogCreate();
    if (g_log) {
        PT_LogSetLevel(g_log, PT_LOG_DEBUG);
        PT_LogSetCategories(g_log, 0xFFFF);
        PT_LogSetFile(g_log, "PT_Throughput");
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "PeerTalk Throughput Test");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Version: %s", PeerTalk_Version());
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    /* Log initial memory state */
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Initial memory: FreeMem=%ld MaxBlock=%ld",
        (long)FreeMem(), (long)MaxBlock());

    /* Initialize test state */
    init_test();

    /* Configure PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "MacThroughput", PT_MAX_PEER_NAME);
    config.max_peers = 4;
    config.discovery_port = 7353;
    config.tcp_port = 7354;

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
        if (g_test.test_complete) {
            print_results();
            g_running = 0;
        }
    }

cleanup:
    if (g_ctx) {
        PeerTalk_Shutdown(g_ctx);
    }
    if (g_log) {
        PT_LogDestroy(g_log);
    }

    return 0;
}
