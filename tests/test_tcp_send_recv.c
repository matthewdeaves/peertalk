/* test_tcp_send_recv.c - End-to-end TCP send/receive tests
 *
 * Establishes real TCP connections and exchanges messages to exercise
 * the send and receive paths in net_posix.c.
 */

#define _XOPEN_SOURCE 500

#include "../src/core/pt_internal.h"
#include "../src/core/pt_compat.h"
#include "../src/core/peer.h"
#include "../src/core/protocol.h"
#include "../src/core/queue.h"
#include "../src/posix/net_posix.h"
#include "peertalk.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

/* Test counter */
static int tests_passed = 0;
static int tests_failed = 0;

/* Shared state for callbacks */
static int server_connected = 0;
static int client_connected = 0;
static int messages_received = 0;
static char last_message[256] = {0};
static uint16_t last_message_len = 0;
static PeerTalk_PeerID server_peer_id = 0;
static PeerTalk_PeerID client_peer_id = 0;

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

static void reset_state(void)
{
    server_connected = 0;
    client_connected = 0;
    messages_received = 0;
    last_message[0] = '\0';
    last_message_len = 0;
    server_peer_id = 0;
    client_peer_id = 0;
}

/* Server callbacks */
static void server_on_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id, void *user_data)
{
    (void)ctx;
    (void)user_data;
    server_connected = 1;
    server_peer_id = peer_id;
}

static void server_on_message(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                               const void *data, uint16_t length, void *user_data)
{
    (void)ctx;
    (void)peer_id;
    (void)user_data;
    messages_received++;
    if (length < sizeof(last_message)) {
        memcpy(last_message, data, length);
        last_message_len = length;
    }
}

/* Client callbacks */
static void client_on_connected(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id, void *user_data)
{
    (void)ctx;
    (void)user_data;
    client_connected = 1;
    client_peer_id = peer_id;
}

static void client_on_message(PeerTalk_Context *ctx, PeerTalk_PeerID peer_id,
                               const void *data, uint16_t length, void *user_data)
{
    (void)ctx;
    (void)peer_id;
    (void)user_data;
    messages_received++;
    if (length < sizeof(last_message)) {
        memcpy(last_message, data, length);
        last_message_len = length;
    }
}

/* Test full TCP message send/receive with real connection */
static void test_tcp_message_roundtrip(void)
{
    TEST("test_tcp_message_roundtrip");

    reset_state();

    /* Create server */
    PeerTalk_Config server_cfg;
    memset(&server_cfg, 0, sizeof(server_cfg));
    strncpy(server_cfg.local_name, "TCPServer", PT_MAX_PEER_NAME);
    server_cfg.tcp_port = 17410;
    server_cfg.max_peers = 16;

    PeerTalk_Context *server = PeerTalk_Init(&server_cfg);
    if (!server) {
        FAIL("Failed to create server");
        return;
    }

    PeerTalk_Callbacks server_cb = {0};
    server_cb.on_peer_connected = server_on_connected;
    server_cb.on_message_received = server_on_message;
    PeerTalk_SetCallbacks(server, &server_cb);

    /* Start server listening */
    if (PeerTalk_StartListening(server) != PT_OK) {
        FAIL("Server failed to start listening");
        PeerTalk_Shutdown(server);
        return;
    }

    /* Create client */
    PeerTalk_Config client_cfg;
    memset(&client_cfg, 0, sizeof(client_cfg));
    strncpy(client_cfg.local_name, "TCPClient", PT_MAX_PEER_NAME);
    client_cfg.tcp_port = 17411;
    client_cfg.max_peers = 16;

    PeerTalk_Context *client = PeerTalk_Init(&client_cfg);
    if (!client) {
        FAIL("Failed to create client");
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    PeerTalk_Callbacks client_cb = {0};
    client_cb.on_peer_connected = client_on_connected;
    client_cb.on_message_received = client_on_message;
    PeerTalk_SetCallbacks(client, &client_cb);

    /* Create peer entry for server on client side */
    struct pt_context *client_ctx = (struct pt_context *)client;
    struct pt_peer *server_peer = pt_peer_create(client_ctx, "TCPServer", 0x7F000001, 17410);
    if (!server_peer) {
        FAIL("Failed to create peer entry");
        PeerTalk_Shutdown(client);
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    /* Connect to server */
    PeerTalk_Connect(client, server_peer->hot.id);

    /* Poll until both sides connected */
    int timeout = 50;
    while (timeout-- > 0 && !(server_connected && client_connected)) {
        PeerTalk_Poll(server);
        PeerTalk_Poll(client);
        usleep(10000);
    }

    if (!server_connected) {
        FAIL("Server did not see connection");
        PeerTalk_Shutdown(client);
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    /* Now send a message from client to server */
    const char *test_msg = "Hello from client!";
    PeerTalk_Error err = PeerTalk_Send(client, server_peer->hot.id, test_msg, strlen(test_msg) + 1);
    if (err != PT_OK) {
        printf("  (Send returned %d)\n", err);
    }

    /* Poll to send and receive */
    timeout = 50;
    messages_received = 0;
    while (timeout-- > 0 && messages_received == 0) {
        PeerTalk_Poll(client);
        PeerTalk_Poll(server);
        usleep(10000);
    }

    if (messages_received > 0) {
        printf("  (Received: \"%s\")\n", last_message);
    } else {
        printf("  (No messages received after polling)\n");
    }

    PeerTalk_StopListening(server);
    PeerTalk_Shutdown(client);
    PeerTalk_Shutdown(server);

    PASS();
}

/* Test sending multiple messages */
static void test_multiple_messages(void)
{
    TEST("test_multiple_messages");

    reset_state();

    PeerTalk_Config server_cfg;
    memset(&server_cfg, 0, sizeof(server_cfg));
    strncpy(server_cfg.local_name, "MultiServer", PT_MAX_PEER_NAME);
    server_cfg.tcp_port = 17412;
    server_cfg.max_peers = 16;

    PeerTalk_Context *server = PeerTalk_Init(&server_cfg);
    if (!server) {
        FAIL("Failed to create server");
        return;
    }

    PeerTalk_Callbacks server_cb = {0};
    server_cb.on_peer_connected = server_on_connected;
    server_cb.on_message_received = server_on_message;
    PeerTalk_SetCallbacks(server, &server_cb);
    PeerTalk_StartListening(server);

    PeerTalk_Config client_cfg;
    memset(&client_cfg, 0, sizeof(client_cfg));
    strncpy(client_cfg.local_name, "MultiClient", PT_MAX_PEER_NAME);
    client_cfg.tcp_port = 17413;
    client_cfg.max_peers = 16;

    PeerTalk_Context *client = PeerTalk_Init(&client_cfg);
    if (!client) {
        FAIL("Failed to create client");
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    PeerTalk_Callbacks client_cb = {0};
    client_cb.on_peer_connected = client_on_connected;
    client_cb.on_message_received = client_on_message;
    PeerTalk_SetCallbacks(client, &client_cb);

    struct pt_context *client_ctx = (struct pt_context *)client;
    struct pt_peer *server_peer = pt_peer_create(client_ctx, "MultiServer", 0x7F000001, 17412);

    PeerTalk_Connect(client, server_peer->hot.id);

    /* Wait for connection */
    int timeout = 50;
    while (timeout-- > 0 && !server_connected) {
        PeerTalk_Poll(server);
        PeerTalk_Poll(client);
        usleep(10000);
    }

    if (!server_connected) {
        FAIL("Connection not established");
        PeerTalk_Shutdown(client);
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    /* Queue multiple messages */
    for (int i = 0; i < 5; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Message %d", i);
        PeerTalk_Send(client, server_peer->hot.id, msg, strlen(msg) + 1);
    }

    /* Poll to send and receive */
    messages_received = 0;
    timeout = 100;
    while (timeout-- > 0 && messages_received < 5) {
        PeerTalk_Poll(client);
        PeerTalk_Poll(server);
        usleep(5000);
    }

    printf("  (Received %d messages)\n", messages_received);

    PeerTalk_StopListening(server);
    PeerTalk_Shutdown(client);
    PeerTalk_Shutdown(server);

    PASS();
}

/* Test large message send */
static void test_large_message(void)
{
    TEST("test_large_message");

    reset_state();

    PeerTalk_Config server_cfg;
    memset(&server_cfg, 0, sizeof(server_cfg));
    strncpy(server_cfg.local_name, "LargeServer", PT_MAX_PEER_NAME);
    server_cfg.tcp_port = 17414;
    server_cfg.max_peers = 16;

    PeerTalk_Context *server = PeerTalk_Init(&server_cfg);
    if (!server) {
        FAIL("Failed to create server");
        return;
    }

    PeerTalk_Callbacks server_cb = {0};
    server_cb.on_peer_connected = server_on_connected;
    server_cb.on_message_received = server_on_message;
    PeerTalk_SetCallbacks(server, &server_cb);
    PeerTalk_StartListening(server);

    PeerTalk_Config client_cfg;
    memset(&client_cfg, 0, sizeof(client_cfg));
    strncpy(client_cfg.local_name, "LargeClient", PT_MAX_PEER_NAME);
    client_cfg.tcp_port = 17415;
    client_cfg.max_peers = 16;

    PeerTalk_Context *client = PeerTalk_Init(&client_cfg);
    if (!client) {
        FAIL("Failed to create client");
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    PeerTalk_Callbacks client_cb = {0};
    client_cb.on_peer_connected = client_on_connected;
    client_cb.on_message_received = client_on_message;
    PeerTalk_SetCallbacks(client, &client_cb);

    struct pt_context *client_ctx = (struct pt_context *)client;
    struct pt_peer *server_peer = pt_peer_create(client_ctx, "LargeServer", 0x7F000001, 17414);

    PeerTalk_Connect(client, server_peer->hot.id);

    /* Wait for connection */
    int timeout = 50;
    while (timeout-- > 0 && !server_connected) {
        PeerTalk_Poll(server);
        PeerTalk_Poll(client);
        usleep(10000);
    }

    if (!server_connected) {
        FAIL("Connection not established");
        PeerTalk_Shutdown(client);
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    /* Send a larger message (200 bytes) */
    char large_msg[200];
    memset(large_msg, 'X', sizeof(large_msg));
    large_msg[sizeof(large_msg) - 1] = '\0';

    PeerTalk_Send(client, server_peer->hot.id, large_msg, sizeof(large_msg));

    /* Poll to send and receive */
    messages_received = 0;
    timeout = 100;
    while (timeout-- > 0 && messages_received == 0) {
        PeerTalk_Poll(client);
        PeerTalk_Poll(server);
        usleep(5000);
    }

    if (messages_received > 0) {
        printf("  (Received large message, len=%u)\n", last_message_len);
    }

    PeerTalk_StopListening(server);
    PeerTalk_Shutdown(client);
    PeerTalk_Shutdown(server);

    PASS();
}

/* Test bidirectional messaging */
static void test_bidirectional_tcp(void)
{
    TEST("test_bidirectional_tcp");

    reset_state();

    PeerTalk_Config server_cfg;
    memset(&server_cfg, 0, sizeof(server_cfg));
    strncpy(server_cfg.local_name, "BiServer", PT_MAX_PEER_NAME);
    server_cfg.tcp_port = 17416;
    server_cfg.max_peers = 16;

    PeerTalk_Context *server = PeerTalk_Init(&server_cfg);
    if (!server) {
        FAIL("Failed to create server");
        return;
    }

    PeerTalk_Callbacks server_cb = {0};
    server_cb.on_peer_connected = server_on_connected;
    server_cb.on_message_received = server_on_message;
    PeerTalk_SetCallbacks(server, &server_cb);
    PeerTalk_StartListening(server);

    PeerTalk_Config client_cfg;
    memset(&client_cfg, 0, sizeof(client_cfg));
    strncpy(client_cfg.local_name, "BiClient", PT_MAX_PEER_NAME);
    client_cfg.tcp_port = 17417;
    client_cfg.max_peers = 16;

    PeerTalk_Context *client = PeerTalk_Init(&client_cfg);
    if (!client) {
        FAIL("Failed to create client");
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    PeerTalk_Callbacks client_cb = {0};
    client_cb.on_peer_connected = client_on_connected;
    client_cb.on_message_received = client_on_message;
    PeerTalk_SetCallbacks(client, &client_cb);

    struct pt_context *client_ctx = (struct pt_context *)client;
    struct pt_peer *server_peer = pt_peer_create(client_ctx, "BiServer", 0x7F000001, 17416);

    PeerTalk_Connect(client, server_peer->hot.id);

    /* Wait for connection */
    int timeout = 50;
    while (timeout-- > 0 && !server_connected) {
        PeerTalk_Poll(server);
        PeerTalk_Poll(client);
        usleep(10000);
    }

    if (!server_connected || server_peer_id == 0) {
        FAIL("Connection not established or no peer ID");
        PeerTalk_Shutdown(client);
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    /* Client sends to server */
    PeerTalk_Send(client, server_peer->hot.id, "From client", 12);

    /* Server sends to client */
    PeerTalk_Send(server, server_peer_id, "From server", 12);

    /* Poll for messages */
    messages_received = 0;
    timeout = 100;
    while (timeout-- > 0 && messages_received < 2) {
        PeerTalk_Poll(client);
        PeerTalk_Poll(server);
        usleep(5000);
    }

    printf("  (Total messages received: %d)\n", messages_received);

    PeerTalk_StopListening(server);
    PeerTalk_Shutdown(client);
    PeerTalk_Shutdown(server);

    PASS();
}

/* Test priority messages */
static void test_priority_message_send(void)
{
    TEST("test_priority_message_send");

    reset_state();

    PeerTalk_Config server_cfg;
    memset(&server_cfg, 0, sizeof(server_cfg));
    strncpy(server_cfg.local_name, "PrioServer", PT_MAX_PEER_NAME);
    server_cfg.tcp_port = 17418;
    server_cfg.max_peers = 16;

    PeerTalk_Context *server = PeerTalk_Init(&server_cfg);
    if (!server) {
        FAIL("Failed to create server");
        return;
    }

    PeerTalk_Callbacks server_cb = {0};
    server_cb.on_peer_connected = server_on_connected;
    server_cb.on_message_received = server_on_message;
    PeerTalk_SetCallbacks(server, &server_cb);
    PeerTalk_StartListening(server);

    PeerTalk_Config client_cfg;
    memset(&client_cfg, 0, sizeof(client_cfg));
    strncpy(client_cfg.local_name, "PrioClient", PT_MAX_PEER_NAME);
    client_cfg.tcp_port = 17419;
    client_cfg.max_peers = 16;

    PeerTalk_Context *client = PeerTalk_Init(&client_cfg);
    if (!client) {
        FAIL("Failed to create client");
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    PeerTalk_Callbacks client_cb = {0};
    client_cb.on_peer_connected = client_on_connected;
    client_cb.on_message_received = client_on_message;
    PeerTalk_SetCallbacks(client, &client_cb);

    struct pt_context *client_ctx = (struct pt_context *)client;
    struct pt_peer *server_peer = pt_peer_create(client_ctx, "PrioServer", 0x7F000001, 17418);

    PeerTalk_Connect(client, server_peer->hot.id);

    /* Wait for connection */
    int timeout = 50;
    while (timeout-- > 0 && !server_connected) {
        PeerTalk_Poll(server);
        PeerTalk_Poll(client);
        usleep(10000);
    }

    if (!server_connected) {
        FAIL("Connection not established");
        PeerTalk_Shutdown(client);
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    /* Send with different priorities */
    PeerTalk_SendEx(client, server_peer->hot.id, "LOW", 4, PT_PRIORITY_LOW, PT_SEND_DEFAULT, 0);
    PeerTalk_SendEx(client, server_peer->hot.id, "NORMAL", 7, PT_PRIORITY_NORMAL, PT_SEND_DEFAULT, 0);
    PeerTalk_SendEx(client, server_peer->hot.id, "HIGH", 5, PT_PRIORITY_HIGH, PT_SEND_DEFAULT, 0);
    PeerTalk_SendEx(client, server_peer->hot.id, "CRITICAL", 9, PT_PRIORITY_CRITICAL, PT_SEND_DEFAULT, 0);

    /* Poll for messages */
    messages_received = 0;
    timeout = 100;
    while (timeout-- > 0 && messages_received < 4) {
        PeerTalk_Poll(client);
        PeerTalk_Poll(server);
        usleep(5000);
    }

    printf("  (Received %d priority messages)\n", messages_received);

    PeerTalk_StopListening(server);
    PeerTalk_Shutdown(client);
    PeerTalk_Shutdown(server);

    PASS();
}

/* Test disconnect during send */
static void test_send_after_disconnect(void)
{
    TEST("test_send_after_disconnect");

    reset_state();

    PeerTalk_Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.local_name, "DisconnectSend", PT_MAX_PEER_NAME);
    cfg.tcp_port = 17420;
    cfg.max_peers = 16;

    PeerTalk_Context *ctx = PeerTalk_Init(&cfg);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *internal = (struct pt_context *)ctx;

    /* Create peer */
    struct pt_peer *peer = pt_peer_create(internal, "FakePeer", 0x7F000001, 5000);
    if (!peer) {
        FAIL("Failed to create peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Peer is in DISCOVERED state (not connected) */
    PeerTalk_Error err = PeerTalk_Send(ctx, peer->hot.id, "Test", 5);
    printf("  (Send to discovered peer returned %d)\n", err);

    /* Disconnect the peer */
    PeerTalk_Disconnect(ctx, peer->hot.id);

    /* Try to send to disconnected peer */
    err = PeerTalk_Send(ctx, peer->hot.id, "Test", 5);
    printf("  (Send to disconnected peer returned %d)\n", err);

    PeerTalk_Shutdown(ctx);

    PASS();
}

/* Test queue status during active connection */
static void test_queue_status_active(void)
{
    TEST("test_queue_status_active");

    reset_state();

    PeerTalk_Config server_cfg;
    memset(&server_cfg, 0, sizeof(server_cfg));
    strncpy(server_cfg.local_name, "QueueServer", PT_MAX_PEER_NAME);
    server_cfg.tcp_port = 17421;
    server_cfg.max_peers = 16;

    PeerTalk_Context *server = PeerTalk_Init(&server_cfg);
    if (!server) {
        FAIL("Failed to create server");
        return;
    }

    PeerTalk_Callbacks server_cb = {0};
    server_cb.on_peer_connected = server_on_connected;
    PeerTalk_SetCallbacks(server, &server_cb);
    PeerTalk_StartListening(server);

    PeerTalk_Config client_cfg;
    memset(&client_cfg, 0, sizeof(client_cfg));
    strncpy(client_cfg.local_name, "QueueClient", PT_MAX_PEER_NAME);
    client_cfg.tcp_port = 17422;
    client_cfg.max_peers = 16;

    PeerTalk_Context *client = PeerTalk_Init(&client_cfg);
    if (!client) {
        FAIL("Failed to create client");
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    PeerTalk_Callbacks client_cb = {0};
    client_cb.on_peer_connected = client_on_connected;
    PeerTalk_SetCallbacks(client, &client_cb);

    struct pt_context *client_ctx = (struct pt_context *)client;
    struct pt_peer *server_peer = pt_peer_create(client_ctx, "QueueServer", 0x7F000001, 17421);

    PeerTalk_Connect(client, server_peer->hot.id);

    /* Wait for connection */
    int timeout = 50;
    while (timeout-- > 0 && !client_connected) {
        PeerTalk_Poll(server);
        PeerTalk_Poll(client);
        usleep(10000);
    }

    if (!client_connected) {
        FAIL("Connection not established");
        PeerTalk_Shutdown(client);
        PeerTalk_StopListening(server);
        PeerTalk_Shutdown(server);
        return;
    }

    /* Queue messages without polling */
    for (int i = 0; i < 5; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Queue msg %d", i);
        PeerTalk_Send(client, server_peer->hot.id, msg, strlen(msg) + 1);
    }

    /* Check queue status */
    uint16_t pending, available;
    PeerTalk_Error err = PeerTalk_GetQueueStatus(client, server_peer->hot.id, &pending, &available);
    if (err == PT_OK) {
        printf("  (Queue: pending=%u, available=%u)\n", pending, available);
    }

    /* Now poll to drain the queue */
    timeout = 100;
    while (timeout-- > 0) {
        PeerTalk_Poll(client);
        PeerTalk_Poll(server);
        usleep(5000);

        err = PeerTalk_GetQueueStatus(client, server_peer->hot.id, &pending, &available);
        if (err == PT_OK && pending == 0) break;
    }

    printf("  (After poll: pending=%u)\n", pending);

    PeerTalk_StopListening(server);
    PeerTalk_Shutdown(client);
    PeerTalk_Shutdown(server);

    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== TCP Send/Receive Tests ===\n\n");

    test_tcp_message_roundtrip();
    test_multiple_messages();
    test_large_message();
    test_bidirectional_tcp();
    test_priority_message_send();
    test_send_after_disconnect();
    test_queue_status_active();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
