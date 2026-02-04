/*
 * test_sendex.c - Unit tests for Phase 3.5 SendEx API
 *
 * Tests PeerTalk_Send(), PeerTalk_SendEx(), and PeerTalk_Broadcast()
 * with priority, coalescing, and backpressure handling.
 */

#include "../include/peertalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test state */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        tests_failed++; \
        return; \
    } \
    tests_passed++; \
} while(0)

/* ========================================================================== */
/* Test: PeerTalk_Send basic functionality                                   */
/* ========================================================================== */

void test_send_basic(void) {
    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    PeerTalk_Error err;
    const char *msg = "Hello";

    printf("TEST: PeerTalk_Send basic functionality\n");

    /* Initialize PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestPeer", PT_MAX_PEER_NAME - 1);
    config.max_peers = 8;

    ctx = PeerTalk_Init(&config);
    TEST_ASSERT(ctx != NULL, "PeerTalk_Init failed");

    /* Try sending to non-existent peer - should fail */
    err = PeerTalk_Send(ctx, 999, msg, strlen(msg));
    TEST_ASSERT(err == PT_ERR_PEER_NOT_FOUND, "Send to non-existent peer should fail");

    /* Cleanup */
    PeerTalk_Shutdown(ctx);
    printf("  PASS\n");
}

/* ========================================================================== */
/* Test: PeerTalk_SendEx with priorities                                     */
/* ========================================================================== */

void test_sendex_priorities(void) {
    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    PeerTalk_Error err;
    const char *msg = "Priority test";

    printf("TEST: PeerTalk_SendEx with priorities\n");

    /* Initialize PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestPeer", PT_MAX_PEER_NAME - 1);
    config.max_peers = 8;

    ctx = PeerTalk_Init(&config);
    TEST_ASSERT(ctx != NULL, "PeerTalk_Init failed");

    /* Test invalid priority - should fail */
    err = PeerTalk_SendEx(ctx, 1, msg, strlen(msg), 99, PT_SEND_DEFAULT, 0);
    TEST_ASSERT(err == PT_ERR_INVALID_PARAM, "Invalid priority should fail");

    /* Test NULL data - should fail */
    err = PeerTalk_SendEx(ctx, 1, NULL, 100, PT_PRIORITY_NORMAL, PT_SEND_DEFAULT, 0);
    TEST_ASSERT(err == PT_ERR_INVALID_PARAM, "NULL data should fail");

    /* Test zero length - should fail */
    err = PeerTalk_SendEx(ctx, 1, msg, 0, PT_PRIORITY_NORMAL, PT_SEND_DEFAULT, 0);
    TEST_ASSERT(err == PT_ERR_INVALID_PARAM, "Zero length should fail");

    /* Test oversized message - should fail */
    err = PeerTalk_SendEx(ctx, 1, msg, PT_MAX_MESSAGE_SIZE + 1, PT_PRIORITY_NORMAL, PT_SEND_DEFAULT, 0);
    TEST_ASSERT(err == PT_ERR_INVALID_PARAM, "Oversized message should fail");

    /* Cleanup */
    PeerTalk_Shutdown(ctx);
    printf("  PASS\n");
}

/* ========================================================================== */
/* Test: PeerTalk_GetPeers                                                   */
/* ========================================================================== */

void test_getpeers(void) {
    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    PeerTalk_Error err;
    PeerTalk_PeerInfo peers[16];
    uint16_t count;

    printf("TEST: PeerTalk_GetPeers\n");

    /* Initialize PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestPeer", PT_MAX_PEER_NAME - 1);
    config.max_peers = 16;

    ctx = PeerTalk_Init(&config);
    TEST_ASSERT(ctx != NULL, "PeerTalk_Init failed");

    /* Get peers - should be empty */
    err = PeerTalk_GetPeers(ctx, peers, 16, &count);
    TEST_ASSERT(err == PT_OK, "GetPeers failed");
    TEST_ASSERT(count == 0, "Should have 0 peers initially");

    /* Test NULL checks */
    err = PeerTalk_GetPeers(NULL, peers, 16, &count);
    TEST_ASSERT(err == PT_ERR_INVALID_STATE, "NULL context should fail");

    err = PeerTalk_GetPeers(ctx, NULL, 16, &count);
    TEST_ASSERT(err == PT_ERR_INVALID_PARAM, "NULL peers buffer should fail");

    err = PeerTalk_GetPeers(ctx, peers, 16, NULL);
    TEST_ASSERT(err == PT_ERR_INVALID_PARAM, "NULL out_count should fail");

    /* Cleanup */
    PeerTalk_Shutdown(ctx);
    printf("  PASS\n");
}

/* ========================================================================== */
/* Test: PeerTalk_Broadcast                                                  */
/* ========================================================================== */

void test_broadcast(void) {
    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    PeerTalk_Error err;
    const char *msg = "Broadcast test";

    printf("TEST: PeerTalk_Broadcast\n");

    /* Initialize PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestPeer", PT_MAX_PEER_NAME - 1);
    config.max_peers = 8;

    ctx = PeerTalk_Init(&config);
    TEST_ASSERT(ctx != NULL, "PeerTalk_Init failed");

    /* Broadcast with no peers - should fail */
    err = PeerTalk_Broadcast(ctx, msg, strlen(msg));
    TEST_ASSERT(err == PT_ERR_PEER_NOT_FOUND, "Broadcast with no peers should fail");

    /* Test NULL checks */
    err = PeerTalk_Broadcast(NULL, msg, strlen(msg));
    TEST_ASSERT(err == PT_ERR_INVALID_STATE, "NULL context should fail");

    err = PeerTalk_Broadcast(ctx, NULL, 100);
    TEST_ASSERT(err == PT_ERR_INVALID_PARAM, "NULL data should fail");

    err = PeerTalk_Broadcast(ctx, msg, 0);
    TEST_ASSERT(err == PT_ERR_INVALID_PARAM, "Zero length should fail");

    err = PeerTalk_Broadcast(ctx, msg, PT_MAX_MESSAGE_SIZE + 1);
    TEST_ASSERT(err == PT_ERR_INVALID_PARAM, "Oversized message should fail");

    /* Cleanup */
    PeerTalk_Shutdown(ctx);
    printf("  PASS\n");
}

/* ========================================================================== */
/* Test: SendEx with PT_SEND_UNRELIABLE flag                                 */
/* ========================================================================== */

void test_sendex_unreliable(void) {
    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    PeerTalk_Error err;
    const char *msg = "Unreliable test";

    printf("TEST: PeerTalk_SendEx with PT_SEND_UNRELIABLE\n");

    /* Initialize PeerTalk */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestPeer", PT_MAX_PEER_NAME - 1);
    config.max_peers = 8;
    config.transports = PT_TRANSPORT_TCP | PT_TRANSPORT_UDP;

    ctx = PeerTalk_Init(&config);
    TEST_ASSERT(ctx != NULL, "PeerTalk_Init failed");

    /* Try unreliable send to non-existent peer - should fail */
    err = PeerTalk_SendEx(ctx, 999, msg, strlen(msg),
                         PT_PRIORITY_NORMAL,
                         PT_SEND_UNRELIABLE,
                         0);
    TEST_ASSERT(err == PT_ERR_PEER_NOT_FOUND, "Unreliable send to non-existent peer should fail");

    /* Cleanup */
    PeerTalk_Shutdown(ctx);
    printf("  PASS\n");
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
    printf("=== PeerTalk SendEx API Tests ===\n\n");

    test_send_basic();
    test_sendex_priorities();
    test_getpeers();
    test_broadcast();
    test_sendex_unreliable();

    printf("\n=== Test Results ===\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);

    if (tests_failed == 0) {
        printf("\n✓ ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("\n✗ SOME TESTS FAILED\n");
        return 1;
    }
}
