/* test_perf_benchmarks.c - Performance benchmarks for PeerTalk
 *
 * Measures:
 * - Queue throughput (messages/second)
 * - Message latency (push-to-pop time)
 * - Priority queue overhead
 * - Coalescing efficiency
 * - Memory allocation patterns
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "../src/core/queue.h"
#include "../src/core/protocol.h"

/* Benchmark configuration */
#define BENCH_ITERATIONS    10000
#define BENCH_WARMUP        1000
#define MSG_SIZE_SMALL      32
#define MSG_SIZE_MEDIUM     128
#define MSG_SIZE_LARGE      256

static int tests_passed = 0;
static int tests_failed = 0;

/* High-resolution timer */
static double get_time_usec(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000000.0 + (double)tv.tv_usec;
}

#define BENCH_START() double _bench_start = get_time_usec()
#define BENCH_END() (get_time_usec() - _bench_start)

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
 * Queue Throughput Benchmarks
 * ======================================================================== */

static void bench_queue_push_throughput(void)
{
    TEST("bench_queue_push_throughput");

    pt_queue q;
    uint8_t msg[MSG_SIZE_SMALL];
    int i;
    double elapsed;
    double msgs_per_sec;

    memset(msg, 0xAB, sizeof(msg));
    pt_queue_init(NULL, &q, 32);
    pt_queue_ext_init(&q);

    /* Warmup */
    for (i = 0; i < BENCH_WARMUP && i < 32; i++) {
        pt_queue_push_coalesce(&q, msg, sizeof(msg), PT_PRIO_NORMAL, PT_COALESCE_NONE);
    }
    pt_queue_reset(&q);

    /* Benchmark: push until full, reset, repeat */
    BENCH_START();
    for (i = 0; i < BENCH_ITERATIONS; i++) {
        if (pt_queue_push_coalesce(&q, msg, sizeof(msg), PT_PRIO_NORMAL, PT_COALESCE_NONE) != 0) {
            pt_queue_reset(&q);
        }
    }
    elapsed = BENCH_END();

    msgs_per_sec = (double)BENCH_ITERATIONS / (elapsed / 1000000.0);
    printf("\n    %d messages in %.2f ms = %.0f msgs/sec\n",
           BENCH_ITERATIONS, elapsed / 1000.0, msgs_per_sec);

    pt_queue_free(&q);

    if (msgs_per_sec > 100000) {
        PASS();
    } else {
        FAIL("Throughput %.0f msgs/sec is below 100k threshold", msgs_per_sec);
    }
}

static void bench_queue_pop_throughput(void)
{
    TEST("bench_queue_pop_throughput");

    pt_queue q;
    uint8_t msg[MSG_SIZE_SMALL];
    uint8_t out[PT_QUEUE_SLOT_SIZE];
    uint16_t len;
    int i, batch;
    double elapsed;
    double msgs_per_sec;
    int total_popped = 0;

    memset(msg, 0xAB, sizeof(msg));
    pt_queue_init(NULL, &q, 32);
    pt_queue_ext_init(&q);

    /* Benchmark: fill queue, pop all, repeat */
    BENCH_START();
    for (batch = 0; batch < BENCH_ITERATIONS / 32; batch++) {
        /* Fill */
        for (i = 0; i < 32; i++) {
            pt_queue_push_coalesce(&q, msg, sizeof(msg), PT_PRIO_NORMAL, PT_COALESCE_NONE);
        }
        /* Pop all */
        while (pt_queue_pop_priority(&q, out, &len) == 0) {
            total_popped++;
        }
    }
    elapsed = BENCH_END();

    msgs_per_sec = (double)total_popped / (elapsed / 1000000.0);
    printf("\n    %d messages in %.2f ms = %.0f msgs/sec\n",
           total_popped, elapsed / 1000.0, msgs_per_sec);

    pt_queue_free(&q);

    if (msgs_per_sec > 100000) {
        PASS();
    } else {
        FAIL("Throughput %.0f msgs/sec is below 100k threshold", msgs_per_sec);
    }
}

/* ========================================================================
 * Latency Benchmarks
 * ======================================================================== */

static void bench_push_pop_latency(void)
{
    TEST("bench_push_pop_latency");

    pt_queue q;
    uint8_t msg[MSG_SIZE_SMALL];
    uint8_t out[PT_QUEUE_SLOT_SIZE];
    uint16_t len;
    int i;
    double total_latency = 0;
    double min_latency = 1e9;
    double max_latency = 0;
    double start, latency;

    memset(msg, 0xAB, sizeof(msg));
    pt_queue_init(NULL, &q, 32);
    pt_queue_ext_init(&q);

    /* Measure push+pop latency */
    for (i = 0; i < BENCH_ITERATIONS; i++) {
        start = get_time_usec();
        pt_queue_push_coalesce(&q, msg, sizeof(msg), PT_PRIO_NORMAL, PT_COALESCE_NONE);
        pt_queue_pop_priority(&q, out, &len);
        latency = get_time_usec() - start;

        total_latency += latency;
        if (latency < min_latency) min_latency = latency;
        if (latency > max_latency) max_latency = latency;
    }

    printf("\n    Latency: avg=%.2f us, min=%.2f us, max=%.2f us\n",
           total_latency / BENCH_ITERATIONS, min_latency, max_latency);

    pt_queue_free(&q);

    if (total_latency / BENCH_ITERATIONS < 100) {  /* < 100 usec average */
        PASS();
    } else {
        FAIL("Average latency %.2f us exceeds 100 us threshold",
             total_latency / BENCH_ITERATIONS);
    }
}

/* ========================================================================
 * Priority Queue Performance
 * ======================================================================== */

static void bench_priority_ordering(void)
{
    TEST("bench_priority_ordering");

    pt_queue q;
    uint8_t msg[4];
    uint8_t out[PT_QUEUE_SLOT_SIZE];
    uint16_t len;
    int i;
    double elapsed;
    int correct_order = 1;

    pt_queue_init(NULL, &q, 32);
    pt_queue_ext_init(&q);

    /* Push mixed priorities */
    BENCH_START();
    for (i = 0; i < 1000; i++) {
        msg[0] = PT_PRIO_LOW;
        pt_queue_push_coalesce(&q, msg, 1, PT_PRIO_LOW, PT_COALESCE_NONE);
        msg[0] = PT_PRIO_CRITICAL;
        pt_queue_push_coalesce(&q, msg, 1, PT_PRIO_CRITICAL, PT_COALESCE_NONE);
        msg[0] = PT_PRIO_NORMAL;
        pt_queue_push_coalesce(&q, msg, 1, PT_PRIO_NORMAL, PT_COALESCE_NONE);
        msg[0] = PT_PRIO_HIGH;
        pt_queue_push_coalesce(&q, msg, 1, PT_PRIO_HIGH, PT_COALESCE_NONE);

        /* Pop and verify order */
        if (pt_queue_pop_priority(&q, out, &len) == 0) {
            if (out[0] != PT_PRIO_CRITICAL) correct_order = 0;
        }
        if (pt_queue_pop_priority(&q, out, &len) == 0) {
            if (out[0] != PT_PRIO_HIGH) correct_order = 0;
        }
        if (pt_queue_pop_priority(&q, out, &len) == 0) {
            if (out[0] != PT_PRIO_NORMAL) correct_order = 0;
        }
        if (pt_queue_pop_priority(&q, out, &len) == 0) {
            if (out[0] != PT_PRIO_LOW) correct_order = 0;
        }
    }
    elapsed = BENCH_END();

    printf("\n    4000 priority ops in %.2f ms (%.0f ops/sec)\n",
           elapsed / 1000.0, 4000.0 / (elapsed / 1000000.0));

    pt_queue_free(&q);

    if (correct_order) {
        PASS();
    } else {
        FAIL("Priority ordering incorrect");
    }
}

/* ========================================================================
 * Coalescing Performance
 * ======================================================================== */

static void bench_coalesce_hit_rate(void)
{
    TEST("bench_coalesce_hit_rate");

    pt_queue q;
    uint8_t msg[MSG_SIZE_SMALL];
    int i;
    uint16_t initial_count, final_count;
    double elapsed;
    double coalesce_rate;

    memset(msg, 0xAB, sizeof(msg));
    pt_queue_init(NULL, &q, 32);
    pt_queue_ext_init(&q);

    /* Push first message with coalesce key */
    pt_queue_push_coalesce(&q, msg, sizeof(msg), PT_PRIO_NORMAL, PT_COALESCE_POSITION);
    initial_count = pt_queue_count(&q);

    /* Push many updates with same key - should coalesce */
    BENCH_START();
    for (i = 0; i < BENCH_ITERATIONS; i++) {
        msg[0] = (uint8_t)(i & 0xFF);  /* Change content */
        pt_queue_push_coalesce(&q, msg, sizeof(msg), PT_PRIO_NORMAL, PT_COALESCE_POSITION);
    }
    elapsed = BENCH_END();

    final_count = pt_queue_count(&q);
    coalesce_rate = 100.0 * (1.0 - (double)final_count / (double)(BENCH_ITERATIONS + 1));

    printf("\n    %d pushes, queue: %u -> %u items (%.1f%% coalesced)\n",
           BENCH_ITERATIONS, initial_count, final_count, coalesce_rate);
    printf("    %.2f ms = %.0f coalesce ops/sec\n",
           elapsed / 1000.0, BENCH_ITERATIONS / (elapsed / 1000000.0));

    pt_queue_free(&q);

    if (coalesce_rate > 90.0) {
        PASS();
    } else {
        FAIL("Coalesce rate %.1f%% is below 90%% threshold", coalesce_rate);
    }
}

/* ========================================================================
 * Message Size Impact
 * ======================================================================== */

static void bench_message_sizes(void)
{
    TEST("bench_message_sizes");

    pt_queue q;
    uint8_t msg[PT_QUEUE_SLOT_SIZE];
    uint8_t out[PT_QUEUE_SLOT_SIZE];
    uint16_t len;
    int i;
    double elapsed_small, elapsed_medium, elapsed_large;
    double start_time;
    int count = BENCH_ITERATIONS / 10;

    memset(msg, 0xAB, sizeof(msg));
    pt_queue_init(NULL, &q, 32);
    pt_queue_ext_init(&q);

    /* Small messages (32 bytes) */
    start_time = get_time_usec();
    for (i = 0; i < count; i++) {
        pt_queue_push_coalesce(&q, msg, MSG_SIZE_SMALL, PT_PRIO_NORMAL, PT_COALESCE_NONE);
        if (pt_queue_is_full(&q)) {
            while (pt_queue_pop_priority(&q, out, &len) == 0);
        }
    }
    elapsed_small = get_time_usec() - start_time;
    pt_queue_reset(&q);

    /* Medium messages (128 bytes) */
    start_time = get_time_usec();
    for (i = 0; i < count; i++) {
        pt_queue_push_coalesce(&q, msg, MSG_SIZE_MEDIUM, PT_PRIO_NORMAL, PT_COALESCE_NONE);
        if (pt_queue_is_full(&q)) {
            while (pt_queue_pop_priority(&q, out, &len) == 0);
        }
    }
    elapsed_medium = get_time_usec() - start_time;
    pt_queue_reset(&q);

    /* Large messages (256 bytes) */
    start_time = get_time_usec();
    for (i = 0; i < count; i++) {
        pt_queue_push_coalesce(&q, msg, MSG_SIZE_LARGE, PT_PRIO_NORMAL, PT_COALESCE_NONE);
        if (pt_queue_is_full(&q)) {
            while (pt_queue_pop_priority(&q, out, &len) == 0);
        }
    }
    elapsed_large = get_time_usec() - start_time;

    printf("\n    %d ops each:\n", count);
    printf("      32-byte:  %.2f ms (%.0f ops/sec)\n",
           elapsed_small / 1000.0, count / (elapsed_small / 1000000.0));
    printf("      128-byte: %.2f ms (%.0f ops/sec)\n",
           elapsed_medium / 1000.0, count / (elapsed_medium / 1000000.0));
    printf("      256-byte: %.2f ms (%.0f ops/sec)\n",
           elapsed_large / 1000.0, count / (elapsed_large / 1000000.0));

    pt_queue_free(&q);
    PASS();
}

/* ========================================================================
 * Protocol Encoding Performance
 * ======================================================================== */

static void bench_discovery_encode(void)
{
    TEST("bench_discovery_encode");

    pt_discovery_packet pkt = {
        .version = 1,
        .type = PT_DISC_TYPE_ANNOUNCE,
        .flags = 0,
        .sender_port = 7354,
        .transports = 0x03,
        .name_len = 8,
        .name = "TestPeer"
    };
    uint8_t buf[64];
    int i;
    double elapsed;
    int success = 0;

    BENCH_START();
    for (i = 0; i < BENCH_ITERATIONS; i++) {
        if (pt_discovery_encode(&pkt, buf, sizeof(buf)) > 0) {
            success++;
        }
    }
    elapsed = BENCH_END();

    printf("\n    %d encodes in %.2f ms = %.0f ops/sec\n",
           success, elapsed / 1000.0, success / (elapsed / 1000000.0));

    if (success == BENCH_ITERATIONS) {
        PASS();
    } else {
        FAIL("Only %d/%d encodes succeeded", success, BENCH_ITERATIONS);
    }
}

static void bench_discovery_decode(void)
{
    TEST("bench_discovery_decode");

    pt_discovery_packet pkt = {
        .version = 1,
        .type = PT_DISC_TYPE_ANNOUNCE,
        .flags = 0,
        .sender_port = 7354,
        .transports = 0x03,
        .name_len = 8,
        .name = "TestPeer"
    };
    uint8_t buf[64];
    pt_discovery_packet decoded;
    int len;
    int i;
    double elapsed;
    int success = 0;

    len = pt_discovery_encode(&pkt, buf, sizeof(buf));

    BENCH_START();
    for (i = 0; i < BENCH_ITERATIONS; i++) {
        if (pt_discovery_decode(NULL, buf, len, &decoded) == 0) {
            success++;
        }
    }
    elapsed = BENCH_END();

    printf("\n    %d decodes in %.2f ms = %.0f ops/sec\n",
           success, elapsed / 1000.0, success / (elapsed / 1000000.0));

    if (success == BENCH_ITERATIONS) {
        PASS();
    } else {
        FAIL("Only %d/%d decodes succeeded", success, BENCH_ITERATIONS);
    }
}

/* ========================================================================
 * Memory Allocation Pattern Test
 * ======================================================================== */

static void bench_queue_memory(void)
{
    TEST("bench_queue_memory");

    pt_queue q;
    size_t slot_size = sizeof(pt_queue_slot);
    size_t queue_overhead = sizeof(pt_queue);
    size_t total_32 = queue_overhead + 32 * slot_size;
    size_t total_16 = queue_overhead + 16 * slot_size;

    printf("\n    Queue overhead: %zu bytes\n", queue_overhead);
    printf("    Slot size: %zu bytes\n", slot_size);
    printf("    32-slot queue: %zu bytes (%.1f KB)\n", total_32, total_32 / 1024.0);
    printf("    16-slot queue: %zu bytes (%.1f KB)\n", total_16, total_16 / 1024.0);

    /* Verify allocation works */
    pt_queue_init(NULL, &q, 32);
    pt_queue_ext_init(&q);
    pt_queue_free(&q);

    /* On Classic Mac, 32 slots should fit in < 16KB */
    if (total_32 < 16384) {
        PASS();
    } else {
        FAIL("32-slot queue uses %zu bytes, exceeds 16KB limit", total_32);
    }
}

/* ========================================================================
 * Stress Test: Sustained Load
 * ======================================================================== */

static void bench_sustained_load(void)
{
    TEST("bench_sustained_load");

    pt_queue q;
    uint8_t msg[64];
    uint8_t out[PT_QUEUE_SLOT_SIZE];
    uint16_t len;
    int push_count = 0, pop_count = 0;
    int i;
    double elapsed;
    double target_time = 1000000.0;  /* 1 second */

    memset(msg, 0xAB, sizeof(msg));
    pt_queue_init(NULL, &q, 32);
    pt_queue_ext_init(&q);

    /* Simulate sustained load for 1 second: alternating push/pop */
    BENCH_START();
    while ((elapsed = BENCH_END()) < target_time) {
        /* Push burst */
        for (i = 0; i < 10; i++) {
            if (pt_queue_push_coalesce(&q, msg, sizeof(msg),
                    (uint8_t)(i % 4), PT_COALESCE_NONE) == 0) {
                push_count++;
            }
        }
        /* Pop some */
        for (i = 0; i < 5; i++) {
            if (pt_queue_pop_priority(&q, out, &len) == 0) {
                pop_count++;
            }
        }
    }

    printf("\n    1 second sustained: %d pushes, %d pops\n", push_count, pop_count);
    printf("    Final queue depth: %u\n", pt_queue_count(&q));

    pt_queue_free(&q);

    if (push_count > 10000 && pop_count > 5000) {
        PASS();
    } else {
        FAIL("Throughput too low: %d pushes, %d pops", push_count, pop_count);
    }
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== PeerTalk Performance Benchmarks ===\n\n");

    printf("Queue Throughput:\n");
    bench_queue_push_throughput();
    bench_queue_pop_throughput();

    printf("\nLatency:\n");
    bench_push_pop_latency();

    printf("\nPriority Queue:\n");
    bench_priority_ordering();

    printf("\nCoalescing:\n");
    bench_coalesce_hit_rate();

    printf("\nMessage Size Impact:\n");
    bench_message_sizes();

    printf("\nDiscovery Protocol:\n");
    bench_discovery_encode();
    bench_discovery_decode();

    printf("\nMemory:\n");
    bench_queue_memory();

    printf("\nStress Test:\n");
    bench_sustained_load();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
