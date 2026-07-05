/*
 * test_mesh.c -- PeerTalk mesh-stability / dead-link stress test.
 *
 * Purpose: reproduce and detect the simultaneous-connect mesh failure where a
 * pair of peers forms a TCP connection whose tiebreaker leaves it half-dead --
 * both sides report CONNECTED, but one direction delivers no data (the exact
 * BomberTalk symptom: a player frozen at spawn while everyone else sees them
 * move). test_multi broadcasts once and exits, so it can't see a link that
 * dies after formation, and its short window misses slow-booting Macs.
 *
 * This app: auto-connects to every discovered peer (max tiebreaker stress),
 * runs for a long fixed window (default 90s, --secs N), sends a heartbeat every
 * 3s over PT_RELIABLE, and tracks heartbeats RECEIVED per peer. A peer that is
 * CONNECTED but from which we receive zero heartbeats is a DEAD LINK -> FAIL.
 * Periodic status dumps show per-peer heartbeat counts over time so a link that
 * dies mid-run (counter freezes while still connected) is visible too.
 *
 * On disconnect it retries connecting, so transient drops that recover are
 * distinguished from permanent dead links.
 *
 * PASS: >=1 live link and 0 dead links. FAIL: any dead link.
 */

#include "test_common.h"

#define HB_MSG      MSG_CHAT
#define MAX_TRACK   8

static PT_Context *g_ctx;
static char g_track_addr[MAX_TRACK][32];
static int  g_track_hb[MAX_TRACK];
static int  g_track_count = 0;
static int  g_connects = 0;
static int  g_disconnects = 0;
static int  g_errors = 0;
static int  g_num_discovered = 0;
static int  g_conn_threshold = 2;  /* --peers N: defer connects until N discovered
                                      (default 2 = a 3-machine mesh; 0 = connect
                                      immediately per discovery). */
static int  g_burst_done = 0;
static int  g_use_direction = 1;   /* --nodir turns off the lower-IP-dials rule */
static unsigned long g_first_disc_time = 0;  /* for the barrier fallback timer */
static const char *g_name = "Unnamed";

/* Should this node dial `p`? Only if it's a fresh/dropped peer AND (unless
   --nodir) this node is the designated initiator for the pair. Applying the
   direction rule everywhere means each pair is dialed from exactly one side. */
static int should_dial(PT_Peer *p)
{
    if (!test_should_connect(p)) return 0;
    if (g_use_direction && !PT_ShouldInitiate(g_ctx, p)) return 0;
    return 1;
}

/* Connect to every currently-discovered (or previously-disconnected) peer in
   one pass. Called either immediately per-discovery (threshold 0) or, in
   barrier mode, all at once when the discovered count reaches the threshold --
   which replicates BomberTalk's game-start burst where every peer, already
   sitting discovered in the lobby, dials the whole mesh simultaneously. That
   synchronized burst is what stresses the simultaneous-connect tiebreaker. */
static void connect_all_discovered(void)
{
    int pc = PT_GetPeerCount(g_ctx);
    int k;
    for (k = 0; k < pc; k++) {
        PT_Peer *pp = PT_GetPeer(g_ctx, k);
        if (pp && should_dial(pp)) PT_Connect(g_ctx, pp);
    }
}

/* Index into the per-peer heartbeat table for an address, allocating on
   first sight. Returns -1 if the (small) table is full. */
static int track_index(const char *addr)
{
    int i;
    if (!addr) return -1;
    for (i = 0; i < g_track_count; i++) {
        if (strcmp(g_track_addr[i], addr) == 0) return i;
    }
    if (g_track_count < MAX_TRACK) {
        strncpy(g_track_addr[g_track_count], addr, 31);
        g_track_addr[g_track_count][31] = '\0';
        g_track_hb[g_track_count] = 0;
        return g_track_count++;
    }
    return -1;
}

static void on_discovered(PT_Peer *p, void *d)
{
    (void)d;
    g_num_discovered++;
    if (g_first_disc_time == 0) g_first_disc_time = test_time_sec();
    TEST_LOG("[DISC] %s @ %s (discovered=%d)",
             PT_PeerName(p), PT_PeerAddress(p), g_num_discovered);
    /* Connect immediately if we're not waiting at the barrier: either barrier
       disabled (threshold 0), or it has already fired (a peer discovered after
       the burst must still be dialed, else a late-booting peer is never
       connected). Otherwise defer to the barrier burst in main(). */
    if (should_dial(p) && (g_conn_threshold == 0 || g_burst_done)) {
        PT_Connect(g_ctx, p);
    }
}

static void on_connected(PT_Peer *p, void *d)
{
    (void)d;
    g_connects++;
    TEST_LOG("[CONN] %s @ %s (total=%d)",
             PT_PeerName(p), PT_PeerAddress(p), g_connects);
}

static void on_disconnected(PT_Peer *p, PT_DisconnectReason r, void *d)
{
    (void)d;
    g_disconnects++;
    TEST_WARN("[DISC-] %s @ %s reason=%s (total=%d)",
              PT_PeerName(p), PT_PeerAddress(p), test_reason_str(r),
              g_disconnects);
    /* Retry: a permanent dead link will keep failing here; a transient
       drop will recover. Both are visible in the final report. */
    if (should_dial(p)) PT_Connect(g_ctx, p);
}

static void on_heartbeat(PT_Peer *p, const void *data, size_t len, void *u)
{
    int i;
    (void)data; (void)len; (void)u;
    i = track_index(PT_PeerAddress(p));
    if (i >= 0) g_track_hb[i]++;
}

static void on_error(PT_Peer *p, PT_Status e, const char *desc, void *d)
{
    (void)d;
    g_errors++;
    TEST_WARN("[ERR] %s code=%d: %s",
              p ? PT_PeerAddress(p) : "(none)", (int)e, desc ? desc : "");
}

int main(int argc, char **argv)
{
    unsigned long start, last_hb, last_status, secs;
    int seq, i, dead, live;

    g_name = test_parse_name(argc, argv);
    secs = 90;
    for (i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--secs") == 0) secs = (unsigned long)atoi(argv[i + 1]);
        if (strcmp(argv[i], "--peers") == 0) g_conn_threshold = atoi(argv[i + 1]);
    }
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--nodir") == 0) g_use_direction = 0;
    }

    test_install_signal_handler();
    test_init_toolbox();
    test_init_logging("test_mesh");

    TEST_LOG("=== test_mesh: %s (window %lus) ===", g_name, secs);

    if (PT_Init(&g_ctx, g_name) != PT_OK) {
        TEST_WARN("PT_Init FAILED");
        test_shutdown_logging();
        return 1;
    }
    test_remote_log_enable(g_ctx);

    PT_RegisterMessage(g_ctx, HB_MSG, PT_RELIABLE);
    PT_OnPeerDiscovered(g_ctx, on_discovered, NULL);
    PT_OnConnected(g_ctx, on_connected, NULL);
    PT_OnDisconnected(g_ctx, on_disconnected, NULL);
    PT_OnMessage(g_ctx, HB_MSG, on_heartbeat, NULL);
    PT_OnError(g_ctx, on_error, NULL);

    if (PT_StartDiscovery(g_ctx) != PT_OK) {
        TEST_WARN("PT_StartDiscovery FAILED");
        PT_Shutdown(g_ctx);
        test_shutdown_logging();
        return 1;
    }
    TEST_LOG("Discovery started; dial-direction=%s threshold=%d",
             g_use_direction ? "lower-IP-only" : "bidirectional(nodir)",
             g_conn_threshold);

    start = test_time_sec();
    last_hb = 0;
    last_status = 0;
    seq = 0;

    while (g_running && test_time_sec() - start < secs) {
        unsigned long now = test_time_sec();
        PT_Poll(g_ctx);

        /* Barrier: dial the whole mesh in one synchronized burst once we've
           discovered the expected peers (or after a fallback timeout so a
           smaller mesh than the threshold still connects). */
        if (g_conn_threshold > 0 && !g_burst_done && g_first_disc_time != 0 &&
            (g_num_discovered >= g_conn_threshold ||
             now - g_first_disc_time >= 25)) {
            g_burst_done = 1;
            TEST_LOG("Barrier reached (%d discovered): connecting to all at once",
                     g_num_discovered);
            connect_all_discovered();
        }

        /* Heartbeat broadcast every 3s */
        if (now - last_hb >= 3) {
            char buf[48];
            sprintf(buf, "HB %s %d", g_name, seq++);
            PT_Broadcast(g_ctx, HB_MSG, buf, strlen(buf));
            last_hb = now;
        }

        /* Status dump every 10s: connected count + per-peer HB received.
           A frozen HB count for a still-connected peer = link died mid-run. */
        if (now - last_status >= 10) {
            int pc = PT_GetPeerCount(g_ctx);
            int cc = 0, k;
            for (k = 0; k < pc; k++) {
                PT_Peer *pp = PT_GetPeer(g_ctx, k);
                if (pp && PT_GetPeerState(pp) == PT_PEER_CONNECTED) cc++;
            }
            TEST_LOG("[%lus] connected=%d disconnects=%d errors=%d",
                     now - start, cc, g_disconnects, g_errors);
            for (k = 0; k < g_track_count; k++) {
                TEST_LOG("    HB recv from %s: %d",
                         g_track_addr[k], g_track_hb[k]);
            }
            last_status = now;
        }

        test_sleep_ms(16);
    }

    /* Final report: dead-link detection. */
    TEST_LOG("=== test_mesh report ===");
    dead = 0;
    live = 0;
    {
        int pc = PT_GetPeerCount(g_ctx);
        int k;
        for (k = 0; k < pc; k++) {
            PT_Peer *pp = PT_GetPeer(g_ctx, k);
            if (pp && PT_GetPeerState(pp) == PT_PEER_CONNECTED) {
                int ti = track_index(PT_PeerAddress(pp));
                int hb = (ti >= 0) ? g_track_hb[ti] : 0;
                if (hb == 0) {
                    TEST_WARN("DEAD LINK: CONNECTED to %s but received 0 "
                              "heartbeats", PT_PeerAddress(pp));
                    dead++;
                } else {
                    TEST_LOG("LIVE LINK: %s hb=%d", PT_PeerAddress(pp), hb);
                    live++;
                }
            }
        }
    }
    TEST_LOG("connects=%d disconnects=%d errors=%d live=%d dead=%d",
             g_connects, g_disconnects, g_errors, live, dead);

    if (dead > 0) {
        TEST_LOG("*** FAIL: %d dead link(s) ***", dead);
    } else if (live > 0) {
        TEST_LOG("*** PASS ***");
    } else {
        TEST_LOG("*** INCONCLUSIVE: no peers connected ***");
    }

    PT_Shutdown(g_ctx);
    test_shutdown_logging();
    return (dead > 0) ? 1 : 0;
}
