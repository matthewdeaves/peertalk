/* test_common.h - Shared test macros and utilities
 *
 * Include this header in all test files to avoid duplication of
 * TEST/PASS/FAIL macros and common helper functions.
 */

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

/* Ensure POSIX features are available */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ========================================================================
 * Test Counters
 *
 * By default, this header declares the test counters.
 * If you need to define your own (e.g., for external linkage),
 * define TEST_NO_COUNTERS before including this header.
 * ======================================================================== */

#ifndef TEST_NO_COUNTERS
static int tests_passed = 0;
static int tests_failed = 0;
static int tests_skipped = 0;
#endif

/* ========================================================================
 * Core Test Macros
 * ======================================================================== */

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

#define SKIP(reason) \
    do { \
        printf("SKIP: %s\n", reason); \
        tests_skipped++; \
    } while (0)

/* ========================================================================
 * Assertion Macros (for critical invariants)
 * ======================================================================== */

/* Assert that should never fail - indicates bug in test or code */
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "ASSERTION FAILED: %s\n", msg); \
            fprintf(stderr, "  at %s:%d in %s\n", __FILE__, __LINE__, __func__); \
            abort(); \
        } \
    } while (0)

/* Assert with formatted message */
#define TEST_ASSERT_FMT(cond, fmt, ...) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "ASSERTION FAILED: " fmt "\n", ##__VA_ARGS__); \
            fprintf(stderr, "  at %s:%d in %s\n", __FILE__, __LINE__, __func__); \
            abort(); \
        } \
    } while (0)

/* Soft assertion - fails test but continues */
#define TEST_EXPECT(cond, msg) \
    do { \
        if (!(cond)) { \
            FAIL("%s", msg); \
            return; \
        } \
    } while (0)

/* ========================================================================
 * Comparison Macros
 * ======================================================================== */

#define TEST_ASSERT_EQ(expected, actual, name) \
    do { \
        if ((expected) != (actual)) { \
            FAIL("%s: expected %d, got %d", name, (int)(expected), (int)(actual)); \
            return; \
        } \
    } while (0)

#define TEST_ASSERT_EQ_U32(expected, actual, name) \
    do { \
        if ((expected) != (actual)) { \
            FAIL("%s: expected %u, got %u", name, (unsigned)(expected), (unsigned)(actual)); \
            return; \
        } \
    } while (0)

#define TEST_ASSERT_NOT_NULL(ptr, name) \
    do { \
        if ((ptr) == NULL) { \
            FAIL("%s should not be NULL", name); \
            return; \
        } \
    } while (0)

#define TEST_ASSERT_NULL(ptr, name) \
    do { \
        if ((ptr) != NULL) { \
            FAIL("%s should be NULL", name); \
            return; \
        } \
    } while (0)

#define TEST_ASSERT_STR_EQ(expected, actual, name) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            FAIL("%s: expected \"%s\", got \"%s\"", name, expected, actual); \
            return; \
        } \
    } while (0)

/* ========================================================================
 * Test Summary
 * ======================================================================== */

#define TEST_SUMMARY() \
    do { \
        printf("\n=== Results ===\n"); \
        printf("Passed:  %d\n", tests_passed); \
        printf("Failed:  %d\n", tests_failed); \
        if (tests_skipped > 0) { \
            printf("Skipped: %d\n", tests_skipped); \
        } \
        printf("Total:   %d\n", tests_passed + tests_failed + tests_skipped); \
    } while (0)

#define TEST_RESULT() (tests_failed > 0 ? 1 : 0)

/* ========================================================================
 * Memory Helpers
 * ======================================================================== */

/* Safe free that NULLs the pointer */
#define SAFE_FREE(ptr) \
    do { \
        if (ptr) { \
            free(ptr); \
            ptr = NULL; \
        } \
    } while (0)

/* ========================================================================
 * Timing Helpers (for benchmarks)
 * ======================================================================== */

#ifdef __linux__
#include <time.h>

static inline uint64_t test_get_time_usec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static inline uint64_t test_get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#elif defined(__APPLE__)
#include <mach/mach_time.h>

static inline uint64_t test_get_time_usec(void) {
    static mach_timebase_info_data_t info = {0};
    if (info.denom == 0) {
        mach_timebase_info(&info);
    }
    uint64_t now = mach_absolute_time();
    return (now * info.numer / info.denom) / 1000;
}

static inline uint64_t test_get_time_ns(void) {
    static mach_timebase_info_data_t info = {0};
    if (info.denom == 0) {
        mach_timebase_info(&info);
    }
    return mach_absolute_time() * info.numer / info.denom;
}

#else
/* Fallback for other platforms */
#include <time.h>

static inline uint64_t test_get_time_usec(void) {
    return (uint64_t)clock() * 1000000ULL / CLOCKS_PER_SEC;
}

static inline uint64_t test_get_time_ns(void) {
    return (uint64_t)clock() * 1000000000ULL / CLOCKS_PER_SEC;
}
#endif

/* ========================================================================
 * Benchmark Macros
 * ======================================================================== */

#define BENCH_ITERATIONS 3  /* Run each benchmark multiple times */

/* Run benchmark and report median */
#define BENCH_START() uint64_t _bench_start = test_get_time_usec()

#define BENCH_END(ops, name) \
    do { \
        uint64_t _bench_end = test_get_time_usec(); \
        uint64_t _elapsed = _bench_end - _bench_start; \
        double _ops_per_sec = (_elapsed > 0) ? ((double)(ops) / _elapsed) * 1000000.0 : 0; \
        printf("  %s: %.2f ops/sec (%.2f usec total)\n", name, _ops_per_sec, (double)_elapsed); \
    } while (0)

#endif /* TEST_COMMON_H */
