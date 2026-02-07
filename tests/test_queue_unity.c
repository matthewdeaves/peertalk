/* test_queue_unity.c - Example test using Unity framework
 *
 * This demonstrates how to write tests using Unity instead of the
 * custom TEST/PASS/FAIL macros. Unity provides:
 * - Better assertions with clear failure messages
 * - setUp/tearDown fixtures
 * - XML/JUnit output for CI integration
 *
 * Build: make test-unity
 */

#include "unity/unity.h"
#include "../src/core/queue.h"
#include "../src/core/pt_internal.h"
#include "../src/core/pt_compat.h"

/* Test fixture state */
static struct pt_context *ctx;
static pt_queue queue;

/* ========================================================================
 * Fixtures - called before/after each test
 * ======================================================================== */

void setUp(void)
{
    pt_platform_ops *plat;

    ctx = (struct pt_context *)pt_alloc_clear(sizeof(struct pt_context));
    TEST_ASSERT_NOT_NULL_MESSAGE(ctx, "Failed to allocate context");

    plat = (pt_platform_ops *)pt_alloc_clear(sizeof(pt_platform_ops));
    TEST_ASSERT_NOT_NULL_MESSAGE(plat, "Failed to allocate platform ops");

    ctx->magic = PT_CONTEXT_MAGIC;
    ctx->plat = plat;

    int ret = pt_queue_init(ctx, &queue, 8);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, ret, "pt_queue_init failed");
}

void tearDown(void)
{
    pt_queue_free(&queue);
    if (ctx) {
        if (ctx->plat) pt_free(ctx->plat);
        ctx->magic = 0;
        pt_free(ctx);
        ctx = NULL;
    }
}

/* ========================================================================
 * Test Cases
 * ======================================================================== */

void test_queue_init_sets_magic(void)
{
    TEST_ASSERT_EQUAL_HEX32(PT_QUEUE_MAGIC, queue.magic);
}

void test_queue_init_sets_capacity(void)
{
    TEST_ASSERT_EQUAL_UINT16(8, queue.capacity);
    TEST_ASSERT_EQUAL_UINT16(7, queue.capacity_mask);
}

void test_queue_starts_empty(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, queue.count);
    TEST_ASSERT_TRUE(pt_queue_is_empty(&queue));
    TEST_ASSERT_FALSE(pt_queue_is_full(&queue));
}

void test_queue_push_pop_single(void)
{
    uint8_t send_buf[] = {1, 2, 3, 4, 5};
    uint8_t recv_buf[256];
    uint16_t len;

    int ret = pt_queue_push(ctx, &queue, send_buf, 5, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_UINT16(1, queue.count);

    ret = pt_queue_pop(&queue, recv_buf, &len);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_UINT16(5, len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(send_buf, recv_buf, 5);
    TEST_ASSERT_EQUAL_UINT16(0, queue.count);
}

void test_queue_fifo_order(void)
{
    uint8_t msg1[] = {1};
    uint8_t msg2[] = {2};
    uint8_t msg3[] = {3};
    uint8_t recv_buf[256];
    uint16_t len;

    pt_queue_push(ctx, &queue, msg1, 1, 0, 0);
    pt_queue_push(ctx, &queue, msg2, 1, 0, 0);
    pt_queue_push(ctx, &queue, msg3, 1, 0, 0);

    pt_queue_pop(&queue, recv_buf, &len);
    TEST_ASSERT_EQUAL_UINT8(1, recv_buf[0]);

    pt_queue_pop(&queue, recv_buf, &len);
    TEST_ASSERT_EQUAL_UINT8(2, recv_buf[0]);

    pt_queue_pop(&queue, recv_buf, &len);
    TEST_ASSERT_EQUAL_UINT8(3, recv_buf[0]);
}

void test_queue_full_returns_error(void)
{
    uint8_t msg[] = {0xAA};

    /* Fill queue (capacity 8) */
    for (int i = 0; i < 8; i++) {
        int ret = pt_queue_push(ctx, &queue, msg, 1, 0, 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, ret, "Push should succeed");
    }

    TEST_ASSERT_TRUE(pt_queue_is_full(&queue));

    /* Next push should fail */
    int ret = pt_queue_push(ctx, &queue, msg, 1, 0, 0);
    TEST_ASSERT_EQUAL_INT(PT_ERR_BUFFER_FULL, ret);
}

void test_queue_pressure_calculation(void)
{
    uint8_t msg[] = {0xBB};

    /* Empty: 0% */
    TEST_ASSERT_EQUAL_UINT8(0, pt_queue_pressure(&queue));

    /* 50% full (4/8) */
    for (int i = 0; i < 4; i++) {
        pt_queue_push(ctx, &queue, msg, 1, 0, 0);
    }
    TEST_ASSERT_EQUAL_UINT8(50, pt_queue_pressure(&queue));

    /* 100% full (8/8) */
    for (int i = 0; i < 4; i++) {
        pt_queue_push(ctx, &queue, msg, 1, 0, 0);
    }
    TEST_ASSERT_EQUAL_UINT8(100, pt_queue_pressure(&queue));
}

void test_queue_isr_push(void)
{
    uint8_t msg[] = {0x11, 0x22};

    int ret = pt_queue_push_isr(&queue, msg, 2);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_UINT8(1, queue.has_data);
    TEST_ASSERT_EQUAL_UINT16(1, queue.count);
}

void test_queue_peek_consume(void)
{
    uint8_t msg[] = {0xDD, 0xEE, 0xFF};
    void *peek_data;
    uint16_t len;

    pt_queue_push(ctx, &queue, msg, 3, 0, 0);

    /* Peek should not remove */
    int ret = pt_queue_peek(&queue, &peek_data, &len);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_UINT16(3, len);
    TEST_ASSERT_EQUAL_UINT8(0xDD, ((uint8_t *)peek_data)[0]);
    TEST_ASSERT_EQUAL_UINT16(1, queue.count);

    /* Consume removes */
    pt_queue_consume(&queue);
    TEST_ASSERT_EQUAL_UINT16(0, queue.count);
}

void test_queue_rejects_non_power_of_two(void)
{
    pt_queue bad_queue;
    int ret = pt_queue_init(ctx, &bad_queue, 7);
    TEST_ASSERT_EQUAL_INT(PT_ERR_INVALID_PARAM, ret);
}

void test_queue_detects_corrupted_magic(void)
{
    uint8_t msg[] = {0xFF};

    queue.magic = 0xDEADBEEF;  /* Corrupt */

    int ret = pt_queue_push(ctx, &queue, msg, 1, 0, 0);
    TEST_ASSERT_NOT_EQUAL(0, ret);

    queue.magic = PT_QUEUE_MAGIC;  /* Restore for tearDown */
}

/* ========================================================================
 * Main - Unity runner
 * ======================================================================== */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_queue_init_sets_magic);
    RUN_TEST(test_queue_init_sets_capacity);
    RUN_TEST(test_queue_starts_empty);
    RUN_TEST(test_queue_push_pop_single);
    RUN_TEST(test_queue_fifo_order);
    RUN_TEST(test_queue_full_returns_error);
    RUN_TEST(test_queue_pressure_calculation);
    RUN_TEST(test_queue_isr_push);
    RUN_TEST(test_queue_peek_consume);
    RUN_TEST(test_queue_rejects_non_power_of_two);
    RUN_TEST(test_queue_detects_corrupted_magic);

    return UNITY_END();
}
