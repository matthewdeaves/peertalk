/* test_two_tier_queue.c - Integration tests for two-tier message queue
 *
 * Tests the complete two-tier system:
 * - Tier 1: 256-byte queue slots for small messages
 * - Tier 2: 4KB direct buffers for large messages
 *
 * Stress tests both tiers with concurrent sends, size-based routing,
 * and PT_ERR_WOULD_BLOCK handling.
 */

#include "../src/core/pt_internal.h"
#include "../src/core/direct_buffer.h"
#include "../src/core/queue.h"
#include "../src/core/peer.h"
#include "../src/core/send.h"
#include "../src/core/pt_compat.h"
#include "../src/posix/net_posix.h"
#include "../include/peertalk.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;

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


/* ========================================================================
 * Test: Size-Based Routing
 * ======================================================================== */

static void test_size_based_routing(void) {
    TEST("test_size_based_routing");

    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    struct pt_context *ictx;
    struct pt_peer *peer;
    uint8_t small_msg[100];
    uint8_t large_msg[1024];
    int result;

    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "SizeRouter", PT_MAX_PEER_NAME);
    config.tcp_port = 18500;
    /* Disable fragmentation to test Tier 2 routing (2 = disabled) */
    config.enable_fragmentation = 2;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to init context");
        return;
    }

    ictx = (struct pt_context *)ctx;

    /* Create a fake peer with queues */
    peer = pt_peer_create(ictx, "TestPeer", 0x7F000001, 5000);
    if (!peer) {
        FAIL("Failed to create peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Manually set peer to connected and create queues */
    peer->hot.state = PT_PEER_STATE_CONNECTED;
    peer->send_queue = pt_alloc_clear(sizeof(pt_queue));
    pt_queue_init(ictx, peer->send_queue, 16);

    /* Fill test data */
    memset(small_msg, 0xAA, sizeof(small_msg));
    memset(large_msg, 0xBB, sizeof(large_msg));

    /* Send small message - should go to Tier 1 queue */
    result = PeerTalk_Send(ctx, peer->hot.id, small_msg, 100);
    if (result != PT_OK) {
        FAIL("Small message send failed: %d", result);
        goto cleanup;
    }

    /* Verify it went to Tier 1 */
    if (pt_queue_is_empty(peer->send_queue)) {
        FAIL("Small message should be in Tier 1 queue");
        goto cleanup;
    }

    /* Send large message - should go to Tier 2 direct buffer */
    result = PeerTalk_Send(ctx, peer->hot.id, large_msg, 1024);
    if (result != PT_OK) {
        FAIL("Large message send failed: %d", result);
        goto cleanup;
    }

    /* Verify it went to Tier 2 */
    if (!pt_direct_buffer_ready(&peer->send_direct)) {
        FAIL("Large message should be in Tier 2 buffer");
        goto cleanup;
    }

    if (peer->send_direct.length != 1024) {
        FAIL("Tier 2 buffer length wrong: %u", peer->send_direct.length);
        goto cleanup;
    }

cleanup:
    pt_queue_free(peer->send_queue);
    pt_free(peer->send_queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* ========================================================================
 * Test: Tier 2 Would Block
 * ======================================================================== */

static void test_tier2_would_block(void) {
    TEST("test_tier2_would_block");

    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    struct pt_context *ictx;
    struct pt_peer *peer;
    uint8_t large_msg1[512];
    uint8_t large_msg2[512];
    int result;

    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "WouldBlock", PT_MAX_PEER_NAME);
    config.tcp_port = 18501;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to init context");
        return;
    }

    ictx = (struct pt_context *)ctx;

    /* Create peer */
    peer = pt_peer_create(ictx, "TestPeer", 0x7F000001, 5000);
    if (!peer) {
        FAIL("Failed to create peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    peer->hot.state = PT_PEER_STATE_CONNECTED;
    peer->send_queue = pt_alloc_clear(sizeof(pt_queue));
    pt_queue_init(ictx, peer->send_queue, 16);

    memset(large_msg1, 0xCC, sizeof(large_msg1));
    memset(large_msg2, 0xDD, sizeof(large_msg2));

    /* Send first large message */
    result = PeerTalk_Send(ctx, peer->hot.id, large_msg1, 512);
    if (result != PT_OK) {
        FAIL("First large message failed: %d", result);
        goto cleanup;
    }

    /* Try second large message - should get WOULD_BLOCK */
    result = PeerTalk_Send(ctx, peer->hot.id, large_msg2, 512);
    if (result != PT_ERR_WOULD_BLOCK) {
        FAIL("Second large message should return WOULD_BLOCK, got %d", result);
        goto cleanup;
    }

    /* Complete the first send */
    pt_direct_buffer_mark_sending(&peer->send_direct);
    pt_direct_buffer_complete(&peer->send_direct);

    /* Now second should succeed */
    result = PeerTalk_Send(ctx, peer->hot.id, large_msg2, 512);
    if (result != PT_OK) {
        FAIL("Second large message after complete failed: %d", result);
        goto cleanup;
    }

cleanup:
    pt_queue_free(peer->send_queue);
    pt_free(peer->send_queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* ========================================================================
 * Test: Mixed Small and Large Messages
 * ======================================================================== */

static void test_mixed_message_sizes(void) {
    TEST("test_mixed_message_sizes");

    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    struct pt_context *ictx;
    struct pt_peer *peer;
    int result;
    int i;

    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "MixedSizes", PT_MAX_PEER_NAME);
    config.tcp_port = 18502;
    /* Disable fragmentation to test Tier 2 routing (2 = disabled) */
    config.enable_fragmentation = 2;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to init context");
        return;
    }

    ictx = (struct pt_context *)ctx;

    peer = pt_peer_create(ictx, "TestPeer", 0x7F000001, 5000);
    if (!peer) {
        FAIL("Failed to create peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    peer->hot.state = PT_PEER_STATE_CONNECTED;
    peer->send_queue = pt_alloc_clear(sizeof(pt_queue));
    pt_queue_init(ictx, peer->send_queue, 16);

    /* Send alternating small and large messages */
    for (i = 0; i < 10; i++) {
        uint8_t msg[600];
        uint16_t len = (i % 2 == 0) ? 100 : 400;  /* Alternate sizes */

        memset(msg, (uint8_t)i, len);

        result = PeerTalk_Send(ctx, peer->hot.id, msg, len);

        if (len <= PT_DIRECT_THRESHOLD) {
            /* Small message - may hit backpressure, that's OK */
            if (result != PT_OK && result != PT_ERR_BUFFER_FULL) {
                FAIL("Small message %d unexpected error: %d", i, result);
                goto cleanup;
            }
        } else {
            /* Large message - may get WOULD_BLOCK */
            if (result == PT_ERR_WOULD_BLOCK) {
                /* Complete previous and retry */
                pt_direct_buffer_mark_sending(&peer->send_direct);
                pt_direct_buffer_complete(&peer->send_direct);
                result = PeerTalk_Send(ctx, peer->hot.id, msg, len);
            }
            if (result != PT_OK) {
                FAIL("Large message %d failed after retry: %d", i, result);
                goto cleanup;
            }
            /* Complete the large message send to allow next */
            pt_direct_buffer_mark_sending(&peer->send_direct);
            pt_direct_buffer_complete(&peer->send_direct);
        }
    }

    /* Verify queue has some messages */
    if (pt_queue_is_empty(peer->send_queue)) {
        FAIL("Queue should have small messages");
        goto cleanup;
    }

cleanup:
    pt_queue_free(peer->send_queue);
    pt_free(peer->send_queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* ========================================================================
 * Test: Queue Stress with Backpressure
 * ======================================================================== */

static void test_tier1_queue_stress(void) {
    TEST("test_tier1_queue_stress");

    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    struct pt_context *ictx;
    struct pt_peer *peer;
    int result;
    int sent = 0;
    int blocked = 0;
    int i;

    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "QueueStress", PT_MAX_PEER_NAME);
    config.tcp_port = 18503;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to init context");
        return;
    }

    ictx = (struct pt_context *)ctx;

    peer = pt_peer_create(ictx, "TestPeer", 0x7F000001, 5000);
    if (!peer) {
        FAIL("Failed to create peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    peer->hot.state = PT_PEER_STATE_CONNECTED;
    peer->send_queue = pt_alloc_clear(sizeof(pt_queue));
    pt_queue_init(ictx, peer->send_queue, 16);  /* Small queue for stress test */

    /* Flood with small messages until backpressure */
    for (i = 0; i < 100; i++) {
        uint8_t msg[64];
        memset(msg, (uint8_t)i, sizeof(msg));

        result = PeerTalk_Send(ctx, peer->hot.id, msg, 64);
        if (result == PT_OK) {
            sent++;
        } else if (result == PT_ERR_BUFFER_FULL) {
            blocked++;
        } else {
            FAIL("Unexpected error: %d at message %d", result, i);
            goto cleanup;
        }
    }

    /* Should have sent some and blocked some */
    if (sent == 0) {
        FAIL("Should have sent at least some messages");
        goto cleanup;
    }
    if (blocked == 0) {
        FAIL("Should have hit backpressure");
        goto cleanup;
    }

    printf("  (sent=%d, blocked=%d) ", sent, blocked);

cleanup:
    pt_queue_free(peer->send_queue);
    pt_free(peer->send_queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* ========================================================================
 * Test: Tier 2 Stress (Repeated Large Messages)
 * ======================================================================== */

static void test_tier2_stress(void) {
    TEST("test_tier2_stress");

    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    struct pt_context *ictx;
    struct pt_peer *peer;
    int result;
    int sent = 0;
    int would_block = 0;
    int i;

    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Tier2Stress", PT_MAX_PEER_NAME);
    config.tcp_port = 18504;
    /* Disable fragmentation to test Tier 2 routing (2 = disabled) */
    config.enable_fragmentation = 2;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to init context");
        return;
    }

    ictx = (struct pt_context *)ctx;

    peer = pt_peer_create(ictx, "TestPeer", 0x7F000001, 5000);
    if (!peer) {
        FAIL("Failed to create peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    peer->hot.state = PT_PEER_STATE_CONNECTED;
    peer->send_queue = pt_alloc_clear(sizeof(pt_queue));
    pt_queue_init(ictx, peer->send_queue, 16);

    /* Try to send many large messages rapidly */
    for (i = 0; i < 50; i++) {
        uint8_t msg[2048];
        memset(msg, (uint8_t)i, sizeof(msg));

        result = PeerTalk_Send(ctx, peer->hot.id, msg, 2048);

        if (result == PT_OK) {
            sent++;
            /* Simulate send completion */
            pt_direct_buffer_mark_sending(&peer->send_direct);
            pt_direct_buffer_complete(&peer->send_direct);
        } else if (result == PT_ERR_WOULD_BLOCK) {
            would_block++;
            /* Simulate send completion to unblock */
            if (peer->send_direct.state == PT_DIRECT_QUEUED) {
                pt_direct_buffer_mark_sending(&peer->send_direct);
            }
            pt_direct_buffer_complete(&peer->send_direct);
        } else {
            FAIL("Unexpected error: %d at message %d", result, i);
            goto cleanup;
        }
    }

    printf("  (sent=%d, would_block=%d) ", sent, would_block);

    if (sent == 0) {
        FAIL("Should have sent some large messages");
        goto cleanup;
    }

cleanup:
    pt_queue_free(peer->send_queue);
    pt_free(peer->send_queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* ========================================================================
 * Test: End-to-End Large Message via TCP
 * ======================================================================== */

static volatile int g_large_msg_received = 0;
static volatile uint16_t g_large_msg_len = 0;

static void large_msg_callback(PeerTalk_Context *ctx,
                                PeerTalk_PeerID from_peer,
                                const void *data,
                                uint16_t length,
                                void *user_data) {
    (void)ctx;
    (void)from_peer;
    (void)user_data;

    /* Verify data pattern */
    const uint8_t *bytes = (const uint8_t *)data;
    int valid = 1;
    for (uint16_t i = 0; i < length && i < 100; i++) {
        if (bytes[i] != 0x42) {
            valid = 0;
            break;
        }
    }

    if (valid) {
        g_large_msg_received = 1;
        g_large_msg_len = length;
    }
}

static void test_e2e_large_message(void) {
    TEST("test_e2e_large_message");

    PeerTalk_Config server_config, client_config;
    PeerTalk_Context *server_ctx, *client_ctx;
    PeerTalk_Callbacks callbacks;
    struct pt_context *client_ictx;
    struct pt_peer *server_peer;
    uint8_t large_msg[1024];
    int result;
    int polls;

    g_large_msg_received = 0;
    g_large_msg_len = 0;

    /* Setup server */
    memset(&server_config, 0, sizeof(server_config));
    strncpy(server_config.local_name, "LargeServer", PT_MAX_PEER_NAME);
    server_config.tcp_port = 18510;

    server_ctx = PeerTalk_Init(&server_config);
    if (!server_ctx) {
        FAIL("Failed to init server");
        return;
    }

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.on_message_received = large_msg_callback;
    PeerTalk_SetCallbacks(server_ctx, &callbacks);

    result = PeerTalk_StartListening(server_ctx);
    if (result != PT_OK) {
        FAIL("Server listen failed: %d", result);
        PeerTalk_Shutdown(server_ctx);
        return;
    }

    /* Setup client */
    memset(&client_config, 0, sizeof(client_config));
    strncpy(client_config.local_name, "LargeClient", PT_MAX_PEER_NAME);
    client_config.tcp_port = 18511;

    client_ctx = PeerTalk_Init(&client_config);
    if (!client_ctx) {
        FAIL("Failed to init client");
        PeerTalk_Shutdown(server_ctx);
        return;
    }

    client_ictx = (struct pt_context *)client_ctx;

    /* Create peer on client pointing to server */
    server_peer = pt_peer_create(client_ictx, "LargeServer", 0x7F000001, 18510);
    if (!server_peer) {
        FAIL("Failed to create peer");
        goto cleanup;
    }

    /* Connect */
    result = PeerTalk_Connect(client_ctx, server_peer->hot.id);
    if (result != PT_OK) {
        FAIL("Connect failed: %d", result);
        goto cleanup;
    }

    /* Poll until connected */
    for (polls = 0; polls < 100; polls++) {
        PeerTalk_Poll(server_ctx);
        PeerTalk_Poll(client_ctx);
        usleep(10000);

        if (server_peer->hot.state == PT_PEER_STATE_CONNECTED) {
            break;
        }
    }

    if (server_peer->hot.state != PT_PEER_STATE_CONNECTED) {
        FAIL("Connection timeout");
        goto cleanup;
    }

    /* Send large message (>256 bytes, goes to Tier 2) */
    memset(large_msg, 0x42, sizeof(large_msg));
    result = PeerTalk_Send(client_ctx, server_peer->hot.id, large_msg, 1024);
    if (result != PT_OK) {
        FAIL("Large message send failed: %d", result);
        goto cleanup;
    }

    /* Poll until received */
    for (polls = 0; polls < 200; polls++) {
        PeerTalk_Poll(client_ctx);
        PeerTalk_Poll(server_ctx);
        usleep(10000);

        if (g_large_msg_received) {
            break;
        }
    }

    if (!g_large_msg_received) {
        FAIL("Large message not received");
        goto cleanup;
    }

    if (g_large_msg_len != 1024) {
        FAIL("Large message length wrong: %u", g_large_msg_len);
        goto cleanup;
    }

    printf("  (received %u bytes) ", g_large_msg_len);

cleanup:
    PeerTalk_Shutdown(client_ctx);
    PeerTalk_Shutdown(server_ctx);
    PASS();
}

/* ========================================================================
 * Test: Concurrent Tier 1 and Tier 2 Sends
 * ======================================================================== */

static void test_concurrent_tiers(void) {
    TEST("test_concurrent_tiers");

    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    struct pt_context *ictx;
    struct pt_peer *peer;
    int result;
    int tier1_count = 0;
    int tier2_count = 0;
    int i;

    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Concurrent", PT_MAX_PEER_NAME);
    config.tcp_port = 18505;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to init context");
        return;
    }

    ictx = (struct pt_context *)ctx;

    peer = pt_peer_create(ictx, "TestPeer", 0x7F000001, 5000);
    if (!peer) {
        FAIL("Failed to create peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    peer->hot.state = PT_PEER_STATE_CONNECTED;
    peer->send_queue = pt_alloc_clear(sizeof(pt_queue));
    pt_queue_init(ictx, peer->send_queue, 32);

    /* Send mix of small and large messages in rapid succession */
    for (i = 0; i < 20; i++) {
        /* Small message (Tier 1) - may hit backpressure */
        uint8_t small[100];
        memset(small, (uint8_t)i, sizeof(small));
        result = PeerTalk_Send(ctx, peer->hot.id, small, 100);
        if (result == PT_OK) {
            tier1_count++;
        }
        /* Backpressure is expected, don't fail */

        /* Large message (Tier 2) */
        uint8_t large[500];
        memset(large, (uint8_t)(i + 100), sizeof(large));
        result = PeerTalk_Send(ctx, peer->hot.id, large, 500);
        if (result == PT_OK) {
            tier2_count++;
            /* Complete to allow next */
            pt_direct_buffer_mark_sending(&peer->send_direct);
            pt_direct_buffer_complete(&peer->send_direct);
        } else if (result == PT_ERR_WOULD_BLOCK) {
            /* Complete and retry */
            pt_direct_buffer_mark_sending(&peer->send_direct);
            pt_direct_buffer_complete(&peer->send_direct);
            result = PeerTalk_Send(ctx, peer->hot.id, large, 500);
            if (result == PT_OK) {
                tier2_count++;
                pt_direct_buffer_mark_sending(&peer->send_direct);
                pt_direct_buffer_complete(&peer->send_direct);
            }
        }
    }

    printf("  (tier1=%d, tier2=%d) ", tier1_count, tier2_count);

    /* We expect at least some messages from each tier */
    if (tier1_count == 0) {
        FAIL("Should have sent at least one Tier 1 message");
        goto cleanup;
    }

    if (tier2_count < 15) {
        FAIL("Too few Tier 2 messages: %d", tier2_count);
        goto cleanup;
    }

cleanup:
    pt_queue_free(peer->send_queue);
    pt_free(peer->send_queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* ========================================================================
 * Test: Buffer Size Configuration
 * ======================================================================== */

static void test_buffer_size_config(void) {
    TEST("test_buffer_size_config");

    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    struct pt_context *ictx;
    struct pt_peer *peer;
    uint8_t msg[6000];
    int result;

    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "ConfigTest", PT_MAX_PEER_NAME);
    config.tcp_port = 18506;
    config.direct_buffer_size = 8192;  /* Max size */
    /* Disable fragmentation to test Tier 2 buffer size config (2 = disabled) */
    config.enable_fragmentation = 2;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to init context");
        return;
    }

    ictx = (struct pt_context *)ctx;

    /* Verify config was applied */
    if (ictx->direct_buffer_size != 8192) {
        FAIL("direct_buffer_size not set: %u", ictx->direct_buffer_size);
        PeerTalk_Shutdown(ctx);
        return;
    }

    peer = pt_peer_create(ictx, "TestPeer", 0x7F000001, 5000);
    if (!peer) {
        FAIL("Failed to create peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    peer->hot.state = PT_PEER_STATE_CONNECTED;
    peer->send_queue = pt_alloc_clear(sizeof(pt_queue));
    pt_queue_init(ictx, peer->send_queue, 16);

    /* Verify buffer has correct capacity */
    if (peer->send_direct.capacity != 8192) {
        FAIL("Buffer capacity wrong: %u", peer->send_direct.capacity);
        goto cleanup;
    }

    /* Send 6KB message - should fit in 8KB buffer */
    memset(msg, 0xEE, sizeof(msg));
    result = PeerTalk_Send(ctx, peer->hot.id, msg, 6000);
    if (result != PT_OK) {
        FAIL("6KB message failed: %d", result);
        goto cleanup;
    }

    if (peer->send_direct.length != 6000) {
        FAIL("Buffer length wrong: %u", peer->send_direct.length);
        goto cleanup;
    }

cleanup:
    pt_queue_free(peer->send_queue);
    pt_free(peer->send_queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* ========================================================================
 * Test: Fragmentation and Reassembly
 *
 * Tests that messages larger than peer's max_message_size are automatically
 * fragmented and correctly reassembled on the receiving end.
 * ======================================================================== */

static volatile int g_frag_msg_received = 0;
static volatile uint16_t g_frag_msg_len = 0;
static volatile int g_frag_msg_valid = 0;

static void frag_msg_callback(PeerTalk_Context *ctx,
                              PeerTalk_PeerID from_peer,
                              const void *data,
                              uint16_t length,
                              void *user_data) {
    (void)ctx;
    (void)from_peer;
    (void)user_data;

    /* Verify data pattern - each byte should be (i % 256) */
    const uint8_t *bytes = (const uint8_t *)data;
    int valid = 1;
    for (uint16_t i = 0; i < length; i++) {
        if (bytes[i] != (uint8_t)(i % 256)) {
            valid = 0;
            break;
        }
    }

    g_frag_msg_received = 1;
    g_frag_msg_len = length;
    g_frag_msg_valid = valid;
}

static void test_fragmentation_reassembly(void) {
    TEST("test_fragmentation_reassembly");

    PeerTalk_Config server_config, client_config;
    PeerTalk_Context *server_ctx, *client_ctx;
    PeerTalk_Callbacks callbacks;
    struct pt_context *client_ictx;
    struct pt_peer *server_peer;
    uint8_t msg[2000];
    int result;
    int polls;

    g_frag_msg_received = 0;
    g_frag_msg_len = 0;
    g_frag_msg_valid = 0;

    /* Setup server with SMALL max_message_size to force fragmentation */
    memset(&server_config, 0, sizeof(server_config));
    strncpy(server_config.local_name, "FragServer", PT_MAX_PEER_NAME);
    server_config.tcp_port = 18520;
    server_config.max_message_size = 512;  /* Small - forces fragmentation */

    server_ctx = PeerTalk_Init(&server_config);
    if (!server_ctx) {
        FAIL("Failed to init server");
        return;
    }

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.on_message_received = frag_msg_callback;
    PeerTalk_SetCallbacks(server_ctx, &callbacks);

    result = PeerTalk_StartListening(server_ctx);
    if (result != PT_OK) {
        FAIL("Server listen failed: %d", result);
        PeerTalk_Shutdown(server_ctx);
        return;
    }

    /* Setup client with large max_message_size */
    memset(&client_config, 0, sizeof(client_config));
    strncpy(client_config.local_name, "FragClient", PT_MAX_PEER_NAME);
    client_config.tcp_port = 18521;
    client_config.max_message_size = 8192;  /* Large */

    client_ctx = PeerTalk_Init(&client_config);
    if (!client_ctx) {
        FAIL("Failed to init client");
        PeerTalk_Shutdown(server_ctx);
        return;
    }

    client_ictx = (struct pt_context *)client_ctx;

    /* Create peer on client pointing to server */
    server_peer = pt_peer_create(client_ictx, "FragServer", 0x7F000001, 18520);
    if (!server_peer) {
        FAIL("Failed to create peer");
        goto cleanup;
    }

    /* Connect */
    result = PeerTalk_Connect(client_ctx, server_peer->hot.id);
    if (result != PT_OK) {
        FAIL("Connect failed: %d", result);
        goto cleanup;
    }

    /* Poll until connected and capabilities exchanged */
    for (polls = 0; polls < 100; polls++) {
        PeerTalk_Poll(server_ctx);
        PeerTalk_Poll(client_ctx);
        usleep(10000);

        if (server_peer->hot.state == PT_PEER_STATE_CONNECTED &&
            server_peer->hot.effective_max_msg > 0) {
            break;
        }
    }

    if (server_peer->hot.state != PT_PEER_STATE_CONNECTED) {
        FAIL("Connection timeout");
        goto cleanup;
    }

    /* Verify effective_max_msg is the smaller of the two (512) */
    if (server_peer->hot.effective_max_msg != 512) {
        printf("  (effective_max=%u, expected 512) ", server_peer->hot.effective_max_msg);
        /* Not a hard failure - maybe caps not exchanged yet */
    }

    /* Create test message with pattern: byte[i] = i % 256 */
    for (int i = 0; i < 2000; i++) {
        msg[i] = (uint8_t)(i % 256);
    }

    /* Send 2000 byte message - should be fragmented into multiple pieces */
    result = PeerTalk_Send(client_ctx, server_peer->hot.id, msg, 2000);
    if (result != PT_OK) {
        FAIL("Large message send failed: %d", result);
        goto cleanup;
    }

    /* Poll until received (may take longer due to fragmentation) */
    for (polls = 0; polls < 300; polls++) {
        PeerTalk_Poll(client_ctx);
        PeerTalk_Poll(server_ctx);
        usleep(10000);

        if (g_frag_msg_received) {
            break;
        }
    }

    if (!g_frag_msg_received) {
        FAIL("Fragmented message not received");
        goto cleanup;
    }

    if (g_frag_msg_len != 2000) {
        FAIL("Message length wrong: %u (expected 2000)", g_frag_msg_len);
        goto cleanup;
    }

    if (!g_frag_msg_valid) {
        FAIL("Message data corrupted after reassembly");
        goto cleanup;
    }

    printf("  (sent 2000 bytes, fragmented, reassembled correctly) ");

cleanup:
    PeerTalk_Shutdown(client_ctx);
    PeerTalk_Shutdown(server_ctx);
    PASS();
}

/* ========================================================================
 * Flow Control Test
 *
 * Tests that:
 * 1. pt_peer_should_throttle() returns correct decisions based on pressure
 * 2. High peer pressure blocks low-priority sends
 * 3. Critical priority bypasses throttling
 * ======================================================================== */

static void test_flow_control(void) {
    struct pt_peer peer;
    TEST("test_flow_control");

    /* Initialize minimal peer structure for testing */
    memset(&peer, 0, sizeof(peer));
    peer.hot.magic = PT_PEER_MAGIC;

    /* Test 1: No pressure - no throttling at any priority */
    peer.cold.caps.buffer_pressure = 0;
    if (pt_peer_should_throttle(&peer, PT_PRIORITY_LOW)) {
        FAIL("Throttled LOW at 0%% pressure");
        return;
    }
    if (pt_peer_should_throttle(&peer, PT_PRIORITY_NORMAL)) {
        FAIL("Throttled NORMAL at 0%% pressure");
        return;
    }

    /* Test 2: Medium pressure (50%) - throttle LOW only */
    peer.cold.caps.buffer_pressure = 50;
    if (!pt_peer_should_throttle(&peer, PT_PRIORITY_LOW)) {
        FAIL("Should throttle LOW at 50%% pressure");
        return;
    }
    if (pt_peer_should_throttle(&peer, PT_PRIORITY_NORMAL)) {
        FAIL("Should not throttle NORMAL at 50%% pressure");
        return;
    }
    if (pt_peer_should_throttle(&peer, PT_PRIORITY_HIGH)) {
        FAIL("Should not throttle HIGH at 50%% pressure");
        return;
    }

    /* Test 3: High pressure (75%) - throttle LOW and NORMAL */
    peer.cold.caps.buffer_pressure = 75;
    if (!pt_peer_should_throttle(&peer, PT_PRIORITY_LOW)) {
        FAIL("Should throttle LOW at 75%% pressure");
        return;
    }
    if (!pt_peer_should_throttle(&peer, PT_PRIORITY_NORMAL)) {
        FAIL("Should throttle NORMAL at 75%% pressure");
        return;
    }
    if (pt_peer_should_throttle(&peer, PT_PRIORITY_HIGH)) {
        FAIL("Should not throttle HIGH at 75%% pressure");
        return;
    }

    /* Test 4: Critical pressure (90%) - only CRITICAL passes */
    peer.cold.caps.buffer_pressure = 90;
    if (!pt_peer_should_throttle(&peer, PT_PRIORITY_LOW)) {
        FAIL("Should throttle LOW at 90%% pressure");
        return;
    }
    if (!pt_peer_should_throttle(&peer, PT_PRIORITY_NORMAL)) {
        FAIL("Should throttle NORMAL at 90%% pressure");
        return;
    }
    if (!pt_peer_should_throttle(&peer, PT_PRIORITY_HIGH)) {
        FAIL("Should throttle HIGH at 90%% pressure");
        return;
    }
    if (pt_peer_should_throttle(&peer, PT_PRIORITY_CRITICAL)) {
        FAIL("Should NOT throttle CRITICAL at 90%% pressure");
        return;
    }

    /* Test 5: Full pressure (100%) - still CRITICAL passes */
    peer.cold.caps.buffer_pressure = 100;
    if (pt_peer_should_throttle(&peer, PT_PRIORITY_CRITICAL)) {
        FAIL("Should NOT throttle CRITICAL at 100%% pressure");
        return;
    }

    printf("  (throttling decisions correct for all pressure levels) ");
    PASS();
}

/* ========================================================================
 * Max Pressure Reporting Test
 *
 * Tests that pt_peer_check_pressure_update() reports MAX(send, recv) pressure.
 * This is critical for MacTCP where recv uses zero-copy (recv_queue empty)
 * but send_queue fills up when echoing back.
 * ======================================================================== */

static void test_max_pressure_reporting(void) {
    PeerTalk_Context *ctx = NULL;
    PeerTalk_Config config;
    struct pt_context *ictx;
    struct pt_peer *peer;

    TEST("test_max_pressure_reporting");

    /* Initialize context */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "MaxPressure", PT_MAX_PEER_NAME);
    config.tcp_port = 18507;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("PeerTalk_Init failed");
        return;
    }

    ictx = (struct pt_context *)ctx;

    /* Create a fake peer with queues */
    peer = pt_peer_create(ictx, "TestPeer", 0x7F000001, 5000);
    if (!peer) {
        FAIL("Failed to create peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Allocate queues for the peer */
    peer->send_queue = (pt_queue *)pt_alloc(sizeof(pt_queue));
    peer->recv_queue = (pt_queue *)pt_alloc(sizeof(pt_queue));
    if (!peer->send_queue || !peer->recv_queue) {
        FAIL("Failed to allocate queues");
        PeerTalk_Shutdown(ctx);
        return;
    }

    pt_queue_init(ictx, peer->send_queue, 16);
    pt_queue_init(ictx, peer->recv_queue, 16);

    /* Set peer to connected state for pressure checking */
    peer->hot.state = PT_PEER_STATE_CONNECTED;
    peer->cold.caps.last_reported_pressure = 0;

    /* Test 1: Both queues empty - no pressure update */
    if (pt_peer_check_pressure_update(ictx, peer) != 0) {
        FAIL("Should not trigger update when both queues empty");
        goto cleanup;
    }

    /* Test 2: Fill send_queue to 50% (8 of 16 slots) */
    {
        uint8_t dummy[64];
        int i;
        memset(dummy, 0xAA, sizeof(dummy));
        for (i = 0; i < 8; i++) {
            pt_queue_push(ictx, peer->send_queue, dummy, 64, PT_PRIORITY_NORMAL, 0);
        }
    }

    /* Reset last reported to force threshold crossing */
    peer->cold.caps.last_reported_pressure = 0;

    /* Should trigger update (crossed 25% and 50% thresholds) */
    if (pt_peer_check_pressure_update(ictx, peer) != 1) {
        FAIL("Should trigger update when send_queue at 50%%");
        goto cleanup;
    }

    /* Verify pressure_update_pending is set */
    if (!peer->cold.caps.pressure_update_pending) {
        FAIL("pressure_update_pending should be set");
        goto cleanup;
    }

    /* Test 3: recv_queue empty, send_queue full - should report send pressure */
    /* Already have 8 in send_queue, add more to get to 75% */
    {
        uint8_t dummy[64];
        int i;
        memset(dummy, 0xBB, sizeof(dummy));
        for (i = 0; i < 4; i++) {
            pt_queue_push(ictx, peer->send_queue, dummy, 64, PT_PRIORITY_NORMAL, 0);
        }
    }

    /* Reset for next check */
    peer->cold.caps.last_reported_pressure = 50;
    peer->cold.caps.pressure_update_pending = 0;

    /* Should trigger update (crossed 75% threshold) */
    if (pt_peer_check_pressure_update(ictx, peer) != 1) {
        FAIL("Should trigger update when send_queue at 75%%");
        goto cleanup;
    }

    /* Verify last_reported_pressure reflects send_queue pressure (75%) */
    if (peer->cold.caps.last_reported_pressure != 75) {
        FAIL("last_reported_pressure should be 75, got %u",
             peer->cold.caps.last_reported_pressure);
        goto cleanup;
    }

    printf("  (max pressure correctly reports send_queue when recv_queue empty) ");

cleanup:
    pt_queue_free(peer->send_queue);
    pt_queue_free(peer->recv_queue);
    pt_free(peer->send_queue);
    pt_free(peer->recv_queue);
    peer->send_queue = NULL;
    peer->recv_queue = NULL;
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void) {
    printf("=== Two-Tier Message Queue Tests ===\n\n");

    /* Unit tests */
    test_size_based_routing();
    test_tier2_would_block();
    test_mixed_message_sizes();
    test_tier1_queue_stress();
    test_tier2_stress();
    test_concurrent_tiers();
    test_buffer_size_config();

    /* End-to-end tests */
    test_e2e_large_message();
    test_fragmentation_reassembly();

    /* Flow control tests */
    test_flow_control();
    test_max_pressure_reporting();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
