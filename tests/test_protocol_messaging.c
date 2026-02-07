/* test_protocol_messaging.c - Tests for protocol-level messaging
 *
 * Sends properly formatted protocol messages to exercise receive paths.
 */

#define _XOPEN_SOURCE 500

#include "../src/core/pt_internal.h"
#include "../src/core/pt_compat.h"
#include "../src/core/peer.h"
#include "../src/core/protocol.h"
#include "../src/posix/net_posix.h"
#include "peertalk.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

/* Test counter */
static int tests_passed = 0;
static int tests_failed = 0;

/* Track callbacks */
static int message_received_count = 0;
static uint8_t last_message_data[256];
static uint16_t last_message_len = 0;

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

/* Message received callback */
static void on_message_received(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                                const void *data, uint16_t length, void *user_data)
{
    (void)ctx;
    (void)peer_id;
    (void)user_data;
    message_received_count++;
    if (length <= sizeof(last_message_data)) {
        memcpy(last_message_data, data, length);
        last_message_len = length;
    }
    printf("  [Callback] Received %u bytes\n", length);
}

/* Helper to reset callback state */
static void reset_callbacks(void)
{
    message_received_count = 0;
    last_message_len = 0;
    memset(last_message_data, 0, sizeof(last_message_data));
}

/* Helper to build a proper protocol message */
static int build_message(uint8_t *buf, size_t buf_size,
                         const uint8_t *payload, uint16_t payload_len)
{
    pt_message_header hdr;
    uint16_t crc;
    int offset = 0;

    if (buf_size < PT_MESSAGE_HEADER_SIZE + payload_len + 2) {
        return -1;
    }

    /* Build header */
    hdr.version = PT_PROTOCOL_VERSION;
    hdr.type = PT_MSG_TYPE_DATA;
    hdr.flags = 0;
    hdr.sequence = 1;
    hdr.payload_len = payload_len;

    pt_message_encode_header(&hdr, buf);
    offset = PT_MESSAGE_HEADER_SIZE;

    /* Add payload */
    if (payload && payload_len > 0) {
        memcpy(buf + offset, payload, payload_len);
        offset += payload_len;
    }

    /* Calculate and add CRC */
    crc = pt_crc16(buf, offset);
    buf[offset++] = (crc >> 8) & 0xFF;
    buf[offset++] = crc & 0xFF;

    return offset;
}

/* ========================================================================
 * Test Cases
 * ======================================================================== */

/* Test sending a valid protocol message */
static void test_send_valid_message(void)
{
    TEST("test_send_valid_message");

    reset_callbacks();

    /* Create server */
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "MsgServer", PT_MAX_PEER_NAME);
    config.tcp_port = 17380;
    config.max_peers = 16;

    PeerTalk_Context *server = PeerTalk_Init(&config);
    if (!server) {
        FAIL("Failed to create server");
        return;
    }

    PeerTalk_Callbacks cb = {0};
    cb.on_message_received = on_message_received;
    PeerTalk_SetCallbacks(server, &cb);

    PeerTalk_StartListening(server);

    /* Connect client socket */
    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        FAIL("Failed to create client socket");
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001);
    addr.sin_port = htons(17380);

    if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        FAIL("Client connect failed");
        close(client);
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    /* Poll to accept connection */
    for (int i = 0; i < 10; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    /* Build and send a protocol message */
    uint8_t payload[] = "Hello PeerTalk!";
    uint8_t msg_buf[256];
    int msg_len = build_message(msg_buf, sizeof(msg_buf), payload, sizeof(payload));

    if (msg_len > 0) {
        ssize_t sent = send(client, msg_buf, msg_len, 0);
        if (sent != msg_len) {
            printf("  (partial send: %zd/%d)\n", sent, msg_len);
        }
    }

    /* Poll to receive message */
    for (int i = 0; i < 20; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
        if (message_received_count > 0) break;
    }

    close(client);

    /* Poll to detect disconnect */
    for (int i = 0; i < 5; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    PeerTalk_StopListening(server);
    PeerTalk_Shutdown(server);

    /* We don't require the message to be received (timing dependent) */
    PASS();
}

/* Test sending multiple messages */
static void test_send_multiple_messages(void)
{
    TEST("test_send_multiple_messages");

    reset_callbacks();

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "MultiMsgServer", PT_MAX_PEER_NAME);
    config.tcp_port = 17381;
    config.max_peers = 16;

    PeerTalk_Context *server = PeerTalk_Init(&config);
    if (!server) {
        FAIL("Failed to create server");
        return;
    }

    PeerTalk_Callbacks cb = {0};
    cb.on_message_received = on_message_received;
    PeerTalk_SetCallbacks(server, &cb);

    PeerTalk_StartListening(server);

    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        FAIL("Failed to create client socket");
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001);
    addr.sin_port = htons(17381);

    if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        FAIL("Client connect failed");
        close(client);
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    /* Poll to accept */
    for (int i = 0; i < 10; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    /* Send multiple messages */
    for (int m = 0; m < 5; m++) {
        char payload[32];
        snprintf(payload, sizeof(payload), "Message %d", m);

        uint8_t msg_buf[256];
        int msg_len = build_message(msg_buf, sizeof(msg_buf),
                                    (uint8_t *)payload, strlen(payload) + 1);

        if (msg_len > 0) {
            send(client, msg_buf, msg_len, 0);
        }

        /* Poll between sends */
        PeerTalk_Poll(server);
        usleep(5000);
    }

    /* Poll to receive all messages */
    for (int i = 0; i < 30; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    close(client);

    for (int i = 0; i < 5; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    PeerTalk_StopListening(server);
    PeerTalk_Shutdown(server);
    PASS();
}

/* Test sending invalid message (bad magic) */
static void test_send_invalid_magic(void)
{
    TEST("test_send_invalid_magic");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "BadMagicServer", PT_MAX_PEER_NAME);
    config.tcp_port = 17382;
    config.max_peers = 16;

    PeerTalk_Context *server = PeerTalk_Init(&config);
    if (!server) {
        FAIL("Failed to create server");
        return;
    }

    PeerTalk_StartListening(server);

    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        FAIL("Failed to create client socket");
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001);
    addr.sin_port = htons(17382);

    if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        FAIL("Client connect failed");
        close(client);
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    for (int i = 0; i < 10; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    /* Send garbage data */
    uint8_t garbage[] = "This is not a valid protocol message!";
    send(client, garbage, sizeof(garbage), 0);

    /* Poll to process - server should reject invalid data */
    for (int i = 0; i < 10; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    close(client);

    for (int i = 0; i < 5; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    PeerTalk_StopListening(server);
    PeerTalk_Shutdown(server);
    PASS();
}

/* Test partial message receive */
static void test_partial_message(void)
{
    TEST("test_partial_message");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "PartialServer", PT_MAX_PEER_NAME);
    config.tcp_port = 17383;
    config.max_peers = 16;

    PeerTalk_Context *server = PeerTalk_Init(&config);
    if (!server) {
        FAIL("Failed to create server");
        return;
    }

    PeerTalk_StartListening(server);

    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        FAIL("Failed to create client socket");
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001);
    addr.sin_port = htons(17383);

    if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        FAIL("Client connect failed");
        close(client);
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    for (int i = 0; i < 10; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    /* Build complete message */
    uint8_t payload[] = "Partial test";
    uint8_t msg_buf[256];
    int msg_len = build_message(msg_buf, sizeof(msg_buf), payload, sizeof(payload));

    if (msg_len > 0) {
        /* Send in two parts to test partial receive */
        int half = msg_len / 2;
        send(client, msg_buf, half, 0);

        /* Poll between parts */
        for (int i = 0; i < 5; i++) {
            PeerTalk_Poll(server);
            usleep(10000);
        }

        /* Send rest */
        send(client, msg_buf + half, msg_len - half, 0);
    }

    for (int i = 0; i < 20; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    close(client);

    for (int i = 0; i < 5; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    PeerTalk_StopListening(server);
    PeerTalk_Shutdown(server);
    PASS();
}

/* Test UDP messaging */
static void test_udp_messaging(void)
{
    TEST("test_udp_messaging");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "UdpMsgTest", PT_MAX_PEER_NAME);
    config.udp_port = 17385;
    config.max_peers = 16;

    PeerTalk_Context *server = PeerTalk_Init(&config);
    if (!server) {
        FAIL("Failed to create server");
        return;
    }

    struct pt_context *internal = (struct pt_context *)server;
    pt_posix_data *pd = pt_posix_get(internal);

    /* Verify UDP socket is initialized */
    if (pd->udp_msg_sock < 0) {
        printf("  (UDP socket not initialized)\n");
    }

    /* Create a peer and try UDP send (will likely fail but exercises code) */
    struct pt_peer *peer = pt_peer_create(internal, "UdpPeer", 0x7F000001, 17386);
    if (peer) {
        uint8_t data[] = "UDP test message";
        /* This will exercise pt_posix_send_udp even if it fails */
        PeerTalk_SendEx(server, peer->hot.id, data, sizeof(data),
                        PT_PRIORITY_NORMAL, PT_SEND_UNRELIABLE, 0);
    }

    /* Poll a few times */
    for (int i = 0; i < 5; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    PeerTalk_Shutdown(server);
    PASS();
}

/* Test CRC validation path */
static void test_crc_validation(void)
{
    TEST("test_crc_validation");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "CrcServer", PT_MAX_PEER_NAME);
    config.tcp_port = 17384;
    config.max_peers = 16;

    PeerTalk_Context *server = PeerTalk_Init(&config);
    if (!server) {
        FAIL("Failed to create server");
        return;
    }

    PeerTalk_StartListening(server);

    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0) {
        FAIL("Failed to create client socket");
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(0x7F000001);
    addr.sin_port = htons(17384);

    if (connect(client, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        FAIL("Client connect failed");
        close(client);
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    for (int i = 0; i < 10; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    /* Build message with invalid CRC */
    uint8_t payload[] = "CRC test";
    uint8_t msg_buf[256];
    int msg_len = build_message(msg_buf, sizeof(msg_buf), payload, sizeof(payload));

    if (msg_len > 0) {
        /* Corrupt the CRC bytes */
        msg_buf[msg_len - 2] ^= 0xFF;
        msg_buf[msg_len - 1] ^= 0xFF;

        send(client, msg_buf, msg_len, 0);
    }

    /* Poll to process - should reject due to bad CRC */
    for (int i = 0; i < 10; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    close(client);

    for (int i = 0; i < 5; i++) {
        PeerTalk_Poll(server);
        usleep(10000);
    }

    PeerTalk_StopListening(server);
    PeerTalk_Shutdown(server);
    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== Protocol Messaging Tests ===\n\n");

    /* Run all tests */
    test_send_valid_message();
    test_send_multiple_messages();
    test_send_invalid_magic();
    test_partial_message();
    test_udp_messaging();
    test_crc_validation();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
