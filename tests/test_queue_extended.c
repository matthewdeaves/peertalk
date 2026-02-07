/* test_queue_extended.c - Extended queue tests for additional coverage
 *
 * Tests queue functions not covered by existing tests.
 * Note: Links against libpeertalk to ensure coverage is tracked properly.
 */

#include "../src/core/pt_internal.h"
#include "../src/core/pt_compat.h"
#include "../src/core/queue.h"
#include "peertalk.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

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

/* Helper to create a test context */
static PeerTalk_Context *create_test_context(void)
{
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestNode", PT_MAX_PEER_NAME);
    config.max_peers = 8;

    return PeerTalk_Init(&config);
}

/* ========================================================================
 * Test Cases
 * ======================================================================== */

/* Test pt_queue_is_empty */
static void test_queue_is_empty(void)
{
    TEST("test_queue_is_empty");

    PeerTalk_Context *ctx = create_test_context();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    pt_queue queue;
    int result = pt_queue_init(internal_ctx, &queue, 8);
    if (result != 0) {
        FAIL("Queue init failed: %d", result);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Initially empty */
    if (!pt_queue_is_empty(&queue)) {
        FAIL("Queue should be empty initially");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Add an item */
    uint8_t data[] = "Hello";
    result = pt_queue_push(internal_ctx, &queue, data, sizeof(data), PT_PRIORITY_NORMAL, 0);
    if (result != 0) {
        FAIL("Queue push failed: %d", result);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Not empty now */
    if (pt_queue_is_empty(&queue)) {
        FAIL("Queue should not be empty after push");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Pop the item */
    uint8_t buf[64];
    uint16_t len;
    result = pt_queue_pop(&queue, buf, &len);
    if (result != 0) {
        FAIL("Queue pop failed: %d", result);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Empty again */
    if (!pt_queue_is_empty(&queue)) {
        FAIL("Queue should be empty after pop");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    pt_queue_free(&queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test pt_queue_free_slots */
static void test_queue_free_slots(void)
{
    TEST("test_queue_free_slots");

    PeerTalk_Context *ctx = create_test_context();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    pt_queue queue;
    int result = pt_queue_init(internal_ctx, &queue, 8);
    if (result != 0) {
        FAIL("Queue init failed: %d", result);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* All slots free initially */
    uint16_t free_slots = pt_queue_free_slots(&queue);
    if (free_slots != 8) {
        FAIL("Should have 8 free slots initially, got %u", free_slots);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Add some items */
    uint8_t data[] = "Test";
    for (int i = 0; i < 3; i++) {
        pt_queue_push(internal_ctx, &queue, data, sizeof(data), PT_PRIORITY_NORMAL, 0);
    }

    /* Should have 5 free slots now */
    free_slots = pt_queue_free_slots(&queue);
    if (free_slots != 5) {
        FAIL("Should have 5 free slots after 3 pushes, got %u", free_slots);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    pt_queue_free(&queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test pt_queue_reset */
static void test_queue_reset(void)
{
    TEST("test_queue_reset");

    PeerTalk_Context *ctx = create_test_context();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    pt_queue queue;
    int result = pt_queue_init(internal_ctx, &queue, 8);
    if (result != 0) {
        FAIL("Queue init failed: %d", result);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Add some items */
    uint8_t data[] = "Test data";
    for (int i = 0; i < 5; i++) {
        pt_queue_push(internal_ctx, &queue, data, sizeof(data), PT_PRIORITY_NORMAL, 0);
    }

    /* Verify not empty */
    if (pt_queue_is_empty(&queue)) {
        FAIL("Queue should not be empty after pushes");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Reset the queue */
    pt_queue_reset(&queue);

    /* Should be empty now */
    if (!pt_queue_is_empty(&queue)) {
        FAIL("Queue should be empty after reset");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* All slots should be free */
    uint16_t free_slots = pt_queue_free_slots(&queue);
    if (free_slots != 8) {
        FAIL("Should have 8 free slots after reset, got %u", free_slots);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    pt_queue_free(&queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test queue with multiple priority levels */
static void test_queue_priority_order(void)
{
    TEST("test_queue_priority_order");

    PeerTalk_Context *ctx = create_test_context();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    pt_queue queue;
    int result = pt_queue_init(internal_ctx, &queue, 16);
    if (result != 0) {
        FAIL("Queue init failed: %d", result);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Push items with different priorities */
    uint8_t low[] = "LOW";
    uint8_t normal[] = "NORMAL";
    uint8_t high[] = "HIGH";
    uint8_t critical[] = "CRITICAL";

    pt_queue_push(internal_ctx, &queue, low, 4, PT_PRIORITY_LOW, 0);
    pt_queue_push(internal_ctx, &queue, normal, 7, PT_PRIORITY_NORMAL, 0);
    pt_queue_push(internal_ctx, &queue, high, 5, PT_PRIORITY_HIGH, 0);
    pt_queue_push(internal_ctx, &queue, critical, 9, PT_PRIORITY_CRITICAL, 0);

    /* Pop in priority order using pt_queue_pop_priority */
    uint8_t buf[64];
    uint16_t len;

    result = pt_queue_pop_priority(&queue, buf, &len);
    if (result != 0 || memcmp(buf, "CRITICAL", 9) != 0) {
        FAIL("First pop should be CRITICAL");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    result = pt_queue_pop_priority(&queue, buf, &len);
    if (result != 0 || memcmp(buf, "HIGH", 5) != 0) {
        FAIL("Second pop should be HIGH");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    result = pt_queue_pop_priority(&queue, buf, &len);
    if (result != 0 || memcmp(buf, "NORMAL", 7) != 0) {
        FAIL("Third pop should be NORMAL");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    result = pt_queue_pop_priority(&queue, buf, &len);
    if (result != 0 || memcmp(buf, "LOW", 4) != 0) {
        FAIL("Fourth pop should be LOW");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    pt_queue_free(&queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test queue coalesce functionality */
static void test_queue_coalesce(void)
{
    TEST("test_queue_coalesce");

    PeerTalk_Context *ctx = create_test_context();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    pt_queue queue;
    int result = pt_queue_init(internal_ctx, &queue, 16);
    if (result != 0) {
        FAIL("Queue init failed: %d", result);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Push with same coalesce key - should replace */
    uint8_t pos1[] = "Position1";
    uint8_t pos2[] = "Position2";
    uint8_t pos3[] = "Position3";
    uint16_t coalesce_key = 0x1234;

    result = pt_queue_push_coalesce(&queue, pos1, 10, PT_PRIORITY_NORMAL, coalesce_key);
    if (result != 0) {
        FAIL("First coalesce push failed: %d", result);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    result = pt_queue_push_coalesce(&queue, pos2, 10, PT_PRIORITY_NORMAL, coalesce_key);
    if (result != 0) {
        FAIL("Second coalesce push failed: %d", result);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    result = pt_queue_push_coalesce(&queue, pos3, 10, PT_PRIORITY_NORMAL, coalesce_key);
    if (result != 0) {
        FAIL("Third coalesce push failed: %d", result);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Should only have 1 item (the last one) */
    if (queue.count != 1) {
        FAIL("Queue should have 1 item after coalesce, got %u", queue.count);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Pop should return the last value */
    uint8_t buf[64];
    uint16_t len;
    result = pt_queue_pop(&queue, buf, &len);
    if (result != 0) {
        FAIL("Pop after coalesce failed: %d", result);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    if (memcmp(buf, "Position3", 10) != 0) {
        FAIL("Coalesced value should be Position3");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    pt_queue_free(&queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test queue pressure calculation */
static void test_queue_pressure(void)
{
    TEST("test_queue_pressure");

    PeerTalk_Context *ctx = create_test_context();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    pt_queue queue;
    /* Queue capacity must be power of 2 */
    int result = pt_queue_init(internal_ctx, &queue, 16);
    if (result != 0) {
        FAIL("Queue init failed: %d", result);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Empty queue - pressure should be 0 (returns 0-100 percentage) */
    uint8_t pressure = pt_queue_pressure(&queue);
    if (pressure != 0) {
        FAIL("Empty queue pressure should be 0, got %u", pressure);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Add 8 items (50% full) - pressure should be 50 */
    uint8_t data[] = "Test";
    for (int i = 0; i < 8; i++) {
        pt_queue_push(internal_ctx, &queue, data, sizeof(data), PT_PRIORITY_NORMAL, 0);
    }

    pressure = pt_queue_pressure(&queue);
    if (pressure != 50) {
        FAIL("50%% full queue pressure should be 50, got %u", pressure);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Add 6 more (87.5% full = 14/16) */
    for (int i = 0; i < 6; i++) {
        pt_queue_push(internal_ctx, &queue, data, sizeof(data), PT_PRIORITY_NORMAL, 0);
    }

    pressure = pt_queue_pressure(&queue);
    if (pressure < 85 || pressure > 90) {
        FAIL("87.5%% full queue pressure should be ~87, got %u", pressure);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    pt_queue_free(&queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test queue pop priority with direct pointer */
static void test_queue_pop_priority_direct(void)
{
    TEST("test_queue_pop_priority_direct");

    PeerTalk_Context *ctx = create_test_context();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    pt_queue queue;
    int result = pt_queue_init(internal_ctx, &queue, 8);
    if (result != 0) {
        FAIL("Queue init failed: %d", result);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Add some items */
    uint8_t data1[] = "Hello World!";
    uint8_t data2[] = "Priority Message";

    pt_queue_push(internal_ctx, &queue, data1, sizeof(data1), PT_PRIORITY_NORMAL, 0);
    pt_queue_push(internal_ctx, &queue, data2, sizeof(data2), PT_PRIORITY_HIGH, 0);

    /* Pop direct - gets pointer without copy */
    const void *ptr;
    uint16_t len;
    result = pt_queue_pop_priority_direct(&queue, &ptr, &len);
    if (result != 0) {
        FAIL("Pop priority direct failed: %d", result);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Should get HIGH priority first */
    if (len != sizeof(data2) || memcmp(ptr, data2, len) != 0) {
        FAIL("Direct pop should return HIGH priority message");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Must commit the pop */
    pt_queue_pop_priority_commit(&queue);

    /* Pop the remaining NORMAL priority message */
    result = pt_queue_pop_priority_direct(&queue, &ptr, &len);
    if (result != 0) {
        FAIL("Second pop priority direct failed: %d", result);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    if (len != sizeof(data1) || memcmp(ptr, data1, len) != 0) {
        FAIL("Second direct pop should return NORMAL priority message");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    pt_queue_pop_priority_commit(&queue);

    /* Queue should be empty now */
    if (!pt_queue_is_empty(&queue)) {
        FAIL("Queue should be empty after two pops");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    pt_queue_free(&queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test queue full behavior */
static void test_queue_full(void)
{
    TEST("test_queue_full");

    PeerTalk_Context *ctx = create_test_context();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    pt_queue queue;
    int result = pt_queue_init(internal_ctx, &queue, 4);
    if (result != 0) {
        FAIL("Queue init failed: %d", result);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Fill the queue */
    uint8_t data[] = "Data";
    for (int i = 0; i < 4; i++) {
        result = pt_queue_push(internal_ctx, &queue, data, sizeof(data), PT_PRIORITY_NORMAL, 0);
        if (result != 0) {
            FAIL("Push %d should succeed, got %d", i, result);
            pt_queue_free(&queue);
            PeerTalk_Shutdown(ctx);
            return;
        }
    }

    /* Next push should fail */
    result = pt_queue_push(internal_ctx, &queue, data, sizeof(data), PT_PRIORITY_NORMAL, 0);
    if (result == 0) {
        FAIL("Push to full queue should fail");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Verify count */
    if (queue.count != 4) {
        FAIL("Full queue should have 4 items, got %u", queue.count);
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    pt_queue_free(&queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test queue pop from empty queue */
static void test_queue_pop_empty(void)
{
    TEST("test_queue_pop_empty");

    PeerTalk_Context *ctx = create_test_context();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    pt_queue queue;
    int result = pt_queue_init(internal_ctx, &queue, 4);
    if (result != 0) {
        FAIL("Queue init failed: %d", result);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Try to pop from empty queue */
    uint8_t buf[64];
    uint16_t len;
    result = pt_queue_pop(&queue, buf, &len);
    if (result == 0) {
        FAIL("Pop from empty queue should fail");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Try priority pop from empty queue */
    result = pt_queue_pop_priority(&queue, buf, &len);
    if (result == 0) {
        FAIL("Priority pop from empty queue should fail");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Try direct pop from empty queue */
    const void *ptr;
    result = pt_queue_pop_priority_direct(&queue, &ptr, &len);
    if (result == 0) {
        FAIL("Direct pop from empty queue should fail");
        pt_queue_free(&queue);
        PeerTalk_Shutdown(ctx);
        return;
    }

    pt_queue_free(&queue);
    PeerTalk_Shutdown(ctx);
    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== Extended Queue Tests ===\n\n");

    /* Run all tests */
    test_queue_is_empty();
    test_queue_free_slots();
    test_queue_reset();
    test_queue_priority_order();
    test_queue_coalesce();
    test_queue_pressure();
    test_queue_pop_priority_direct();
    test_queue_full();
    test_queue_pop_empty();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
