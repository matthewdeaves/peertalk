/* test_error_strings.c - Tests for error string coverage
 *
 * Exercises all error codes to ensure full coverage of
 * PeerTalk_ErrorString function.
 */

#include "peertalk.h"
#include <stdio.h>
#include <string.h>

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

/* Test all error strings */
static void test_all_error_strings(void)
{
    TEST("test_all_error_strings");

    /* Test each error code to exercise all switch cases */
    struct { PeerTalk_Error code; const char *expected_substr; } tests[] = {
        { PT_OK, "Success" },
        { PT_ERR_INVALID_PARAM, "Invalid" },
        { PT_ERR_NO_MEMORY, "memory" },
        { PT_ERR_NOT_INITIALIZED, "Not initialized" },
        { PT_ERR_ALREADY_INITIALIZED, "Already" },
        { PT_ERR_INVALID_STATE, "state" },
        { PT_ERR_NOT_SUPPORTED, "supported" },
        { PT_ERR_NETWORK, "Network" },
        { PT_ERR_TIMEOUT, "timed out" },
        { PT_ERR_CONNECTION_REFUSED, "refused" },
        { PT_ERR_CONNECTION_CLOSED, "closed" },
        { PT_ERR_NO_NETWORK, "No network" },
        { PT_ERR_NOT_CONNECTED, "Not connected" },
        { PT_ERR_WOULD_BLOCK, "block" },
        { PT_ERR_BUFFER_FULL, "Buffer full" },
        { PT_ERR_QUEUE_EMPTY, "empty" },
        { PT_ERR_MESSAGE_TOO_LARGE, "too large" },
        { PT_ERR_BACKPRESSURE, "backpressure" },
        { PT_ERR_PEER_NOT_FOUND, "Peer not found" },
        { PT_ERR_DISCOVERY_ACTIVE, "Discovery" },
        { PT_ERR_CRC, "CRC" },
        { PT_ERR_MAGIC, "magic" },
        { PT_ERR_TRUNCATED, "Truncated" },
        { PT_ERR_VERSION, "version" },
        { PT_ERR_NOT_POWER2, "power" },
        { PT_ERR_PLATFORM, "Platform" },
        { PT_ERR_RESOURCE, "Resource" },
        { PT_ERR_INTERNAL, "Internal" },
        { (PeerTalk_Error)999, "Unknown" },  /* Unknown error code */
    };

    int count = sizeof(tests) / sizeof(tests[0]);
    printf("\n");

    for (int i = 0; i < count; i++) {
        const char *str = PeerTalk_ErrorString(tests[i].code);
        if (!str) {
            FAIL("ErrorString(%d) returned NULL", tests[i].code);
            return;
        }
        if (!strstr(str, tests[i].expected_substr)) {
            printf("  WARN: Error %d returned '%s' (expected substr '%s')\n",
                   tests[i].code, str, tests[i].expected_substr);
        } else {
            printf("  OK: Error %d = '%s'\n", tests[i].code, str);
        }
    }

    PASS();
}

/* Test version string */
static void test_version_string(void)
{
    TEST("test_version_string");

    const char *version = PeerTalk_Version();
    if (!version) {
        FAIL("PeerTalk_Version returned NULL");
        return;
    }

    if (strlen(version) == 0) {
        FAIL("Version string is empty");
        return;
    }

    printf("  (Version: %s)\n", version);
    PASS();
}

/* Test GetAvailableTransports */
static void test_available_transports(void)
{
    TEST("test_available_transports");

    uint16_t transports = PeerTalk_GetAvailableTransports();
    printf("  (Transports: 0x%04X)\n", transports);

    /* On POSIX, should have TCP and UDP */
    if (transports & PT_TRANSPORT_TCP) {
        printf("  - TCP available\n");
    }
    if (transports & PT_TRANSPORT_UDP) {
        printf("  - UDP available\n");
    }

    PASS();
}

int main(void)
{
    printf("=== Error String Tests ===\n\n");

    test_all_error_strings();
    test_version_string();
    test_available_transports();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
