/* test_peer.c - Tests for peer management */

#include "../src/core/peer.h"
#include <stdio.h>
#include <assert.h>

/* Include implementation files for testing */
#include "../src/core/peer.c"

/* Test counter */
static int tests_passed = 0;
static int tests_failed = 0;

/* Stub get_ticks for testing */
static pt_tick_t stub_get_ticks(void) {
    return 1000;  /* Fixed timestamp for testing */
}

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
    PeerTalk_Config config = {0};
    struct pt_context *ctx;
    pt_platform_ops *plat;

    config.max_peers = 8;
    config.discovery_port = 7353;
    config.tcp_port = 7354;
    config.udp_port = 7355;
    pt_strncpy(config.local_name, "TestNode", sizeof(config.local_name));

    ctx = (struct pt_context *)pt_alloc_clear(sizeof(struct pt_context));
    if (!ctx) {
        return NULL;
    }

    /* Create platform ops */
    plat = (pt_platform_ops *)pt_alloc_clear(sizeof(pt_platform_ops));
    if (!plat) {
        pt_free(ctx);
        return NULL;
    }

    /* Minimal platform implementation for testing */
    plat->init = NULL;
    plat->shutdown = NULL;
    plat->poll = NULL;
    plat->get_ticks = stub_get_ticks;  /* Use stub for timestamps */
    plat->get_free_mem = NULL;
    plat->get_max_block = NULL;
    plat->send_udp = NULL;

    ctx->magic = PT_CONTEXT_MAGIC;
    ctx->plat = plat;
    pt_memcpy(&ctx->config, &config, sizeof(PeerTalk_Config));

    /* Initialize peer name indices */
    {
        uint16_t i;
        for (i = 0; i < PT_MAX_PEERS; i++) {
            if (ctx->peers) {
                ctx->peers[i].hot.name_idx = (uint8_t)i;
            }
        }
    }

    return ctx;
}

/* Helper to destroy test context */
static void destroy_test_context(struct pt_context *ctx)
{
    if (!ctx) {
        return;
    }

    pt_peer_list_free(ctx);

    if (ctx->plat) {
        pt_free(ctx->plat);
    }

    ctx->magic = 0;
    pt_free(ctx);
}

/* ========================================================================
 * Test Cases
 * ======================================================================== */

/* Test peer list init/free */
static void test_peer_list_init(void)
{
    TEST("test_peer_list_init");

    struct pt_context *ctx = create_test_context();
    int ret;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    ret = pt_peer_list_init(ctx, 8);
    if (ret != 0) {
        FAIL("pt_peer_list_init failed: %d", ret);
        destroy_test_context(ctx);
        return;
    }

    if (ctx->max_peers != 8) {
        FAIL("max_peers should be 8, got %u", ctx->max_peers);
        destroy_test_context(ctx);
        return;
    }

    if (ctx->peer_count != 0) {
        FAIL("peer_count should be 0, got %u", ctx->peer_count);
        destroy_test_context(ctx);
        return;
    }

    pt_peer_list_free(ctx);
    destroy_test_context(ctx);

    PASS();
}

/* Test peer create/destroy */
static void test_peer_create_destroy(void)
{
    TEST("test_peer_create_destroy");

    struct pt_context *ctx = create_test_context();
    struct pt_peer *peer;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_peer_list_init(ctx, 8);

    peer = pt_peer_create(ctx, "TestPeer", 0xC0A80001, 5001);
    if (!peer) {
        FAIL("pt_peer_create failed");
        destroy_test_context(ctx);
        return;
    }

    if (peer->hot.magic != PT_PEER_MAGIC) {
        FAIL("Magic should be PT_PEER_MAGIC");
        destroy_test_context(ctx);
        return;
    }

    if (peer->hot.state != PT_PEER_STATE_DISCOVERED) {
        FAIL("Initial state should be DISCOVERED");
        destroy_test_context(ctx);
        return;
    }

    if (ctx->peer_count != 1) {
        FAIL("peer_count should be 1, got %u", ctx->peer_count);
        destroy_test_context(ctx);
        return;
    }

    pt_peer_destroy(ctx, peer);

    if (peer->hot.state != PT_PEER_STATE_UNUSED) {
        FAIL("State should be UNUSED after destroy");
        destroy_test_context(ctx);
        return;
    }

    if (ctx->peer_count != 0) {
        FAIL("peer_count should be 0 after destroy, got %u", ctx->peer_count);
        destroy_test_context(ctx);
        return;
    }

    destroy_test_context(ctx);
    PASS();
}

/* Test peer find functions */
static void test_peer_find(void)
{
    TEST("test_peer_find");

    struct pt_context *ctx = create_test_context();
    struct pt_peer *peer, *found;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_peer_list_init(ctx, 8);

    peer = pt_peer_create(ctx, "FindMe", 0x0A000001, 5002);
    if (!peer) {
        FAIL("pt_peer_create failed");
        destroy_test_context(ctx);
        return;
    }

    /* Test find by ID */
    found = pt_peer_find_by_id(ctx, peer->hot.id);
    if (found != peer) {
        FAIL("find_by_id failed");
        destroy_test_context(ctx);
        return;
    }

    /* Test find by address */
    found = pt_peer_find_by_addr(ctx, 0x0A000001, 5002);
    if (found != peer) {
        FAIL("find_by_addr failed");
        destroy_test_context(ctx);
        return;
    }

    /* Test find by invalid ID */
    found = pt_peer_find_by_id(ctx, 99);
    if (found != NULL) {
        FAIL("find_by_id should return NULL for invalid ID");
        destroy_test_context(ctx);
        return;
    }

    /* Test find by invalid address */
    found = pt_peer_find_by_addr(ctx, 0x0A000002, 5002);
    if (found != NULL) {
        FAIL("find_by_addr should return NULL for invalid address");
        destroy_test_context(ctx);
        return;
    }

    /* Test find by name */
    found = pt_peer_find_by_name(ctx, "FindMe");
    if (found != peer) {
        FAIL("find_by_name failed");
        destroy_test_context(ctx);
        return;
    }

    /* Test find by non-existent name */
    found = pt_peer_find_by_name(ctx, "DoesNotExist");
    if (found != NULL) {
        FAIL("find_by_name should return NULL for non-existent name");
        destroy_test_context(ctx);
        return;
    }

    /* Test find by NULL name */
    found = pt_peer_find_by_name(ctx, NULL);
    if (found != NULL) {
        FAIL("find_by_name should return NULL for NULL name");
        destroy_test_context(ctx);
        return;
    }

    /* Test find by empty name */
    found = pt_peer_find_by_name(ctx, "");
    if (found != NULL) {
        FAIL("find_by_name should return NULL for empty name");
        destroy_test_context(ctx);
        return;
    }

    destroy_test_context(ctx);
    PASS();
}

/* Test state transitions */
static void test_peer_state_transitions(void)
{
    TEST("test_peer_state_transitions");

    struct pt_context *ctx = create_test_context();
    struct pt_peer *peer;
    int ret;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_peer_list_init(ctx, 8);

    peer = pt_peer_create(ctx, "StateTest", 0xC0A80001, 5003);
    if (!peer) {
        FAIL("pt_peer_create failed");
        destroy_test_context(ctx);
        return;
    }

    /* Test valid transition: DISCOVERED → CONNECTING */
    ret = pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTING);
    if (ret != 0 || peer->hot.state != PT_PEER_STATE_CONNECTING) {
        FAIL("DISCOVERED → CONNECTING failed");
        destroy_test_context(ctx);
        return;
    }

    /* Test valid transition: CONNECTING → CONNECTED */
    ret = pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTED);
    if (ret != 0 || peer->hot.state != PT_PEER_STATE_CONNECTED) {
        FAIL("CONNECTING → CONNECTED failed");
        destroy_test_context(ctx);
        return;
    }

    /* Test valid transition: CONNECTED → DISCONNECTING */
    ret = pt_peer_set_state(ctx, peer, PT_PEER_STATE_DISCONNECTING);
    if (ret != 0 || peer->hot.state != PT_PEER_STATE_DISCONNECTING) {
        FAIL("CONNECTED → DISCONNECTING failed");
        destroy_test_context(ctx);
        return;
    }

    /* Test valid transition: DISCONNECTING → UNUSED */
    ret = pt_peer_set_state(ctx, peer, PT_PEER_STATE_UNUSED);
    if (ret != 0 || peer->hot.state != PT_PEER_STATE_UNUSED) {
        FAIL("DISCONNECTING → UNUSED failed");
        destroy_test_context(ctx);
        return;
    }

    /* Test invalid transition: UNUSED → CONNECTED */
    ret = pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTED);
    if (ret == 0) {
        FAIL("UNUSED → CONNECTED should fail");
        destroy_test_context(ctx);
        return;
    }

    /* Reset to DISCOVERED */
    ret = pt_peer_set_state(ctx, peer, PT_PEER_STATE_DISCOVERED);
    if (ret != 0) {
        FAIL("UNUSED → DISCOVERED failed");
        destroy_test_context(ctx);
        return;
    }

    /* Test refresh: DISCOVERED → DISCOVERED */
    ret = pt_peer_set_state(ctx, peer, PT_PEER_STATE_DISCOVERED);
    if (ret != 0) {
        FAIL("DISCOVERED → DISCOVERED (refresh) failed");
        destroy_test_context(ctx);
        return;
    }

    /* Test recovery: DISCOVERED → CONNECTING → FAILED → DISCOVERED */
    pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTING);
    pt_peer_set_state(ctx, peer, PT_PEER_STATE_FAILED);
    ret = pt_peer_set_state(ctx, peer, PT_PEER_STATE_DISCOVERED);
    if (ret != 0 || peer->hot.state != PT_PEER_STATE_DISCOVERED) {
        FAIL("FAILED → DISCOVERED (recovery) failed");
        destroy_test_context(ctx);
        return;
    }

    destroy_test_context(ctx);
    PASS();
}

/* Test timeout detection */
static void test_peer_timeout(void)
{
    TEST("test_peer_timeout");

    struct pt_context *ctx = create_test_context();
    struct pt_peer *peer;
    int timed_out;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_peer_list_init(ctx, 8);

    peer = pt_peer_create(ctx, "TimeoutTest", 0xC0A80001, 5004);
    if (!peer) {
        FAIL("pt_peer_create failed");
        destroy_test_context(ctx);
        return;
    }

    /* Set last_seen to 100 */
    peer->hot.last_seen = 100;

    /* Check not timed out at 200 (elapsed=100, timeout=200) */
    timed_out = pt_peer_is_timed_out(peer, 200, 200);
    if (timed_out != 0) {
        FAIL("Should not be timed out (elapsed=100, timeout=200)");
        destroy_test_context(ctx);
        return;
    }

    /* Check timed out at 301 (elapsed=201, timeout=200) */
    timed_out = pt_peer_is_timed_out(peer, 301, 200);
    if (timed_out != 1) {
        FAIL("Should be timed out (elapsed=201, timeout=200)");
        destroy_test_context(ctx);
        return;
    }

    destroy_test_context(ctx);
    PASS();
}

/* Test canary checks */
static void test_peer_canaries(void)
{
    TEST("test_peer_canaries");

    struct pt_context *ctx = create_test_context();
    struct pt_peer *peer;
    int ret;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_peer_list_init(ctx, 8);

    peer = pt_peer_create(ctx, "CanaryTest", 0xC0A80001, 5005);
    if (!peer) {
        FAIL("pt_peer_create failed");
        destroy_test_context(ctx);
        return;
    }

#ifdef PT_DEBUG
    /* Verify canaries initialized correctly */
    if (peer->cold.obuf_canary != PT_CANARY_OBUF) {
        FAIL("obuf_canary not initialized");
        destroy_test_context(ctx);
        return;
    }

    if (peer->cold.ibuf_canary != PT_CANARY_IBUF) {
        FAIL("ibuf_canary not initialized");
        destroy_test_context(ctx);
        return;
    }

    /* Check canaries are valid */
    ret = pt_peer_check_canaries(ctx, peer);
    if (ret != 0) {
        FAIL("Canaries should be valid");
        destroy_test_context(ctx);
        return;
    }

    /* Corrupt output buffer canary */
    peer->cold.obuf_canary = 0xDEADDEAD;

    /* Check canaries detect corruption */
    ret = pt_peer_check_canaries(ctx, peer);
    if (ret == 0) {
        FAIL("Should detect corruption");
        destroy_test_context(ctx);
        return;
    }

    /* Restore canary */
    peer->cold.obuf_canary = PT_CANARY_OBUF;

    /* Verify canaries are valid again */
    ret = pt_peer_check_canaries(ctx, peer);
    if (ret != 0) {
        FAIL("Canaries should be valid after restore");
        destroy_test_context(ctx);
        return;
    }
#else
    /* In release builds, canaries are not present */
    ret = pt_peer_check_canaries(ctx, peer);
    if (ret != 0) {
        FAIL("Canary check should succeed in release build");
        destroy_test_context(ctx);
        return;
    }
#endif

    destroy_test_context(ctx);
    PASS();
}

/* Test get_info */
static void test_peer_get_info(void)
{
    TEST("test_peer_get_info");

    struct pt_context *ctx = create_test_context();
    struct pt_peer *peer;
    PeerTalk_PeerInfo info;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_peer_list_init(ctx, 8);

    peer = pt_peer_create(ctx, "InfoTest", 0xC0A80102, 7354);
    if (!peer) {
        FAIL("pt_peer_create failed");
        destroy_test_context(ctx);
        return;
    }

    /* Set state to CONNECTED */
    pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTING);
    pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTED);

    /* Get peer info */
    pt_peer_get_info(peer, &info);

    /* Verify info */
    if (info.id != peer->hot.id) {
        FAIL("ID mismatch");
        destroy_test_context(ctx);
        return;
    }

    if (info.address != 0xC0A80102) {
        FAIL("Address mismatch");
        destroy_test_context(ctx);
        return;
    }

    if (info.port != 7354) {
        FAIL("Port mismatch");
        destroy_test_context(ctx);
        return;
    }

    if (info.connected != 1) {
        FAIL("Connected should be 1");
        destroy_test_context(ctx);
        return;
    }

    destroy_test_context(ctx);
    PASS();
}

/* Test peer state machine edge cases (HIGH PRIORITY) */
static void test_peer_state_edge_cases(void)
{
    TEST("test_peer_state_edge_cases");

    struct pt_context *ctx = create_test_context();
    struct pt_peer *peer;
    int ret;

    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_peer_list_init(ctx, 8);

    /* Edge Case 1: Invalid transition to FAILED from DISCOVERED (without CONNECTING) */
    peer = pt_peer_create(ctx, "EdgeTest1", 0xC0A80001, 5004);
    if (!peer) {
        FAIL("pt_peer_create failed");
        destroy_test_context(ctx);
        return;
    }

    /* Try DISCOVERED → FAILED (should fail - FAILED only allowed from CONNECTING/CONNECTED) */
    ret = pt_peer_set_state(ctx, peer, PT_PEER_STATE_FAILED);
    if (ret == 0) {
        FAIL("DISCOVERED → FAILED should not be allowed (must go through CONNECTING first)");
        destroy_test_context(ctx);
        return;
    }

    /* Verify state didn't change */
    if (peer->hot.state != PT_PEER_STATE_DISCOVERED) {
        FAIL("State should remain DISCOVERED after failed transition");
        destroy_test_context(ctx);
        return;
    }

    /* Edge Case 2: Destroy peer and verify magic is cleared */
    uint32_t *magic_ptr = (uint32_t *)peer;  /* First field should be magic */

    pt_peer_destroy(ctx, peer);

    /* After destroy, magic should be cleared (0) */
    if (*magic_ptr == PT_PEER_MAGIC) {
        FAIL("Magic should be cleared after destroy");
        destroy_test_context(ctx);
        return;
    }

    /* Edge Case 3: Try invalid transition FAILED → CONNECTED */
    peer = pt_peer_create(ctx, "EdgeTest2", 0xC0A80002, 5005);
    if (!peer) {
        FAIL("pt_peer_create failed");
        destroy_test_context(ctx);
        return;
    }

    pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTING);
    pt_peer_set_state(ctx, peer, PT_PEER_STATE_FAILED);

    /* Try FAILED → CONNECTED (should fail) */
    ret = pt_peer_set_state(ctx, peer, PT_PEER_STATE_CONNECTED);
    if (ret == 0) {
        FAIL("FAILED → CONNECTED should not be allowed");
        destroy_test_context(ctx);
        return;
    }

    /* Edge Case 4: Multiple consecutive FAILED states (should be idempotent) */
    pt_peer_set_state(ctx, peer, PT_PEER_STATE_FAILED);
    pt_peer_set_state(ctx, peer, PT_PEER_STATE_FAILED);
    pt_peer_set_state(ctx, peer, PT_PEER_STATE_FAILED);

    if (peer->hot.state != PT_PEER_STATE_FAILED) {
        FAIL("Multiple FAILED transitions should be idempotent");
        destroy_test_context(ctx);
        return;
    }

    destroy_test_context(ctx);
    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== Peer Management Tests ===\n\n");

    /* Run all tests */
    test_peer_list_init();
    test_peer_create_destroy();
    test_peer_find();
    test_peer_state_transitions();
    test_peer_state_edge_cases();
    test_peer_timeout();
    test_peer_canaries();
    test_peer_get_info();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
