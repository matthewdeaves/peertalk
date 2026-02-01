/**
 * @file test_compat.c
 * @brief Test suite for pt_compat.h portability layer
 */

#include "../src/core/pt_compat.h"
#include <stdio.h>
#include <string.h>

/* Simple test framework */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) static int name(void)
#define RUN_TEST(test) do { \
    tests_run++; \
    if (test()) { \
        tests_passed++; \
    } else { \
        printf("FAIL: %s\n", #test); \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  Assertion failed: %s (line %d)\n", #cond, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  Expected %d, got %d (line %d)\n", (int)(b), (int)(a), __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  Expected '%s', got '%s' (line %d)\n", (b), (a), __LINE__); \
        return 0; \
    } \
} while(0)

/* ========================================================================== */
/* Byte Order Tests                                                           */
/* ========================================================================== */

TEST(test_byte_order_16) {
    uint16_t host = 0x1234;
    uint16_t net = pt_htons(host);

    /* Network byte order is big-endian */
    unsigned char *bytes = (unsigned char *)&net;
    ASSERT_EQ(bytes[0], 0x12);
    ASSERT_EQ(bytes[1], 0x34);

    /* Round-trip conversion */
    uint16_t back = pt_ntohs(net);
    ASSERT_EQ(back, host);

    return 1;
}

TEST(test_byte_order_32) {
    uint32_t host = 0x12345678;
    uint32_t net = pt_htonl(host);

    /* Network byte order is big-endian */
    unsigned char *bytes = (unsigned char *)&net;
    ASSERT_EQ(bytes[0], 0x12);
    ASSERT_EQ(bytes[1], 0x34);
    ASSERT_EQ(bytes[2], 0x56);
    ASSERT_EQ(bytes[3], 0x78);

    /* Round-trip conversion */
    uint32_t back = pt_ntohl(net);
    ASSERT_EQ(back, host);

    return 1;
}

/* ========================================================================== */
/* Memory Allocation Tests                                                    */
/* ========================================================================== */

TEST(test_alloc_free) {
    void *ptr = pt_alloc(256);
    ASSERT(ptr != NULL);

    /* Write some data to verify it's writable */
    unsigned char *p = (unsigned char *)ptr;
    p[0] = 0xAA;
    p[255] = 0xBB;
    ASSERT_EQ(p[0], 0xAA);
    ASSERT_EQ(p[255], 0xBB);

    pt_free(ptr);

    /* Free NULL should be safe */
    pt_free(NULL);

    return 1;
}

TEST(test_alloc_clear) {
    void *ptr = pt_alloc_clear(256);
    ASSERT(ptr != NULL);

    /* Verify memory is zeroed */
    unsigned char *p = (unsigned char *)ptr;
    for (int i = 0; i < 256; i++) {
        ASSERT_EQ(p[i], 0);
    }

    pt_free(ptr);
    return 1;
}

/* ========================================================================== */
/* Memory Utilities Tests                                                     */
/* ========================================================================== */

TEST(test_memcpy_memset) {
    unsigned char src[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    unsigned char dest[16];

    /* Test memset */
    pt_memset(dest, 0xFF, sizeof(dest));
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(dest[i], 0xFF);
    }

    /* Test memcpy */
    pt_memcpy(dest, src, sizeof(src));
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(dest[i], i);
    }

    /* Test pt_memcpy_isr (ISR-safe version) */
    pt_memset(dest, 0, sizeof(dest));
    pt_memcpy_isr(dest, src, sizeof(src));
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ(dest[i], i);
    }

    return 1;
}

TEST(test_memcmp_test) {
    unsigned char a[4] = {1, 2, 3, 4};
    unsigned char b[4] = {1, 2, 3, 4};
    unsigned char c[4] = {1, 2, 3, 5};
    unsigned char d[4] = {1, 2, 3, 3};

    /* Equal */
    ASSERT_EQ(pt_memcmp(a, b, 4), 0);

    /* Less than */
    ASSERT(pt_memcmp(a, c, 4) < 0);

    /* Greater than */
    ASSERT(pt_memcmp(a, d, 4) > 0);

    return 1;
}

/* ========================================================================== */
/* String Utilities Tests                                                     */
/* ========================================================================== */

TEST(test_strlen_test) {
    ASSERT_EQ(pt_strlen(""), 0);
    ASSERT_EQ(pt_strlen("hello"), 5);
    ASSERT_EQ(pt_strlen("PeerTalk"), 8);
    return 1;
}

TEST(test_strncpy_test) {
    char dest[16];

    /* Normal copy */
    pt_strncpy(dest, "hello", sizeof(dest));
    ASSERT_STR_EQ(dest, "hello");

    /* Truncation */
    pt_strncpy(dest, "this is a very long string", 8);
    ASSERT_EQ(dest[7], '\0'); /* Must null-terminate */
    ASSERT_EQ(pt_strlen(dest), 7); /* Truncated to 7 chars + null */

    /* Empty string */
    pt_strncpy(dest, "", sizeof(dest));
    ASSERT_STR_EQ(dest, "");

    return 1;
}

/* ========================================================================== */
/* Atomic Operations Tests                                                    */
/* ========================================================================== */

TEST(test_atomic_bits) {
    pt_atomic_t flags = 0;

    /* Test set bit */
    pt_atomic_set_bit(&flags, PT_FLAG_DATA_AVAILABLE);
    ASSERT(pt_atomic_test_bit(&flags, PT_FLAG_DATA_AVAILABLE));
    ASSERT_EQ(flags, (1U << PT_FLAG_DATA_AVAILABLE));

    /* Test multiple bits */
    pt_atomic_set_bit(&flags, PT_FLAG_CONNECT_COMPLETE);
    ASSERT(pt_atomic_test_bit(&flags, PT_FLAG_DATA_AVAILABLE));
    ASSERT(pt_atomic_test_bit(&flags, PT_FLAG_CONNECT_COMPLETE));
    ASSERT(!pt_atomic_test_bit(&flags, PT_FLAG_ERROR));

    /* Test clear bit */
    pt_atomic_clear_bit(&flags, PT_FLAG_DATA_AVAILABLE);
    ASSERT(!pt_atomic_test_bit(&flags, PT_FLAG_DATA_AVAILABLE));
    ASSERT(pt_atomic_test_bit(&flags, PT_FLAG_CONNECT_COMPLETE));

    /* Test test-and-clear */
    int was_set = pt_atomic_test_and_clear_bit(&flags, PT_FLAG_CONNECT_COMPLETE);
    ASSERT(was_set);
    ASSERT(!pt_atomic_test_bit(&flags, PT_FLAG_CONNECT_COMPLETE));
    ASSERT_EQ(flags, 0);

    /* Test test-and-clear on already-clear bit */
    was_set = pt_atomic_test_and_clear_bit(&flags, PT_FLAG_ERROR);
    ASSERT(!was_set);
    ASSERT_EQ(flags, 0);

    return 1;
}

/* ========================================================================== */
/* snprintf Tests                                                             */
/* ========================================================================== */

TEST(test_snprintf_basic) {
    char buf[64];

    /* Integer formatting */
    pt_snprintf(buf, sizeof(buf), "%d", 42);
    ASSERT_STR_EQ(buf, "42");

    pt_snprintf(buf, sizeof(buf), "%d", -123);
    ASSERT_STR_EQ(buf, "-123");

    pt_snprintf(buf, sizeof(buf), "%u", 456);
    ASSERT_STR_EQ(buf, "456");

    /* Hex formatting */
    pt_snprintf(buf, sizeof(buf), "%x", 0xABCD);
    ASSERT_STR_EQ(buf, "abcd");

    pt_snprintf(buf, sizeof(buf), "%X", 0xABCD);
    ASSERT_STR_EQ(buf, "ABCD");

    /* String formatting */
    pt_snprintf(buf, sizeof(buf), "Hello, %s!", "world");
    ASSERT_STR_EQ(buf, "Hello, world!");

    /* Character formatting */
    pt_snprintf(buf, sizeof(buf), "%c%c%c", 'A', 'B', 'C');
    ASSERT_STR_EQ(buf, "ABC");

    /* Percent literal */
    pt_snprintf(buf, sizeof(buf), "100%%");
    ASSERT_STR_EQ(buf, "100%");

    return 1;
}

TEST(test_snprintf_width) {
    char buf[64];

    /* Zero padding */
    pt_snprintf(buf, sizeof(buf), "%08x", 0x1234);
    ASSERT_STR_EQ(buf, "00001234");

    pt_snprintf(buf, sizeof(buf), "%04d", 42);
    ASSERT_STR_EQ(buf, "0042");

    /* Width without zero-pad */
    pt_snprintf(buf, sizeof(buf), "%8d", 42);
    ASSERT_STR_EQ(buf, "      42");

    return 1;
}

TEST(test_snprintf_string) {
    char buf[64];

    /* String substitution */
    pt_snprintf(buf, sizeof(buf), "%s %s", "Hello", "World");
    ASSERT_STR_EQ(buf, "Hello World");

    /* NULL pointer handling */
    pt_snprintf(buf, sizeof(buf), "%s", (char *)NULL);
    ASSERT_STR_EQ(buf, "(null)");

    return 1;
}

TEST(test_snprintf_long) {
    char buf[64];

    /* Long integers */
    pt_snprintf(buf, sizeof(buf), "%ld", 123456789L);
    ASSERT_STR_EQ(buf, "123456789");

    pt_snprintf(buf, sizeof(buf), "%lu", 987654321UL);
    ASSERT_STR_EQ(buf, "987654321");

    pt_snprintf(buf, sizeof(buf), "%lx", 0xDEADBEEFL);
    ASSERT_STR_EQ(buf, "deadbeef");

    return 1;
}

TEST(test_snprintf_truncate) {
    char buf[8];

    /* Truncation with null termination */
    pt_snprintf(buf, sizeof(buf), "This is a very long string");
    ASSERT_EQ(buf[7], '\0'); /* Must be null-terminated */
    ASSERT_EQ(pt_strlen(buf), 7); /* Exactly 7 chars */

    /* Zero-length buffer */
    pt_snprintf(buf, 0, "test");
    /* Should not crash */

    return 1;
}

/* ========================================================================== */
/* Main                                                                        */
/* ========================================================================== */

int main(void) {
    printf("Running pt_compat tests...\n\n");

    /* Byte order tests */
    printf("Byte Order Tests:\n");
    RUN_TEST(test_byte_order_16);
    RUN_TEST(test_byte_order_32);

    /* Memory tests */
    printf("\nMemory Allocation Tests:\n");
    RUN_TEST(test_alloc_free);
    RUN_TEST(test_alloc_clear);

    /* Memory utility tests */
    printf("\nMemory Utility Tests:\n");
    RUN_TEST(test_memcpy_memset);
    RUN_TEST(test_memcmp_test);

    /* String utility tests */
    printf("\nString Utility Tests:\n");
    RUN_TEST(test_strlen_test);
    RUN_TEST(test_strncpy_test);

    /* Atomic tests */
    printf("\nAtomic Flag Tests:\n");
    RUN_TEST(test_atomic_bits);

    /* snprintf tests */
    printf("\nsnprintf Tests:\n");
    RUN_TEST(test_snprintf_basic);
    RUN_TEST(test_snprintf_width);
    RUN_TEST(test_snprintf_string);
    RUN_TEST(test_snprintf_long);
    RUN_TEST(test_snprintf_truncate);

    /* Summary */
    printf("\n========================================\n");
    printf("Tests: %d/%d passed\n", tests_passed, tests_run);
    printf("========================================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
