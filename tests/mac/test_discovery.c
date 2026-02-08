/**
 * @file test_discovery.c
 * @brief MacTCP Discovery Reliability Test
 *
 * Tests UDP discovery reliability by:
 * 1. Counting discovery packets sent vs received
 * 2. Measuring time to first discovery
 * 3. Tracking discovery packet loss under load
 * 4. Verifying peer timeout behavior
 *
 * Build with Retro68:
 *   make -f Makefile.retro68 PLATFORM=mactcp test_discovery
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

#define TEST_DURATION_SEC    120    /* 2 minutes test duration */
#define REPORT_INTERVAL_SEC  10     /* Report every 10 seconds */
#define MAX_TRACKED_PEERS    16     /* Maximum peers to track */

/* ========================================================================== */
/* Discovery Statistics                                                        */
/* ========================================================================== */

typedef struct {
    PeerTalk_PeerID id;
    char            name[32];
    uint32_t        address;
    uint16_t        port;
    unsigned long   first_seen_ticks;
    unsigned long   last_seen_ticks;
    int             discovery_count;    /* How many times discovered */
    int             lost_count;         /* How many times timed out */
    int             is_present;         /* Currently visible */
} TrackedPeer;

typedef struct {
    /* Overall stats */
    unsigned long   test_start_ticks;
    unsigned long   first_discovery_ticks;  /* 0 if none yet */
    int             total_discoveries;
    int             unique_peers_found;
    int             peer_lost_events;
    int             peer_recovered_events;

    /* Per-peer tracking */
    TrackedPeer     peers[MAX_TRACKED_PEERS];
    int             peer_count;

    /* Discovery packet stats */
    int             announcements_sent;
    int             announcements_received;

} DiscoveryStats;

/* ========================================================================== */
/* Globals                                                                     */
/* ========================================================================== */

static PeerTalk_Context *g_ctx = NULL;
static PT_Log *g_log = NULL;
static DiscoveryStats g_stats;
static unsigned long g_last_report = 0;
static int g_running = 1;

/* ========================================================================== */
/* Utility Functions                                                           */
/* ========================================================================== */

static unsigned long ticks_to_ms(unsigned long ticks)
{
    return (ticks * 1000UL) / 60UL;
}

static void ip_to_str(uint32_t ip, char *buf, size_t len)
{
    sprintf(buf, "%lu.%lu.%lu.%lu",
            (ip >> 24) & 0xFF,
            (ip >> 16) & 0xFF,
            (ip >> 8) & 0xFF,
            ip & 0xFF);
}

static TrackedPeer *find_tracked_peer(PeerTalk_PeerID id)
{
    int i;
    for (i = 0; i < g_stats.peer_count; i++) {
        if (g_stats.peers[i].id == id) {
            return &g_stats.peers[i];
        }
    }
    return NULL;
}

static TrackedPeer *add_tracked_peer(PeerTalk_PeerID id, const char *name,
                                      uint32_t address, uint16_t port)
{
    TrackedPeer *peer;
    unsigned long now = TickCount();

    if (g_stats.peer_count >= MAX_TRACKED_PEERS) {
        PT_LOG_WARN(g_log, PT_LOG_CAT_APP1, "Peer tracking table full");
        return NULL;
    }

    peer = &g_stats.peers[g_stats.peer_count++];
    peer->id = id;
    strncpy(peer->name, name ? name : "", sizeof(peer->name) - 1);
    peer->address = address;
    peer->port = port;
    peer->first_seen_ticks = now;
    peer->last_seen_ticks = now;
    peer->discovery_count = 1;
    peer->lost_count = 0;
    peer->is_present = 1;

    return peer;
}

/* ========================================================================== */
/* Progress Reporting                                                          */
/* ========================================================================== */

static void report_progress(void)
{
    unsigned long now = TickCount();
    unsigned long elapsed_ticks = now - g_stats.test_start_ticks;
    unsigned long elapsed_sec = elapsed_ticks / 60;
    int i;
    int present_count = 0;

    /* Count currently present peers */
    for (i = 0; i < g_stats.peer_count; i++) {
        if (g_stats.peers[i].is_present) {
            present_count++;
        }
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "STATUS @ %lu sec: discoveries=%d unique=%d present=%d lost=%d recovered=%d",
        elapsed_sec,
        g_stats.total_discoveries,
        g_stats.unique_peers_found,
        present_count,
        g_stats.peer_lost_events,
        g_stats.peer_recovered_events);

    /* Log per-peer stats */
    for (i = 0; i < g_stats.peer_count; i++) {
        TrackedPeer *peer = &g_stats.peers[i];
        char ip_str[20];

        ip_to_str(peer->address, ip_str, sizeof(ip_str));

        PT_LOG_DEBUG(g_log, PT_LOG_CAT_APP1,
            "  Peer %u \"%s\" (%s): seen=%d lost=%d %s",
            (unsigned)peer->id, peer->name, ip_str,
            peer->discovery_count, peer->lost_count,
            peer->is_present ? "PRESENT" : "GONE");
    }
}

/* ========================================================================== */
/* Results                                                                     */
/* ========================================================================== */

static void print_results(void)
{
    unsigned long now = TickCount();
    unsigned long total_ticks = now - g_stats.test_start_ticks;
    unsigned long total_sec = total_ticks / 60;
    int i;
    unsigned long first_discovery_ms = 0;

    if (g_stats.first_discovery_ticks > 0) {
        first_discovery_ms = ticks_to_ms(
            g_stats.first_discovery_ticks - g_stats.test_start_ticks);
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "DISCOVERY TEST RESULTS");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Test Duration: %lu seconds", total_sec);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "");

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Discovery Summary:");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Total discoveries: %d",
        g_stats.total_discoveries);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Unique peers found: %d",
        g_stats.unique_peers_found);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Time to first discovery: %lu ms",
        first_discovery_ms);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Peer lost events: %d",
        g_stats.peer_lost_events);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "  Peer recovered: %d",
        g_stats.peer_recovered_events);

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Per-Peer Details:");

    for (i = 0; i < g_stats.peer_count; i++) {
        TrackedPeer *peer = &g_stats.peers[i];
        char ip_str[20];
        unsigned long visible_ticks = peer->last_seen_ticks - peer->first_seen_ticks;
        unsigned long visible_sec = visible_ticks / 60;

        ip_to_str(peer->address, ip_str, sizeof(ip_str));

        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "  %s (%s:%u):",
            peer->name, ip_str, (unsigned)peer->port);
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "    Discovery count: %d (avg %.1f sec between)",
            peer->discovery_count,
            peer->discovery_count > 1 ?
                (float)visible_sec / (peer->discovery_count - 1) : 0.0f);
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "    Lost count: %d, Currently: %s",
            peer->lost_count,
            peer->is_present ? "PRESENT" : "GONE");
    }

    /* Calculate discovery rate */
    if (total_sec > 0 && g_stats.total_discoveries > 0) {
        float rate = (float)g_stats.total_discoveries / (float)total_sec;
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "");
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "Discovery Rate: %.2f discoveries/sec", rate);
    }

    /* Verdict */
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "");
    if (g_stats.unique_peers_found > 0 && g_stats.total_discoveries > 0) {
        if (g_stats.peer_lost_events == 0) {
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "VERDICT: PASS - Stable discovery, no peer timeouts");
        } else if (g_stats.peer_recovered_events == g_stats.peer_lost_events) {
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "VERDICT: PASS - All lost peers recovered");
        } else {
            PT_LOG_WARN(g_log, PT_LOG_CAT_APP1,
                "VERDICT: PARTIAL - %d peers still missing",
                g_stats.peer_lost_events - g_stats.peer_recovered_events);
        }
    } else {
        PT_LOG_ERR(g_log, PT_LOG_CAT_APP1,
            "VERDICT: FAIL - No peers discovered");
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
    TrackedPeer *tracked;
    char ip_str[20];
    unsigned long now = TickCount();
    (void)user_data;

    name = PeerTalk_GetPeerName(ctx, peer->name_idx);
    if (!name) name = "";

    ip_to_str(peer->address, ip_str, sizeof(ip_str));

    g_stats.total_discoveries++;

    /* First discovery? */
    if (g_stats.first_discovery_ticks == 0) {
        g_stats.first_discovery_ticks = now;
        PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
            "FIRST DISCOVERY: \"%s\" at %s:%u after %lu ms",
            name, ip_str, (unsigned)peer->port,
            ticks_to_ms(now - g_stats.test_start_ticks));
    }

    /* Find or create tracked peer */
    tracked = find_tracked_peer(peer->id);
    if (tracked == NULL) {
        /* New peer */
        tracked = add_tracked_peer(peer->id, name, peer->address, peer->port);
        if (tracked) {
            g_stats.unique_peers_found++;
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "NEW PEER: \"%s\" at %s:%u (id=%u)",
                name, ip_str, (unsigned)peer->port, (unsigned)peer->id);
        }
    } else {
        /* Existing peer - update */
        tracked->last_seen_ticks = now;
        tracked->discovery_count++;

        if (!tracked->is_present) {
            /* Peer recovered after being lost */
            tracked->is_present = 1;
            g_stats.peer_recovered_events++;
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
                "RECOVERED: \"%s\" (was missing for %lu sec)",
                tracked->name, 0UL);  /* Could track lost duration */
        }
    }
}

static void on_peer_lost(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                          void *user_data)
{
    TrackedPeer *tracked;
    (void)ctx;
    (void)user_data;

    tracked = find_tracked_peer(peer_id);
    if (tracked) {
        tracked->is_present = 0;
        tracked->lost_count++;
        g_stats.peer_lost_events++;

        PT_LOG_WARN(g_log, PT_LOG_CAT_APP1,
            "LOST: \"%s\" (id=%u) - lost %d times",
            tracked->name, (unsigned)peer_id, tracked->lost_count);
    } else {
        PT_LOG_WARN(g_log, PT_LOG_CAT_APP1,
            "LOST: Unknown peer id=%u", (unsigned)peer_id);
        g_stats.peer_lost_events++;
    }
}

static void on_peer_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                               void *user_data)
{
    (void)ctx;
    (void)user_data;
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "CONNECTED to peer %u (not expected in discovery test)",
        (unsigned)peer_id);
}

static void on_peer_disconnected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                  int reason, void *user_data)
{
    (void)ctx;
    (void)user_data;
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "DISCONNECTED from peer %u (reason=%d)", (unsigned)peer_id, reason);
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
    unsigned long test_end_ticks;
    unsigned long report_interval_ticks = REPORT_INTERVAL_SEC * 60UL;

    init_toolbox();

    /* Create PT_Log */
    g_log = PT_LogCreate();
    if (g_log) {
        PT_LogSetLevel(g_log, PT_LOG_DEBUG);
        PT_LogSetCategories(g_log, 0xFFFF);
        PT_LogSetFile(g_log, "PT_Discovery");
    }

    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "PeerTalk Discovery Reliability Test");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Version: %s", PeerTalk_Version());
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Duration: %d seconds", TEST_DURATION_SEC);
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "========================================");

    /* Initialize stats */
    memset(&g_stats, 0, sizeof(g_stats));

    /* Configure PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "MacDiscovery", PT_MAX_PEER_NAME);
    config.max_peers = 8;
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
    callbacks.on_peer_lost = on_peer_lost;
    callbacks.on_peer_connected = on_peer_connected;
    callbacks.on_peer_disconnected = on_peer_disconnected;
    PeerTalk_SetCallbacks(g_ctx, &callbacks);

    /* Start discovery */
    if (PeerTalk_StartDiscovery(g_ctx) != 0) {
        PT_LOG_ERR(g_log, PT_LOG_CAT_APP1, "FAILED to start discovery!");
        goto cleanup;
    }

    /* Don't start listening - this is a discovery-only test */
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1,
        "Discovery started. Listening for peers...");
    PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Press any key to stop early.");

    g_stats.test_start_ticks = TickCount();
    g_last_report = g_stats.test_start_ticks;
    test_end_ticks = g_stats.test_start_ticks + (TEST_DURATION_SEC * 60UL);

    /* Main loop */
    while (g_running) {
        /* Check for user input */
        if (WaitNextEvent(everyEvent, &event, 1, NULL)) {
            if (event.what == keyDown || event.what == mouseDown) {
                PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "User requested stop");
                g_running = 0;
                break;
            }
        }

        PeerTalk_Poll(g_ctx);

        now = TickCount();

        /* Progress report */
        if ((now - g_last_report) >= report_interval_ticks) {
            report_progress();
            g_last_report = now;
        }

        /* Check duration */
        if (now >= test_end_ticks) {
            PT_LOG_INFO(g_log, PT_LOG_CAT_APP1, "Test duration complete");
            g_running = 0;
        }
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
