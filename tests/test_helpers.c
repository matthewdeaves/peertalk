/*
 * PeerTalk Helper Functions Test
 *
 * Tests for peer query helpers, queue status, and statistics functions.
 *
 * Tests:
 * - PeerTalk_GetPeersVersion()
 * - PeerTalk_GetPeerByID()
 * - PeerTalk_GetPeer()
 * - PeerTalk_FindPeerByName()
 * - PeerTalk_FindPeerByAddress()
 * - PeerTalk_GetQueueStatus()
 * - PeerTalk_ResetStats()
 */

#include "../include/peertalk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TEST_PASS() do { \
    tests_passed++; \
    printf("  ✓ PASS\n"); \
} while(0)

#define TEST_FAIL(msg) do { \
    tests_failed++; \
    printf("  ✗ FAIL: %s\n", (msg)); \
} while(0)

static int tests_passed = 0;
static int tests_failed = 0;

/* ========================================================================== */
/* Test: GetPeersVersion                                                      */
/* ========================================================================== */

void test_get_peers_version(void) {
    printf("\nTest: PeerTalk_GetPeersVersion\n");

    PeerTalk_Context *ctx;
    PeerTalk_Config config;

    /* Initialize */
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestPeer", PT_MAX_PEER_NAME - 1);
    config.max_peers = 16;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        TEST_FAIL("Init failed");
        return;
    }

    /* Get initial version (starts at 0 before any peer changes) */
    uint32_t version1 = PeerTalk_GetPeersVersion(ctx);

    /* Version should be stable without changes */
    uint32_t version2 = PeerTalk_GetPeersVersion(ctx);
    if (version1 != version2) {
        TEST_FAIL("Version changed without peer modifications");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Test with NULL context */
    uint32_t version_null = PeerTalk_GetPeersVersion(NULL);
    if (version_null != 0) {
        TEST_FAIL("Version with NULL context should return 0");
        PeerTalk_Shutdown(ctx);
        return;
    }

    TEST_PASS();
    PeerTalk_Shutdown(ctx);
}

/* ========================================================================== */
/* Test: GetPeerByID / GetPeer                                                */
/* ========================================================================== */

void test_get_peer_by_id(void) {
    printf("\nTest: PeerTalk_GetPeerByID / PeerTalk_GetPeer\n");

    PeerTalk_Context *ctx;
    PeerTalk_Config config;

    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestPeer", PT_MAX_PEER_NAME - 1);
    config.max_peers = 16;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        TEST_FAIL("Init failed");
        return;
    }

    /* Test with invalid peer ID */
    const PeerTalk_PeerInfo *peer_ptr = PeerTalk_GetPeerByID(ctx, 1);
    if (peer_ptr != NULL) {
        TEST_FAIL("GetPeerByID should return NULL for non-existent peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_PeerInfo peer_info;
    PeerTalk_Error err = PeerTalk_GetPeer(ctx, 1, &peer_info);
    if (err != PT_ERR_PEER_NOT_FOUND) {
        TEST_FAIL("GetPeer should return PT_ERR_PEER_NOT_FOUND");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Test with peer ID 0 (invalid) */
    peer_ptr = PeerTalk_GetPeerByID(ctx, 0);
    if (peer_ptr != NULL) {
        TEST_FAIL("GetPeerByID should return NULL for peer ID 0");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Test NULL context */
    peer_ptr = PeerTalk_GetPeerByID(NULL, 1);
    if (peer_ptr != NULL) {
        TEST_FAIL("GetPeerByID should return NULL for NULL context");
        PeerTalk_Shutdown(ctx);
        return;
    }

    err = PeerTalk_GetPeer(NULL, 1, &peer_info);
    if (err != PT_ERR_INVALID_STATE) {
        TEST_FAIL("GetPeer should return PT_ERR_INVALID_STATE for NULL context");
        PeerTalk_Shutdown(ctx);
        return;
    }

    TEST_PASS();
    PeerTalk_Shutdown(ctx);
}

/* ========================================================================== */
/* Test: FindPeerByName                                                       */
/* ========================================================================== */

void test_find_peer_by_name(void) {
    printf("\nTest: PeerTalk_FindPeerByName\n");

    PeerTalk_Context *ctx;
    PeerTalk_Config config;

    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestPeer", PT_MAX_PEER_NAME - 1);
    config.max_peers = 16;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        TEST_FAIL("Init failed");
        return;
    }

    /* Test with non-existent peer */
    PeerTalk_PeerID peer_id = PeerTalk_FindPeerByName(ctx, "Alice", NULL);
    if (peer_id != 0) {
        TEST_FAIL("FindPeerByName should return 0 for non-existent peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Test with NULL context */
    peer_id = PeerTalk_FindPeerByName(NULL, "Alice", NULL);
    if (peer_id != 0) {
        TEST_FAIL("FindPeerByName should return 0 for NULL context");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Test with NULL name */
    peer_id = PeerTalk_FindPeerByName(ctx, NULL, NULL);
    if (peer_id != 0) {
        TEST_FAIL("FindPeerByName should return 0 for NULL name");
        PeerTalk_Shutdown(ctx);
        return;
    }

    TEST_PASS();
    PeerTalk_Shutdown(ctx);
}

/* ========================================================================== */
/* Test: FindPeerByAddress                                                    */
/* ========================================================================== */

void test_find_peer_by_address(void) {
    printf("\nTest: PeerTalk_FindPeerByAddress\n");

    PeerTalk_Context *ctx;
    PeerTalk_Config config;

    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestPeer", PT_MAX_PEER_NAME - 1);
    config.max_peers = 16;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        TEST_FAIL("Init failed");
        return;
    }

    /* Test with non-existent address */
    uint32_t test_addr = (127 << 24) | (0 << 16) | (0 << 8) | 1;  /* 127.0.0.1 */
    PeerTalk_PeerID peer_id = PeerTalk_FindPeerByAddress(ctx, test_addr, 9999, NULL);
    if (peer_id != 0) {
        TEST_FAIL("FindPeerByAddress should return 0 for non-existent peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Test with NULL context */
    peer_id = PeerTalk_FindPeerByAddress(NULL, test_addr, 9999, NULL);
    if (peer_id != 0) {
        TEST_FAIL("FindPeerByAddress should return 0 for NULL context");
        PeerTalk_Shutdown(ctx);
        return;
    }

    TEST_PASS();
    PeerTalk_Shutdown(ctx);
}

/* ========================================================================== */
/* Test: GetQueueStatus                                                       */
/* ========================================================================== */

void test_get_queue_status(void) {
    printf("\nTest: PeerTalk_GetQueueStatus\n");

    PeerTalk_Context *ctx;
    PeerTalk_Config config;

    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestPeer", PT_MAX_PEER_NAME - 1);
    config.max_peers = 16;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        TEST_FAIL("Init failed");
        return;
    }

    /* Test with invalid peer ID */
    uint16_t pending = 0, available = 0;
    PeerTalk_Error err = PeerTalk_GetQueueStatus(ctx, 1, &pending, &available);
    if (err != PT_ERR_PEER_NOT_FOUND) {
        TEST_FAIL("GetQueueStatus should return PT_ERR_PEER_NOT_FOUND");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Test with NULL context */
    err = PeerTalk_GetQueueStatus(NULL, 1, &pending, &available);
    if (err != PT_ERR_INVALID_STATE) {
        TEST_FAIL("GetQueueStatus should return PT_ERR_INVALID_STATE for NULL context");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Test with NULL output parameters (should be allowed) */
    /* Can't test without a real peer, but error handling is tested above */

    TEST_PASS();
    PeerTalk_Shutdown(ctx);
}

/* ========================================================================== */
/* Test: ResetStats                                                           */
/* ========================================================================== */

void test_reset_stats(void) {
    printf("\nTest: PeerTalk_ResetStats\n");

    PeerTalk_Context *ctx;
    PeerTalk_Config config;

    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "TestPeer", PT_MAX_PEER_NAME - 1);
    config.max_peers = 16;

    ctx = PeerTalk_Init(&config);
    if (!ctx) {
        TEST_FAIL("Init failed");
        return;
    }

    /* Get initial stats */
    PeerTalk_GlobalStats stats1;
    if (PeerTalk_GetGlobalStats(ctx, &stats1) != PT_OK) {
        TEST_FAIL("GetGlobalStats failed");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Reset all stats (peer_id == 0) */
    PeerTalk_Error err = PeerTalk_ResetStats(ctx, 0);
    if (err != PT_OK) {
        TEST_FAIL("ResetStats failed");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Verify stats are zeroed */
    PeerTalk_GlobalStats stats2;
    if (PeerTalk_GetGlobalStats(ctx, &stats2) != PT_OK) {
        TEST_FAIL("GetGlobalStats failed after reset");
        PeerTalk_Shutdown(ctx);
        return;
    }

    if (stats2.total_bytes_sent != 0 ||
        stats2.total_bytes_received != 0 ||
        stats2.total_messages_sent != 0 ||
        stats2.total_messages_received != 0) {
        TEST_FAIL("Stats not properly reset");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Test with invalid peer ID */
    err = PeerTalk_ResetStats(ctx, 99);
    if (err != PT_ERR_PEER_NOT_FOUND) {
        TEST_FAIL("ResetStats should return PT_ERR_PEER_NOT_FOUND");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Test with NULL context */
    err = PeerTalk_ResetStats(NULL, 0);
    if (err != PT_ERR_INVALID_STATE) {
        TEST_FAIL("ResetStats should return PT_ERR_INVALID_STATE for NULL context");
        PeerTalk_Shutdown(ctx);
        return;
    }

    TEST_PASS();
    PeerTalk_Shutdown(ctx);
}

/* ========================================================================== */
/* Main                                                                        */
/* ========================================================================== */

int main(void) {
    printf("PeerTalk Helper Functions Test Suite\n");
    printf("====================================\n");

    test_get_peers_version();
    test_get_peer_by_id();
    test_find_peer_by_name();
    test_find_peer_by_address();
    test_get_queue_status();
    test_reset_stats();

    printf("\n====================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);

    return (tests_failed == 0) ? 0 : 1;
}
