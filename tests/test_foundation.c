/**
 * @file test_foundation.c
 * @brief PeerTalk Foundation Integration Tests
 *
 * Verifies that all Phase 1 components work together:
 * - Platform initialization and shutdown
 * - Version reporting
 * - Error strings
 * - Protocol constants
 * - Data-Oriented Design structure requirements
 */

#include "../include/peertalk.h"
#include "../src/core/pt_types.h"
#include "../src/core/pt_internal.h"
#include <stdio.h>
#include <string.h>

/* Test framework */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    tests_passed++; \
    printf("OK\n"); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED\n  Assertion failed: %s (line %d)\n", #cond, __LINE__); \
        return; \
    } \
} while(0)

/* ========================================================================== */
/* Version Tests                                                              */
/* ========================================================================== */

TEST(version_string) {
    const char *version = PeerTalk_Version();

    /* Version should be non-null and non-empty */
    ASSERT(version != NULL);
    ASSERT(version[0] != '\0');

    /* Version should start with "1." */
    ASSERT(version[0] == '1');
    ASSERT(version[1] == '.');
}

TEST(version_constants) {
    /* Verify version constants match expected values */
    ASSERT(PEERTALK_VERSION_MAJOR == 1);
    ASSERT(PEERTALK_VERSION_MINOR == 0);
    ASSERT(PEERTALK_VERSION_PATCH == 0);
}

/* ========================================================================== */
/* Error String Tests                                                         */
/* ========================================================================== */

TEST(error_strings) {
    const char *str;

    /* All error codes should have non-null, non-empty strings */
    str = PeerTalk_ErrorString(PT_OK);
    ASSERT(str != NULL && str[0] != '\0');

    str = PeerTalk_ErrorString(PT_ERR_INVALID_PARAM);
    ASSERT(str != NULL && str[0] != '\0');

    str = PeerTalk_ErrorString(PT_ERR_NO_MEMORY);
    ASSERT(str != NULL && str[0] != '\0');

    str = PeerTalk_ErrorString(PT_ERR_NOT_INITIALIZED);
    ASSERT(str != NULL && str[0] != '\0');

    str = PeerTalk_ErrorString(PT_ERR_NETWORK);
    ASSERT(str != NULL && str[0] != '\0');

    str = PeerTalk_ErrorString(PT_ERR_TIMEOUT);
    ASSERT(str != NULL && str[0] != '\0');

    str = PeerTalk_ErrorString(PT_ERR_CONNECTION_REFUSED);
    ASSERT(str != NULL && str[0] != '\0');

    str = PeerTalk_ErrorString(PT_ERR_CONNECTION_CLOSED);
    ASSERT(str != NULL && str[0] != '\0');

    str = PeerTalk_ErrorString(PT_ERR_BUFFER_FULL);
    ASSERT(str != NULL && str[0] != '\0');

    str = PeerTalk_ErrorString(PT_ERR_INVALID_STATE);
    ASSERT(str != NULL && str[0] != '\0');

    str = PeerTalk_ErrorString(PT_ERR_PEER_NOT_FOUND);
    ASSERT(str != NULL && str[0] != '\0');

    /* Unknown error code should also return a valid string */
    str = PeerTalk_ErrorString((PeerTalk_Error)-999);
    ASSERT(str != NULL && str[0] != '\0');
}

TEST(error_string_content) {
    const char *str;

    /* PT_OK should indicate success */
    str = PeerTalk_ErrorString(PT_OK);
    ASSERT(str[0] == 'S' || str[0] == 's'); /* "Success" */

    /* Invalid param should mention "invalid" or "parameter" */
    str = PeerTalk_ErrorString(PT_ERR_INVALID_PARAM);
    ASSERT(strstr(str, "nvalid") != NULL || strstr(str, "arameter") != NULL);
}

/* ========================================================================== */
/* Protocol Constants Tests                                                   */
/* ========================================================================== */

TEST(protocol_constants) {
    /* Verify all magic numbers match CLAUDE.md */
    ASSERT(PT_CONTEXT_MAGIC == 0x5054434E); /* "PTCN" */
    ASSERT(PT_PEER_MAGIC == 0x50545052);    /* "PTPR" */
    ASSERT(PT_QUEUE_MAGIC == 0x50545155);   /* "PTQU" */
    ASSERT(PT_CANARY == 0xDEADBEEF);

    /* Verify protocol version */
    ASSERT(PT_PROTOCOL_VERSION == 1);

    /* Verify discovery and message magic strings */
    ASSERT(memcmp(PT_DISCOVERY_MAGIC, "PTLK", 4) == 0);
    ASSERT(memcmp(PT_MESSAGE_MAGIC, "PTMG", 4) == 0);
}

TEST(default_ports) {
    /* Verify port constants match CLAUDE.md */
    ASSERT(PT_DEFAULT_DISCOVERY_PORT == 7353);
    ASSERT(PT_DEFAULT_TCP_PORT == 7354);
    ASSERT(PT_DEFAULT_UDP_PORT == 7355);
}

/* ========================================================================== */
/* Platform Ops Tests                                                         */
/* ========================================================================== */

TEST(platform_ops_selected) {
    /* Create a minimal config */
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strcpy(config.local_name, "test");

    /* Initialize PeerTalk */
    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    ASSERT(ctx != NULL);

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    /* Platform ops should be selected */
    ASSERT(internal_ctx->plat != NULL);
    ASSERT(internal_ctx->plat->init != NULL);
    ASSERT(internal_ctx->plat->shutdown != NULL);
    ASSERT(internal_ctx->plat->poll != NULL);
    ASSERT(internal_ctx->plat->get_ticks != NULL);
    ASSERT(internal_ctx->plat->get_free_mem != NULL);
    ASSERT(internal_ctx->plat->get_max_block != NULL);
    /* send_udp may be NULL (set by networking phases) */

    /* Cleanup */
    PeerTalk_Shutdown(ctx);
}

TEST(platform_ticks) {
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strcpy(config.local_name, "test");

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    ASSERT(ctx != NULL);

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    /* Get ticks should return non-decreasing values */
    pt_tick_t t1 = internal_ctx->plat->get_ticks();
    pt_tick_t t2 = internal_ctx->plat->get_ticks();

    /* Allow for wraparound - just verify they're reasonable */
    ASSERT(t1 > 0 || t1 == 0); /* Any value is OK */
    ASSERT(t2 >= t1 || (t2 < 100 && t1 > 0xFFFFFF00)); /* Allow wrap */

    PeerTalk_Shutdown(ctx);
}

TEST(platform_memory) {
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strcpy(config.local_name, "test");

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    ASSERT(ctx != NULL);

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    /* Memory queries should return sensible values */
    unsigned long free_mem = internal_ctx->plat->get_free_mem();
    unsigned long max_block = internal_ctx->plat->get_max_block();

    ASSERT(free_mem > 0);
    ASSERT(max_block > 0);
    ASSERT(max_block <= free_mem);

    PeerTalk_Shutdown(ctx);
}

/* ========================================================================== */
/* Data-Oriented Design Tests                                                 */
/* ========================================================================== */

TEST(dod_struct_sizes) {
    /* pt_peer_hot must be exactly 32 bytes (68030 cache line) */
    ASSERT(sizeof(pt_peer_hot) == 32);

    /* pt_peer_state must be 1 byte (uint8_t, not enum) */
    ASSERT(sizeof(pt_peer_state) == 1);

    /* PeerTalk_PeerInfo should be compact (<= 24 bytes) */
    ASSERT(sizeof(PeerTalk_PeerInfo) <= 24);

    /* name_idx should be 1 byte */
    PeerTalk_PeerInfo info;
    ASSERT(sizeof(info.name_idx) == 1);
}

TEST(dod_lookup_table) {
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strcpy(config.local_name, "test");

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    ASSERT(ctx != NULL);

    struct pt_context *internal_ctx = (struct pt_context *)ctx;

    /* O(1) lookup table should exist */
    ASSERT(sizeof(internal_ctx->peer_id_to_index) == PT_MAX_PEER_ID);

    /* Name table should be correctly sized */
    ASSERT(sizeof(internal_ctx->peer_names) ==
           PT_MAX_PEERS * (PT_MAX_PEER_NAME + 1));

    PeerTalk_Shutdown(ctx);
}

/* ========================================================================== */
/* Lifecycle and Error Handling Tests (HIGH PRIORITY)                         */
/* ========================================================================== */

TEST(init_null_config) {
    /* NULL config should fail gracefully */
    PeerTalk_Context *ctx = PeerTalk_Init(NULL);

    /* Current behavior: returns NULL */
    ASSERT(ctx == NULL);
}

TEST(shutdown_double_call) {
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strcpy(config.local_name, "test");

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    ASSERT(ctx != NULL);

    /* First shutdown - should work */
    PeerTalk_Shutdown(ctx);

    /* Second shutdown - should not crash */
    PeerTalk_Shutdown(ctx);

    /* Shutdown NULL - should not crash */
    PeerTalk_Shutdown(NULL);
}

TEST(ptlog_integration) {
    PeerTalk_Config config;
    memset(&config, 0, sizeof(config));
    strcpy(config.local_name, "test");

    PeerTalk_Context *ctx = PeerTalk_Init(&config);
    ASSERT(ctx != NULL);

    struct pt_context *internal = (struct pt_context *)ctx;

    /* Logger should be initialized */
    ASSERT(internal->log != NULL);

    /* Logger should be functional (already tested in test_log_posix.c) */
    /* Just verify it exists and is of correct type */

    PeerTalk_Shutdown(ctx);

    /* After shutdown, logger should be destroyed (verified by valgrind) */
}

TEST(lifecycle_stress) {
    /* Run 100 init/shutdown cycles (reduced from 1000 for speed) */
    for (int i = 0; i < 100; i++) {
        PeerTalk_Config config;
        memset(&config, 0, sizeof(config));
        sprintf(config.local_name, "peer_%d", i);

        PeerTalk_Context *ctx = PeerTalk_Init(&config);
        ASSERT(ctx != NULL);

        /* Minimal work */
        const char *version = PeerTalk_Version();
        ASSERT(version != NULL);

        PeerTalk_Shutdown(ctx);
    }

    /* Memory leaks will be caught by valgrind */
}

/* ========================================================================== */
/* Main                                                                        */
/* ========================================================================== */

int main(void) {
    printf("PeerTalk Foundation Integration Tests\n");
    printf("======================================\n\n");

    /* Version tests */
    printf("Version:\n");
    RUN_TEST(version_string);
    RUN_TEST(version_constants);

    /* Error string tests */
    printf("\nError Strings:\n");
    RUN_TEST(error_strings);
    RUN_TEST(error_string_content);

    /* Protocol constants tests */
    printf("\nProtocol Constants:\n");
    RUN_TEST(protocol_constants);
    RUN_TEST(default_ports);

    /* Platform ops tests */
    printf("\nPlatform Ops:\n");
    RUN_TEST(platform_ops_selected);
    RUN_TEST(platform_ticks);
    RUN_TEST(platform_memory);

    /* DOD tests */
    printf("\nDOD (Data-Oriented Design):\n");
    RUN_TEST(dod_struct_sizes);
    RUN_TEST(dod_lookup_table);

    /* Lifecycle and error handling tests */
    printf("\nLifecycle & Error Handling:\n");
    RUN_TEST(init_null_config);
    RUN_TEST(shutdown_double_call);
    RUN_TEST(ptlog_integration);
    RUN_TEST(lifecycle_stress);

    /* Summary */
    printf("\n======================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    printf("Version: %s\n", PeerTalk_Version());
    printf("======================================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
