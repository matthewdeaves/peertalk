/**
 * @file test_log_threads.c
 * @brief Multi-threaded PT_Log stress test (MEDIUM PRIORITY)
 *
 * Tests concurrent logging from multiple threads to verify:
 * - No crashes or data corruption
 * - Proper sequence number handling
 * - File output integrity
 * - No lost messages
 */

#include "../include/pt_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#define NUM_THREADS 8
#define LOGS_PER_THREAD 1000
#define TEST_FILE "/tmp/pt_log_thread_test.log"

/* Shared log instance */
static PT_Log *g_log = NULL;
static int g_total_expected = NUM_THREADS * LOGS_PER_THREAD;

/* Thread function - writes LOGS_PER_THREAD messages */
void *log_thread(void *arg) {
    unsigned long thread_id = (unsigned long)arg;

    for (int i = 0; i < LOGS_PER_THREAD; i++) {
        PT_LOG_INFO(g_log, PT_LOG_CAT_GENERAL,
                    "Thread %lu: message %d", thread_id, i);

        /* Occasional yields to increase interleaving */
        if (i % 100 == 0) {
            sched_yield();
        }
    }

    return NULL;
}

/* Count lines in log file */
static int count_log_lines(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        return -1;
    }

    int count = 0;
    char buf[512];
    while (fgets(buf, sizeof(buf), f)) {
        count++;
    }

    fclose(f);
    return count;
}

/* Test 1: Concurrent logging to file */
static void test_concurrent_file_logging(void) {
    printf("test_concurrent_file_logging... ");
    fflush(stdout);

    unlink(TEST_FILE);

    g_log = PT_LogCreate();
    assert(g_log != NULL);

    int ret = PT_LogSetFile(g_log, TEST_FILE);
    assert(ret == 0);

    PT_LogSetOutput(g_log, PT_LOG_OUT_FILE);
    PT_LogSetLevel(g_log, PT_LOG_INFO);

    /* Spawn threads */
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        ret = pthread_create(&threads[i], NULL, log_thread, (void *)(unsigned long)i);
        assert(ret == 0);
    }

    /* Wait for all threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    /* Flush and verify */
    PT_LogFlush(g_log);
    PT_LogDestroy(g_log);

    int line_count = count_log_lines(TEST_FILE);
    if (line_count != g_total_expected) {
        printf("FAIL (expected %d lines, got %d)\n", g_total_expected, line_count);
        exit(1);
    }

    unlink(TEST_FILE);
    printf("PASS\n");
}

/* Test 2: Concurrent logging with callback */
static int g_callback_count = 0;
static pthread_mutex_t g_callback_mutex = PTHREAD_MUTEX_INITIALIZER;

static void callback_counter(
    PT_LogLevel level,
    PT_LogCategory category,
    uint32_t timestamp_ms,
    const char *message,
    void *user_data
) {
    (void)level;
    (void)category;
    (void)timestamp_ms;
    (void)message;
    (void)user_data;

    pthread_mutex_lock(&g_callback_mutex);
    g_callback_count++;
    pthread_mutex_unlock(&g_callback_mutex);
}

static void test_concurrent_callback_logging(void) {
    printf("test_concurrent_callback_logging... ");
    fflush(stdout);

    g_callback_count = 0;

    g_log = PT_LogCreate();
    assert(g_log != NULL);

    PT_LogSetCallback(g_log, callback_counter, NULL);
    PT_LogSetOutput(g_log, PT_LOG_OUT_CALLBACK);
    PT_LogSetLevel(g_log, PT_LOG_INFO);

    /* Spawn threads */
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        int ret = pthread_create(&threads[i], NULL, log_thread, (void *)(unsigned long)i);
        assert(ret == 0);
    }

    /* Wait for all threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    PT_LogDestroy(g_log);

    if (g_callback_count != g_total_expected) {
        printf("FAIL (expected %d callbacks, got %d)\n", g_total_expected, g_callback_count);
        exit(1);
    }

    printf("PASS\n");
}

/* Test 3: Sequence number monotonicity under concurrency */
static uint32_t g_sequences[NUM_THREADS][LOGS_PER_THREAD];

static void *sequence_thread(void *arg) {
    unsigned long thread_id = (unsigned long)arg;

    for (int i = 0; i < LOGS_PER_THREAD; i++) {
        g_sequences[thread_id][i] = PT_LogNextSeq(g_log);
    }

    return NULL;
}

static void test_concurrent_sequence_numbers(void) {
    printf("test_concurrent_sequence_numbers... ");
    fflush(stdout);

    g_log = PT_LogCreate();
    assert(g_log != NULL);

    /* Spawn threads that just grab sequence numbers */
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        int ret = pthread_create(&threads[i], NULL, sequence_thread, (void *)(unsigned long)i);
        assert(ret == 0);
    }

    /* Wait for all threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }

    PT_LogDestroy(g_log);

    /* Verify all sequence numbers are unique and in range */
    int seen[NUM_THREADS * LOGS_PER_THREAD];
    memset(seen, 0, sizeof(seen));

    for (int t = 0; t < NUM_THREADS; t++) {
        for (int i = 0; i < LOGS_PER_THREAD; i++) {
            uint32_t seq = g_sequences[t][i];

            /* Must be in range [1, NUM_THREADS * LOGS_PER_THREAD] */
            if (seq < 1 || seq > (uint32_t)g_total_expected) {
                printf("FAIL (sequence %u out of range)\n", seq);
                exit(1);
            }

            /* Must be unique (not seen before) */
            if (seen[seq - 1]) {
                printf("FAIL (duplicate sequence %u)\n", seq);
                exit(1);
            }

            seen[seq - 1] = 1;
        }
    }

    /* Verify all sequences were generated (no gaps) */
    for (int i = 0; i < g_total_expected; i++) {
        if (!seen[i]) {
            printf("FAIL (missing sequence %d)\n", i + 1);
            exit(1);
        }
    }

    printf("PASS\n");
}

int main(void) {
    printf("PT_Log Thread Safety Tests\n");
    printf("===========================\n\n");

    printf("Configuration: %d threads × %d messages = %d total\n\n",
           NUM_THREADS, LOGS_PER_THREAD, g_total_expected);

    test_concurrent_file_logging();
    test_concurrent_callback_logging();
    test_concurrent_sequence_numbers();

    printf("\n===========================\n");
    printf("All thread safety tests PASSED\n");
    return 0;
}
