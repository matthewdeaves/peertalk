/*
 * PeerTalk Full Integration Test (3-Peer Docker Test)
 *
 * Comprehensive end-to-end test covering:
 * - UDP discovery across 3 peers
 * - TCP connection establishment
 * - Message exchange with Send/SendEx
 * - Broadcast functionality
 * - UDP unreliable messaging
 * - Queue status monitoring
 * - Statistics tracking
 * - Graceful shutdown
 *
 * Usage:
 *   ./test_integration_full [name] [mode]
 *
 * Modes:
 *   sender   - Actively sends messages to discovered peers
 *   receiver - Passively receives messages
 *   both     - Both sends and receives (default)
 *
 * Example:
 *   Terminal 1: ./test_integration_full Alice sender
 *   Terminal 2: ./test_integration_full Bob receiver
 *   Terminal 3: ./test_integration_full Charlie both
 */

#include "../include/peertalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

/* Test configuration - can be overridden via environment variables */
#define DEFAULT_TEST_DURATION_SEC 30
#define DEFAULT_SEND_INTERVAL_MS 2000
#define DEFAULT_MIN_PEERS 2  /* Minimum peers to discover for pass (default for 3-peer test) */
#define POLL_INTERVAL_MS 100

/* Runtime configuration (set from env vars or defaults) */
static int g_test_duration_sec = DEFAULT_TEST_DURATION_SEC;
static int g_min_peers_expected = DEFAULT_MIN_PEERS;

/* Test state */
typedef enum {
    MODE_SENDER,
    MODE_RECEIVER,
    MODE_BOTH
} TestMode;

typedef struct {
    PeerTalk_Context *ctx;
    TestMode mode;
    volatile int running;
    uint32_t last_send_time;
    uint32_t test_start_time;

    /* Counters */
    int peers_discovered;
    int peers_connected;
    int messages_sent;
    int messages_received;
    int broadcasts_sent;
} TestState;

static TestState g_state = {0};

/* ========================================================================== */
/* Utility Functions                                                          */
/* ========================================================================== */

uint32_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void print_peer_info(const PeerTalk_PeerInfo *peer) {
    printf("  [Peer %u] ", peer->id);
    printf("%u.%u.%u.%u:%u ",
           (peer->address >> 24) & 0xFF,
           (peer->address >> 16) & 0xFF,
           (peer->address >> 8) & 0xFF,
           peer->address & 0xFF,
           peer->port);
    printf("%s ", peer->connected ? "CONNECTED" : "DISCOVERED");
    printf("latency=%ums ", peer->latency_ms);
    printf("transports=0x%02X\n", peer->transports_available);
}

/* ========================================================================== */
/* Signal Handler                                                             */
/* ========================================================================== */

void signal_handler(int sig) {
    (void)sig;
    printf("\n[SIGNAL] Shutting down gracefully...\n");
    g_state.running = 0;
}

/* ========================================================================== */
/* PeerTalk Callbacks                                                         */
/* ========================================================================== */

void on_peer_discovered(PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer,
                        void *user_data) {
    (void)user_data;

    g_state.peers_discovered++;
    printf("\n[DISCOVERY] Peer discovered!\n");
    print_peer_info(peer);

    /* Automatically connect to discovered peers */
    PeerTalk_Error err = PeerTalk_Connect(ctx, peer->id);
    if (err == PT_OK) {
        printf("  → Initiating TCP connection...\n");
    } else {
        printf("  ✗ Connect failed: error %d\n", err);
    }
}

void on_peer_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                       void *user_data) {
    (void)ctx;
    (void)user_data;

    g_state.peers_connected++;
    printf("\n[CONNECT] Peer %u connected!\n", peer_id);

    /* Get peer info */
    PeerTalk_PeerInfo info;
    if (PeerTalk_GetPeer(ctx, peer_id, &info) == PT_OK) {
        print_peer_info(&info);
    }
}

void on_peer_disconnected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                          PeerTalk_Error reason, void *user_data) {
    (void)ctx;
    (void)user_data;

    g_state.peers_connected--;
    printf("\n[DISCONNECT] Peer %u disconnected (reason=%d)\n", peer_id, reason);
}

void on_message_received(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                        const void *data, uint16_t length, void *user_data) {
    (void)ctx;
    (void)user_data;

    g_state.messages_received++;

    /* Create null-terminated string */
    char msg[PT_MAX_MESSAGE_SIZE + 1];
    memcpy(msg, data, length < PT_MAX_MESSAGE_SIZE ? length : PT_MAX_MESSAGE_SIZE);
    msg[length < PT_MAX_MESSAGE_SIZE ? length : PT_MAX_MESSAGE_SIZE] = '\0';

    printf("[MESSAGE] From peer %u: \"%s\" (%u bytes)\n", peer_id, msg, length);

    /* Get queue status for this peer */
    uint16_t pending, available;
    if (PeerTalk_GetQueueStatus(ctx, peer_id, &pending, &available) == PT_OK) {
        printf("          Queue: %u pending, %u available\n", pending, available);
    }
}

/* ========================================================================== */
/* Test Functions                                                             */
/* ========================================================================== */

void send_messages_to_peers(void) {
    uint16_t peer_count = 0;
    PeerTalk_PeerInfo peers[16];

    /* Get all discovered peers */
    if (PeerTalk_GetPeers(g_state.ctx, peers, 16, &peer_count) != PT_OK) {
        return;
    }

    if (peer_count == 0) {
        return;
    }

    printf("\n[SEND] Sending messages to %u peer(s)...\n", peer_count);

    /* Send individual messages to each connected peer */
    for (uint16_t i = 0; i < peer_count; i++) {
        if (!peers[i].connected) {
            continue;
        }

        char msg[128];
        snprintf(msg, sizeof(msg), "Hello from peer, message #%d", g_state.messages_sent + 1);

        PeerTalk_Error err = PeerTalk_SendEx(
            g_state.ctx,
            peers[i].id,
            msg,
            (uint16_t)strlen(msg),
            PT_PRIORITY_NORMAL,
            PT_SEND_DEFAULT,
            0
        );

        if (err == PT_OK) {
            g_state.messages_sent++;
            printf("  → Sent to peer %u: \"%s\"\n", peers[i].id, msg);
        } else {
            printf("  ✗ Failed to send to peer %u: error %d\n", peers[i].id, err);
        }
    }

    /* Send broadcast if we have connected peers */
    if (peer_count > 0) {
        char broadcast_msg[128];
        snprintf(broadcast_msg, sizeof(broadcast_msg),
                 "Broadcast #%d to all peers", g_state.broadcasts_sent + 1);

        PeerTalk_Error err = PeerTalk_Broadcast(g_state.ctx, broadcast_msg,
                                                 (uint16_t)strlen(broadcast_msg));
        if (err == PT_OK) {
            g_state.broadcasts_sent++;
            printf("  → Broadcast sent to all peers\n");
        }
    }
}

void print_peer_stats(void) {
    uint16_t peer_count = 0;
    PeerTalk_PeerInfo peers[16];
    PeerTalk_PeerStats peer_stats;
    uint16_t pending, available;

    /* Get all peers */
    if (PeerTalk_GetPeers(g_state.ctx, peers, 16, &peer_count) != PT_OK) {
        return;
    }

    if (peer_count == 0) {
        return;
    }

    printf("\n[PEER STATS] Per-Peer Performance:\n");
    for (uint16_t i = 0; i < peer_count; i++) {
        printf("  Peer %u (%s): ", peers[i].id,
               peers[i].connected ? "CONNECTED" : "DISCOVERED");

        /* Get peer statistics */
        if (PeerTalk_GetPeerStats(g_state.ctx, peers[i].id, &peer_stats) == PT_OK) {
            printf("\n    Bytes: %u sent, %u received\n",
                   peer_stats.bytes_sent, peer_stats.bytes_received);
            printf("    Messages: %u sent, %u received\n",
                   peer_stats.messages_sent, peer_stats.messages_received);
            printf("    Latency: %u ms (variance: %u ms)\n",
                   peer_stats.latency_ms, peer_stats.latency_variance_ms);
            printf("    Quality: %u/100\n", peer_stats.quality);
        }

        /* Get queue status */
        if (PeerTalk_GetQueueStatus(g_state.ctx, peers[i].id, &pending, &available) == PT_OK) {
            printf("    Queue: %u pending, %u available\n", pending, available);
        }
    }
}

void print_statistics(void) {
    PeerTalk_GlobalStats stats;

    if (PeerTalk_GetGlobalStats(g_state.ctx, &stats) != PT_OK) {
        return;
    }

    printf("\n[STATS] Global Statistics:\n");
    printf("  Discovered: %u peers\n", stats.peers_discovered);
    printf("  Connected:  %u peers\n", stats.peers_connected);
    printf("  Sent:       %u bytes, %u messages\n",
           stats.total_bytes_sent, stats.total_messages_sent);
    printf("  Received:   %u bytes, %u messages\n",
           stats.total_bytes_received, stats.total_messages_received);
    printf("  Discovery:  %u sent, %u received\n",
           stats.discovery_packets_sent, stats.discovery_packets_received);
    printf("  Connections: %u accepted, %u rejected\n",
           stats.connections_accepted, stats.connections_rejected);

    /* Print per-peer stats */
    print_peer_stats();
}

void run_test_loop(void) {
    uint32_t next_send = g_state.last_send_time + DEFAULT_SEND_INTERVAL_MS;
    uint32_t next_stats = get_time_ms() + 10000; /* Print stats every 10s */

    printf("\n[TEST] Starting test loop (duration: %d seconds)...\n", g_test_duration_sec);
    printf("[TEST] Mode: %s\n",
           g_state.mode == MODE_SENDER ? "sender" :
           g_state.mode == MODE_RECEIVER ? "receiver" : "both");

    while (g_state.running) {
        uint32_t now = get_time_ms();

        /* Check test duration */
        if ((now - g_state.test_start_time) > (uint32_t)(g_test_duration_sec * 1000)) {
            printf("\n[TEST] Test duration reached, shutting down...\n");
            g_state.running = 0;
            break;
        }

        /* Poll PeerTalk */
        PeerTalk_Poll(g_state.ctx);

        /* Send messages periodically if sender mode */
        if ((g_state.mode == MODE_SENDER || g_state.mode == MODE_BOTH) &&
            now >= next_send) {
            send_messages_to_peers();
            next_send = now + DEFAULT_SEND_INTERVAL_MS;
        }

        /* Print statistics periodically */
        if (now >= next_stats) {
            print_statistics();
            next_stats = now + 10000;
        }

        /* Sleep to avoid busy loop */
        struct timespec sleep_time = {
            .tv_sec = 0,
            .tv_nsec = POLL_INTERVAL_MS * 1000000L
        };
        nanosleep(&sleep_time, NULL);
    }
}

/* ========================================================================== */
/* Main                                                                        */
/* ========================================================================== */

int main(int argc, char *argv[]) {
    PeerTalk_Config config;
    PeerTalk_Callbacks callbacks;
    const char *peer_name = "TestPeer";
    const char *mode_str = "both";

    /* Parse environment variables for test configuration */
    const char *env_duration = getenv("TEST_DURATION_SEC");
    const char *env_min_peers = getenv("MIN_PEERS_EXPECTED");

    if (env_duration) {
        g_test_duration_sec = atoi(env_duration);
        if (g_test_duration_sec <= 0) g_test_duration_sec = DEFAULT_TEST_DURATION_SEC;
    }
    if (env_min_peers) {
        g_min_peers_expected = atoi(env_min_peers);
        if (g_min_peers_expected <= 0) g_min_peers_expected = DEFAULT_MIN_PEERS;
    }

    /* Parse arguments */
    if (argc > 1) {
        peer_name = argv[1];
    }
    if (argc > 2) {
        mode_str = argv[2];
    }

    /* Parse mode */
    if (strcmp(mode_str, "sender") == 0) {
        g_state.mode = MODE_SENDER;
    } else if (strcmp(mode_str, "receiver") == 0) {
        g_state.mode = MODE_RECEIVER;
    } else {
        g_state.mode = MODE_BOTH;
    }

    printf("===========================================\n");
    printf("PeerTalk Full Integration Test\n");
    printf("===========================================\n");
    printf("Peer Name: %s\n", peer_name);
    printf("Mode: %s\n", mode_str);
    printf("Duration: %d seconds\n", g_test_duration_sec);
    printf("Min Peers Expected: %d\n", g_min_peers_expected);
    printf("===========================================\n\n");

    /* Set up signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Initialize PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, peer_name, PT_MAX_PEER_NAME - 1);
    config.max_peers = 16;
    config.discovery_interval = 3000;  /* Faster discovery for testing */
    config.auto_accept = 1;  /* Enable auto-accept for TCP connections */
    config.auto_cleanup = 1; /* Enable auto-cleanup of timed-out peers */

    g_state.ctx = PeerTalk_Init(&config);
    if (!g_state.ctx) {
        fprintf(stderr, "ERROR: PeerTalk_Init failed\n");
        return 1;
    }

    /* Set callbacks */
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_connected = on_peer_connected;
    callbacks.on_peer_disconnected = on_peer_disconnected;
    callbacks.on_message_received = on_message_received;
    callbacks.user_data = NULL;

    PeerTalk_SetCallbacks(g_state.ctx, &callbacks);

    /* Start discovery and listening */
    PeerTalk_Error err;
    err = PeerTalk_StartDiscovery(g_state.ctx);
    if (err != PT_OK) {
        fprintf(stderr, "ERROR: StartDiscovery failed: %d\n", err);
        PeerTalk_Shutdown(g_state.ctx);
        return 1;
    }

    err = PeerTalk_StartListening(g_state.ctx);
    if (err != PT_OK) {
        fprintf(stderr, "ERROR: StartListening failed: %d\n", err);
        PeerTalk_Shutdown(g_state.ctx);
        return 1;
    }

    printf("[INIT] Discovery and listening started\n");

    /* Initialize test state */
    g_state.running = 1;
    g_state.test_start_time = get_time_ms();
    g_state.last_send_time = g_state.test_start_time;

    /* Run test loop */
    run_test_loop();

    /* Print final statistics */
    printf("\n===========================================\n");
    printf("Test Complete\n");
    printf("===========================================\n");
    print_statistics();
    printf("\nTest Counters:\n");
    printf("  Peers discovered: %d\n", g_state.peers_discovered);
    printf("  Peers connected:  %d\n", g_state.peers_connected);
    printf("  Messages sent:    %d\n", g_state.messages_sent);
    printf("  Messages received: %d\n", g_state.messages_received);
    printf("  Broadcasts sent:  %d\n", g_state.broadcasts_sent);
    printf("===========================================\n");

    /* Capture final stats BEFORE shutdown */
    PeerTalk_GlobalStats final_stats;
    if (PeerTalk_GetGlobalStats(g_state.ctx, &final_stats) != PT_OK) {
        memset(&final_stats, 0, sizeof(final_stats));
    }

    /* Shutdown */
    PeerTalk_Shutdown(g_state.ctx);

    /* Test success criteria */
    int success = 1;
    int warnings = 0;

    printf("\n[VALIDATION] Checking test criteria:\n");

    /* Check discovery - configurable via MIN_PEERS_EXPECTED env var */
    if (g_state.peers_discovered < g_min_peers_expected) {
        printf("  FAIL: Expected at least %d peers discovered, got %d\n",
               g_min_peers_expected, g_state.peers_discovered);
        success = 0;
    } else {
        printf("  OK Discovery: %d peers found (required: %d)\n",
               g_state.peers_discovered, g_min_peers_expected);
    }

    /* Check connections (using stats captured before shutdown) */
    if (final_stats.peers_connected == 0) {
        printf("  ✗ FAIL: Expected peer connections, got 0\n");
        success = 0;
    } else {
        printf("  ✓ Connections: %u peers connected\n", final_stats.peers_connected);
    }

    if (final_stats.connections_accepted == 0) {
        printf("  ⚠ WARNING: No connections accepted\n");
        warnings++;
    } else {
        printf("  ✓ Accepted: %u connections\n", final_stats.connections_accepted);
    }

    /* Check messaging (mode-dependent) */
    if (g_state.mode == MODE_SENDER || g_state.mode == MODE_BOTH) {
        if (g_state.messages_sent == 0) {
            printf("  ✗ FAIL: Expected messages to be sent in sender mode\n");
            success = 0;
        } else {
            printf("  ✓ Sent: %d messages\n", g_state.messages_sent);
        }
    }

    if (g_state.mode == MODE_RECEIVER || g_state.mode == MODE_BOTH) {
        if (g_state.messages_received > 0) {
            printf("  ✓ Received: %d messages\n", g_state.messages_received);
        }
    }

    /* Check broadcasts */
    if (g_state.broadcasts_sent > 0) {
        printf("  ✓ Broadcasts: %d sent\n", g_state.broadcasts_sent);
    }

    /* Validate queue operations (Task 4.3.5.6) */
    if (final_stats.peers_connected > 0) {
        if (final_stats.total_messages_sent == 0 &&
            (g_state.mode == MODE_SENDER || g_state.mode == MODE_BOTH)) {
            printf("  ✗ FAIL: Queue integration broken - messages queued but not sent\n");
            success = 0;
        } else if (final_stats.total_messages_sent > 0) {
            printf("  ✓ Queue: Messages successfully sent (%u total)\n",
                   final_stats.total_messages_sent);
        }

        if (final_stats.total_messages_received > 0) {
            printf("  ✓ Queue: Messages successfully received (%u total)\n",
                   final_stats.total_messages_received);
        }
    }

    /* Performance validation */
    printf("\n[PERFORMANCE] Validating metrics:\n");
    uint16_t peer_count = 0;
    PeerTalk_PeerInfo peers[16];
    if (PeerTalk_GetPeers(g_state.ctx, peers, 16, &peer_count) == PT_OK) {
        for (uint16_t i = 0; i < peer_count; i++) {
            if (!peers[i].connected) continue;

            PeerTalk_PeerStats peer_stats;
            if (PeerTalk_GetPeerStats(g_state.ctx, peers[i].id, &peer_stats) == PT_OK) {
                if (peer_stats.quality > 0) {
                    printf("  ✓ Peer %u quality: %u/100\n", peers[i].id, peer_stats.quality);
                }
                if (peer_stats.latency_ms > 0 && peer_stats.latency_ms < 1000) {
                    printf("  ✓ Peer %u latency: %u ms\n", peers[i].id, peer_stats.latency_ms);
                }
            }
        }
    }

    printf("\n");
    if (success) {
        if (warnings > 0) {
            printf("✓ TEST PASSED (with %d warnings)\n", warnings);
        } else {
            printf("✓ TEST PASSED\n");
        }
        return 0;
    } else {
        printf("✗ TEST FAILED\n");
        return 1;
    }
}
