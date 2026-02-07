/* test_api_errors.c - Tests for public API error handling
 *
 * Tests error paths and edge cases for public API functions.
 * Note: Links against libpeertalk to ensure coverage is tracked properly.
 */

#include "../src/core/pt_internal.h"
#include "../src/core/pt_compat.h"
#include "../src/core/peer.h"
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

/* ========================================================================
 * Test Cases
 * ======================================================================== */

/* Test PeerTalk_Init with NULL config */
static void test_init_null_config(void)
{
    TEST("test_init_null_config");

    PeerTalk_Context *ctx = PeerTalk_Init(NULL);
    if (ctx != NULL) {
        FAIL("PeerTalk_Init(NULL) should return NULL");
        PeerTalk_Shutdown(ctx);
        return;
    }

    PASS();
}

/* Test PeerTalk_Init with empty local_name */
static void test_init_empty_name(void)
{
    TEST("test_init_empty_name");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    /* local_name is all zeros (empty) */

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (ctx != NULL) {
        FAIL("PeerTalk_Init with empty name should return NULL");
        PeerTalk_Shutdown(ctx);
        return;
    }

    PASS();
}

/* Test PeerTalk_SetCallbacks with NULL context */
static void test_setcallbacks_null_ctx(void)
{
    TEST("test_setcallbacks_null_ctx");

    PeerTalk_Callbacks callbacks = {0};
    PeerTalk_Error err = PeerTalk_SetCallbacks(NULL, &callbacks);

    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_SetCallbacks(NULL, ...) should return PT_ERR_INVALID_PARAM, got %d", err);
        return;
    }

    PASS();
}

/* Test PeerTalk_SetCallbacks with NULL callbacks */
static void test_setcallbacks_null_callbacks(void)
{
    TEST("test_setcallbacks_null_callbacks");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_Error err = PeerTalk_SetCallbacks(ctx, NULL);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_SetCallbacks(ctx, NULL) should return PT_ERR_INVALID_PARAM, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_Poll with NULL context */
static void test_poll_null_ctx(void)
{
    TEST("test_poll_null_ctx");

    PeerTalk_Error err = PeerTalk_Poll(NULL);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_Poll(NULL) should return PT_ERR_INVALID_PARAM, got %d", err);
        return;
    }

    PASS();
}

/* Test PeerTalk_Shutdown with NULL context */
static void test_shutdown_null_ctx(void)
{
    TEST("test_shutdown_null_ctx");

    /* Should not crash */
    PeerTalk_Shutdown(NULL);

    PASS();
}

/* Test PeerTalk_GetPeers with NULL context */
static void test_getpeers_null_ctx(void)
{
    TEST("test_getpeers_null_ctx");

    PeerTalk_PeerInfo peers[4];
    uint16_t count;

    PeerTalk_Error err = PeerTalk_GetPeers(NULL, peers, 4, &count);
    if (err != PT_ERR_INVALID_STATE) {
        FAIL("PeerTalk_GetPeers(NULL, ...) should return PT_ERR_INVALID_STATE, got %d", err);
        return;
    }

    PASS();
}

/* Test PeerTalk_GetPeers with NULL peers buffer */
static void test_getpeers_null_peers(void)
{
    TEST("test_getpeers_null_peers");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    uint16_t count;
    PeerTalk_Error err = PeerTalk_GetPeers(ctx, NULL, 4, &count);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_GetPeers(ctx, NULL, ...) should return PT_ERR_INVALID_PARAM, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_GetPeers with NULL out_count */
static void test_getpeers_null_count(void)
{
    TEST("test_getpeers_null_count");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_PeerInfo peers[4];
    PeerTalk_Error err = PeerTalk_GetPeers(ctx, peers, 4, NULL);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_GetPeers(..., NULL) should return PT_ERR_INVALID_PARAM, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_GetPeersVersion with NULL context */
static void test_getpeersversion_null_ctx(void)
{
    TEST("test_getpeersversion_null_ctx");

    uint32_t version = PeerTalk_GetPeersVersion(NULL);
    if (version != 0) {
        FAIL("PeerTalk_GetPeersVersion(NULL) should return 0, got %u", version);
        return;
    }

    PASS();
}

/* Test PeerTalk_GetPeerByID with NULL context */
static void test_getpeerbyid_null_ctx(void)
{
    TEST("test_getpeerbyid_null_ctx");

    const PeerTalk_PeerInfo *info = PeerTalk_GetPeerByID(NULL, 1);
    if (info != NULL) {
        FAIL("PeerTalk_GetPeerByID(NULL, ...) should return NULL");
        return;
    }

    PASS();
}

/* Test PeerTalk_GetPeerByID with invalid peer ID */
static void test_getpeerbyid_invalid_id(void)
{
    TEST("test_getpeerbyid_invalid_id");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    const PeerTalk_PeerInfo *info = PeerTalk_GetPeerByID(ctx, 999);
    if (info != NULL) {
        FAIL("PeerTalk_GetPeerByID(ctx, 999) should return NULL for non-existent peer");
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_GetPeer with NULL context */
static void test_getpeer_null_ctx(void)
{
    TEST("test_getpeer_null_ctx");

    PeerTalk_PeerInfo info;
    PeerTalk_Error err = PeerTalk_GetPeer(NULL, 1, &info);
    if (err != PT_ERR_INVALID_STATE) {
        FAIL("PeerTalk_GetPeer(NULL, ...) should return PT_ERR_INVALID_STATE, got %d", err);
        return;
    }

    PASS();
}

/* Test PeerTalk_GetPeer with NULL info buffer */
static void test_getpeer_null_info(void)
{
    TEST("test_getpeer_null_info");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_Error err = PeerTalk_GetPeer(ctx, 1, NULL);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_GetPeer(ctx, ..., NULL) should return PT_ERR_INVALID_PARAM, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_GetPeer with invalid peer ID */
static void test_getpeer_invalid_id(void)
{
    TEST("test_getpeer_invalid_id");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_PeerInfo info;
    PeerTalk_Error err = PeerTalk_GetPeer(ctx, 999, &info);
    if (err != PT_ERR_PEER_NOT_FOUND) {
        FAIL("PeerTalk_GetPeer(ctx, 999, ...) should return PT_ERR_PEER_NOT_FOUND, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_FindPeerByName with NULL context */
static void test_findpeerbyname_null_ctx(void)
{
    TEST("test_findpeerbyname_null_ctx");

    PeerTalk_PeerID id = PeerTalk_FindPeerByName(NULL, "Test", NULL);
    if (id != 0) {
        FAIL("PeerTalk_FindPeerByName(NULL, ...) should return 0, got %u", id);
        return;
    }

    PASS();
}

/* Test PeerTalk_FindPeerByName with NULL name */
static void test_findpeerbyname_null_name(void)
{
    TEST("test_findpeerbyname_null_name");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_PeerID id = PeerTalk_FindPeerByName(ctx, NULL, NULL);
    if (id != 0) {
        FAIL("PeerTalk_FindPeerByName(ctx, NULL, ...) should return 0, got %u", id);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_FindPeerByAddress with NULL context */
static void test_findpeerbyaddress_null_ctx(void)
{
    TEST("test_findpeerbyaddress_null_ctx");

    PeerTalk_PeerID id = PeerTalk_FindPeerByAddress(NULL, 0x7F000001, 1234, NULL);
    if (id != 0) {
        FAIL("PeerTalk_FindPeerByAddress(NULL, ...) should return 0, got %u", id);
        return;
    }

    PASS();
}

/* Test PeerTalk_GetQueueStatus with NULL context */
static void test_getqueuestatus_null_ctx(void)
{
    TEST("test_getqueuestatus_null_ctx");

    uint16_t pending, available;
    PeerTalk_Error err = PeerTalk_GetQueueStatus(NULL, 1, &pending, &available);
    if (err != PT_ERR_INVALID_STATE) {
        FAIL("PeerTalk_GetQueueStatus(NULL, ...) should return PT_ERR_INVALID_STATE, got %d", err);
        return;
    }

    PASS();
}

/* Test PeerTalk_GetQueueStatus with invalid peer ID */
static void test_getqueuestatus_invalid_peer(void)
{
    TEST("test_getqueuestatus_invalid_peer");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    uint16_t pending, available;
    PeerTalk_Error err = PeerTalk_GetQueueStatus(ctx, 999, &pending, &available);
    if (err != PT_ERR_PEER_NOT_FOUND) {
        FAIL("PeerTalk_GetQueueStatus(ctx, 999, ...) should return PT_ERR_PEER_NOT_FOUND, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_Connect with NULL context */
static void test_connect_null_ctx(void)
{
    TEST("test_connect_null_ctx");

    PeerTalk_Error err = PeerTalk_Connect(NULL, 1);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_Connect(NULL, ...) should return PT_ERR_INVALID_PARAM, got %d", err);
        return;
    }

    PASS();
}

/* Test PeerTalk_Connect with invalid peer ID */
static void test_connect_invalid_peer(void)
{
    TEST("test_connect_invalid_peer");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_Error err = PeerTalk_Connect(ctx, 999);
    if (err != PT_ERR_PEER_NOT_FOUND) {
        FAIL("PeerTalk_Connect(ctx, 999) should return PT_ERR_PEER_NOT_FOUND, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_Disconnect with NULL context */
static void test_disconnect_null_ctx(void)
{
    TEST("test_disconnect_null_ctx");

    PeerTalk_Error err = PeerTalk_Disconnect(NULL, 1);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_Disconnect(NULL, ...) should return PT_ERR_INVALID_PARAM, got %d", err);
        return;
    }

    PASS();
}

/* Test PeerTalk_Disconnect with invalid peer ID */
static void test_disconnect_invalid_peer(void)
{
    TEST("test_disconnect_invalid_peer");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_Error err = PeerTalk_Disconnect(ctx, 999);
    if (err != PT_ERR_PEER_NOT_FOUND) {
        FAIL("PeerTalk_Disconnect(ctx, 999) should return PT_ERR_PEER_NOT_FOUND, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_Broadcast with NULL context */
static void test_broadcast_null_ctx(void)
{
    TEST("test_broadcast_null_ctx");

    uint8_t data[] = "Hello";
    PeerTalk_Error err = PeerTalk_Broadcast(NULL, data, sizeof(data));
    if (err != PT_ERR_INVALID_STATE) {
        FAIL("PeerTalk_Broadcast(NULL, ...) should return PT_ERR_INVALID_STATE, got %d", err);
        return;
    }

    PASS();
}

/* Test PeerTalk_Broadcast with NULL data */
static void test_broadcast_null_data(void)
{
    TEST("test_broadcast_null_data");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_Error err = PeerTalk_Broadcast(ctx, NULL, 10);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_Broadcast(ctx, NULL, ...) should return PT_ERR_INVALID_PARAM, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_Broadcast with zero length */
static void test_broadcast_zero_length(void)
{
    TEST("test_broadcast_zero_length");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    uint8_t data[] = "Hello";
    PeerTalk_Error err = PeerTalk_Broadcast(ctx, data, 0);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_Broadcast(ctx, data, 0) should return PT_ERR_INVALID_PARAM, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_Broadcast with no connected peers */
static void test_broadcast_no_peers(void)
{
    TEST("test_broadcast_no_peers");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    uint8_t data[] = "Hello";
    PeerTalk_Error err = PeerTalk_Broadcast(ctx, data, sizeof(data));
    if (err != PT_ERR_PEER_NOT_FOUND) {
        FAIL("PeerTalk_Broadcast with no peers should return PT_ERR_PEER_NOT_FOUND, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_Send with invalid parameters */
static void test_send_null_ctx(void)
{
    TEST("test_send_null_ctx");

    uint8_t data[] = "Hello";
    PeerTalk_Error err = PeerTalk_Send(NULL, 1, data, sizeof(data));
    if (err != PT_ERR_INVALID_STATE) {
        FAIL("PeerTalk_Send(NULL, ...) should return PT_ERR_INVALID_STATE, got %d", err);
        return;
    }

    PASS();
}

/* Test PeerTalk_Send with NULL data */
static void test_send_null_data(void)
{
    TEST("test_send_null_data");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    PeerTalk_Error err = PeerTalk_Send(ctx, 1, NULL, 10);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_Send(ctx, ..., NULL, ...) should return PT_ERR_INVALID_PARAM, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_StartDiscovery/StopDiscovery with NULL context */
static void test_discovery_null_ctx(void)
{
    TEST("test_discovery_null_ctx");

    PeerTalk_Error err = PeerTalk_StartDiscovery(NULL);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_StartDiscovery(NULL) should return PT_ERR_INVALID_PARAM, got %d", err);
        return;
    }

    err = PeerTalk_StopDiscovery(NULL);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_StopDiscovery(NULL) should return PT_ERR_INVALID_PARAM, got %d", err);
        return;
    }

    PASS();
}

/* Test PeerTalk_StartListening/StopListening with NULL context */
static void test_listening_null_ctx(void)
{
    TEST("test_listening_null_ctx");

    PeerTalk_Error err = PeerTalk_StartListening(NULL);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_StartListening(NULL) should return PT_ERR_INVALID_PARAM, got %d", err);
        return;
    }

    err = PeerTalk_StopListening(NULL);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_StopListening(NULL) should return PT_ERR_INVALID_PARAM, got %d", err);
        return;
    }

    PASS();
}

/* Test PeerTalk_StopListening when not listening */
static void test_stoplistening_not_listening(void)
{
    TEST("test_stoplistening_not_listening");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    /* Should not crash when not listening */
    PeerTalk_Error err = PeerTalk_StopListening(ctx);
    if (err != PT_OK) {
        FAIL("PeerTalk_StopListening when not listening should return PT_OK, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_GetPeerName with various inputs */
static void test_getpeername(void)
{
    TEST("test_getpeername");

    /* NULL context */
    const char *name = PeerTalk_GetPeerName(NULL, 0);
    if (name != NULL && name[0] != '\0') {
        FAIL("PeerTalk_GetPeerName(NULL, ...) should return empty string");
        return;
    }

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    /* Invalid index (out of range) */
    name = PeerTalk_GetPeerName(ctx, 255);
    if (name == NULL || name[0] != '\0') {
        /* Should return empty string for out of range */
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test pt_get_peer_name with invalid context */
static void test_pt_get_peer_name(void)
{
    TEST("test_pt_get_peer_name");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    /* Test with valid context but out-of-range index */
    const char *name = pt_get_peer_name(internal_ctx, 255);
    if (name == NULL) {
        FAIL("pt_get_peer_name should return empty string for invalid index");
        PeerTalk_Shutdown(ctx);
        return;
    }

    /* Should be empty string */
    if (name[0] != '\0') {
        FAIL("pt_get_peer_name should return empty string for invalid index");
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_SendEx with invalid priority */
static void test_sendex_invalid_priority(void)
{
    TEST("test_sendex_invalid_priority");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    uint8_t data[] = "Hello";
    PeerTalk_Error err = PeerTalk_SendEx(ctx, 1, data, sizeof(data),
                                          255, /* Invalid priority */
                                          PT_SEND_DEFAULT, 0);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_SendEx with invalid priority should return PT_ERR_INVALID_PARAM, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* Test PeerTalk_SendEx with oversized message */
static void test_sendex_oversized(void)
{
    TEST("test_sendex_oversized");

    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strncpy(config.local_name, "Test", PT_MAX_PEER_NAME);

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    uint8_t data[PT_MAX_MESSAGE_SIZE + 100];
    PeerTalk_Error err = PeerTalk_SendEx(ctx, 1, data, PT_MAX_MESSAGE_SIZE + 1,
                                          PT_PRIORITY_NORMAL,
                                          PT_SEND_DEFAULT, 0);
    if (err != PT_ERR_INVALID_PARAM) {
        FAIL("PeerTalk_SendEx with oversized message should return PT_ERR_INVALID_PARAM, got %d", err);
        PeerTalk_Shutdown(ctx);
        return;
    }

    PeerTalk_Shutdown(ctx);
    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== PeerTalk API Error Tests ===\n\n");

    /* Run all tests */
    test_init_null_config();
    test_init_empty_name();
    test_setcallbacks_null_ctx();
    test_setcallbacks_null_callbacks();
    test_poll_null_ctx();
    test_shutdown_null_ctx();
    test_getpeers_null_ctx();
    test_getpeers_null_peers();
    test_getpeers_null_count();
    test_getpeersversion_null_ctx();
    test_getpeerbyid_null_ctx();
    test_getpeerbyid_invalid_id();
    test_getpeer_null_ctx();
    test_getpeer_null_info();
    test_getpeer_invalid_id();
    test_findpeerbyname_null_ctx();
    test_findpeerbyname_null_name();
    test_findpeerbyaddress_null_ctx();
    test_getqueuestatus_null_ctx();
    test_getqueuestatus_invalid_peer();
    test_connect_null_ctx();
    test_connect_invalid_peer();
    test_disconnect_null_ctx();
    test_disconnect_invalid_peer();
    test_broadcast_null_ctx();
    test_broadcast_null_data();
    test_broadcast_zero_length();
    test_broadcast_no_peers();
    test_send_null_ctx();
    test_send_null_data();
    test_discovery_null_ctx();
    test_listening_null_ctx();
    test_stoplistening_not_listening();
    test_getpeername();
    test_pt_get_peer_name();
    test_sendex_invalid_priority();
    test_sendex_oversized();

    /* Summary */
    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
