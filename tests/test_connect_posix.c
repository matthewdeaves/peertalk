#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "peertalk.h"

static PeerTalk_PeerInfo target_peer;
static int connected = 0;
static int is_server = 0;

void on_peer_discovered(PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer,
                        void *user_data) {
    const char *name;
    (void)user_data;
    name = PeerTalk_GetPeerName(ctx, peer->name_idx);
    printf("Discovered: %s (ID=%u)\n", name, peer->id);
    if (!is_server && !connected) {
        memcpy(&target_peer, peer, sizeof(target_peer));
    }
}

void on_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                  void *user_data) {
    (void)ctx; (void)user_data;
    printf("CONNECTED to peer %u\n", peer_id);
    connected = 1;
}

void on_disconnected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                     PeerTalk_Error reason, void *user_data) {
    (void)ctx; (void)reason; (void)user_data;
    printf("DISCONNECTED from peer %u\n", peer_id);
    connected = 0;
}

int main(int argc, char **argv) {
    PeerTalk_Config config;
    PeerTalk_Callbacks callbacks = {0};
    PeerTalk_Context *ctx;
    const char *name;
    int i;

    /* Initialize config to zero */
    memset(&config, 0, sizeof(config));

    is_server = (argc > 1 && strcmp(argv[1], "server") == 0);

    /* Copy name into config array */
    if (is_server) {
        strncpy(config.local_name, "Server", PT_MAX_PEER_NAME);
    } else {
        strncpy(config.local_name, "Client", PT_MAX_PEER_NAME);
    }
    config.max_peers = 16;

    /* Use different ports for server vs client to avoid conflicts */
    if (is_server) {
        config.discovery_port = 7353;
        config.tcp_port = 7354;
    } else {
        config.discovery_port = 7353;  /* Same discovery port to find each other */
        config.tcp_port = 7355;        /* Different TCP port */
    }

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        fprintf(stderr, "Init failed\n");
        return 1;
    }

    /* Register callbacks via PeerTalk_SetCallbacks */
    callbacks.on_peer_discovered = on_peer_discovered;
    callbacks.on_peer_connected = on_connected;
    callbacks.on_peer_disconnected = on_disconnected;
    PeerTalk_SetCallbacks(ctx, &callbacks);

    PeerTalk_StartDiscovery(ctx);
    PeerTalk_StartListening(ctx);

    printf("Running as %s...\n", config.local_name);

    for (i = 0; i < 100; i++) {
        PeerTalk_Poll(ctx);

        /* Client: try to connect to first discovered peer */
        if (!is_server && !connected && target_peer.id != 0) {
            name = PeerTalk_GetPeerName(ctx, target_peer.name_idx);
            printf("Attempting connection to %s...\n", name);
            PeerTalk_Connect(ctx, target_peer.id);
            target_peer.id = 0;  /* Only try once */
        }

        usleep(100000);
    }

    if (connected) {
        printf("Test PASSED - connection established\n");
    } else {
        printf("Test INCOMPLETE - no connection\n");
    }

    PeerTalk_Shutdown(ctx);
    return connected ? 0 : 1;
}
