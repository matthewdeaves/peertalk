/**
 * @file test_stream.c
 * @brief MacTCP One-Way Streaming Test Application
 *
 * Measures pure unidirectional throughput without echo overhead.
 *
 * Test flow for each buffer size:
 *   1. SEND phase: Mac streams to POSIX (partner sinks)
 *   2. RECV phase: POSIX streams to Mac (Mac sinks)
 *
 * This eliminates the round-trip latency of echo-based tests,
 * showing true unidirectional capacity.
 *
 * Build with Retro68:
 *   make -f Makefile.retro68 PLATFORM=mactcp test_stream
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

/* ========================================================================== */
/* Stream Test Protocol                                                        */
/* ========================================================================== */

/* Control message magic */
#define STREAM_MAGIC_0  'S'
#define STREAM_MAGIC_1  'T'
#define STREAM_MAGIC_2  'R'
#define STREAM_MAGIC_3  'M'

/* Stream commands */
#define STREAM_CMD_START_SEND  0x01  /* Mac about to send, partner should sink */
#define STREAM_CMD_START_RECV  0x02  /* Mac ready to receive, partner should stream */
#define STREAM_CMD_STOP        0x03  /* Phase complete */
#define STREAM_CMD_ACK         0x04  /* Partner acknowledges command */

/* Control message structure (12 bytes) */
typedef struct {
    char     magic[4];      /* "STRM" */
    uint8_t  command;       /* STREAM_CMD_* */
    uint8_t  reserved;
    uint16_t msg_size;      /* Message size for this phase */
    uint32_t duration_ms;   /* Duration in milliseconds */
} StreamControl;

/* ========================================================================== */
/* Configuration                                                               */
/* ========================================================================== */

#define TEST_DURATION_SEC    30       /* Duration per phase per size */
#define REPORT_INTERVAL_SEC  5        /* Report progress every N seconds */
#define DISCOVERY_TIMEOUT_TICKS (60 * 60) /* 60 seconds to find a peer */
#define MAX_CONNECT_RETRIES  5
#define DRAIN_WAIT_TICKS     (10 * 60) /* 10 seconds between phases - allows TCP backlog to clear */
#define DRAIN_WAIT_LONG_TICKS (20 * 60) /* 20 seconds after large message RECV phases */
#define ACK_TIMEOUT_TICKS    (30 * 60) /* 30 seconds to wait for ACK */
#define LOG_STREAM_TIMEOUT_TICKS (30 * 60) /* 30 seconds to stream logs */

/* Buffer sizes to test */
static const int g_buffer_sizes[] = { 256, 512, 1024, 2048, 4096 };
#define NUM_BUFFER_SIZES  (sizeof(g_buffer_sizes) / sizeof(g_buffer_sizes[0]))

/* ========================================================================== */
/* Statistics                                                                  */
/* ========================================================================== */

typedef struct {
    int             buffer_size;
    /* Send phase stats */
    unsigned long   send_bytes;
    unsigned long   send_msgs;
    unsigned long   send_start_ticks;
    unsigned long   send_end_ticks;
    unsigned long   send_errors;
    /* Recv phase stats */
    unsigned long   recv_bytes;
    unsigned long   recv_msgs;
    unsigned long   recv_start_ticks;
    unsigned long   recv_end_ticks;
} StreamStats;

typedef struct {
    int             current_size_idx;
    int             phase;              /* 0=send, 1=recv, 2=done */
    int             waiting_for_ack;
    int             test_complete;
    unsigned long   phase_start_ticks;
    StreamStats     stats[NUM_BUFFER_SIZES];
} StreamTest;

/* ========================================================================== */
/* Globals                                                                     */
/* ========================================================================== */

static PeerTalk_Context *g_ctx = NULL;
static PT_Log *g_log = NULL;
static PeerTalk_BufferPool *g_buffer_pool = NULL;
static PeerTalk_PeerID g_connected_peer = 0;
static PeerTalk_PeerID g_target_peer = 0;
static StreamTest g_test;
static unsigned long g_last_report = 0;
static unsigned long g_discovery_start = 0;
static int g_running = 1;
static int g_connect_failures = 0;
static uint8_t g_send_buffer[4096];
static unsigned long g_ack_wait_start = 0;
static int g_ack_retries = 0;
#define MAX_ACK_RETRIES 3
static TableUI g_table;
static unsigned long g_log_stream_start = 0;

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
/* Control Message Functions                                                   */
/* ========================================================================== */

static int send_control(uint8_t command, uint16_t msg_size, uint32_t duration_ms)
{
    StreamControl ctrl;
    PeerTalk_Error err;
    int retries = 0;
    const int max_retries = 5;

    ctrl.magic[0] = STREAM_MAGIC_0;
    ctrl.magic[1] = STREAM_MAGIC_1;
    ctrl.magic[2] = STREAM_MAGIC_2;
    ctrl.magic[3] = STREAM_MAGIC_3;
    ctrl.command = command;
    ctrl.reserved = 0;
    ctrl.msg_size = msg_size;
    ctrl.duration_ms = duration_ms;

    /* Use CRITICAL priority for control messages to bypass flow control.
     * Control messages must always get through for protocol coordination.
     * Retry with exponential backoff if buffer is full. */
    while (retries < max_retries) {
        err = PeerTalk_SendEx(g_ctx, g_connected_peer, &ctrl, sizeof(ctrl),
                              PT_PRIORITY_CRITICAL, PT_SEND_DEFAULT, 0);
        if (err == PT_OK) {
            PT_LOG_DEBUG(g_log, PT_LOG_CAT_APP1,
                "Sent control: cmd=%u size=%u duration=%lu",
                (unsigned)command, (unsigned)msg_size, duration_ms);
            return 0;
        }

        if (err == PT_ERR_BUFFER_FULL || err == PT_ERR_WOULD_BLOCK) {
            /* Queue congested - poll to drain and retry */
            retries++;
            PT_LOG_WARN(g_log, PT_LOG_CAT_APP1,
                "Control message queued (retry %d/%d): cmd=%u err=%d",
                retries, max_retries, (unsigned)command, (int)err);
            PeerTalk_Poll(g_ctx);  /* Give chance to drain */
            continue;
        }

        /* Non-retryable error */
        break;
    }

    PT_LOG_ERR(g_log, PT_LOG_CAT_APP1,
        "Failed to send control message: cmd=%u err=%d",
        (unsigned)command, (int)err);
    return -1;
}

static int is_control_message(const void *data, uint16_t len)
{
    const StreamControl *ctrl = (const StreamControl *)data;

    if (len != sizeof(StreamControl)) {
        return 0;
    }

    return (ctrl->magic[0] == STREAM_MAGIC_0 &&
            ctrl->magic[1] == STREAM_MAGIC_1 &&
            ctrl->magic[2] == STREAM_MAGIC_2 &&
            ctrl->magic[3] == STREAM_MAGIC_3);
}

/* ========================================================================== */
/* Test Logic                                                                  */
/* ========================================================================== */

static void start_send_phase(void)
{
    StreamStats *stats = &g_test.stats[g_test.current_size_idx];
    uint32_t duration_ms = TEST_DURATION_SEC * 1000UL;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "=== SEND PHASE: %d bytes ===", stats->buffer_size);

    /* Tell partner to enter sink mode */
    if (send_control(STREAM_CMD_START_SEND, stats->buffer_size, duration_ms) < 0) {
        g_test.test_complete = 1;
        return;
    }

    g_test.waiting_for_ack = 1;
    g_ack_wait_start = TickCount();
    g_ack_retries = 0;

    status_clear();
    status_linef("SEND: %d bytes", stats->buffer_size);
    status_line("Waiting for partner ACK...");
}

static void start_recv_phase(void)
{
    StreamStats *stats = &g_test.stats[g_test.current_size_idx];
    uint32_t duration_ms = TEST_DURATION_SEC * 1000UL;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "=== RECV PHASE: %d bytes ===", stats->buffer_size);

    /* Tell partner to start streaming to us */
    if (send_control(STREAM_CMD_START_RECV, stats->buffer_size, duration_ms) < 0) {
        g_test.test_complete = 1;
        return;
    }

    g_test.waiting_for_ack = 1;
    g_ack_wait_start = TickCount();
    g_ack_retries = 0;

    status_clear();
    status_linef("RECV: %d bytes", stats->buffer_size);
    status_line("Waiting for partner ACK...");
}

static void on_ack_received(void)
{
    StreamStats *stats = &g_test.stats[g_test.current_size_idx];

    g_test.waiting_for_ack = 0;

    if (g_test.phase == 0) {
        /* Starting send phase */
        stats->send_start_ticks = TickCount();
        g_test.phase_start_ticks = stats->send_start_ticks;
        g_last_report = stats->send_start_ticks;

        status_clear();
        status_linef("SEND: %d bytes", stats->buffer_size);
        status_line("Streaming...");
    } else {
        /* Starting recv phase */
        stats->recv_start_ticks = TickCount();
        g_test.phase_start_ticks = stats->recv_start_ticks;
        g_last_report = stats->recv_start_ticks;

        status_clear();
        status_linef("RECV: %d bytes", stats->buffer_size);
        status_line("Receiving...");
    }
}

static void finish_send_phase(void)
{
    StreamStats *stats = &g_test.stats[g_test.current_size_idx];
    unsigned long elapsed_ms;
    unsigned long kbps;

    stats->send_end_ticks = TickCount();
    elapsed_ms = ticks_to_ms(stats->send_end_ticks - stats->send_start_ticks);
    if (elapsed_ms == 0) elapsed_ms = 1;

    /* Calculate KB/s avoiding overflow: (bytes/1024) * 1000 / ms */
    kbps = (stats->send_bytes / 1024UL) * 1000UL / elapsed_ms;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "SEND COMPLETE %d bytes: %lu KB/s (%lu msgs, %lu errors)",
        stats->buffer_size, kbps, stats->send_msgs, stats->send_errors);

    /* Tell partner to stop */
    send_control(STREAM_CMD_STOP, 0, 0);

    /* Move to recv phase */
    g_test.phase = 1;

    /* Brief pause to let queues drain */
    PT_LOG_DEBUG(g_log, PT_LOG_CAT_APP1, "Draining for 2 seconds...");

    status_clear();
    status_linef("SEND done: %lu KB/s", kbps);
    status_line("Draining...");
}

static void finish_recv_phase(void)
{
    StreamStats *stats = &g_test.stats[g_test.current_size_idx];
    unsigned long elapsed_ms;
    unsigned long kbps;

    stats->recv_end_ticks = TickCount();
    elapsed_ms = ticks_to_ms(stats->recv_end_ticks - stats->recv_start_ticks);
    if (elapsed_ms == 0) elapsed_ms = 1;

    /* Calculate KB/s avoiding overflow: (bytes/1024) * 1000 / ms */
    kbps = (stats->recv_bytes / 1024UL) * 1000UL / elapsed_ms;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "RECV COMPLETE %d bytes: %lu KB/s (%lu msgs)",
        stats->buffer_size, kbps, stats->recv_msgs);

    /* Tell partner to stop */
    send_control(STREAM_CMD_STOP, 0, 0);

    /* Move to next buffer size */
    g_test.current_size_idx++;
    g_test.phase = 0;

    if (g_test.current_size_idx >= (int)NUM_BUFFER_SIZES) {
        g_test.test_complete = 1;
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "All stream tests complete");
    }

    status_clear();
    status_linef("RECV done: %lu KB/s", kbps);
    if (!g_test.test_complete) {
        status_line("Next size...");
    }
}

static void send_data_burst(void)
{
    StreamStats *stats;
    int size;
    PeerTalk_Error err;
    int burst_count = 0;

    if (g_test.phase != 0 || g_test.waiting_for_ack)
        return;

    stats = &g_test.stats[g_test.current_size_idx];
    size = stats->buffer_size;

    /* Send as many messages as we can without blocking */
    while (burst_count < 20) {
        err = PeerTalk_Send(g_ctx, g_connected_peer, g_send_buffer, size);
        if (err == PT_OK) {
            stats->send_bytes += size;
            stats->send_msgs++;
            burst_count++;
        } else if (err == PT_ERR_WOULD_BLOCK || err == PT_ERR_BUFFER_FULL) {
            /* Let poll drain the queue */
            break;
        } else {
            stats->send_errors++;
            break;
        }
    }
}

static void report_progress(void)
{
    StreamStats *stats = &g_test.stats[g_test.current_size_idx];
    unsigned long elapsed_ticks = TickCount() - g_test.phase_start_ticks;
    unsigned long elapsed_ms = ticks_to_ms(elapsed_ticks);
    unsigned long kbps;
    int row;

    if (elapsed_ms == 0) elapsed_ms = 1;

    /* Log current phase progress - use overflow-safe calculation */
    if (g_test.phase == 0) {
        kbps = (stats->send_bytes / 1024UL) * 1000UL / elapsed_ms;
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "SEND %d: %lu KB/s (%lu msgs) errs=%lu",
            stats->buffer_size, kbps, stats->send_msgs, stats->send_errors);
    } else {
        kbps = (stats->recv_bytes / 1024UL) * 1000UL / elapsed_ms;
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "RECV %d: %lu KB/s (%lu msgs)",
            stats->buffer_size, kbps, stats->recv_msgs);
    }

    /* Build results table showing all sizes */
    table_init(&g_table, "Stream (KB/s)", NUM_BUFFER_SIZES, 4);
    table_set_header(&g_table, 0, "Size", 5, TABLE_ALIGN_RIGHT);
    table_set_header(&g_table, 1, "Send", 5, TABLE_ALIGN_RIGHT);
    table_set_header(&g_table, 2, "Recv", 5, TABLE_ALIGN_RIGHT);
    table_set_header(&g_table, 3, "Status", 6, TABLE_ALIGN_LEFT);

    for (row = 0; row < (int)NUM_BUFFER_SIZES; row++) {
        StreamStats *s = &g_test.stats[row];
        unsigned long send_ms, recv_ms, send_kbps, recv_kbps;

        table_set_cell_int(&g_table, row, 0, s->buffer_size);

        if (row < g_test.current_size_idx) {
            /* Completed test */
            send_ms = ticks_to_ms(s->send_end_ticks - s->send_start_ticks);
            recv_ms = ticks_to_ms(s->recv_end_ticks - s->recv_start_ticks);
            if (send_ms == 0) send_ms = 1;
            if (recv_ms == 0) recv_ms = 1;
            send_kbps = (s->send_bytes / 1024UL) * 1000UL / send_ms;
            recv_kbps = (s->recv_bytes / 1024UL) * 1000UL / recv_ms;
            table_set_cell_kbps(&g_table, row, 1, send_kbps);
            table_set_cell_kbps(&g_table, row, 2, recv_kbps);
            table_set_cell_str(&g_table, row, 3, "DONE");
        } else if (row == g_test.current_size_idx) {
            /* Current test */
            if (g_test.phase == 0) {
                /* In send phase */
                kbps = (s->send_bytes / 1024UL) * 1000UL / elapsed_ms;
                table_set_cell_kbps(&g_table, row, 1, kbps);
                table_clear_cell(&g_table, row, 2);
                table_set_cell_str(&g_table, row, 3, "SEND");
            } else {
                /* In recv phase */
                send_ms = ticks_to_ms(s->send_end_ticks - s->send_start_ticks);
                if (send_ms == 0) send_ms = 1;
                send_kbps = (s->send_bytes / 1024UL) * 1000UL / send_ms;
                kbps = (s->recv_bytes / 1024UL) * 1000UL / elapsed_ms;
                table_set_cell_kbps(&g_table, row, 1, send_kbps);
                table_set_cell_kbps(&g_table, row, 2, kbps);
                table_set_cell_str(&g_table, row, 3, "RECV");
            }
        } else {
            /* Not yet started */
            table_clear_cell(&g_table, row, 1);
            table_clear_cell(&g_table, row, 2);
            table_clear_cell(&g_table, row, 3);
        }
    }

    table_set_current_row(&g_table, g_test.current_size_idx);
    table_render(&g_table);
}

/* ========================================================================== */
/* Results                                                                     */
/* ========================================================================== */

static void print_results(void)
{
    int i;
    StreamStats *stats;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "ONE-WAY STREAMING TEST RESULTS");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    for (i = 0; i < (int)NUM_BUFFER_SIZES; i++) {
        stats = &g_test.stats[i];

        if (stats->send_msgs > 0 || stats->recv_msgs > 0) {
            unsigned long send_ms = ticks_to_ms(stats->send_end_ticks - stats->send_start_ticks);
            unsigned long recv_ms = ticks_to_ms(stats->recv_end_ticks - stats->recv_start_ticks);
            unsigned long send_kbps = 0, recv_kbps = 0;

            if (send_ms > 0) {
                send_kbps = (stats->send_bytes / 1024UL) * 1000UL / send_ms;
            }
            if (recv_ms > 0) {
                recv_kbps = (stats->recv_bytes / 1024UL) * 1000UL / recv_ms;
            }

            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "%4d bytes: SEND %4lu KB/s  RECV %4lu KB/s",
                stats->buffer_size, send_kbps, recv_kbps);
        } else {
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "%4d bytes: NO DATA", stats->buffer_size);
        }
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
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

    if (PeerTalk_GetPeerCapabilities(ctx, peer_id, &caps) == PT_OK) {
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "Peer capabilities: max_msg=%u recv_buf=%u",
            (unsigned)caps.max_message_size,
            (unsigned)caps.recv_buffer_size);
    }

    status_clear();
    status_linef("Connected to peer %u", (unsigned)peer_id);
    status_line("Starting stream test...");

    /* Start first send phase */
    start_send_phase();
}

static void on_peer_disconnected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                  int reason, void *user_data)
{
    (void)ctx;
    (void)user_data;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "DISCONNECTED from peer %u (reason=%d)", (unsigned)peer_id, reason);
    g_connected_peer = 0;

    if (!g_test.test_complete) {
        PT_LOG_ERR(g_log, PT_LOG_CAT_APP1, "Peer disconnected during test!");
        g_test.test_complete = 1;
    }
}

static void on_message_received(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                 const void *data, uint16_t len, void *user_data)
{
    StreamStats *stats;
    (void)ctx;
    (void)peer_id;
    (void)user_data;

    /* Log ALL incoming messages during ACK wait for debugging */
    if (g_test.waiting_for_ack) {
        const uint8_t *bytes = (const uint8_t *)data;
        PT_LOG_DEBUG(g_log, PT_LOG_CAT_APP1,
            "MSG during ACK wait: len=%u bytes=[%02X %02X %02X %02X ...]",
            (unsigned)len,
            len > 0 ? bytes[0] : 0,
            len > 1 ? bytes[1] : 0,
            len > 2 ? bytes[2] : 0,
            len > 3 ? bytes[3] : 0);
    }

    /* Check for control message (ACK) */
    if (is_control_message(data, len)) {
        const StreamControl *ctrl = (const StreamControl *)data;
        PT_LOG_DEBUG(g_log, PT_LOG_CAT_APP1,
            "Control msg: cmd=%u size=%u duration=%lu",
            (unsigned)ctrl->command,
            (unsigned)ctrl->msg_size,
            (unsigned long)ctrl->duration_ms);
        if (ctrl->command == STREAM_CMD_ACK) {
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Received ACK from partner");
            on_ack_received();
        }
        return;
    }

    /* Data message - count it during recv phase */
    if (g_test.phase == 1 && !g_test.waiting_for_ack &&
        g_test.current_size_idx < (int)NUM_BUFFER_SIZES) {
        stats = &g_test.stats[g_test.current_size_idx];
        stats->recv_bytes += len;
        stats->recv_msgs++;
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
    unsigned long drain_start = 0;
    int draining = 0;        /* 0=no drain, 1=post-send, 2=post-recv, 3=post-recv-long */

    /* Bootstrap PeerTalk first for optimal buffer allocation */
    g_buffer_pool = PeerTalk_Bootstrap(4);

    init_toolbox();

    status_init("PeerTalk Stream Test");
    status_line("Initializing...");

    /* Create PT_Log */
    g_log = PT_LogCreate();
    if (g_log) {
        PT_LogSetLevel(g_log, PT_LOG_DEBUG);
        PT_LogSetCategories(g_log, 0xFFFF);
        PT_LogSetFile(g_log, "PT_Stream");
        PT_LogClearFile(g_log);  /* Fresh log each run */
        log_stream_init(g_log);
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "PeerTalk One-Way Stream Test");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Version: %s", PeerTalk_Version());
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Memory after pool alloc: FreeMem=%ld MaxBlock=%ld",
        (long)FreeMem(), (long)MaxBlock());
    {
        uint16_t pool_count = 0;
        uint32_t pool_size = 0;
        PeerTalk_GetBufferPoolInfo(g_buffer_pool, &pool_count, &pool_size);
        if (g_buffer_pool) {
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "Buffer pool: %u buffers × %luKB",
                (unsigned)pool_count, (unsigned long)(pool_size/1024));
        }
    }

    init_test();

    /* Configure PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "MacStream", PT_MAX_PEER_NAME);
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

    if (PeerTalk_StartDiscovery(g_ctx) != 0) {
        PT_LOG_ERR(g_log, PT_LOG_CAT_APP1, "FAILED to start discovery!");
        goto cleanup;
    }

    if (PeerTalk_StartListening(g_ctx) != 0) {
        PT_LOG_ERR(g_log, PT_LOG_CAT_APP1, "FAILED to start listening!");
        goto cleanup;
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Waiting for peer...");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Press any key to exit.");

    /* Show test description while waiting */
    status_clear();
    status_line("ONE-WAY STREAM TEST");
    status_line("------------------------");
    status_line("Measures pure throughput");
    status_line("without echo overhead.");
    status_line("");
    status_line("SEND: Mac streams to POSIX");
    status_line("RECV: POSIX streams to Mac");
    status_line("30s per phase per size.");
    status_line("");
    status_line("Waiting for peer...");

    g_discovery_start = TickCount();

    /* Main loop */
    while (g_running) {
        if (WaitNextEvent(everyEvent, &event, 0, NULL)) {
            if (event.what == keyDown || event.what == mouseDown) {
                g_running = 0;
                break;
            }
        }

        PeerTalk_Poll(g_ctx);

        now = TickCount();

        /* Discovery timeout */
        if (g_target_peer == 0 && (now - g_discovery_start) >= DISCOVERY_TIMEOUT_TICKS) {
            PT_LOG_ERR(g_log, PT_LOG_CAT_APP1, "No peer discovered, giving up");
            g_test.test_complete = 1;
            g_running = 0;
            break;
        }

        /* ACK timeout with retry */
        if (g_test.waiting_for_ack && (now - g_ack_wait_start) >= ACK_TIMEOUT_TICKS) {
            g_ack_retries++;
            if (g_ack_retries >= MAX_ACK_RETRIES) {
                PT_LOG_ERR(g_log, PT_LOG_CAT_APP1,
                    "Timeout waiting for partner ACK (gave up after %d retries)",
                    MAX_ACK_RETRIES);
                g_test.test_complete = 1;
            } else {
                /* Retry: resend the control message */
                StreamStats *stats = &g_test.stats[g_test.current_size_idx];
                uint32_t duration_ms = TEST_DURATION_SEC * 1000UL;
                uint8_t cmd = (g_test.phase == 0) ?
                              STREAM_CMD_START_SEND : STREAM_CMD_START_RECV;

                PT_LOG_WARN(g_log, PT_LOG_CAT_APP1,
                    "ACK timeout, retrying (%d/%d)...", g_ack_retries, MAX_ACK_RETRIES);

                if (send_control(cmd, stats->buffer_size, duration_ms) == 0) {
                    g_ack_wait_start = now;  /* Reset timeout */
                    status_linef("Retry %d/%d...", g_ack_retries, MAX_ACK_RETRIES);
                } else {
                    PT_LOG_ERR(g_log, PT_LOG_CAT_APP1, "Retry send failed");
                    g_test.test_complete = 1;
                }
            }
        }

        /* Test logic */
        if (g_connected_peer && !g_test.test_complete && !g_test.waiting_for_ack) {
            if (draining == 1) {
                /* Post-send drain: wait for pending data before RECV phase */
                if ((now - drain_start) >= DRAIN_WAIT_TICKS) {
                    draining = 0;
                    start_recv_phase();
                }
            } else if (draining == 2) {
                /* Post-recv drain (normal): wait for pending RDS data before next SEND phase */
                if ((now - drain_start) >= DRAIN_WAIT_TICKS) {
                    draining = 0;
                    start_send_phase();
                }
            } else if (draining == 3) {
                /* Post-recv drain (long): for large message sizes with more RDS accumulation */
                if ((now - drain_start) >= DRAIN_WAIT_LONG_TICKS) {
                    draining = 0;
                    start_send_phase();
                }
            } else if (g_test.phase == 0) {
                /* Send phase */
                send_data_burst();

                /* Progress report */
                if ((now - g_last_report) >= report_interval_ticks) {
                    report_progress();
                    g_last_report = now;
                }

                /* Check if phase duration elapsed */
                if ((now - g_test.phase_start_ticks) >= test_duration_ticks) {
                    finish_send_phase();
                    draining = 1;
                    drain_start = now;
                }
            } else if (g_test.phase == 1) {
                /* Recv phase - just receiving in callback */

                /* Progress report */
                if ((now - g_last_report) >= report_interval_ticks) {
                    report_progress();
                    g_last_report = now;
                }

                /* Check if phase duration elapsed */
                if ((now - g_test.phase_start_ticks) >= test_duration_ticks) {
                    StreamStats *stats = &g_test.stats[g_test.current_size_idx];
                    int drain_secs;
                    finish_recv_phase();
                    if (!g_test.test_complete) {
                        /* Drain period before starting next size's send phase.
                         * This allows any pending RDS data from high-speed receive
                         * to be fully processed before we wait for ACK messages.
                         * Use longer drain for sizes >= 512 where receive backlog is significant. */
                        drain_secs = (stats->buffer_size >= 512) ? 15 : 10;
                        status_linef("Draining %ds...", drain_secs);
                        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Draining for %d seconds...", drain_secs);
                        draining = (stats->buffer_size >= 512) ? 3 : 2;  /* 3=long drain (15s), 2=normal (10s) */
                        drain_start = now;
                    }
                }
            }
        }

        /* Test complete - print results and stream logs */
        if (g_test.test_complete && !g_log_stream.streaming && !g_log_stream.complete) {
            print_results();

            status_clear();
            status_line("Test complete!");
            status_line("");

            if (g_connected_peer) {
                status_line("Streaming logs to partner...");
                PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                    "Streaming %lu bytes of logs to partner...",
                    (unsigned long)g_log_stream.length);
                log_stream_send(g_ctx, g_connected_peer);
                g_log_stream_start = TickCount();
            } else {
                status_line("No peer - cannot stream logs");
                g_running = 0;
            }
        }

        if (g_test.test_complete) {
            if (g_log_stream.complete) {
                g_running = 0;
            } else if (g_log_stream.streaming && g_connected_peer == 0) {
                g_running = 0;
            } else if (g_log_stream.streaming &&
                       (now - g_log_stream_start) >= LOG_STREAM_TIMEOUT_TICKS) {
                /* Log streaming timed out - exit anyway */
                PT_LOG_WARN(g_log, PT_LOG_CAT_APP1,
                    "Log streaming timed out after 30s, exiting");
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

    status_cleanup();
    return 0;
}
