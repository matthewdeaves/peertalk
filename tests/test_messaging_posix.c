/*
 * PeerTalk POSIX Message I/O Test
 *
 * Simplified test that verifies message send/receive functions compile and link.
 * Full end-to-end testing will happen in Session 4.6 (Integration Test).
 */

#include "../include/peertalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int messages_received = 0;

void on_message_received(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                          const void *data, uint16_t len, void *user_data) {
    (void)ctx;
    (void)peer_id;
    (void)user_data;

    printf("Received message (%u bytes): %.*s\n", len, len, (const char *)data);
    messages_received++;
}

void on_peer_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                        void *user_data) {
    (void)ctx;
    (void)user_data;
    printf("Connected to peer %u\n", peer_id);
}

int main(void) {
    PeerTalk_Config config;
    PeerTalk_Callbacks callbacks = {0};
    PeerTalk_Context *ctx;

    printf("PeerTalk Message I/O Test\n");
    printf("=========================\n\n");

    /* Create context */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestPeer", PT_MAX_PEER_NAME);
    config.max_peers = 16;
    config.discovery_port = 7360;
    config.tcp_port = 7361;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        fprintf(stderr, "Failed to initialize PeerTalk\n");
        return 1;
    }

    /* Set up callbacks */
    callbacks.on_message_received = on_message_received;
    callbacks.on_peer_connected = on_peer_connected;
    PeerTalk_SetCallbacks(ctx, &callbacks);

    /* Start services */
    PeerTalk_StartDiscovery(ctx);
    PeerTalk_StartListening(ctx);

    printf("Services started:\n");
    printf("  - UDP Discovery on port %u\n", config.discovery_port);
    printf("  - TCP Listening on port %u\n", config.tcp_port);
    printf("\nSession 4.3 Message I/O functions implemented:\n");
    printf("  - pt_posix_send()         [internal send with writev]\n");
    printf("  - pt_posix_recv()         [internal receive state machine]\n");
    printf("  - pt_posix_send_control() [internal PING/PONG/DISCONNECT]\n");
    printf("  - Receive state machine   [HEADER -> PAYLOAD -> CRC]\n");
    printf("\nPublic API functions (PeerTalk_Send, PeerTalk_GetPeers) will be\n");
    printf("implemented in Session 4.6 (Integration Test).\n");
    printf("\nFor now, this test verifies the code compiles and links.\n");

    /* Poll a few times to verify no crashes */
    printf("\nPolling 5 times to verify no crashes...\n");
    for (int i = 0; i < 5; i++) {
        PeerTalk_Poll(ctx);
        sleep(1);
    }

    PeerTalk_Shutdown(ctx);

    printf("\n=========================\n");
    printf("TEST PASSED - Message I/O code compiles and runs\n");
    printf("Full end-to-end messaging tests in Session 4.6\n");

    return 0;
}
