/* Feature test macros - must be before any includes */
#define _DEFAULT_SOURCE

/**
 * @file test_partner.c
 * @brief POSIX Test Partner for Mac Hardware Testing
 *
 * Runs on Linux/macOS and provides a peer for testing MacTCP on real Mac hardware.
 *
 * Usage:
 *   ./test_partner              # Run as discovery partner
 *   ./test_partner --connect IP # Connect to specified IP after discovery
 *
 * This program:
 * 1. Initializes PeerTalk with POSIX platform
 * 2. Starts discovery (broadcasts and listens)
 * 3. Logs all discovered peers
 * 4. Optionally connects to a specific IP
 * 5. Sends test messages if connected
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>

#include "peertalk.h"

/* State */
static PeerTalk_Context *g_ctx = NULL;
static volatile int g_running = 1;
static int g_peers_found = 0;
static PeerTalk_PeerID g_connected_peer = 0;
static const char *g_connect_ip = NULL;

/* Signal handler */
static void sigint_handler(int sig)
{
    (void)sig;
    printf("\nReceived SIGINT, shutting down...\n");
    g_running = 0;
}

/* Callbacks */
static void on_peer_discovered(PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer,
                                void *user_data)
{
    char ip_str[16];
    const char *name;
    (void)user_data;

    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
             (peer->address >> 24) & 0xFF,
             (peer->address >> 16) & 0xFF,
             (peer->address >> 8) & 0xFF,
             peer->address & 0xFF);

    name = PeerTalk_GetPeerName(ctx, peer->name_idx);
    if (!name) name = "(unknown)";

    printf("[DISCOVERED] Peer %u: \"%s\" at %s:%u\n",
           peer->id, name, ip_str, peer->port);

    g_peers_found++;

    /* Auto-connect if this matches the requested IP */
    if (g_connect_ip && strcmp(ip_str, g_connect_ip) == 0) {
        printf("[ACTION] Connecting to %s...\n", ip_str);
        if (PeerTalk_Connect(ctx, peer->id) == 0) {
            printf("[ACTION] Connection initiated to peer %u\n", peer->id);
        } else {
            printf("[ERROR] Failed to initiate connection\n");
        }
    }
}

static void on_peer_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                               void *user_data)
{
    (void)user_data;

    printf("[CONNECTED] Peer %u\n", peer_id);
    g_connected_peer = peer_id;

    /* Send a test message */
    const char *msg = "Hello from POSIX!";
    if (PeerTalk_Send(ctx, peer_id, msg, strlen(msg)) == PT_OK) {
        printf("[SENT] Test message to peer %u\n", peer_id);
    }
}

static void on_peer_disconnected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                  int reason, void *user_data)
{
    (void)ctx;
    (void)user_data;

    printf("[DISCONNECTED] Peer %u (reason=%d)\n", peer_id, reason);
    if (g_connected_peer == peer_id) {
        g_connected_peer = 0;
    }
}

static void on_message_received(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                 const void *data, uint16_t len, void *user_data)
{
    (void)user_data;

    printf("[MESSAGE] From peer %u: %u bytes\n", peer_id, len);

    /* Print as string if printable, otherwise hex */
    int printable = 1;
    for (uint16_t i = 0; i < len && i < 64; i++) {
        unsigned char c = ((unsigned char *)data)[i];
        if (c < 32 || c > 126) {
            printable = 0;
            break;
        }
    }

    if (printable && len < 256) {
        printf("  \"%.*s\"\n", len, (char *)data);
    } else {
        printf("  ");
        for (uint16_t i = 0; i < len && i < 32; i++) {
            printf("%02X ", ((unsigned char *)data)[i]);
        }
        if (len > 32) printf("...");
        printf("\n");
    }

    /* Echo back */
    const char *reply = "ACK from POSIX";
    PeerTalk_Send(ctx, peer_id, reply, strlen(reply));
}

static void print_usage(const char *prog)
{
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  --connect IP   Connect to specified IP after discovery\n");
    printf("  --port PORT    Use specified discovery port (default: 7353)\n");
    printf("  --help         Show this help\n");
    printf("\n");
    printf("This program acts as a test partner for MacTCP hardware testing.\n");
    printf("Run it on your Linux/macOS machine while running test_mactcp on the Mac.\n");
    printf("\n");
    printf("Press Ctrl+C to exit.\n");
}

int main(int argc, char *argv[])
{
    PeerTalk_Config config;
    PeerTalk_Callbacks callbacks;
    int discovery_port = 7353;
    int tcp_port = 7354;
    time_t last_status = 0;
    time_t start_time;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--connect") == 0 && i + 1 < argc) {
            g_connect_ip = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            discovery_port = atoi(argv[++i]);
            tcp_port = discovery_port + 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Set up signal handler */
    signal(SIGINT, sigint_handler);

    printf("========================================\n");
    printf("PeerTalk POSIX Test Partner\n");
    printf("Version: %s\n", PeerTalk_Version());
    printf("========================================\n");
    printf("\n");

    /* Configure PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "POSIXPartner", PT_MAX_PEER_NAME);
    config.max_peers = 16;
    config.discovery_port = discovery_port;
    config.tcp_port = tcp_port;

    printf("Configuration:\n");
    printf("  Name: %s\n", config.local_name);
    printf("  Discovery port: %d\n", discovery_port);
    printf("  TCP port: %d\n", tcp_port);
    if (g_connect_ip) {
        printf("  Auto-connect to: %s\n", g_connect_ip);
    }
    printf("\n");

    /* Initialize PeerTalk */
    printf("Initializing PeerTalk...\n");
    g_ctx = PeerTalk_Init(&config);
    if (!g_ctx) {
        fprintf(stderr, "FAILED to initialize PeerTalk!\n");
        return 1;
    }
    printf("PeerTalk initialized.\n\n");

    /* Set callbacks */
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_connected = on_peer_connected;
    callbacks.on_peer_disconnected = on_peer_disconnected;
    callbacks.on_message_received = on_message_received;
    PeerTalk_SetCallbacks(g_ctx, &callbacks);

    /* Start discovery */
    printf("Starting discovery...\n");
    if (PeerTalk_StartDiscovery(g_ctx) != PT_OK) {
        fprintf(stderr, "FAILED to start discovery!\n");
        PeerTalk_Shutdown(g_ctx);
        return 1;
    }

    /* Start listening */
    printf("Starting TCP listener...\n");
    if (PeerTalk_StartListening(g_ctx) != PT_OK) {
        fprintf(stderr, "FAILED to start listening!\n");
        PeerTalk_Shutdown(g_ctx);
        return 1;
    }

    printf("\nRunning... Press Ctrl+C to exit.\n");
    printf("Waiting for peers...\n\n");

    start_time = time(NULL);
    last_status = start_time;

    /* Main loop */
    while (g_running) {
        PeerTalk_Poll(g_ctx);

        /* Status update every 10 seconds */
        time_t now = time(NULL);
        if (now - last_status >= 10) {
            printf("[STATUS] Running %ld sec, peers_found=%d, connected=%s\n",
                   (long)(now - start_time),
                   g_peers_found,
                   g_connected_peer ? "yes" : "no");
            last_status = now;
        }

        usleep(10000);  /* 10ms sleep to avoid busy-wait */
    }

    /* Summary */
    printf("\n========================================\n");
    printf("TEST PARTNER SHUTDOWN\n");
    printf("Peers discovered: %d\n", g_peers_found);
    printf("Final connected: %s\n", g_connected_peer ? "yes" : "no");
    printf("========================================\n");

    PeerTalk_Shutdown(g_ctx);

    return 0;
}
