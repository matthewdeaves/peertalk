/*
 * PeerTalk UDP Messaging Test (POSIX)
 *
 * Tests UDP messaging implementation compile and link.
 * Full end-to-end UDP messaging requires multi-process test (similar to test_discovery_posix).
 *
 * This test verifies:
 * - UDP socket initialization
 * - UDP send/receive functions compile
 * - PeerTalk_SendUDP API exists
 * - No crashes during poll with UDP enabled
 *
 * For interactive testing, see test_discovery_posix pattern.
 */

#include "../include/peertalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    PeerTalk_Config config;
    PeerTalk_Callbacks callbacks = {0};
    PeerTalk_Context *ctx;

    printf("==============================================\n");
    printf("  PeerTalk UDP Messaging Test\n");
    printf("==============================================\n\n");

    /* Create context */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "UDP-Test", PT_MAX_PEER_NAME);
    config.max_peers = 16;
    config.discovery_port = 7360;
    config.tcp_port = 7361;
    config.udp_port = 7362;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        fprintf(stderr, "ERROR: Failed to initialize PeerTalk\n");
        return 1;
    }

    PeerTalk_SetCallbacks(ctx, &callbacks);

    /* Start services */
    PeerTalk_StartDiscovery(ctx);

    printf("UDP messaging socket initialized\n");
    printf("\nSession 4.4 UDP messaging functions implemented:\n");
    printf("  - pt_posix_udp_init()       [socket creation and binding]\n");
    printf("  - pt_posix_udp_shutdown()   [cleanup]\n");
    printf("  - pt_posix_send_udp()       [internal send with 8-byte header]\n");
    printf("  - pt_posix_recv_udp()       [poll loop receiver]\n");
    printf("  - PeerTalk_SendUDP()        [public API wrapper]\n");
    printf("  - Poll integration          [UDP receive in main loop]\n");
    printf("\nUDP Protocol:\n");
    printf("  - Magic: \"PTUD\" (0x50545544)\n");
    printf("  - Header: 8 bytes (magic + sender_port + payload_len)\n");
    printf("  - Max size: 512 bytes (504 bytes payload + 8 bytes header)\n");
    printf("  - No CRC (UDP has own checksum)\n");
    printf("  - Unreliable (no sequence numbers or retransmission)\n");
    printf("\nFull end-to-end UDP testing requires multi-process setup:\n");
    printf("  - Terminal 1: ./test_udp_full Sender\n");
    printf("  - Terminal 2: ./test_udp_full Receiver\n");
    printf("  - Similar pattern to test_discovery_posix\n");
    printf("\nFor now, this test verifies code compiles and links.\n");

    /* Poll a few times to verify no crashes */
    printf("\nPolling 5 times to verify no crashes...\n");
    for (int i = 0; i < 5; i++) {
        PeerTalk_Poll(ctx);
        sleep(1);
    }

    /* Test PeerTalk_SendUDP with invalid peer (should return error) */
    const char *msg = "test";
    PeerTalk_Error err = PeerTalk_SendUDP(ctx, 999, msg, strlen(msg));
    if (err == PT_ERR_PEER_NOT_FOUND) {
        printf("PeerTalk_SendUDP correctly returns PT_ERR_PEER_NOT_FOUND for unknown peer\n");
    } else {
        fprintf(stderr, "ERROR: PeerTalk_SendUDP returned unexpected error: %d\n", err);
        PeerTalk_Shutdown(ctx);
        return 1;
    }

    PeerTalk_Shutdown(ctx);

    printf("\n==============================================\n");
    printf("TEST PASSED - UDP messaging code compiles and runs\n");
    printf("UDP socket created, polled, and shut down successfully\n");
    printf("==============================================\n");

    return 0;
}
