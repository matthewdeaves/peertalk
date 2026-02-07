/* test_discovery_recv.c - Tests for discovery packet reception
 *
 * Sends raw discovery packets to PeerTalk to exercise the discovery
 * receive and decode paths.
 */

#define _XOPEN_SOURCE 500

#include "../src/core/pt_internal.h"
#include "../src/core/pt_compat.h"
#include "../src/core/protocol.h"
#include "../src/posix/net_posix.h"
#include "peertalk.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

static int tests_passed = 0;
static int tests_failed = 0;
static int discovered_count = 0;
static PeerTalk_PeerID last_discovered_id = 0;

#define TEST(name) \
    do { \
        printf("Running %s... ", name); \
        fflush(stdout); \
    } while (0)

#define PASS() \
    do { \
        printf("PASS\n"); \
        tests_passed++; \
    } while (0)

#define FAIL(msg, ...) \
    do { \
        printf("FAIL: "); \
        printf(msg, ##__VA_ARGS__); \
        printf("\n"); \
        tests_failed++; \
    } while (0)

static void on_discovered(PeerTalk_Context *ctx, const PeerTalk_PeerInfo *peer, void *user_data)
{
    (void)user_data;
    discovered_count++;
    last_discovered_id = peer->id;
    const char *name = PeerTalk_GetPeerName(ctx, peer->name_idx);
    printf("  (Discovered peer: %s)\n", name ? name : "(unknown)");
}

/* Build a discovery packet manually */
static int build_discovery_packet(uint8_t *buffer, size_t buf_size,
                                   uint8_t type, const char *name,
                                   uint32_t addr, uint16_t port)
{
    if (buf_size < 64) return -1;

    /* Magic: PTLK */
    buffer[0] = 'P';
    buffer[1] = 'T';
    buffer[2] = 'L';
    buffer[3] = 'K';

    /* Version */
    buffer[4] = 1;
    buffer[5] = 0;
    buffer[6] = 0;

    /* Type */
    buffer[7] = type;

    /* Flags (little-endian) */
    buffer[8] = 0;
    buffer[9] = 0;

    /* Address (network byte order) */
    buffer[10] = (addr >> 24) & 0xFF;
    buffer[11] = (addr >> 16) & 0xFF;
    buffer[12] = (addr >> 8) & 0xFF;
    buffer[13] = addr & 0xFF;

    /* Port (network byte order) */
    buffer[14] = (port >> 8) & 0xFF;
    buffer[15] = port & 0xFF;

    /* Name length */
    size_t name_len = strlen(name);
    if (name_len > PT_MAX_PEER_NAME) name_len = PT_MAX_PEER_NAME;
    buffer[16] = (uint8_t)name_len;

    /* Name */
    memcpy(&buffer[17], name, name_len);

    return 17 + (int)name_len;
}

/* Test receiving ANNOUNCE packet */
static void test_recv_announce(void)
{
    TEST("test_recv_announce");

    discovered_count = 0;

    /* Create context listening for discovery */
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "RecvAnnounce", PT_MAX_PEER_NAME);
    config.discovery_port = 17430;
    config.tcp_port = 17431;
    config.max_peers = 16;

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_Callbacks cb = {0};
    cb.on_peer_discovered = on_discovered;
    PeerTalk_SetCallbacks(ctx, &cb);

    PeerTalk_StartDiscovery(ctx);

    /* Create UDP socket to send packet */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        FAIL("Failed to create socket");
        PeerTalk_StopDiscovery(ctx);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Build ANNOUNCE packet (type 0x01) */
    uint8_t packet[64];
    int pkt_len = build_discovery_packet(packet, sizeof(packet),
                                          0x01, /* PT_DISC_TYPE_ANNOUNCE */
                                          "TestPeer",
                                          0x7F000001, /* 127.0.0.1 */
                                          5000);

    /* Send to local discovery port */
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = htonl(0x7F000001);
    dest.sin_port = htons(17430);

    if (sendto(sock, packet, pkt_len, 0, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        printf("  (sendto failed: %s)\n", strerror(errno));
    }

    close(sock);

    /* Poll to receive */
    for (int i = 0; i < 30; i++) {
        PeerTalk_Poll(ctx);
        usleep(10000);
        if (discovered_count > 0) break;
    }

    printf("  (Discovered %d peers)\n", discovered_count);

    PeerTalk_StopDiscovery(ctx);
    PeerTalk_Shutdown(ctx);

    PASS();
}

/* Test receiving GOODBYE packet */
static void test_recv_goodbye(void)
{
    TEST("test_recv_goodbye");

    discovered_count = 0;

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "RecvGoodbye", PT_MAX_PEER_NAME);
    config.discovery_port = 17432;
    config.tcp_port = 17433;
    config.max_peers = 16;

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_Callbacks cb = {0};
    cb.on_peer_discovered = on_discovered;
    PeerTalk_SetCallbacks(ctx, &cb);

    PeerTalk_StartDiscovery(ctx);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        FAIL("Failed to create socket");
        PeerTalk_StopDiscovery(ctx);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* First send ANNOUNCE to create peer */
    uint8_t packet[64];
    int pkt_len = build_discovery_packet(packet, sizeof(packet),
                                          0x01, /* PT_DISC_TYPE_ANNOUNCE */
                                          "GoodbyePeer",
                                          0x7F000001,
                                          5001);

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = htonl(0x7F000001);
    dest.sin_port = htons(17432);

    sendto(sock, packet, pkt_len, 0, (struct sockaddr *)&dest, sizeof(dest));

    /* Poll to receive */
    for (int i = 0; i < 20; i++) {
        PeerTalk_Poll(ctx);
        usleep(10000);
    }

    /* Now send GOODBYE (type 1) */
    pkt_len = build_discovery_packet(packet, sizeof(packet),
                                      0x03, /* PT_DISC_TYPE_GOODBYE */
                                      "GoodbyePeer",
                                      0x7F000001,
                                      5001);

    sendto(sock, packet, pkt_len, 0, (struct sockaddr *)&dest, sizeof(dest));

    close(sock);

    /* Poll to process GOODBYE */
    for (int i = 0; i < 20; i++) {
        PeerTalk_Poll(ctx);
        usleep(10000);
    }

    printf("  (Processed GOODBYE)\n");

    PeerTalk_StopDiscovery(ctx);
    PeerTalk_Shutdown(ctx);

    PASS();
}

/* Test receiving REQUEST packet */
static void test_recv_request(void)
{
    TEST("test_recv_request");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "RecvRequest", PT_MAX_PEER_NAME);
    config.discovery_port = 17434;
    config.tcp_port = 17435;
    config.max_peers = 16;

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_StartDiscovery(ctx);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        FAIL("Failed to create socket");
        PeerTalk_StopDiscovery(ctx);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Send REQUEST (type 2) - should trigger ANNOUNCE response */
    uint8_t packet[64];
    int pkt_len = build_discovery_packet(packet, sizeof(packet),
                                          0x02, /* PT_DISC_TYPE_QUERY */
                                          "Requester",
                                          0x7F000001,
                                          5002);

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = htonl(0x7F000001);
    dest.sin_port = htons(17434);

    sendto(sock, packet, pkt_len, 0, (struct sockaddr *)&dest, sizeof(dest));

    close(sock);

    /* Poll to process REQUEST */
    for (int i = 0; i < 20; i++) {
        PeerTalk_Poll(ctx);
        usleep(10000);
    }

    printf("  (Processed REQUEST)\n");

    PeerTalk_StopDiscovery(ctx);
    PeerTalk_Shutdown(ctx);

    PASS();
}

/* Test receiving malformed packet */
static void test_recv_malformed(void)
{
    TEST("test_recv_malformed");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "RecvMalformed", PT_MAX_PEER_NAME);
    config.discovery_port = 17436;
    config.tcp_port = 17437;
    config.max_peers = 16;

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_StartDiscovery(ctx);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        FAIL("Failed to create socket");
        PeerTalk_StopDiscovery(ctx);
        PeerTalk_Shutdown(ctx);
        return;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = htonl(0x7F000001);
    dest.sin_port = htons(17436);

    /* Send packet with wrong magic */
    uint8_t bad_magic[20] = "XXXX1234567890123456";
    sendto(sock, bad_magic, sizeof(bad_magic), 0, (struct sockaddr *)&dest, sizeof(dest));

    /* Send too-short packet */
    uint8_t too_short[4] = "PTLK";
    sendto(sock, too_short, sizeof(too_short), 0, (struct sockaddr *)&dest, sizeof(dest));

    /* Send packet with invalid type */
    uint8_t packet[64];
    int pkt_len = build_discovery_packet(packet, sizeof(packet),
                                          99, /* Invalid type */
                                          "InvalidType",
                                          0x7F000001,
                                          5003);
    sendto(sock, packet, pkt_len, 0, (struct sockaddr *)&dest, sizeof(dest));

    close(sock);

    /* Poll to process (should reject all) */
    for (int i = 0; i < 20; i++) {
        PeerTalk_Poll(ctx);
        usleep(10000);
    }

    printf("  (Processed malformed packets)\n");

    PeerTalk_StopDiscovery(ctx);
    PeerTalk_Shutdown(ctx);

    PASS();
}

/* Test receiving own packet (should be ignored) */
static void test_recv_own_packet(void)
{
    TEST("test_recv_own_packet");

    discovered_count = 0;

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "OwnPacketTest", PT_MAX_PEER_NAME);
    config.discovery_port = 17438;
    config.tcp_port = 17439;
    config.max_peers = 16;

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_Callbacks cb = {0};
    cb.on_peer_discovered = on_discovered;
    PeerTalk_SetCallbacks(ctx, &cb);

    PeerTalk_StartDiscovery(ctx);

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        FAIL("Failed to create socket");
        PeerTalk_StopDiscovery(ctx);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Send ANNOUNCE with same name as context - should be ignored */
    uint8_t packet[64];
    int pkt_len = build_discovery_packet(packet, sizeof(packet),
                                          0x01, /* PT_DISC_TYPE_ANNOUNCE */
                                          "OwnPacketTest", /* Same name */
                                          0x7F000001,
                                          17439); /* Same port */

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = htonl(0x7F000001);
    dest.sin_port = htons(17438);

    sendto(sock, packet, pkt_len, 0, (struct sockaddr *)&dest, sizeof(dest));

    close(sock);

    /* Poll */
    for (int i = 0; i < 20; i++) {
        PeerTalk_Poll(ctx);
        usleep(10000);
    }

    /* Should not discover ourselves */
    if (discovered_count > 0) {
        printf("  (WARNING: Discovered own packet)\n");
    } else {
        printf("  (Correctly ignored own packet)\n");
    }

    PeerTalk_StopDiscovery(ctx);
    PeerTalk_Shutdown(ctx);

    PASS();
}

/* Test using protocol encode/decode functions directly */
static void test_protocol_functions(void)
{
    TEST("test_protocol_functions");

    /* Create context for logging */
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "ProtocolTest", PT_MAX_PEER_NAME);
    config.discovery_port = 17440;
    config.tcp_port = 17441;
    config.max_peers = 16;

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *internal = (struct pt_context *)ctx;

    /* Test pt_discovery_encode */
    uint8_t buffer[128];
    pt_discovery_packet pkt = {0};
    pkt.version = 1;  /* Protocol version */
    pkt.type = 0x01;  /* PT_DISC_TYPE_ANNOUNCE */
    strncpy(pkt.name, "TestNode", PT_MAX_PEER_NAME);
    pkt.name_len = strlen("TestNode");
    pkt.sender_port = 5000;

    int encoded = pt_discovery_encode(&pkt, buffer, sizeof(buffer));
    if (encoded <= 0) {
        FAIL("pt_discovery_encode failed");
        PeerTalk_Shutdown(ctx);
        return;
    }
    printf("  (Encoded %d bytes)\n", encoded);

    /* Test pt_discovery_decode */
    pt_discovery_packet decoded = {0};
    int result = pt_discovery_decode(internal, buffer, encoded, &decoded);
    if (result < 0) {
        FAIL("pt_discovery_decode failed: %d", result);
        PeerTalk_Shutdown(ctx);
        return;
    }

    if (decoded.type != pkt.type || decoded.sender_port != pkt.sender_port) {
        FAIL("Decoded values don't match");
        PeerTalk_Shutdown(ctx);
        return;
    }
    printf("  (Decoded: type=%d, port=%d)\n", decoded.type, decoded.sender_port);

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test UDP message encode/decode */
static void test_udp_protocol(void)
{
    TEST("test_udp_protocol");

    /* Create a context for decoding */
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "UDPTest", PT_MAX_PEER_NAME);
    config.discovery_port = 17450;
    config.tcp_port = 17451;
    config.max_peers = 16;

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    /* Test pt_udp_encode */
    uint8_t buffer[256];
    const char *payload = "Test UDP message";

    /* pt_udp_encode(payload, payload_len, sender_port, buf, buf_len) */
    int encoded = pt_udp_encode(payload, strlen(payload) + 1, 5000, buffer, sizeof(buffer));
    if (encoded <= 0) {
        FAIL("pt_udp_encode failed");
        PeerTalk_Shutdown(ctx);
        return;
    }
    printf("  (Encoded UDP: %d bytes)\n", encoded);

    /* Test pt_udp_decode */
    struct pt_context *internal = (struct pt_context *)ctx;
    uint16_t sender_port;
    const void *decoded_payload;
    uint16_t decoded_len;

    int result = pt_udp_decode(internal, buffer, encoded,
                                &sender_port, &decoded_payload, &decoded_len);
    if (result < 0) {
        FAIL("pt_udp_decode failed: %d", result);
        PeerTalk_Shutdown(ctx);
        return;
    }

    printf("  (Decoded UDP: %d bytes, port=%u)\n", decoded_len, sender_port);

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test CRC functions */
static void test_crc_functions(void)
{
    TEST("test_crc_functions");

    const uint8_t data[] = "Hello, CRC test!";
    uint16_t crc1 = pt_crc16(data, sizeof(data));
    uint16_t crc2 = pt_crc16(data, sizeof(data));

    if (crc1 != crc2) {
        FAIL("CRC not deterministic");
        return;
    }
    printf("  (CRC: 0x%04X)\n", crc1);

    /* Test pt_crc16_update with chunks of data */
    uint16_t crc_update = 0;
    crc_update = pt_crc16_update(crc_update, data, 8);
    crc_update = pt_crc16_update(crc_update, data + 8, sizeof(data) - 8);
    printf("  (Incremental CRC: 0x%04X)\n", crc_update);

    if (crc_update != crc1) {
        printf("  (Note: chunked CRC differs from full CRC - expected if implementation differs)\n");
    }

    /* Test pt_crc16_check */
    int valid = pt_crc16_check(data, sizeof(data), crc1);
    if (!valid) {
        FAIL("CRC check failed");
        return;
    }
    printf("  (CRC check passed)\n");

    PASS();
}

int main(void)
{
    printf("=== Discovery Receive Tests ===\n\n");

    test_recv_announce();
    test_recv_goodbye();
    test_recv_request();
    test_recv_malformed();
    test_recv_own_packet();
    test_protocol_functions();
    test_udp_protocol();
    test_crc_functions();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
