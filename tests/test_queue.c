/* test_queue.c - Tests for message queue
 *
 * Note: This test links against libpeertalk to ensure coverage is tracked
 * properly. Do NOT include .c files directly.
 */

#include "../src/core/queue.h"
#include "../src/core/pt_internal.h"
#include "../src/core/pt_compat.h"
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
static struct pt_context *create_test_context(void)
{
    struct pt_context *ctx;
    pt_platform_ops *plat;

    ctx = (struct pt_context *)pt_alloc_clear(sizeof(struct pt_context));
    if (!ctx) {
        return NULL;
    }

    /* Create minimal platform ops */
    plat = (pt_platform_ops *)pt_alloc_clear(sizeof(pt_platform_ops));
    if (!plat) {
        pt_free(ctx);
        return NULL;
    }

    ctx->magic = PT_CONTEXT_MAGIC;
    ctx->plat = plat;

    return ctx;
}

/* Helper to destroy test context */
static void destroy_test_context(struct pt_context *ctx)
{
    if (!ctx) {
        return;
    }

    if (ctx->plat) {
        pt_free(ctx->plat);
    }

    ctx->magic = 0;
    pt_free(ctx);
}

/* ========================================================================
 * Test Cases
 * ======================================================================== */

/* Test queue init/free */
static void test_queue_init_free(void)
{
    TEST("test_queue_init_free");

    struct pt_context *ctx = create_test_context();
    pt_queue q;
    int ret;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    ret = pt_queue_init(ctx, &q, 8);
    if (ret != 0) {
        FAIL("pt_queue_init failed: %d", ret);
        destroy_test_context(ctx);
        return;
    }

    if (q.magic != PT_QUEUE_MAGIC) {
        FAIL("Magic should be PT_QUEUE_MAGIC");
        destroy_test_context(ctx);
        return;
    }

    if (q.capacity != 8) {
        FAIL("Capacity should be 8, got %u", q.capacity);
        destroy_test_context(ctx);
        return;
    }

    if (q.capacity_mask != 7) {
        FAIL("Capacity mask should be 7, got %u", q.capacity_mask);
        destroy_test_context(ctx);
        return;
    }

    if (q.count != 0) {
        FAIL("Count should be 0, got %u", q.count);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_free(&q);

    if (q.slots != NULL) {
        FAIL("Slots should be NULL after free");
        destroy_test_context(ctx);
        return;
    }

    destroy_test_context(ctx);
    PASS();
}

/* Test basic push/pop */
static void test_queue_push_pop(void)
{
    TEST("test_queue_push_pop");

    struct pt_context *ctx = create_test_context();
    pt_queue q;
    uint8_t send_buf[16] = {1, 2, 3, 4, 5};
    uint8_t recv_buf[256];
    uint16_t len;
    int ret;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_queue_init(ctx, &q, 4);

    /* Push message */
    ret = pt_queue_push(ctx, &q, send_buf, 5, 0, 0);
    if (ret != 0) {
        FAIL("Push failed: %d", ret);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    if (q.count != 1) {
        FAIL("Count should be 1, got %u", q.count);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Pop message */
    ret = pt_queue_pop(&q, recv_buf, &len);
    if (ret != 0) {
        FAIL("Pop failed: %d", ret);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    if (len != 5) {
        FAIL("Length should be 5, got %u", len);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    if (recv_buf[0] != 1 || recv_buf[4] != 5) {
        FAIL("Data mismatch");
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    if (q.count != 0) {
        FAIL("Count should be 0 after pop, got %u", q.count);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_free(&q);
    destroy_test_context(ctx);
    PASS();
}

/* Test FIFO order */
static void test_queue_fifo_order(void)
{
    TEST("test_queue_fifo_order");

    struct pt_context *ctx = create_test_context();
    pt_queue q;
    uint8_t msg1[] = {1, 1, 1};
    uint8_t msg2[] = {2, 2, 2};
    uint8_t msg3[] = {3, 3, 3};
    uint8_t recv_buf[256];
    uint16_t len;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_queue_init(ctx, &q, 8);

    /* Push 3 messages */
    pt_queue_push(ctx, &q, msg1, 3, 0, 0);
    pt_queue_push(ctx, &q, msg2, 3, 0, 0);
    pt_queue_push(ctx, &q, msg3, 3, 0, 0);

    if (q.count != 3) {
        FAIL("Count should be 3, got %u", q.count);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Pop and verify order */
    pt_queue_pop(&q, recv_buf, &len);
    if (recv_buf[0] != 1) {
        FAIL("First message should be 1, got %u", recv_buf[0]);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_pop(&q, recv_buf, &len);
    if (recv_buf[0] != 2) {
        FAIL("Second message should be 2, got %u", recv_buf[0]);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_pop(&q, recv_buf, &len);
    if (recv_buf[0] != 3) {
        FAIL("Third message should be 3, got %u", recv_buf[0]);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_free(&q);
    destroy_test_context(ctx);
    PASS();
}

/* Test queue full condition */
static void test_queue_full(void)
{
    TEST("test_queue_full");

    struct pt_context *ctx = create_test_context();
    pt_queue q;
    uint8_t msg[] = {0xAA};
    int ret;
    uint16_t i;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_queue_init(ctx, &q, 4);

    /* Fill queue */
    for (i = 0; i < 4; i++) {
        ret = pt_queue_push(ctx, &q, msg, 1, 0, 0);
        if (ret != 0) {
            FAIL("Push %u failed: %d", i, ret);
            pt_queue_free(&q);
            destroy_test_context(ctx);
            return;
        }
    }

    /* Verify full */
    if (!pt_queue_is_full(&q)) {
        FAIL("Queue should be full");
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Try to push to full queue */
    ret = pt_queue_push(ctx, &q, msg, 1, 0, 0);
    if (ret != PT_ERR_BUFFER_FULL) {
        FAIL("Push to full queue should return PT_ERR_BUFFER_FULL, got %d", ret);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_free(&q);
    destroy_test_context(ctx);
    PASS();
}

/* Test pressure calculation */
static void test_queue_pressure(void)
{
    TEST("test_queue_pressure");

    struct pt_context *ctx = create_test_context();
    pt_queue q;
    uint8_t msg[] = {0xBB};
    uint8_t pressure;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_queue_init(ctx, &q, 8);

    /* Empty: 0% */
    pressure = pt_queue_pressure(&q);
    if (pressure != 0) {
        FAIL("Empty queue pressure should be 0, got %u", pressure);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* 50% full (4/8) */
    pt_queue_push(ctx, &q, msg, 1, 0, 0);
    pt_queue_push(ctx, &q, msg, 1, 0, 0);
    pt_queue_push(ctx, &q, msg, 1, 0, 0);
    pt_queue_push(ctx, &q, msg, 1, 0, 0);
    pressure = pt_queue_pressure(&q);
    if (pressure != 50) {
        FAIL("50%% full pressure should be 50, got %u", pressure);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* 100% full (8/8) */
    pt_queue_push(ctx, &q, msg, 1, 0, 0);
    pt_queue_push(ctx, &q, msg, 1, 0, 0);
    pt_queue_push(ctx, &q, msg, 1, 0, 0);
    pt_queue_push(ctx, &q, msg, 1, 0, 0);
    pressure = pt_queue_pressure(&q);
    if (pressure != 100) {
        FAIL("100%% full pressure should be 100, got %u", pressure);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_free(&q);
    destroy_test_context(ctx);
    PASS();
}

/* Test wrap-around behavior */
static void test_queue_wrap_around(void)
{
    TEST("test_queue_wrap_around");

    struct pt_context *ctx = create_test_context();
    pt_queue q;
    uint8_t msg[] = {0xCC};
    uint8_t recv_buf[256];
    uint16_t len;
    uint16_t i;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_queue_init(ctx, &q, 4);

    /* Fill queue */
    for (i = 0; i < 4; i++) {
        pt_queue_push(ctx, &q, msg, 1, 0, 0);
    }

    /* Pop 2 messages */
    pt_queue_pop(&q, recv_buf, &len);
    pt_queue_pop(&q, recv_buf, &len);

    /* Push 2 more (should wrap around) */
    pt_queue_push(ctx, &q, msg, 1, 0, 0);
    pt_queue_push(ctx, &q, msg, 1, 0, 0);

    /* Verify count is still 4 */
    if (q.count != 4) {
        FAIL("Count should be 4 after wrap, got %u", q.count);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Verify write_idx wrapped correctly using bitwise AND */
    if (q.write_idx != 2) {
        FAIL("Write index should be 2 after wrap, got %u", q.write_idx);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_free(&q);
    destroy_test_context(ctx);
    PASS();
}

/* Test coalescing */
static void test_queue_coalesce(void)
{
    TEST("test_queue_coalesce");

    struct pt_context *ctx = create_test_context();
    pt_queue q;
    uint8_t msg1[] = {1, 1, 1};
    uint8_t msg2[] = {2, 2, 2};
    uint8_t updated[] = {9, 9, 9};
    uint8_t recv_buf[256];
    uint16_t len;
    int ret;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_queue_init(ctx, &q, 8);

    /* Push regular message */
    pt_queue_push(ctx, &q, msg1, 3, 0, 0);

    /* Push coalescable message */
    pt_queue_push(ctx, &q, msg2, 3, 0, PT_SLOT_COALESCABLE);

    /* Coalesce (should replace last coalescable) */
    ret = pt_queue_coalesce(&q, updated, 3);
    if (ret != 0) {
        FAIL("Coalesce failed: %d", ret);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Count should still be 2 (replaced, not added) */
    if (q.count != 2) {
        FAIL("Count should be 2 after coalesce, got %u", q.count);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Pop first (unchanged) */
    pt_queue_pop(&q, recv_buf, &len);
    if (recv_buf[0] != 1) {
        FAIL("First message should be unchanged");
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Pop second (should be updated) */
    pt_queue_pop(&q, recv_buf, &len);
    if (recv_buf[0] != 9) {
        FAIL("Second message should be coalesced, got %u", recv_buf[0]);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_free(&q);
    destroy_test_context(ctx);
    PASS();
}

/* Test peek/consume */
static void test_queue_peek_consume(void)
{
    TEST("test_queue_peek_consume");

    struct pt_context *ctx = create_test_context();
    pt_queue q;
    uint8_t msg[] = {0xDD, 0xEE, 0xFF};
    void *peek_data;
    uint16_t len;
    int ret;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_queue_init(ctx, &q, 4);

    /* Push message */
    pt_queue_push(ctx, &q, msg, 3, 0, 0);

    /* Peek (zero-copy) */
    ret = pt_queue_peek(&q, &peek_data, &len);
    if (ret != 0) {
        FAIL("Peek failed: %d", ret);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    if (len != 3) {
        FAIL("Peek length should be 3, got %u", len);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    if (((uint8_t *)peek_data)[0] != 0xDD) {
        FAIL("Peek data mismatch");
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Count should still be 1 (peek doesn't remove) */
    if (q.count != 1) {
        FAIL("Count should be 1 after peek, got %u", q.count);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Consume */
    pt_queue_consume(&q);

    /* Count should be 0 now */
    if (q.count != 0) {
        FAIL("Count should be 0 after consume, got %u", q.count);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_free(&q);
    destroy_test_context(ctx);
    PASS();
}

/* Test ISR push */
static void test_queue_isr_push(void)
{
    TEST("test_queue_isr_push");

    struct pt_context *ctx = create_test_context();
    pt_queue q;
    uint8_t msg[] = {0x11, 0x22, 0x33};
    uint8_t recv_buf[256];
    uint16_t len;
    int ret;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_queue_init(ctx, &q, 4);

    /* Push using ISR-safe variant */
    ret = pt_queue_push_isr(&q, msg, 3);
    if (ret != 0) {
        FAIL("ISR push failed: %d", ret);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Verify has_data flag set */
    if (q.has_data != 1) {
        FAIL("has_data flag should be set");
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Pop from main loop */
    ret = pt_queue_pop(&q, recv_buf, &len);
    if (ret != 0 || len != 3 || recv_buf[0] != 0x11) {
        FAIL("Pop after ISR push failed");
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_free(&q);
    destroy_test_context(ctx);
    PASS();
}

/* Test power-of-two validation */
static void test_queue_power_of_two(void)
{
    TEST("test_queue_power_of_two");

    struct pt_context *ctx = create_test_context();
    pt_queue q;
    int ret;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    /* Valid: 8 (power of 2) */
    ret = pt_queue_init(ctx, &q, 8);
    if (ret != 0) {
        FAIL("Should accept 8 (power of 2), got %d", ret);
        destroy_test_context(ctx);
        return;
    }
    pt_queue_free(&q);

    /* Invalid: 7 (not power of 2) */
    ret = pt_queue_init(ctx, &q, 7);
    if (ret != PT_ERR_INVALID_PARAM) {
        FAIL("Should reject 7 (not power of 2), got %d", ret);
        destroy_test_context(ctx);
        return;
    }

    /* Valid: 16 (power of 2) */
    ret = pt_queue_init(ctx, &q, 16);
    if (ret != 0) {
        FAIL("Should accept 16 (power of 2), got %d", ret);
        destroy_test_context(ctx);
        return;
    }
    pt_queue_free(&q);

    destroy_test_context(ctx);
    PASS();
}

/* Test magic validation */
static void test_queue_magic(void)
{
    TEST("test_queue_magic");

    struct pt_context *ctx = create_test_context();
    pt_queue q;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_queue_init(ctx, &q, 4);

    /* Verify magic is set */
    if (q.magic != PT_QUEUE_MAGIC) {
        FAIL("Magic should be PT_QUEUE_MAGIC");
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Corrupt magic */
    q.magic = 0xDEADBEEF;

    /* Operations should fail */
    uint8_t msg[] = {0xFF};
    int ret = pt_queue_push(ctx, &q, msg, 1, 0, 0);
    if (ret == 0) {
        FAIL("Push should fail with corrupted magic");
        q.magic = PT_QUEUE_MAGIC;  /* Restore for cleanup */
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Restore magic and cleanup */
    q.magic = PT_QUEUE_MAGIC;
    pt_queue_free(&q);
    destroy_test_context(ctx);
    PASS();
}

/* Test pressure calculation overflow */
static void test_queue_pressure_overflow(void)
{
    TEST("test_queue_pressure_overflow");

    struct pt_context *ctx = create_test_context();
    pt_queue q;
    uint8_t msg[] = {0x00};
    uint8_t pressure;
    uint16_t i;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    /* Large queue to test overflow protection (800/1024 = 78.125%) */
    pt_queue_init(ctx, &q, 1024);

    /* Fill to 800 */
    for (i = 0; i < 800; i++) {
        pt_queue_push(ctx, &q, msg, 1, 0, 0);
    }

    /* Calculate pressure */
    pressure = pt_queue_pressure(&q);

    /* Should be 78% (800/1024 * 100 = 78.125, truncated to 78) */
    if (pressure != 78) {
        FAIL("Pressure should be 78, got %u", pressure);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_free(&q);
    destroy_test_context(ctx);
    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== Message Queue Tests ===\n\n");

    /* Run all tests */
    test_queue_init_free();
    test_queue_push_pop();
    test_queue_fifo_order();
    test_queue_full();
    test_queue_pressure();
    test_queue_wrap_around();
    test_queue_coalesce();
    test_queue_peek_consume();
    test_queue_isr_push();
    test_queue_power_of_two();
    test_queue_magic();
    test_queue_pressure_overflow();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
