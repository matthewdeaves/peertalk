/**
 * @file test_mactcp.c
 * @brief MacTCP Hardware Test Application
 *
 * Simple test app for verifying MacTCP networking on real hardware.
 * Runs discovery and logs results via PT_Log.
 *
 * Build with Retro68, transfer to Mac, run to test.
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

#include "peertalk.h"
#include "pt_log.h"

/* Test state */
static PeerTalk_Context *g_ctx = NULL;
static PT_Log *g_log = NULL;
static int g_peers_found = 0;
static int g_connected = 0;
static unsigned long g_start_ticks = 0;

/* Callbacks */
static void on_peer_discovered(PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer,
                                void *user_data)
{
    const char *name;
    (void)user_data;

    name = PeerTalk_GetPeerName(ctx, peer->name_idx);
    if (!name) name = "(unknown)";

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "DISCOVERED peer %u: \"%s\" at %lu.%lu.%lu.%lu:%u",
        (unsigned)peer->id, name,
        (peer->address >> 24) & 0xFF,
        (peer->address >> 16) & 0xFF,
        (peer->address >> 8) & 0xFF,
        peer->address & 0xFF,
        (unsigned)peer->port);

    g_peers_found++;
}

static void on_peer_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                               void *user_data)
{
    (void)ctx;
    (void)user_data;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "CONNECTED to peer %u", (unsigned)peer_id);

    g_connected++;
}

static void on_peer_disconnected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                  int reason, void *user_data)
{
    (void)ctx;
    (void)user_data;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "DISCONNECTED from peer %u (reason=%d)", (unsigned)peer_id, reason);
}

static void on_message_received(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                 const void *data, uint16_t len, void *user_data)
{
    (void)ctx;
    (void)user_data;

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "MESSAGE from peer %u: %u bytes", (unsigned)peer_id, (unsigned)len);

    /* Echo first 32 bytes as hex */
    if (len > 0) {
        char hex[65];
        int i;
        int show = (len > 32) ? 32 : len;
        for (i = 0; i < show; i++) {
            sprintf(&hex[i*2], "%02X", ((unsigned char*)data)[i]);
        }
        hex[show*2] = '\0';
        PT_LOG_DEBUG(g_log, PT_LOG_CAT_APP1, "  data: %s%s", hex, len > 32 ? "..." : "");
    }
}

/* Initialize Toolbox */
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

/* Main entry point */
int main(void)
{
    PeerTalk_Config config;
    PeerTalk_Callbacks callbacks;
    EventRecord event;
    int running = 1;
    unsigned long now;
    unsigned long last_status = 0;
    int test_duration_secs = 60;  /* Run for 60 seconds */

    /* Initialize Mac Toolbox */
    init_toolbox();

    /* Create PT_Log - outputs to file "PT_Log" in app folder */
    g_log = PT_LogCreate();
    if (g_log) {
        PT_LogSetLevel(g_log, PT_LOG_DEBUG);
        PT_LogSetCategories(g_log, 0xFFFF);  /* All categories */
        PT_LogSetFile(g_log, "PT_Log");
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "PeerTalk MacTCP Test");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Version: %s", PeerTalk_Version());
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "========================================");

    /* Configure PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "MacTest", PT_MAX_PEER_NAME);
    config.max_peers = 8;
    config.discovery_port = 7353;  /* Default discovery port */
    config.tcp_port = 7354;        /* Default TCP port */

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Initializing PeerTalk...");

    /* Initialize PeerTalk */
    g_ctx = PeerTalk_Init(&config);
    if (!g_ctx) {
        PT_LOG_ERR(g_log, PT_LOG_CAT_APP1,
            "FAILED to initialize PeerTalk!");
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "Check that MacTCP is installed and configured.");
        goto cleanup;
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "PeerTalk initialized successfully");

    /* Set callbacks */
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_connected = on_peer_connected;
    callbacks.on_peer_disconnected = on_peer_disconnected;
    callbacks.on_message_received = on_message_received;
    PeerTalk_SetCallbacks(g_ctx, &callbacks);

    /* Start discovery */
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Starting discovery on port %u...", (unsigned)config.discovery_port);

    {
        int disc_err = PeerTalk_StartDiscovery(g_ctx);
        if (disc_err != 0) {
            PT_LOG_ERR(g_log, PT_LOG_CAT_APP1,
                "FAILED to start discovery! Error code: %d", disc_err);
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "Common causes: MacTCP not configured, port in use");
            goto cleanup;
        }
    }

    /* Start listening for TCP connections */
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Starting TCP listener on port %u...", (unsigned)config.tcp_port);

    {
        int listen_err = PeerTalk_StartListening(g_ctx);
        if (listen_err != 0) {
            PT_LOG_ERR(g_log, PT_LOG_CAT_APP1,
                "FAILED to start listening! Error code: %d", listen_err);
            goto cleanup;
        }
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Running test for %d seconds...", test_duration_secs);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Press any key or click to exit early.");

    g_start_ticks = TickCount();
    last_status = g_start_ticks;

    /* Main event loop */
    while (running) {
        /* Check for user input (key or click to exit) */
        if (WaitNextEvent(everyEvent, &event, 1, NULL)) {
            switch (event.what) {
            case mouseDown:
            case keyDown:
            case autoKey:
                running = 0;
                break;
            }
        }

        /* Poll PeerTalk */
        PeerTalk_Poll(g_ctx);

        /* Status update every 10 seconds */
        now = TickCount();
        if ((now - last_status) >= 600) {  /* 10 seconds */
            unsigned long elapsed = (now - g_start_ticks) / 60;
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "STATUS: %lu sec, peers_found=%d, connected=%d",
                elapsed, g_peers_found, g_connected);
            last_status = now;
        }

        /* Check timeout */
        if ((now - g_start_ticks) >= (unsigned long)(test_duration_secs * 60)) {
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "Test duration reached, exiting.");
            running = 0;
        }
    }

cleanup:
    /* Summary */
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "TEST COMPLETE");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Peers discovered: %d", g_peers_found);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Connections: %d", g_connected);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Result: %s", (g_peers_found > 0 || g_connected > 0) ? "PASS" : "NO PEERS FOUND");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "========================================");

    /* Shutdown */
    if (g_ctx) {
        PeerTalk_Shutdown(g_ctx);
    }

    if (g_log) {
        PT_LogDestroy(g_log);
    }

    return 0;
}
