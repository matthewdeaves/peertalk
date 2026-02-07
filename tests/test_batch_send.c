/* test_batch_send.c - Tests for batch send operations
 *
 * Tests pt_batch_* functions and pt_drain_send_queue.
 * Note: Links against libpeertalk to ensure coverage is tracked properly.
 */

#include "../src/core/pt_internal.h"
#include "../src/core/pt_compat.h"
#include "../src/core/send.h"
#include "../src/core/queue.h"
#include "../src/core/peer.h"
#include "peertalk.h"
#include <stdio.h>
#include <string.h>

/* Test counter */
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

/* Track batch send calls for testing */
static int batch_send_count = 0;
static int batch_send_fail = 0;
static uint16_t last_batch_used = 0;
static uint8_t last_batch_count = 0;

/* Mock batch send function */
static int mock_batch_send(struct pt_context *ctx, struct pt_peer *peer, pt_batch *batch)
{
    (void)ctx;
    (void)peer;

    batch_send_count++;
    last_batch_used = batch->used;
    last_batch_count = batch->count;

    if (batch_send_fail) {
        return -1;
    }
    return 0;
}

/* ========================================================================
 * Test Cases
 * ======================================================================== */

/* Test batch initialization */
static void test_batch_init(void)
{
    TEST("test_batch_init");

    pt_batch batch;
    pt_batch_init(&batch);

    if (batch.used != 0) {
        FAIL("Batch used should be 0, got %u", batch.used);
        return;
    }

    if (batch.count != 0) {
        FAIL("Batch count should be 0, got %u", batch.count);
        return;
    }

    PASS();
}

/* Test adding single message to batch */
static void test_batch_add_single(void)
{
    TEST("test_batch_add_single");

    pt_batch batch;
    pt_batch_init(&batch);

    uint8_t data[] = "Hello, World!";
    int result = pt_batch_add(&batch, data, sizeof(data));

    if (result != 0) {
        FAIL("Batch add should succeed, got %d", result);
        return;
    }

    if (batch.count != 1) {
        FAIL("Batch count should be 1, got %u", batch.count);
        return;
    }

    /* 4-byte header + data length */
    uint16_t expected_used = PT_BATCH_HEADER + sizeof(data);
    if (batch.used != expected_used) {
        FAIL("Batch used should be %u, got %u", expected_used, batch.used);
        return;
    }

    PASS();
}

/* Test adding multiple messages to batch */
static void test_batch_add_multiple(void)
{
    TEST("test_batch_add_multiple");

    pt_batch batch;
    pt_batch_init(&batch);

    uint8_t data1[] = "First";
    uint8_t data2[] = "Second";
    uint8_t data3[] = "Third";

    int result = pt_batch_add(&batch, data1, sizeof(data1));
    if (result != 0) {
        FAIL("First add should succeed");
        return;
    }

    result = pt_batch_add(&batch, data2, sizeof(data2));
    if (result != 0) {
        FAIL("Second add should succeed");
        return;
    }

    result = pt_batch_add(&batch, data3, sizeof(data3));
    if (result != 0) {
        FAIL("Third add should succeed");
        return;
    }

    if (batch.count != 3) {
        FAIL("Batch count should be 3, got %u", batch.count);
        return;
    }

    uint16_t expected_used = 3 * PT_BATCH_HEADER + sizeof(data1) + sizeof(data2) + sizeof(data3);
    if (batch.used != expected_used) {
        FAIL("Batch used should be %u, got %u", expected_used, batch.used);
        return;
    }

    PASS();
}

/* Test batch overflow */
static void test_batch_overflow(void)
{
    TEST("test_batch_overflow");

    pt_batch batch;
    pt_batch_init(&batch);

    /* Create a large message that fits */
    uint8_t large_data[1000];
    memset(large_data, 'A', sizeof(large_data));

    int result = pt_batch_add(&batch, large_data, sizeof(large_data));
    if (result != 0) {
        FAIL("First large add should succeed");
        return;
    }

    /* Try to add another large message that won't fit */
    result = pt_batch_add(&batch, large_data, sizeof(large_data));
    if (result == 0) {
        FAIL("Second large add should fail (batch full)");
        return;
    }

    /* Count should still be 1 */
    if (batch.count != 1) {
        FAIL("Batch count should be 1 after failed add, got %u", batch.count);
        return;
    }

    PASS();
}

/* Test batch prepare */
static void test_batch_prepare(void)
{
    TEST("test_batch_prepare");

    pt_batch batch;
    pt_batch_init(&batch);

    /* Create a mock peer with sequence number */
    struct pt_peer peer;
    memset(&peer, 0, sizeof(peer));
    peer.hot.magic = PT_PEER_MAGIC;
    peer.hot.send_seq = 100;

    /* Empty batch should return 0 */
    int result = pt_batch_prepare(&peer, &batch);
    if (result != 0) {
        FAIL("Empty batch prepare should return 0, got %d", result);
        return;
    }

    /* Add some data */
    uint8_t data[] = "Test message";
    pt_batch_add(&batch, data, sizeof(data));

    /* Prepare should return batch size */
    result = pt_batch_prepare(&peer, &batch);
    if (result != (int)batch.used) {
        FAIL("Prepare should return batch.used (%u), got %d", batch.used, result);
        return;
    }

    /* Sequence should have been incremented */
    if (peer.hot.send_seq != 101) {
        FAIL("Sequence should be 101 after prepare, got %u", peer.hot.send_seq);
        return;
    }

    PASS();
}

/* Test pt_drain_send_queue */
static void test_drain_send_queue(void)
{
    TEST("test_drain_send_queue");

    /* Create a real context */
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestNode", PT_MAX_PEER_NAME);
    config.max_peers = 8;

    PeerTalk_Context *ctx_public = PeerTalk_Init(&config);
    if (!ctx_public) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *ctx = (struct pt_context *)ctx_public;

    /* Create a test peer */
    struct pt_peer *peer = pt_peer_create(ctx, "TestPeer", 0x7F000001, 5000);
    if (!peer) {
        FAIL("Failed to create peer");
        PeerTalk_Shutdown(ctx_public);
        return;
    }

    /* Allocate a queue for the peer */
    peer->send_queue = (pt_queue *)pt_alloc(sizeof(pt_queue));
    if (!peer->send_queue) {
        FAIL("Failed to allocate queue");
        PeerTalk_Shutdown(ctx_public);
        return;
    }

    int result = pt_queue_init(ctx, peer->send_queue, 16);
    if (result != 0) {
        FAIL("Queue init failed");
        pt_free(peer->send_queue);
        PeerTalk_Shutdown(ctx_public);
        return;
    }

    /* Queue some messages */
    uint8_t msg1[] = "Message 1";
    uint8_t msg2[] = "Message 2";
    uint8_t msg3[] = "Message 3";

    pt_queue_push(ctx, peer->send_queue, msg1, sizeof(msg1), PT_PRIORITY_NORMAL, 0);
    pt_queue_push(ctx, peer->send_queue, msg2, sizeof(msg2), PT_PRIORITY_HIGH, 0);
    pt_queue_push(ctx, peer->send_queue, msg3, sizeof(msg3), PT_PRIORITY_LOW, 0);

    /* Reset tracking */
    batch_send_count = 0;
    batch_send_fail = 0;

    /* Drain the queue */
    int sent = pt_drain_send_queue(ctx, peer, mock_batch_send);

    if (sent < 1) {
        FAIL("Should have sent at least 1 batch, got %d", sent);
        pt_queue_free(peer->send_queue);
        pt_free(peer->send_queue);
        PeerTalk_Shutdown(ctx_public);
        return;
    }

    if (batch_send_count < 1) {
        FAIL("Mock send should have been called at least once");
        pt_queue_free(peer->send_queue);
        pt_free(peer->send_queue);
        PeerTalk_Shutdown(ctx_public);
        return;
    }

    /* Queue should be empty now */
    if (!pt_queue_is_empty(peer->send_queue)) {
        FAIL("Queue should be empty after drain");
        pt_queue_free(peer->send_queue);
        pt_free(peer->send_queue);
        PeerTalk_Shutdown(ctx_public);
        return;
    }

    pt_queue_free(peer->send_queue);
    pt_free(peer->send_queue);
    peer->send_queue = NULL;
    PeerTalk_Shutdown(ctx_public);
    PASS();
}

/* Test drain with send failure */
static void test_drain_send_queue_failure(void)
{
    TEST("test_drain_send_queue_failure");

    /* Create a real context */
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestNode", PT_MAX_PEER_NAME);
    config.max_peers = 8;

    PeerTalk_Context *ctx_public = PeerTalk_Init(&config);
    if (!ctx_public) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *ctx = (struct pt_context *)ctx_public;

    /* Create a test peer */
    struct pt_peer *peer = pt_peer_create(ctx, "TestPeer", 0x7F000001, 5000);
    if (!peer) {
        FAIL("Failed to create peer");
        PeerTalk_Shutdown(ctx_public);
        return;
    }

    /* Allocate a queue for the peer */
    peer->send_queue = (pt_queue *)pt_alloc(sizeof(pt_queue));
    if (!peer->send_queue) {
        FAIL("Failed to allocate queue");
        PeerTalk_Shutdown(ctx_public);
        return;
    }

    int result = pt_queue_init(ctx, peer->send_queue, 16);
    if (result != 0) {
        FAIL("Queue init failed");
        pt_free(peer->send_queue);
        PeerTalk_Shutdown(ctx_public);
        return;
    }

    /* Queue some messages */
    uint8_t msg[] = "Test Message";
    pt_queue_push(ctx, peer->send_queue, msg, sizeof(msg), PT_PRIORITY_NORMAL, 0);

    /* Set mock to fail */
    batch_send_count = 0;
    batch_send_fail = 1;

    /* Try to drain - should handle failure gracefully */
    (void)pt_drain_send_queue(ctx, peer, mock_batch_send);

    /* Even with failure, drain should complete */
    if (batch_send_count < 1) {
        FAIL("Mock send should have been called");
        batch_send_fail = 0;
        pt_queue_free(peer->send_queue);
        pt_free(peer->send_queue);
        PeerTalk_Shutdown(ctx_public);
        return;
    }

    batch_send_fail = 0;
    pt_queue_free(peer->send_queue);
    pt_free(peer->send_queue);
    peer->send_queue = NULL;
    PeerTalk_Shutdown(ctx_public);
    PASS();
}

/* Test drain with empty queue */

/* Test drain with empty queue */
static void test_drain_send_queue_empty(void)
{
    TEST("test_drain_send_queue_empty");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestNode", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx_public = PeerTalk_Init(&config);
    if (!ctx_public) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *ctx = (struct pt_context *)ctx_public;

    struct pt_peer peer;
    memset(&peer, 0, sizeof(peer));
    peer.hot.magic = PT_PEER_MAGIC;

    pt_queue q;
    pt_queue_init(ctx, &q, 4);
    peer.send_queue = &q;

    batch_send_count = 0;

    int sent = pt_drain_send_queue(ctx, &peer, mock_batch_send);
    if (sent != 0) {
        FAIL("Empty queue should return 0 batches sent, got %d", sent);
        pt_queue_free(&q);
        PeerTalk_Shutdown(ctx_public);
        return;
    }

    if (batch_send_count != 0) {
        FAIL("Send function should not be called for empty queue");
        pt_queue_free(&q);
        PeerTalk_Shutdown(ctx_public);
        return;
    }

    pt_queue_free(&q);
    PeerTalk_Shutdown(ctx_public);
    PASS();
}

/* Test batch header encoding */
static void test_batch_header_encoding(void)
{
    TEST("test_batch_header_encoding");

    pt_batch batch;
    pt_batch_init(&batch);

    uint8_t data[] = "Test";  /* 5 bytes including null */
    int result = pt_batch_add(&batch, data, 5);
    if (result != 0) {
        FAIL("Batch add failed");
        return;
    }

    /* Check header encoding (big-endian length) */
    uint16_t encoded_len = (batch.buffer[0] << 8) | batch.buffer[1];
    if (encoded_len != 5) {
        FAIL("Header should encode length 5, got %u", encoded_len);
        return;
    }

    /* Check reserved bytes are zero */
    if (batch.buffer[2] != 0 || batch.buffer[3] != 0) {
        FAIL("Reserved bytes should be 0");
        return;
    }

    /* Check data follows header */
    if (memcmp(&batch.buffer[4], data, 5) != 0) {
        FAIL("Data should follow header");
        return;
    }

    PASS();
}

/* Test filling batch to capacity */
static void test_batch_fill_to_capacity(void)
{
    TEST("test_batch_fill_to_capacity");

    pt_batch batch;
    pt_batch_init(&batch);

    /* Fill with small messages until full */
    uint8_t small_data[32];
    memset(small_data, 'X', sizeof(small_data));

    int count = 0;
    while (pt_batch_add(&batch, small_data, sizeof(small_data)) == 0) {
        count++;
        if (count > 100) {
            FAIL("Should have filled batch by now");
            return;
        }
    }

    /* Verify we added multiple messages */
    if (count < 10) {
        FAIL("Should have added at least 10 small messages, added %d", count);
        return;
    }

    if (batch.count != (uint8_t)count) {
        FAIL("Batch count should be %d, got %u", count, batch.count);
        return;
    }

    /* Verify used is close to max */
    if (batch.used < PT_BATCH_MAX_SIZE - 100) {
        FAIL("Batch should be nearly full, used=%u, max=%d", batch.used, PT_BATCH_MAX_SIZE);
        return;
    }

    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== Batch Send Tests ===\n\n");

    /* Run all tests */
    test_batch_init();
    test_batch_add_single();
    test_batch_add_multiple();
    test_batch_overflow();
    test_batch_prepare();
    test_drain_send_queue();
    test_drain_send_queue_failure();
    test_drain_send_queue_empty();
    test_batch_header_encoding();
    test_batch_fill_to_capacity();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
