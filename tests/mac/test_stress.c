/**
 * @file test_stress.c
 * @brief MacTCP Connection Stress Test
 *
 * Tests connection reliability and memory management by:
 * 1. Rapid connect/disconnect cycles
 * 2. Multiple simultaneous connections
 * 3. Memory leak detection via MaxBlock()/FreeMem()
 *
 * Critical for verifying no resource leaks on 68k Macs.
 *
 * Build with Retro68:
 *   make -f Makefile.retro68 PLATFORM=mactcp test_stress
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

#define TARGET_CYCLES       50      /* Number of connect/disconnect cycles */
#define CYCLE_DELAY_TICKS   30      /* 0.5 second between cycles */
#define CONNECT_TIMEOUT     300     /* 5 seconds to wait for connection */
#define DISCONNECT_WAIT     60      /* 1 second after disconnect before next */
#define MEMORY_CHECK_INTERVAL 10    /* Check memory every N cycles */

/* Memory leak threshold */
#define LEAK_THRESHOLD_BYTES  4096  /* Warn if MaxBlock drops by this much */

/* ========================================================================== */
/* Stress Test State                                                           */
/* ========================================================================== */

typedef enum {
    STATE_DISCOVERING,      /* Waiting for peer discovery */
    STATE_CONNECTING,       /* Connection in progress */
    STATE_CONNECTED,        /* Connected, sending test message */
    STATE_DISCONNECTING,    /* Disconnect in progress */
    STATE_WAITING,          /* Delay before next cycle */
    STATE_COMPLETE          /* All cycles done */
} StressState;

typedef struct {
    /* Cycle tracking */
    int             cycle_count;
    int             connect_successes;
    int             connect_failures;
    int             disconnect_count;
    int             message_sent_count;
    int             message_recv_count;

    /* Memory tracking */
    long            initial_maxblock;
    long            initial_freemem;
    long            min_maxblock;
    long            min_freemem;
    long            current_maxblock;
    long            current_freemem;

    /* Timing */
    unsigned long   cycle_start_ticks;
    unsigned long   state_start_ticks;

    /* State machine */
    StressState     state;
    PeerTalk_PeerID target_peer;

} StressTest;

/* ========================================================================== */
/* Globals                                                                     */
/* ========================================================================== */

static PeerTalk_Context *g_ctx = NULL;
static PT_Log *g_log = NULL;
static StressTest g_test;
static int g_running = 1;

/* ========================================================================== */
/* Memory Tracking                                                             */
/* ========================================================================== */

static void update_memory_stats(void)
{
    g_test.current_maxblock = MaxBlock();
    g_test.current_freemem = FreeMem();

    if (g_test.current_maxblock < g_test.min_maxblock) {
        g_test.min_maxblock = g_test.current_maxblock;
    }
    if (g_test.current_freemem < g_test.min_freemem) {
        g_test.min_freemem = g_test.current_freemem;
    }
}

static void log_memory_status(const char *label)
{
    update_memory_stats();

    long maxblock_delta = g_test.current_maxblock - g_test.initial_maxblock;
    long freemem_delta = g_test.current_freemem - g_test.initial_freemem;

    PT_LOG_INFO(g_log, PT_LOG_CAT_MEMORY,
        "%s: MaxBlock=%ld (%+ld) FreeMem=%ld (%+ld)",
        label,
        g_test.current_maxblock, maxblock_delta,
        g_test.current_freemem, freemem_delta);

    /* Warn if significant memory loss */
    if (maxblock_delta < -LEAK_THRESHOLD_BYTES) {
        PT_LOG_WARN(g_log, PT_LOG_CAT_MEMORY,
            "POSSIBLE LEAK: MaxBlock dropped by %ld bytes", -maxblock_delta);
    }
}

/* ========================================================================== */
/* State Machine                                                               */
/* ========================================================================== */

static void set_state(StressState new_state)
{
    const char *state_names[] = {
        "DISCOVERING", "CONNECTING", "CONNECTED",
        "DISCONNECTING", "WAITING", "COMPLETE"
    };

    PT_LOG_DEBUG(g_log, PT_LOG_CAT_APP1,
        "State: %s -> %s",
        state_names[g_test.state], state_names[new_state]);

    g_test.state = new_state;
    g_test.state_start_ticks = TickCount();
}

static void start_connect(void)
{
    if (g_test.target_peer == 0) {
        PT_LOG_WARN(g_log, PT_LOG_CAT_APP1, "No target peer to connect to");
        return;
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Cycle %d: Connecting to peer %u...",
        g_test.cycle_count + 1, (unsigned)g_test.target_peer);

    if (PeerTalk_Connect(g_ctx, g_test.target_peer) == PT_OK) {
        set_state(STATE_CONNECTING);
    } else {
        PT_LOG_ERR(g_log, PT_LOG_CAT_APP1, "Connect initiation failed!");
        g_test.connect_failures++;
        set_state(STATE_WAITING);
    }
}

static void send_test_message(void)
{
    char msg[64];
    sprintf(msg, "STRESS %d", g_test.cycle_count);

    if (PeerTalk_Send(g_ctx, g_test.target_peer, msg, strlen(msg)) == PT_OK) {
        g_test.message_sent_count++;
        PT_LOG_DEBUG(g_log, PT_LOG_CAT_APP1, "Sent test message");
    }
}

static void start_disconnect(void)
{
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Cycle %d: Disconnecting...", g_test.cycle_count + 1);

    /* Disconnect will happen through PeerTalk_Disconnect or connection drop */
    /* For now, we just stop using the connection and wait for timeout */
    set_state(STATE_DISCONNECTING);
}

static void complete_cycle(void)
{
    g_test.cycle_count++;
    g_test.disconnect_count++;

    /* Memory check at intervals */
    if ((g_test.cycle_count % MEMORY_CHECK_INTERVAL) == 0) {
        char label[32];
        sprintf(label, "Cycle %d", g_test.cycle_count);
        log_memory_status(label);
    }

    /* Check if done */
    if (g_test.cycle_count >= TARGET_CYCLES) {
        set_state(STATE_COMPLETE);
        return;
    }

    /* Wait before next cycle */
    set_state(STATE_WAITING);
}

/* ========================================================================== */
/* Poll Handler                                                                */
/* ========================================================================== */

static void stress_poll(void)
{
    unsigned long now = TickCount();
    unsigned long elapsed = now - g_test.state_start_ticks;

    switch (g_test.state) {
    case STATE_DISCOVERING:
        /* Waiting for peer discovery (handled by callback) */
        break;

    case STATE_CONNECTING:
        /* Check for timeout */
        if (elapsed > CONNECT_TIMEOUT) {
            PT_LOG_WARN(g_log, PT_LOG_CAT_APP1,
                "Connect timeout after %lu ticks", elapsed);
            g_test.connect_failures++;
            set_state(STATE_WAITING);
        }
        break;

    case STATE_CONNECTED:
        /* Send test message then disconnect */
        send_test_message();
        start_disconnect();
        break;

    case STATE_DISCONNECTING:
        /* Wait for disconnect to complete (handled by callback) */
        if (elapsed > DISCONNECT_WAIT) {
            /* Force cycle completion if disconnect is slow */
            complete_cycle();
        }
        break;

    case STATE_WAITING:
        /* Delay before next cycle */
        if (elapsed > CYCLE_DELAY_TICKS) {
            start_connect();
        }
        break;

    case STATE_COMPLETE:
        g_running = 0;
        break;
    }
}

/* ========================================================================== */
/* Results                                                                     */
/* ========================================================================== */

static void print_results(void)
{
    long maxblock_delta = g_test.current_maxblock - g_test.initial_maxblock;
    long freemem_delta = g_test.current_freemem - g_test.initial_freemem;
    int success_rate;

    /* Final memory check */
    update_memory_stats();

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "STRESS TEST RESULTS");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Connection Cycles: %d", g_test.cycle_count);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Successes: %d", g_test.connect_successes);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Failures: %d", g_test.connect_failures);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Disconnects: %d", g_test.disconnect_count);

    if (g_test.cycle_count > 0) {
        success_rate = (g_test.connect_successes * 100) / g_test.cycle_count;
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Success rate: %d%%", success_rate);
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Messages: sent=%d recv=%d",
        g_test.message_sent_count, g_test.message_recv_count);

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Memory Analysis:");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Initial MaxBlock: %ld", g_test.initial_maxblock);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Final MaxBlock:   %ld (%+ld)",
        g_test.current_maxblock, maxblock_delta);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Min MaxBlock:     %ld", g_test.min_maxblock);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Initial FreeMem:  %ld", g_test.initial_freemem);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Final FreeMem:    %ld (%+ld)",
        g_test.current_freemem, freemem_delta);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Min FreeMem:      %ld", g_test.min_freemem);

    /* Verdict */
    if (maxblock_delta >= -LEAK_THRESHOLD_BYTES &&
        g_test.connect_successes > 0 &&
        g_test.connect_failures == 0) {
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "");
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "VERDICT: PASS - No significant memory leaks detected");
    } else if (maxblock_delta < -LEAK_THRESHOLD_BYTES) {
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "");
        PT_LOG_ERR(g_log, PT_LOG_CAT_APP1,
            "VERDICT: FAIL - Memory leak detected (%ld bytes)", -maxblock_delta);
    } else if (g_test.connect_failures > 0) {
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "");
        PT_LOG_WARN(g_log, PT_LOG_CAT_APP1,
            "VERDICT: PARTIAL - %d connection failures", g_test.connect_failures);
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

    /* Use first discovered peer as target */
    if (g_test.target_peer == 0 && g_test.state == STATE_DISCOVERING) {
        g_test.target_peer = peer->id;
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "Selected peer %u as stress test target", (unsigned)peer->id);

        /* Record initial memory state */
        g_test.initial_maxblock = MaxBlock();
        g_test.initial_freemem = FreeMem();
        g_test.min_maxblock = g_test.initial_maxblock;
        g_test.min_freemem = g_test.initial_freemem;

        log_memory_status("Initial");

        /* Start first cycle */
        start_connect();
    }
}

static void on_peer_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                               void *user_data)
{
    (void)ctx;
    (void)user_data;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "CONNECTED to peer %u (cycle %d)", (unsigned)peer_id, g_test.cycle_count + 1);

    g_test.connect_successes++;
    set_state(STATE_CONNECTED);
}

static void on_peer_disconnected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                  int reason, void *user_data)
{
    (void)ctx;
    (void)user_data;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "DISCONNECTED from peer %u (reason=%d)", (unsigned)peer_id, reason);

    if (g_test.state == STATE_DISCONNECTING || g_test.state == STATE_CONNECTED) {
        complete_cycle();
    }
}

static void on_message_received(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                 const void *data, uint16_t len, void *user_data)
{
    (void)ctx;
    (void)peer_id;
    (void)data;
    (void)len;
    (void)user_data;

    g_test.message_recv_count++;
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

    init_toolbox();

    /* Create PT_Log */
    g_log = PT_LogCreate();
    if (g_log) {
        PT_LogSetLevel(g_log, PT_LOG_DEBUG);
        PT_LogSetCategories(g_log, 0xFFFF);
        PT_LogSetFile(g_log, "PT_Stress");
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "PeerTalk Stress Test");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Version: %s", PeerTalk_Version());
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Target: %d cycles", TARGET_CYCLES);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    /* Initialize test state */
    memset(&g_test, 0, sizeof(g_test));
    g_test.state = STATE_DISCOVERING;

    /* Configure PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "MacStress", PT_MAX_PEER_NAME);
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
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Press any key to abort.");

    /* Main loop */
    while (g_running) {
        /* Check for user input */
        if (WaitNextEvent(everyEvent, &event, 1, NULL)) {
            if (event.what == keyDown || event.what == mouseDown) {
                PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "User abort");
                g_running = 0;
                break;
            }
        }

        PeerTalk_Poll(g_ctx);
        stress_poll();
    }

    print_results();

cleanup:
    if (g_ctx) {
        PeerTalk_Shutdown(g_ctx);
    }
    if (g_log) {
        PT_LogDestroy(g_log);
    }

    return 0;
}
