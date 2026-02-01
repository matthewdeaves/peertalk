/*
 * PeerTalk Discovery Test (POSIX)
 *
 * Standalone test program for UDP discovery between peers.
 * Can be run as separate processes on same machine or different machines.
 *
 * Usage:
 *   ./test_discovery_posix [name]
 *
 * Example:
 *   Terminal 1: ./test_discovery_posix Alice
 *   Terminal 2: ./test_discovery_posix Bob
 *
 * Expected behavior:
 * - Each peer sends discovery announcements every 5 seconds
 * - Each peer discovers the other within ~5 seconds
 * - Ctrl-C sends goodbye and exits cleanly
 * - Peers should see each other's names and addresses
 */

#include "../include/peertalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

/* Global flag for signal handling */
static volatile int running = 1;

/* Discovered peer count */
static int peer_count = 0;

/* ========================================================================== */
/* Callbacks                                                                  */
/* ========================================================================== */

void on_peer_discovered(PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer,
                        void *user_data) {
    (void)ctx;
    (void)user_data;

    peer_count++;

    printf("\n[DISCOVERY] Peer found!\n");
    printf("  ID:      %u\n", peer->id);
    printf("  Address: %u.%u.%u.%u:%u\n",
           (peer->address >> 24) & 0xFF,
           (peer->address >> 16) & 0xFF,
           (peer->address >> 8) & 0xFF,
           peer->address & 0xFF,
           peer->port);
    printf("  Flags:   0x%04X\n", peer->flags);
    printf("  Latency: %u ms\n", peer->latency_ms);
    printf("  Transports: ");
    if (peer->transports_available & PT_TRANSPORT_TCP)
        printf("TCP ");
    if (peer->transports_available & PT_TRANSPORT_UDP)
        printf("UDP ");
    if (peer->transports_available & PT_TRANSPORT_APPLETALK)
        printf("AppleTalk ");
    printf("\n");
    printf("  Total peers: %d\n", peer_count);
    fflush(stdout);
}

void on_peer_lost(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id, void *user_data) {
    (void)ctx;
    (void)user_data;

    peer_count--;

    printf("\n[GOODBYE] Peer %u disconnected\n", peer_id);
    printf("  Total peers: %d\n", peer_count);
    fflush(stdout);
}

void sigint_handler(int sig) {
    (void)sig;
    printf("\n\nCaught Ctrl-C, shutting down...\n");
    running = 0;
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(int argc, char *argv[]) {
    PeerTalk_Config config;
    PeerTalk_Callbacks callbacks;
    PeerTalk_Context *ctx;
    const char *name = "TestPeer";
    int iterations = 0;
    int max_iterations = 300;  /* 30 seconds at 1s poll */

    /* Parse command line */
    if (argc > 1) {
        name = argv[1];
    }

    printf("==============================================\n");
    printf("  PeerTalk Discovery Test (POSIX)\n");
    printf("==============================================\n");
    printf("Local peer name: %s\n", name);
    printf("Running for 30 seconds or until Ctrl-C\n");
    printf("Press Ctrl-C to send GOODBYE and exit\n");
    printf("==============================================\n\n");

    /* Setup signal handler for Ctrl-C */
    signal(SIGINT, sigint_handler);

    /* Configure PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, name, PT_MAX_PEER_NAME);
    config.max_peers = 16;
    config.discovery_port = 7353;
    config.tcp_port = 7354;
    config.udp_port = 7355;

    /* Setup callbacks */
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_lost = on_peer_lost;
    callbacks.user_data = NULL;

    /* Initialize PeerTalk */
    printf("Initializing PeerTalk...\n");
    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        fprintf(stderr, "ERROR: Failed to initialize PeerTalk\n");
        return 1;
    }

    /* Set callbacks */
    PeerTalk_SetCallbacks(ctx, &callbacks);

    /* Start discovery */
    printf("Starting discovery on UDP port %u...\n", config.discovery_port);
    if (PeerTalk_StartDiscovery(ctx) != PT_OK) {
        fprintf(stderr, "ERROR: Failed to start discovery\n");
        PeerTalk_Shutdown(ctx);
        return 1;
    }

    printf("Discovery started successfully!\n");
    printf("Listening for peers...\n\n");

    /* Main loop - poll for 30 seconds or until Ctrl-C */
    while (running && iterations < max_iterations) {
        PeerTalk_Poll(ctx);
        sleep(1);  /* 1 second poll interval */
        iterations += 10;

        /* Print status every 10 seconds */
        if (iterations % 100 == 0) {
            printf("[STATUS] %d seconds elapsed, %d peers discovered\n",
                   iterations / 10, peer_count);
        }
    }

    /* Shutdown */
    printf("\nShutting down...\n");
    printf("Final peer count: %d\n", peer_count);

    PeerTalk_StopDiscovery(ctx);
    PeerTalk_Shutdown(ctx);

    printf("Shutdown complete\n");
    printf("==============================================\n");

    return 0;
}
