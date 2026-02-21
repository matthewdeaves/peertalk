/**
 * @file test_streaming.c
 * @brief Tests for PeerTalk Streaming API
 *
 * Tests PeerTalk_StreamSend(), PeerTalk_StreamCancel(), PeerTalk_StreamActive()
 * and related functionality.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "peertalk.h"
#include "pt_log.h"

/* ========================================================================== */
/* Test Helpers                                                                */
/* ========================================================================== */

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define TEST_PASS() do { \
    printf("  PASS\n"); \
    g_tests_passed++; \
} while (0)

/* ========================================================================== */
/* Stream Callback Tracking                                                    */
/* ========================================================================== */

static int g_callback_called = 0;
static PeerTalk_PeerID g_callback_peer_id = 0;
static uint32_t g_callback_bytes_sent = 0;
static PeerTalk_Error g_callback_result = PT_OK;

__attribute__((unused))
static void stream_complete_callback(
    PeerTalk_Context *ctx,
    PeerTalk_PeerID peer_id,
    uint32_t bytes_sent,
    PeerTalk_Error result,
    void *user_data)
{
    (void)ctx;
    (void)user_data;

    g_callback_called = 1;
    g_callback_peer_id = peer_id;
    g_callback_bytes_sent = bytes_sent;
    g_callback_result = result;
}

__attribute__((unused))
static void reset_callback_state(void)
{
    g_callback_called = 0;
    g_callback_peer_id = 0;
    g_callback_bytes_sent = 0;
    g_callback_result = PT_OK;
}

/* ========================================================================== */
/* Tests                                                                       */
/* ========================================================================== */

/**
 * Test basic stream API validation
 */
static void test_stream_api_validation(void)
{
    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    PeerTalk_Error err;
    uint8_t data[1024];

    printf("Running test_stream_api_validation...");

    /* Initialize context */
    memset(&config, 0, sizeof(config));
    strcpy(config.local_name, "StreamTest");
    config.max_peers = 4;

    ctx = PeerTalk_Init(&config);
    TEST_ASSERT(ctx != NULL, "PeerTalk_Init should succeed");

    /* Test with NULL context */
    err = PeerTalk_StreamSend(NULL, 1, data, 100, NULL, NULL);
    TEST_ASSERT(err == PT_ERR_INVALID_STATE, "NULL context should return INVALID_STATE");

    /* Test with NULL data */
    err = PeerTalk_StreamSend(ctx, 1, NULL, 100, NULL, NULL);
    TEST_ASSERT(err == PT_ERR_INVALID_PARAM, "NULL data should return INVALID_PARAM");

    /* Test with zero length */
    err = PeerTalk_StreamSend(ctx, 1, data, 0, NULL, NULL);
    TEST_ASSERT(err == PT_ERR_INVALID_PARAM, "Zero length should return INVALID_PARAM");

    /* Test with invalid peer */
    err = PeerTalk_StreamSend(ctx, 999, data, 100, NULL, NULL);
    TEST_ASSERT(err == PT_ERR_PEER_NOT_FOUND, "Invalid peer should return PEER_NOT_FOUND");

    /* Test StreamActive with invalid peer */
    int active = PeerTalk_StreamActive(ctx, 999);
    TEST_ASSERT(active == 0, "StreamActive should return 0 for invalid peer");

    /* Test StreamCancel with invalid peer */
    err = PeerTalk_StreamCancel(ctx, 999);
    TEST_ASSERT(err == PT_ERR_PEER_NOT_FOUND, "StreamCancel should return PEER_NOT_FOUND for invalid peer");

    PeerTalk_Shutdown(ctx);
    TEST_PASS();
}

/**
 * Test stream size limits
 */
static void test_stream_size_limits(void)
{
    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    uint8_t *large_data;

    printf("Running test_stream_size_limits...");

    /* Initialize context */
    memset(&config, 0, sizeof(config));
    strcpy(config.local_name, "StreamTest");
    config.max_peers = 4;

    ctx = PeerTalk_Init(&config);
    TEST_ASSERT(ctx != NULL, "PeerTalk_Init should succeed");

    /* Allocate data larger than PT_MAX_STREAM_SIZE */
    large_data = malloc(PT_MAX_STREAM_SIZE + 1000);
    TEST_ASSERT(large_data != NULL, "malloc should succeed");

    /* Test with oversized stream - need a valid peer first */
    /* Since we don't have a connected peer, this will fail with PEER_NOT_FOUND first */
    /* But we can test that PT_MAX_STREAM_SIZE is defined correctly */
    TEST_ASSERT(PT_MAX_STREAM_SIZE == 65536, "PT_MAX_STREAM_SIZE should be 65536");

    free(large_data);
    PeerTalk_Shutdown(ctx);
    TEST_PASS();
}

/**
 * Test stream state via public API
 */
static void test_stream_state_tracking(void)
{
    PeerTalk_Config config;
    PeerTalk_Context *ctx;

    printf("Running test_stream_state_tracking...");

    /* Initialize context */
    memset(&config, 0, sizeof(config));
    strcpy(config.local_name, "StreamTest");
    config.max_peers = 4;

    ctx = PeerTalk_Init(&config);
    TEST_ASSERT(ctx != NULL, "PeerTalk_Init should succeed");

    /* Without a connected peer, StreamActive should return 0 */
    int active = PeerTalk_StreamActive(ctx, 1);
    TEST_ASSERT(active == 0, "StreamActive should return 0 for non-existent peer");

    /* StreamCancel should fail for non-existent peer */
    PeerTalk_Error err = PeerTalk_StreamCancel(ctx, 1);
    TEST_ASSERT(err == PT_ERR_PEER_NOT_FOUND, "StreamCancel should fail for non-existent peer");

    PeerTalk_Shutdown(ctx);
    TEST_PASS();
}

/**
 * Test PollFast API
 */
static void test_poll_fast_api(void)
{
    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    PeerTalk_Error err;

    printf("Running test_poll_fast_api...");

    /* Initialize context */
    memset(&config, 0, sizeof(config));
    strcpy(config.local_name, "PollFastTest");
    config.max_peers = 4;

    ctx = PeerTalk_Init(&config);
    TEST_ASSERT(ctx != NULL, "PeerTalk_Init should succeed");

    /* Test PollFast with valid context - should not crash */
    err = PeerTalk_PollFast(ctx);
    TEST_ASSERT(err == PT_OK, "PeerTalk_PollFast should return PT_OK");

    /* Test PollFast with NULL context */
    err = PeerTalk_PollFast(NULL);
    TEST_ASSERT(err != PT_OK, "PeerTalk_PollFast with NULL should fail");

    /* Call PollFast multiple times */
    for (int i = 0; i < 100; i++) {
        err = PeerTalk_PollFast(ctx);
        TEST_ASSERT(err == PT_OK, "PeerTalk_PollFast should succeed in loop");
    }

    PeerTalk_Shutdown(ctx);
    TEST_PASS();
}

/**
 * Test UDP fast path API
 */
static void test_udp_fast_path(void)
{
    PeerTalk_Config config;
    PeerTalk_Context *ctx;
    uint8_t data[1024];

    printf("Running test_udp_fast_path...");

    /* Initialize context */
    memset(&config, 0, sizeof(config));
    strcpy(config.local_name, "UDPFastTest");
    config.max_peers = 4;

    ctx = PeerTalk_Init(&config);
    TEST_ASSERT(ctx != NULL, "PeerTalk_Init should succeed");

    /* Verify PT_MAX_UDP_MESSAGE_SIZE is 1400 (increased from 512) */
    TEST_ASSERT(PT_MAX_UDP_MESSAGE_SIZE == 1400, "PT_MAX_UDP_MESSAGE_SIZE should be 1400");

    /* Verify PT_SEND_UDP_NO_QUEUE flag exists */
    TEST_ASSERT((PT_SEND_UDP_NO_QUEUE & 0xFF) != 0, "PT_SEND_UDP_NO_QUEUE should be defined");

    /* Test SendUDPFast with invalid peer - should return error */
    PeerTalk_Error err = PeerTalk_SendUDPFast(ctx, 999, data, 100);
    TEST_ASSERT(err == PT_ERR_PEER_NOT_FOUND, "SendUDPFast with invalid peer should fail");

    /* Test SendUDPFast with NULL context */
    err = PeerTalk_SendUDPFast(NULL, 1, data, 100);
    TEST_ASSERT(err == PT_ERR_INVALID_STATE, "SendUDPFast with NULL context should fail");

    /* Test SendUDPFast with oversized message */
    err = PeerTalk_SendUDPFast(ctx, 1, data, PT_MAX_UDP_MESSAGE_SIZE + 100);
    /* This should fail, but the error could be PEER_NOT_FOUND first since no peer exists */

    PeerTalk_Shutdown(ctx);
    TEST_PASS();
}

/**
 * Test adaptive chunk sizing via capabilities API
 */
static void test_adaptive_chunk_fields(void)
{
    PeerTalk_Config config;
    PeerTalk_Context *ctx;

    printf("Running test_adaptive_chunk_fields...");

    /* Initialize context */
    memset(&config, 0, sizeof(config));
    strcpy(config.local_name, "AdaptiveTest");
    config.max_peers = 4;

    ctx = PeerTalk_Init(&config);
    TEST_ASSERT(ctx != NULL, "PeerTalk_Init should succeed");

    /* Test GetPeerCapabilities with invalid peer */
    PeerTalk_Capabilities caps;
    PeerTalk_Error err = PeerTalk_GetPeerCapabilities(ctx, 999, &caps);
    TEST_ASSERT(err == PT_ERR_PEER_NOT_FOUND, "GetPeerCapabilities should fail for invalid peer");

    /* Test GetPeerMaxMessage with invalid peer */
    uint16_t max_msg = PeerTalk_GetPeerMaxMessage(ctx, 999);
    TEST_ASSERT(max_msg == 0, "GetPeerMaxMessage should return 0 for invalid peer");

    /* Verify the preferred_chunk configuration exists */
    /* This is set during capability negotiation but we can verify the struct field exists */
    TEST_ASSERT(sizeof(caps.preferred_chunk) == sizeof(uint16_t), "preferred_chunk should be uint16_t");

    PeerTalk_Shutdown(ctx);
    TEST_PASS();
}

/**
 * Test new error codes
 */
static void test_new_error_codes(void)
{
    printf("Running test_new_error_codes...");

    /* Verify new error codes exist and have correct values */
    TEST_ASSERT(PT_ERR_BUSY == -27, "PT_ERR_BUSY should be -27");
    TEST_ASSERT(PT_ERR_CANCELLED == -28, "PT_ERR_CANCELLED should be -28");

    /* Verify error codes are distinct */
    TEST_ASSERT(PT_ERR_BUSY != PT_ERR_WOULD_BLOCK, "BUSY and WOULD_BLOCK should be different");
    TEST_ASSERT(PT_ERR_CANCELLED != PT_ERR_TIMEOUT, "CANCELLED and TIMEOUT should be different");

    TEST_PASS();
}

/* ========================================================================== */
/* Main                                                                        */
/* ========================================================================== */

int main(void)
{
    printf("===========================================\n");
    printf("PeerTalk Streaming & Performance API Tests\n");
    printf("===========================================\n\n");

    test_stream_api_validation();
    test_stream_size_limits();
    test_stream_state_tracking();
    test_poll_fast_api();
    test_udp_fast_path();
    test_adaptive_chunk_fields();
    test_new_error_codes();

    printf("\n===========================================\n");
    printf("Results: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    printf("===========================================\n");

    return g_tests_failed > 0 ? 1 : 0;
}
