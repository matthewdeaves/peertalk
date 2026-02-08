/* test_direct_buffer.c - Tests for Tier 2 direct buffers
 *
 * Tests the two-tier message queue system:
 * - Tier 1: 256-byte queue slots for control messages
 * - Tier 2: 4KB direct buffers for large messages
 */

#include "../src/core/direct_buffer.h"
#include "../src/core/pt_compat.h"
#include "../include/peertalk.h"
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

/* ========================================================================
 * Basic Lifecycle Tests
 * ======================================================================== */

static void test_direct_buffer_init_free(void)
{
    TEST("test_direct_buffer_init_free");

    pt_direct_buffer buf;
    int ret;

    /* Initialize with default size */
    ret = pt_direct_buffer_init(&buf, PT_DIRECT_DEFAULT_SIZE);
    if (ret != PT_OK) {
        FAIL("Init failed: %d", ret);
        return;
    }

    if (buf.data == NULL) {
        FAIL("Data buffer should be allocated");
        return;
    }

    if (buf.capacity != PT_DIRECT_DEFAULT_SIZE) {
        FAIL("Capacity should be %d, got %u", PT_DIRECT_DEFAULT_SIZE, buf.capacity);
        pt_direct_buffer_free(&buf);
        return;
    }

    if (buf.state != PT_DIRECT_IDLE) {
        FAIL("Initial state should be IDLE");
        pt_direct_buffer_free(&buf);
        return;
    }

    if (buf.length != 0) {
        FAIL("Initial length should be 0, got %u", buf.length);
        pt_direct_buffer_free(&buf);
        return;
    }

    pt_direct_buffer_free(&buf);

    if (buf.data != NULL) {
        FAIL("Data should be NULL after free");
        return;
    }

    PASS();
}

static void test_direct_buffer_init_custom_size(void)
{
    TEST("test_direct_buffer_init_custom_size");

    pt_direct_buffer buf;
    int ret;

    /* Initialize with custom size */
    ret = pt_direct_buffer_init(&buf, 2048);
    if (ret != PT_OK) {
        FAIL("Init with custom size failed: %d", ret);
        return;
    }

    if (buf.capacity != 2048) {
        FAIL("Capacity should be 2048, got %u", buf.capacity);
        pt_direct_buffer_free(&buf);
        return;
    }

    pt_direct_buffer_free(&buf);
    PASS();
}

static void test_direct_buffer_init_zero_uses_default(void)
{
    TEST("test_direct_buffer_init_zero_uses_default");

    pt_direct_buffer buf;
    int ret;

    /* Initialize with 0 should use default */
    ret = pt_direct_buffer_init(&buf, 0);
    if (ret != PT_OK) {
        FAIL("Init with 0 failed: %d", ret);
        return;
    }

    if (buf.capacity != PT_DIRECT_DEFAULT_SIZE) {
        FAIL("Capacity should be %d when 0 passed, got %u",
             PT_DIRECT_DEFAULT_SIZE, buf.capacity);
        pt_direct_buffer_free(&buf);
        return;
    }

    pt_direct_buffer_free(&buf);
    PASS();
}

static void test_direct_buffer_init_max_size(void)
{
    TEST("test_direct_buffer_init_max_size");

    pt_direct_buffer buf;
    int ret;

    /* Initialize with max size */
    ret = pt_direct_buffer_init(&buf, PT_DIRECT_MAX_SIZE);
    if (ret != PT_OK) {
        FAIL("Init with max size failed: %d", ret);
        return;
    }

    if (buf.capacity != PT_DIRECT_MAX_SIZE) {
        FAIL("Capacity should be %d, got %u", PT_DIRECT_MAX_SIZE, buf.capacity);
        pt_direct_buffer_free(&buf);
        return;
    }

    pt_direct_buffer_free(&buf);

    /* Try exceeding max size */
    ret = pt_direct_buffer_init(&buf, PT_DIRECT_MAX_SIZE + 1);
    if (ret != PT_ERR_INVALID_PARAM) {
        FAIL("Should reject size > max, got %d", ret);
        pt_direct_buffer_free(&buf);
        return;
    }

    PASS();
}

/* ========================================================================
 * Queue/Send Tests
 * ======================================================================== */

static void test_direct_buffer_queue_basic(void)
{
    TEST("test_direct_buffer_queue_basic");

    pt_direct_buffer buf;
    uint8_t data[1024];
    int ret;
    int i;

    /* Fill test data */
    for (i = 0; i < 1024; i++) {
        data[i] = (uint8_t)(i & 0xFF);
    }

    pt_direct_buffer_init(&buf, PT_DIRECT_DEFAULT_SIZE);

    /* Queue data */
    ret = pt_direct_buffer_queue(&buf, data, 1024, PT_PRIORITY_HIGH);
    if (ret != PT_OK) {
        FAIL("Queue failed: %d", ret);
        pt_direct_buffer_free(&buf);
        return;
    }

    if (buf.state != PT_DIRECT_QUEUED) {
        FAIL("State should be QUEUED after queue");
        pt_direct_buffer_free(&buf);
        return;
    }

    if (buf.length != 1024) {
        FAIL("Length should be 1024, got %u", buf.length);
        pt_direct_buffer_free(&buf);
        return;
    }

    if (buf.priority != PT_PRIORITY_HIGH) {
        FAIL("Priority should be HIGH");
        pt_direct_buffer_free(&buf);
        return;
    }

    /* Verify data was copied */
    if (memcmp(buf.data, data, 1024) != 0) {
        FAIL("Data mismatch after queue");
        pt_direct_buffer_free(&buf);
        return;
    }

    pt_direct_buffer_free(&buf);
    PASS();
}

static void test_direct_buffer_would_block(void)
{
    TEST("test_direct_buffer_would_block");

    pt_direct_buffer buf;
    uint8_t data1[512] = {1, 2, 3, 4};
    uint8_t data2[512] = {5, 6, 7, 8};
    int ret;

    pt_direct_buffer_init(&buf, PT_DIRECT_DEFAULT_SIZE);

    /* Queue first message */
    ret = pt_direct_buffer_queue(&buf, data1, 512, PT_PRIORITY_NORMAL);
    if (ret != PT_OK) {
        FAIL("First queue failed: %d", ret);
        pt_direct_buffer_free(&buf);
        return;
    }

    /* Try to queue second - should fail with WOULD_BLOCK */
    ret = pt_direct_buffer_queue(&buf, data2, 512, PT_PRIORITY_NORMAL);
    if (ret != PT_ERR_WOULD_BLOCK) {
        FAIL("Should return WOULD_BLOCK, got %d", ret);
        pt_direct_buffer_free(&buf);
        return;
    }

    /* Complete the first send */
    pt_direct_buffer_mark_sending(&buf);
    pt_direct_buffer_complete(&buf);

    /* Now second queue should succeed */
    ret = pt_direct_buffer_queue(&buf, data2, 512, PT_PRIORITY_NORMAL);
    if (ret != PT_OK) {
        FAIL("Second queue after complete failed: %d", ret);
        pt_direct_buffer_free(&buf);
        return;
    }

    pt_direct_buffer_free(&buf);
    PASS();
}

static void test_direct_buffer_message_too_large(void)
{
    TEST("test_direct_buffer_message_too_large");

    pt_direct_buffer buf;
    uint8_t data[PT_DIRECT_DEFAULT_SIZE + 1];
    int ret;

    pt_direct_buffer_init(&buf, PT_DIRECT_DEFAULT_SIZE);

    /* Try to queue message larger than capacity */
    ret = pt_direct_buffer_queue(&buf, data, PT_DIRECT_DEFAULT_SIZE + 1, PT_PRIORITY_NORMAL);
    if (ret != PT_ERR_MESSAGE_TOO_LARGE) {
        FAIL("Should return MESSAGE_TOO_LARGE, got %d", ret);
        pt_direct_buffer_free(&buf);
        return;
    }

    /* Buffer should still be idle */
    if (buf.state != PT_DIRECT_IDLE) {
        FAIL("State should still be IDLE");
        pt_direct_buffer_free(&buf);
        return;
    }

    pt_direct_buffer_free(&buf);
    PASS();
}

/* ========================================================================
 * State Transition Tests
 * ======================================================================== */

static void test_direct_buffer_state_transitions(void)
{
    TEST("test_direct_buffer_state_transitions");

    pt_direct_buffer buf;
    uint8_t data[256] = {0};
    int ret;

    pt_direct_buffer_init(&buf, PT_DIRECT_DEFAULT_SIZE);

    /* Start IDLE */
    if (buf.state != PT_DIRECT_IDLE) {
        FAIL("Should start IDLE");
        pt_direct_buffer_free(&buf);
        return;
    }

    if (!pt_direct_buffer_available(&buf)) {
        FAIL("Should be available when IDLE");
        pt_direct_buffer_free(&buf);
        return;
    }

    /* Queue: IDLE -> QUEUED */
    ret = pt_direct_buffer_queue(&buf, data, 256, PT_PRIORITY_NORMAL);
    if (ret != PT_OK || buf.state != PT_DIRECT_QUEUED) {
        FAIL("Queue should transition to QUEUED");
        pt_direct_buffer_free(&buf);
        return;
    }

    if (!pt_direct_buffer_ready(&buf)) {
        FAIL("Should be ready when QUEUED");
        pt_direct_buffer_free(&buf);
        return;
    }

    if (pt_direct_buffer_available(&buf)) {
        FAIL("Should NOT be available when QUEUED");
        pt_direct_buffer_free(&buf);
        return;
    }

    /* Mark sending: QUEUED -> SENDING */
    ret = pt_direct_buffer_mark_sending(&buf);
    if (ret != 0 || buf.state != PT_DIRECT_SENDING) {
        FAIL("Mark sending should transition to SENDING");
        pt_direct_buffer_free(&buf);
        return;
    }

    if (pt_direct_buffer_ready(&buf)) {
        FAIL("Should NOT be ready when SENDING");
        pt_direct_buffer_free(&buf);
        return;
    }

    /* Complete: SENDING -> IDLE */
    pt_direct_buffer_complete(&buf);
    if (buf.state != PT_DIRECT_IDLE) {
        FAIL("Complete should transition to IDLE");
        pt_direct_buffer_free(&buf);
        return;
    }

    if (!pt_direct_buffer_available(&buf)) {
        FAIL("Should be available after complete");
        pt_direct_buffer_free(&buf);
        return;
    }

    pt_direct_buffer_free(&buf);
    PASS();
}

static void test_direct_buffer_mark_sending_wrong_state(void)
{
    TEST("test_direct_buffer_mark_sending_wrong_state");

    pt_direct_buffer buf;
    int ret;

    pt_direct_buffer_init(&buf, PT_DIRECT_DEFAULT_SIZE);

    /* Try to mark sending when IDLE (should fail) */
    ret = pt_direct_buffer_mark_sending(&buf);
    if (ret != -1) {
        FAIL("Mark sending when IDLE should fail");
        pt_direct_buffer_free(&buf);
        return;
    }

    pt_direct_buffer_free(&buf);
    PASS();
}

/* ========================================================================
 * Receive Path Tests
 * ======================================================================== */

static void test_direct_buffer_receive(void)
{
    TEST("test_direct_buffer_receive");

    pt_direct_buffer buf;
    uint8_t data[2048];
    int ret;
    int i;

    /* Fill test data */
    for (i = 0; i < 2048; i++) {
        data[i] = (uint8_t)(i & 0xFF);
    }

    pt_direct_buffer_init(&buf, PT_DIRECT_DEFAULT_SIZE);

    /* Receive data */
    ret = pt_direct_buffer_receive(&buf, data, 2048);
    if (ret != PT_OK) {
        FAIL("Receive failed: %d", ret);
        pt_direct_buffer_free(&buf);
        return;
    }

    if (buf.length != 2048) {
        FAIL("Length should be 2048, got %u", buf.length);
        pt_direct_buffer_free(&buf);
        return;
    }

    /* Verify data was copied */
    if (memcmp(buf.data, data, 2048) != 0) {
        FAIL("Data mismatch after receive");
        pt_direct_buffer_free(&buf);
        return;
    }

    pt_direct_buffer_free(&buf);
    PASS();
}

static void test_direct_buffer_receive_too_large(void)
{
    TEST("test_direct_buffer_receive_too_large");

    pt_direct_buffer buf;
    uint8_t data[PT_DIRECT_DEFAULT_SIZE + 1];
    int ret;

    pt_direct_buffer_init(&buf, PT_DIRECT_DEFAULT_SIZE);

    /* Try to receive message larger than capacity */
    ret = pt_direct_buffer_receive(&buf, data, PT_DIRECT_DEFAULT_SIZE + 1);
    if (ret != PT_ERR_MESSAGE_TOO_LARGE) {
        FAIL("Should return MESSAGE_TOO_LARGE, got %d", ret);
        pt_direct_buffer_free(&buf);
        return;
    }

    pt_direct_buffer_free(&buf);
    PASS();
}

/* ========================================================================
 * Edge Cases
 * ======================================================================== */

static void test_direct_buffer_null_safety(void)
{
    TEST("test_direct_buffer_null_safety");

    int ret;
    uint8_t data[32] = {0};

    /* NULL buffer init */
    ret = pt_direct_buffer_init(NULL, PT_DIRECT_DEFAULT_SIZE);
    if (ret != PT_ERR_INVALID_PARAM) {
        FAIL("Init with NULL should fail");
        return;
    }

    /* NULL buffer free - should not crash */
    pt_direct_buffer_free(NULL);

    /* NULL buffer queue */
    pt_direct_buffer buf;
    pt_direct_buffer_init(&buf, PT_DIRECT_DEFAULT_SIZE);

    ret = pt_direct_buffer_queue(&buf, NULL, 32, PT_PRIORITY_NORMAL);
    if (ret != PT_ERR_INVALID_PARAM) {
        FAIL("Queue with NULL data should fail");
        pt_direct_buffer_free(&buf);
        return;
    }

    ret = pt_direct_buffer_queue(&buf, data, 0, PT_PRIORITY_NORMAL);
    if (ret != PT_ERR_INVALID_PARAM) {
        FAIL("Queue with zero length should fail");
        pt_direct_buffer_free(&buf);
        return;
    }

    pt_direct_buffer_free(&buf);
    PASS();
}

static void test_direct_buffer_ready_available_null(void)
{
    TEST("test_direct_buffer_ready_available_null");

    /* NULL checks should return 0, not crash */
    if (pt_direct_buffer_ready(NULL) != 0) {
        FAIL("ready(NULL) should return 0");
        return;
    }

    if (pt_direct_buffer_available(NULL) != 0) {
        FAIL("available(NULL) should return 0");
        return;
    }

    PASS();
}

/* ========================================================================
 * Constants Verification
 * ======================================================================== */

static void test_direct_buffer_constants(void)
{
    TEST("test_direct_buffer_constants");

    /* Verify constants match documented values */
    if (PT_DIRECT_DEFAULT_SIZE != 4096) {
        FAIL("PT_DIRECT_DEFAULT_SIZE should be 4096, got %d", PT_DIRECT_DEFAULT_SIZE);
        return;
    }

    if (PT_DIRECT_MAX_SIZE != 8192) {
        FAIL("PT_DIRECT_MAX_SIZE should be 8192, got %d", PT_DIRECT_MAX_SIZE);
        return;
    }

    if (PT_DIRECT_THRESHOLD != 256) {
        FAIL("PT_DIRECT_THRESHOLD should be 256, got %d", PT_DIRECT_THRESHOLD);
        return;
    }

    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== Direct Buffer (Tier 2) Tests ===\n\n");

    /* Lifecycle tests */
    test_direct_buffer_init_free();
    test_direct_buffer_init_custom_size();
    test_direct_buffer_init_zero_uses_default();
    test_direct_buffer_init_max_size();

    /* Queue/Send tests */
    test_direct_buffer_queue_basic();
    test_direct_buffer_would_block();
    test_direct_buffer_message_too_large();

    /* State transition tests */
    test_direct_buffer_state_transitions();
    test_direct_buffer_mark_sending_wrong_state();

    /* Receive path tests */
    test_direct_buffer_receive();
    test_direct_buffer_receive_too_large();

    /* Edge cases */
    test_direct_buffer_null_safety();
    test_direct_buffer_ready_available_null();

    /* Constants */
    test_direct_buffer_constants();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
