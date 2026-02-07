/* test_queue_threads.c - Multi-threaded queue safety tests
 *
 * Tests concurrent access to the message queue from multiple threads,
 * validating the ISR-safe push and thread-safe operations.
 */

/* Define our own counters */
#define TEST_NO_COUNTERS
#include "test_common.h"
#include "../src/core/queue.h"
#include "../src/core/pt_internal.h"
#include "../src/core/pt_compat.h"
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

/* Test counters */
static int tests_passed = 0;
static int tests_failed = 0;
static int tests_skipped = 0;

/* Test configuration */
#define NUM_PRODUCER_THREADS 4
#define NUM_CONSUMER_THREADS 2
#define MESSAGES_PER_PRODUCER 1000
#define QUEUE_CAPACITY 64
#define STRESS_DURATION_SEC 2

/* Shared test state */
typedef struct {
    pt_queue queue;
    struct pt_context *ctx;
    pthread_barrier_t barrier;
    atomic_int messages_pushed;
    atomic_int messages_popped;
    atomic_int push_failures;
    atomic_int pop_failures;
    volatile int running;
} ThreadTestState;

static ThreadTestState g_state;

/* Helper to create a test context */
static struct pt_context *create_test_context(void)
{
    struct pt_context *ctx;
    pt_platform_ops *plat;

    ctx = (struct pt_context *)pt_alloc_clear(sizeof(struct pt_context));
    if (!ctx) {
        return NULL;
    }

    plat = (pt_platform_ops *)pt_alloc_clear(sizeof(pt_platform_ops));
    if (!plat) {
        pt_free(ctx);
        return NULL;
    }

    ctx->magic = PT_CONTEXT_MAGIC;
    ctx->plat = plat;

    return ctx;
}

static void destroy_test_context(struct pt_context *ctx)
{
    if (!ctx) return;
    if (ctx->plat) pt_free(ctx->plat);
    ctx->magic = 0;
    pt_free(ctx);
}

/* ========================================================================
 * Producer Thread - pushes messages using ISR-safe variant
 * ======================================================================== */

static void *producer_thread(void *arg)
{
    int thread_id = *(int *)arg;
    uint8_t msg[32];
    int i;

    /* Wait for all threads to be ready */
    pthread_barrier_wait(&g_state.barrier);

    for (i = 0; i < MESSAGES_PER_PRODUCER; i++) {
        /* Create unique message: [thread_id][sequence] */
        msg[0] = (uint8_t)thread_id;
        msg[1] = (uint8_t)(i & 0xFF);
        msg[2] = (uint8_t)((i >> 8) & 0xFF);

        /* Use ISR-safe push (simulates interrupt context) */
        int ret = pt_queue_push_isr(&g_state.queue, msg, 3);

        if (ret == 0) {
            atomic_fetch_add(&g_state.messages_pushed, 1);
        } else {
            /* Queue full - expected under contention */
            atomic_fetch_add(&g_state.push_failures, 1);
            /* Back off briefly */
            usleep(10);
            i--; /* Retry */
        }
    }

    return NULL;
}

/* ========================================================================
 * Consumer Thread - pops messages from queue
 * ======================================================================== */

static void *consumer_thread(void *arg)
{
    (void)arg;
    uint8_t buf[256];
    uint16_t len;

    /* Wait for all threads to be ready */
    pthread_barrier_wait(&g_state.barrier);

    int expected_total = NUM_PRODUCER_THREADS * MESSAGES_PER_PRODUCER;

    while (atomic_load(&g_state.messages_popped) < expected_total) {
        int ret = pt_queue_pop(&g_state.queue, buf, &len);

        if (ret == 0) {
            atomic_fetch_add(&g_state.messages_popped, 1);
            /* Verify message integrity */
            if (len != 3) {
                atomic_fetch_add(&g_state.pop_failures, 1);
            }
        } else {
            /* Queue empty - spin briefly */
            usleep(1);
        }
    }

    return NULL;
}

/* ========================================================================
 * Test: Multi-producer, Multi-consumer
 * ======================================================================== */

static void test_queue_mpmc(void)
{
    TEST("test_queue_mpmc");

    pthread_t producers[NUM_PRODUCER_THREADS];
    pthread_t consumers[NUM_CONSUMER_THREADS];
    int thread_ids[NUM_PRODUCER_THREADS];
    int i;

    /* Initialize state */
    g_state.ctx = create_test_context();
    if (!g_state.ctx) {
        FAIL("Failed to create context");
        return;
    }

    int ret = pt_queue_init(g_state.ctx, &g_state.queue, QUEUE_CAPACITY);
    if (ret != 0) {
        FAIL("pt_queue_init failed: %d", ret);
        destroy_test_context(g_state.ctx);
        return;
    }

    atomic_store(&g_state.messages_pushed, 0);
    atomic_store(&g_state.messages_popped, 0);
    atomic_store(&g_state.push_failures, 0);
    atomic_store(&g_state.pop_failures, 0);

    /* Initialize barrier for synchronized start */
    pthread_barrier_init(&g_state.barrier, NULL,
                         NUM_PRODUCER_THREADS + NUM_CONSUMER_THREADS);

    /* Create producer threads */
    for (i = 0; i < NUM_PRODUCER_THREADS; i++) {
        thread_ids[i] = i;
        pthread_create(&producers[i], NULL, producer_thread, &thread_ids[i]);
    }

    /* Create consumer threads */
    for (i = 0; i < NUM_CONSUMER_THREADS; i++) {
        pthread_create(&consumers[i], NULL, consumer_thread, NULL);
    }

    /* Wait for all threads to complete */
    for (i = 0; i < NUM_PRODUCER_THREADS; i++) {
        pthread_join(producers[i], NULL);
    }
    for (i = 0; i < NUM_CONSUMER_THREADS; i++) {
        pthread_join(consumers[i], NULL);
    }

    pthread_barrier_destroy(&g_state.barrier);

    /* Verify results */
    int expected = NUM_PRODUCER_THREADS * MESSAGES_PER_PRODUCER;
    int pushed = atomic_load(&g_state.messages_pushed);
    int popped = atomic_load(&g_state.messages_popped);
    int pop_errors = atomic_load(&g_state.pop_failures);

    printf("\n  Pushed: %d, Popped: %d, Pop errors: %d\n", pushed, popped, pop_errors);

    if (pushed != expected) {
        FAIL("Expected %d pushed, got %d", expected, pushed);
        pt_queue_free(&g_state.queue);
        destroy_test_context(g_state.ctx);
        return;
    }

    if (popped != expected) {
        FAIL("Expected %d popped, got %d", expected, popped);
        pt_queue_free(&g_state.queue);
        destroy_test_context(g_state.ctx);
        return;
    }

    if (pop_errors > 0) {
        FAIL("Message integrity errors: %d", pop_errors);
        pt_queue_free(&g_state.queue);
        destroy_test_context(g_state.ctx);
        return;
    }

    /* Queue should be empty */
    if (g_state.queue.count != 0) {
        FAIL("Queue should be empty, has %u items", g_state.queue.count);
        pt_queue_free(&g_state.queue);
        destroy_test_context(g_state.ctx);
        return;
    }

    pt_queue_free(&g_state.queue);
    destroy_test_context(g_state.ctx);
    PASS();
}

/* ========================================================================
 * Stress Thread - alternates between push and pop
 * ======================================================================== */

static void *stress_thread(void *arg)
{
    int thread_id = *(int *)arg;
    uint8_t msg[16];
    uint8_t buf[256];
    uint16_t len;
    int ops = 0;

    pthread_barrier_wait(&g_state.barrier);

    while (g_state.running) {
        /* Alternate between push and pop based on thread_id and op count */
        if ((thread_id + ops) % 2 == 0) {
            msg[0] = (uint8_t)thread_id;
            msg[1] = (uint8_t)(ops & 0xFF);
            if (pt_queue_push_isr(&g_state.queue, msg, 2) == 0) {
                atomic_fetch_add(&g_state.messages_pushed, 1);
            }
        } else {
            if (pt_queue_pop(&g_state.queue, buf, &len) == 0) {
                atomic_fetch_add(&g_state.messages_popped, 1);
            }
        }
        ops++;
    }

    return NULL;
}

/* ========================================================================
 * Test: Stress test with mixed operations
 * ======================================================================== */

static void test_queue_stress(void)
{
    TEST("test_queue_stress");

    pthread_t threads[8];
    int thread_ids[8];
    int i;

    /* Initialize state */
    g_state.ctx = create_test_context();
    if (!g_state.ctx) {
        FAIL("Failed to create context");
        return;
    }

    int ret = pt_queue_init(g_state.ctx, &g_state.queue, 32);
    if (ret != 0) {
        FAIL("pt_queue_init failed: %d", ret);
        destroy_test_context(g_state.ctx);
        return;
    }

    atomic_store(&g_state.messages_pushed, 0);
    atomic_store(&g_state.messages_popped, 0);
    g_state.running = 1;

    pthread_barrier_init(&g_state.barrier, NULL, 8);

    /* Create stress threads */
    for (i = 0; i < 8; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, stress_thread, &thread_ids[i]);
    }

    /* Run for duration */
    sleep(STRESS_DURATION_SEC);
    g_state.running = 0;

    /* Wait for threads */
    for (i = 0; i < 8; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_barrier_destroy(&g_state.barrier);

    int pushed = atomic_load(&g_state.messages_pushed);
    int popped = atomic_load(&g_state.messages_popped);

    printf("\n  Stress: %d pushed, %d popped in %d sec\n",
           pushed, popped, STRESS_DURATION_SEC);

    /* Drain remaining messages */
    uint8_t buf[256];
    uint16_t len;
    int drained = 0;
    while (pt_queue_pop(&g_state.queue, buf, &len) == 0) {
        drained++;
    }

    /* Verify: pushed = popped + drained */
    if (pushed != (popped + drained)) {
        FAIL("Message count mismatch: pushed=%d, popped=%d, drained=%d",
             pushed, popped, drained);
        pt_queue_free(&g_state.queue);
        destroy_test_context(g_state.ctx);
        return;
    }

    /* Check magic is still valid (no corruption) */
    if (g_state.queue.magic != PT_QUEUE_MAGIC) {
        FAIL("Queue magic corrupted during stress test");
        pt_queue_free(&g_state.queue);
        destroy_test_context(g_state.ctx);
        return;
    }

    pt_queue_free(&g_state.queue);
    destroy_test_context(g_state.ctx);
    PASS();
}

/* ========================================================================
 * Test: Single producer, single consumer (baseline)
 * ======================================================================== */

static atomic_int spsc_produced;
static atomic_int spsc_consumed;
static volatile int spsc_running;
static pt_queue spsc_queue;

static void *spsc_producer(void *arg)
{
    struct pt_context *ctx = (struct pt_context *)arg;
    uint8_t msg[8] = {0};
    int seq = 0;

    while (spsc_running || seq < 10000) {
        msg[0] = (uint8_t)(seq & 0xFF);
        msg[1] = (uint8_t)((seq >> 8) & 0xFF);

        if (pt_queue_push(ctx, &spsc_queue, msg, 2, 0, 0) == 0) {
            atomic_fetch_add(&spsc_produced, 1);
            seq++;
        } else {
            usleep(1);
        }

        if (seq >= 10000) break;
    }

    return NULL;
}

static void *spsc_consumer(void *arg)
{
    (void)arg;
    uint8_t buf[256];
    uint16_t len;

    while (spsc_running || atomic_load(&spsc_consumed) < atomic_load(&spsc_produced)) {
        if (pt_queue_pop(&spsc_queue, buf, &len) == 0) {
            atomic_fetch_add(&spsc_consumed, 1);
        } else {
            usleep(1);
        }
    }

    return NULL;
}

static void test_queue_spsc(void)
{
    TEST("test_queue_spsc");

    struct pt_context *ctx = create_test_context();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    int ret = pt_queue_init(ctx, &spsc_queue, 64);
    if (ret != 0) {
        FAIL("pt_queue_init failed: %d", ret);
        destroy_test_context(ctx);
        return;
    }

    atomic_store(&spsc_produced, 0);
    atomic_store(&spsc_consumed, 0);
    spsc_running = 1;

    pthread_t producer, consumer;
    pthread_create(&producer, NULL, spsc_producer, ctx);
    pthread_create(&consumer, NULL, spsc_consumer, NULL);

    pthread_join(producer, NULL);
    spsc_running = 0;
    pthread_join(consumer, NULL);

    int produced = atomic_load(&spsc_produced);
    int consumed = atomic_load(&spsc_consumed);

    printf("\n  SPSC: %d produced, %d consumed\n", produced, consumed);

    if (produced != 10000) {
        FAIL("Expected 10000 produced, got %d", produced);
        pt_queue_free(&spsc_queue);
        destroy_test_context(ctx);
        return;
    }

    if (consumed != produced) {
        FAIL("Produced %d != consumed %d", produced, consumed);
        pt_queue_free(&spsc_queue);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_free(&spsc_queue);
    destroy_test_context(ctx);
    PASS();
}

/* ========================================================================
 * Test: ISR-safe push from "interrupt" context
 * ======================================================================== */

static atomic_int isr_pushed;
static volatile int isr_running;

static void *isr_simulator(void *arg)
{
    pt_queue *q = (pt_queue *)arg;
    uint8_t msg[4];

    while (isr_running) {
        msg[0] = 0xAB;
        msg[1] = 0xCD;

        /* Simulate rapid interrupt-time pushes */
        if (pt_queue_push_isr(q, msg, 2) == 0) {
            atomic_fetch_add(&isr_pushed, 1);
        }
        /* No sleep - simulate rapid interrupts */
    }

    return NULL;
}

static void test_queue_isr_concurrent(void)
{
    TEST("test_queue_isr_concurrent");

    struct pt_context *ctx = create_test_context();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    pt_queue q;
    int ret = pt_queue_init(ctx, &q, 128);
    if (ret != 0) {
        FAIL("pt_queue_init failed: %d", ret);
        destroy_test_context(ctx);
        return;
    }

    atomic_store(&isr_pushed, 0);
    isr_running = 1;

    /* Start 4 "interrupt" threads */
    pthread_t isr_threads[4];
    for (int i = 0; i < 4; i++) {
        pthread_create(&isr_threads[i], NULL, isr_simulator, &q);
    }

    /* Main loop drains queue */
    uint8_t buf[256];
    uint16_t len;
    int main_consumed = 0;
    uint64_t start = test_get_time_usec();

    while ((test_get_time_usec() - start) < 1000000) { /* 1 second */
        if (pt_queue_pop(&q, buf, &len) == 0) {
            main_consumed++;
            if (buf[0] != 0xAB || buf[1] != 0xCD || len != 2) {
                FAIL("Data corruption detected");
                isr_running = 0;
                for (int i = 0; i < 4; i++) pthread_join(isr_threads[i], NULL);
                pt_queue_free(&q);
                destroy_test_context(ctx);
                return;
            }
        }
    }

    isr_running = 0;
    for (int i = 0; i < 4; i++) {
        pthread_join(isr_threads[i], NULL);
    }

    /* Drain remaining */
    while (pt_queue_pop(&q, buf, &len) == 0) {
        main_consumed++;
    }

    int pushed = atomic_load(&isr_pushed);
    printf("\n  ISR concurrent: %d pushed, %d consumed\n", pushed, main_consumed);

    if (pushed != main_consumed) {
        FAIL("Push/pop count mismatch: %d vs %d", pushed, main_consumed);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    /* Verify queue integrity */
    if (q.magic != PT_QUEUE_MAGIC) {
        FAIL("Queue magic corrupted");
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    if (q.count != 0) {
        FAIL("Queue not empty after drain: %u", q.count);
        pt_queue_free(&q);
        destroy_test_context(ctx);
        return;
    }

    pt_queue_free(&q);
    destroy_test_context(ctx);
    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== Queue Thread Safety Tests ===\n\n");

    test_queue_spsc();
    test_queue_mpmc();
    test_queue_stress();
    test_queue_isr_concurrent();

    TEST_SUMMARY();
    return TEST_RESULT();
}
