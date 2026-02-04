/*
 * PeerTalk Integration Test - Basic Poll Loop (Session 4.6)
 *
 * Minimal integration test for the main poll loop implementation.
 * Tests that the optimized select()-based polling with cached fd_sets works correctly.
 *
 * This test verifies:
 * - PeerTalk initialization and shutdown
 * - Discovery socket polling
 * - Listen socket polling
 * - Poll loop runs without errors
 * - Statistics tracking works
 *
 * NOTE: This is a simplified version of the integration test. Full 3-peer
 * scenario with messaging will be implemented after Phase 3.5 (PeerTalk_Send API).
 */

#define _DEFAULT_SOURCE  /* For usleep() */

#include "../include/peertalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

/* Test state */
static volatile int running = 1;
static int peer_discovered_count = 0;

/* ========================================================================== */
/* Callbacks                                                                  */
/* ========================================================================== */

void on_peer_discovered(PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer,
                        void *user_data) {
    (void)ctx;
    (void)user_data;

    peer_discovered_count++;

    printf("[TEST] Peer %u discovered (name_idx=%u)\n",
           peer->id, peer->name_idx);
    fflush(stdout);
}

void on_peer_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                       void *user_data) {
    (void)ctx;
    (void)user_data;

    printf("[TEST] Connected to peer %u\n", peer_id);
    fflush(stdout);
}

void on_peer_disconnected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                         PeerTalk_Error reason, void *user_data) {
    (void)ctx;
    (void)user_data;

    printf("[TEST] Disconnected from peer %u (reason=%d)\n", peer_id, reason);
    fflush(stdout);
}

void on_peer_lost(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                  void *user_data) {
    (void)ctx;
    (void)user_data;

    peer_discovered_count--;

    printf("[TEST] Lost peer %u\n", peer_id);
    fflush(stdout);
}

void on_message_received(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                        const void *data, uint16_t length,
                        void *user_data) {
    (void)ctx;
    (void)user_data;

    printf("[TEST] Message from peer %u (len=%u): \"%.*s\"\n",
           peer_id, length, length, (const char *)data);
    fflush(stdout);
}

void sigint_handler(int sig) {
    (void)sig;
    printf("\n[TEST] Caught Ctrl-C, shutting down...\n");
    running = 0;
}

/* ========================================================================== */
/* Main Test                                                                  */
/* ========================================================================== */

int main(void) {
    PeerTalk_Config config;
    PeerTalk_Callbacks callbacks;
    PeerTalk_Context *ctx;
    PeerTalk_Error err;
    PeerTalk_GlobalStats stats;
    int poll_count = 0;
    const int max_polls = 50;  /* 5 seconds at 100ms per poll */

    /* Install signal handler */
    signal(SIGINT, sigint_handler);

    printf("=== PeerTalk Integration Test (Session 4.6) ===\n\n");

    /* Initialize PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "IntegrationTest", PT_MAX_PEER_NAME - 1);
    config.discovery_port = 7353;
    config.tcp_port = 7354;
    config.udp_port = 7355;
    config.max_peers = 16;

    printf("[1] Initializing PeerTalk...\n");
    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        fprintf(stderr, "ERROR: PeerTalk_Init failed\n");
        return 1;
    }
    printf("    ✓ PeerTalk initialized\n\n");

    /* Register callbacks */
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_connected = on_peer_connected;
    callbacks.on_peer_disconnected = on_peer_disconnected;
    callbacks.on_peer_lost = on_peer_lost;
    callbacks.on_message_received = on_message_received;

    printf("[2] Registering callbacks...\n");
    err = PeerTalk_SetCallbacks(ctx, &callbacks);
    if (err != PT_OK) {
        fprintf(stderr, "ERROR: PeerTalk_SetCallbacks failed: %d\n", err);
        PeerTalk_Shutdown(ctx);
        return 1;
    }
    printf("    ✓ Callbacks registered\n\n");

    /* Start discovery */
    printf("[3] Starting discovery...\n");
    err = PeerTalk_StartDiscovery(ctx);
    if (err != PT_OK) {
        fprintf(stderr, "ERROR: PeerTalk_StartDiscovery failed: %d\n", err);
        PeerTalk_Shutdown(ctx);
        return 1;
    }
    printf("    ✓ Discovery started on port %d\n\n", config.discovery_port);

    /* Start listening */
    printf("[4] Starting TCP listener...\n");
    err = PeerTalk_StartListening(ctx);
    if (err != PT_OK) {
        fprintf(stderr, "ERROR: PeerTalk_StartListening failed: %d\n", err);
        PeerTalk_Shutdown(ctx);
        return 1;
    }
    printf("    ✓ Listening on port %d\n\n", config.tcp_port);

    /* Poll loop test */
    printf("[5] Testing poll loop (%d iterations)...\n", max_polls);
    printf("    (This tests the optimized select()-based polling)\n");

    while (running && poll_count < max_polls) {
        PeerTalk_Poll(ctx);
        usleep(100000);  /* 100ms */
        poll_count++;

        if (poll_count % 10 == 0) {
            printf("    ... %d polls completed\n", poll_count);
        }
    }

    printf("    ✓ Poll loop completed %d iterations\n\n", poll_count);

    /* Check statistics */
    printf("[6] Checking statistics...\n");
    err = PeerTalk_GetGlobalStats(ctx, &stats);
    if (err != PT_OK) {
        fprintf(stderr, "ERROR: PeerTalk_GetGlobalStats failed: %d\n", err);
        PeerTalk_Shutdown(ctx);
        return 1;
    }

    printf("    Global Statistics:\n");
    printf("      Discovery packets sent:     %u\n", stats.discovery_packets_sent);
    printf("      Discovery packets received: %u\n", stats.discovery_packets_received);
    printf("      Peers discovered:           %u\n", stats.peers_discovered);
    printf("      Peers connected:            %u\n", stats.peers_connected);
    printf("      Connections accepted:       %u\n", stats.connections_accepted);
    printf("      Total bytes sent:           %u\n", stats.total_bytes_sent);
    printf("      Total bytes received:       %u\n", stats.total_bytes_received);
    printf("    ✓ Statistics retrieved\n\n");

    /* Shutdown */
    printf("[7] Shutting down...\n");
    PeerTalk_Shutdown(ctx);
    printf("    ✓ Shutdown complete\n\n");

    /* Test results */
    printf("=== Test Results ===\n");
    printf("  ✓ Initialization: PASS\n");
    printf("  ✓ Discovery start: PASS\n");
    printf("  ✓ Listen start: PASS\n");
    printf("  ✓ Poll loop: PASS (%d iterations)\n", poll_count);
    printf("  ✓ Statistics: PASS\n");
    printf("  ✓ Shutdown: PASS\n");
    printf("\n");

    if (stats.discovery_packets_sent > 0) {
        printf("✓ INTEGRATION TEST PASSED\n");
        printf("  Poll loop working correctly with %u discovery packets sent\n",
               stats.discovery_packets_sent);
        return 0;
    } else {
        fprintf(stderr, "✗ TEST WARNING: No discovery packets sent\n");
        fprintf(stderr, "  Poll loop ran but discovery may not be working\n");
        return 1;
    }
}
