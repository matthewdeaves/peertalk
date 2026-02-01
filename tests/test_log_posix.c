/*
 * PT_Log POSIX Tests
 */

#define _DEFAULT_SOURCE  /* For usleep() */
#include "../include/pt_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

/* Test callback state */
static int g_callback_count = 0;
static PT_LogLevel g_last_level;
static PT_LogCategory g_last_category;

static void test_callback(
    PT_LogLevel     level,
    PT_LogCategory  category,
    uint32_t        timestamp_ms,
    const char     *message,
    void           *user_data
) {
    g_callback_count++;
    g_last_level = level;
    g_last_category = category;
    (void)timestamp_ms;
    (void)message;
    (void)user_data;
}

void test_create_destroy(void) {
    printf("test_create_destroy...");

    PT_Log *log = PT_LogCreate();
    assert(log != NULL);
    assert(PT_LogGetLevel(log) == PT_LOG_INFO);  /* Default level */
    assert(PT_LogGetCategories(log) == PT_LOG_CAT_ALL);  /* Default categories */
    assert(PT_LogGetOutput(log) == PT_LOG_OUT_CONSOLE);  /* Default output */

    PT_LogDestroy(log);
    PT_LogDestroy(NULL);  /* Should be safe */

    printf(" OK\n");
}

void test_level_filtering(void) {
    printf("test_level_filtering...");

    PT_Log *log = PT_LogCreate();
    assert(log != NULL);

    g_callback_count = 0;
    PT_LogSetCallback(log, test_callback, NULL);
    PT_LogSetOutput(log, PT_LOG_OUT_CALLBACK);
    PT_LogSetCategories(log, PT_LOG_CAT_ALL);

    /* Set level to WARN - should filter out INFO and DEBUG */
    PT_LogSetLevel(log, PT_LOG_WARN);
    assert(PT_LogGetLevel(log) == PT_LOG_WARN);

    PT_LOG_ERR(log, PT_LOG_CAT_GENERAL, "Error message");    /* Should pass */
    PT_LOG_WARN(log, PT_LOG_CAT_GENERAL, "Warning message"); /* Should pass */
    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "Info message");    /* Should be filtered */
    PT_LOG_DEBUG(log, PT_LOG_CAT_GENERAL, "Debug message");  /* Should be filtered */

    assert(g_callback_count == 2);  /* Only ERR and WARN */

    /* Set level to DEBUG - all should pass */
    PT_LogSetLevel(log, PT_LOG_DEBUG);
    g_callback_count = 0;

    PT_LOG_ERR(log, PT_LOG_CAT_GENERAL, "Error");
    PT_LOG_WARN(log, PT_LOG_CAT_GENERAL, "Warn");
    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "Info");
    PT_LOG_DEBUG(log, PT_LOG_CAT_GENERAL, "Debug");

    assert(g_callback_count == 4);  /* All levels */

    /* Set level to NONE - nothing should pass */
    PT_LogSetLevel(log, PT_LOG_NONE);
    g_callback_count = 0;

    PT_LOG_ERR(log, PT_LOG_CAT_GENERAL, "Should be filtered");
    assert(g_callback_count == 0);

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_category_filtering(void) {
    printf("test_category_filtering...");

    PT_Log *log = PT_LogCreate();
    assert(log != NULL);

    g_callback_count = 0;
    PT_LogSetCallback(log, test_callback, NULL);
    PT_LogSetOutput(log, PT_LOG_OUT_CALLBACK);
    PT_LogSetLevel(log, PT_LOG_DEBUG);

    /* Set categories to NETWORK only */
    PT_LogSetCategories(log, PT_LOG_CAT_NETWORK);
    assert(PT_LogGetCategories(log) == PT_LOG_CAT_NETWORK);

    PT_LOG_INFO(log, PT_LOG_CAT_NETWORK, "Network message");  /* Should pass */
    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "General message");  /* Should be filtered */
    PT_LOG_INFO(log, PT_LOG_CAT_MEMORY, "Memory message");    /* Should be filtered */

    assert(g_callback_count == 1);
    assert(g_last_category == PT_LOG_CAT_NETWORK);

    /* Set categories to NETWORK | MEMORY */
    PT_LogSetCategories(log, PT_LOG_CAT_NETWORK | PT_LOG_CAT_MEMORY);
    g_callback_count = 0;

    PT_LOG_INFO(log, PT_LOG_CAT_NETWORK, "Network");  /* Pass */
    PT_LOG_INFO(log, PT_LOG_CAT_MEMORY, "Memory");    /* Pass */
    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "General");  /* Filtered */

    assert(g_callback_count == 2);

    /* Set categories to ALL */
    PT_LogSetCategories(log, PT_LOG_CAT_ALL);
    g_callback_count = 0;

    PT_LOG_INFO(log, PT_LOG_CAT_NETWORK, "Net");
    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "Gen");
    PT_LOG_INFO(log, PT_LOG_CAT_MEMORY, "Mem");
    PT_LOG_INFO(log, PT_LOG_CAT_APP1, "App1");

    assert(g_callback_count == 4);

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_formatting(void) {
    printf("test_formatting...");

    PT_Log *log = PT_LogCreate();
    assert(log != NULL);

    PT_LogSetLevel(log, PT_LOG_DEBUG);
    PT_LogSetOutput(log, PT_LOG_OUT_CONSOLE);

    /* Test various format strings */
    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "Simple message");
    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "Integer: %d", 42);
    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "String: %s", "hello");
    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "Multiple: %d %s %x", 123, "test", 0xABCD);

    /* Test long message (should truncate gracefully) */
    char long_msg[512];
    memset(long_msg, 'A', 500);
    long_msg[500] = '\0';
    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "Long: %s", long_msg);

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_file_output(void) {
    printf("test_file_output...");

    const char *filename = "/tmp/pt_log_test.log";
    unlink(filename);  /* Remove if exists */

    PT_Log *log = PT_LogCreate();
    assert(log != NULL);

    int ret = PT_LogSetFile(log, filename);
    assert(ret == 0);

    PT_LogSetLevel(log, PT_LOG_DEBUG);
    PT_LogSetOutput(log, PT_LOG_OUT_FILE);

    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "File test message 1");
    PT_LOG_WARN(log, PT_LOG_CAT_NETWORK, "File test message 2");
    PT_LOG_ERR(log, PT_LOG_CAT_MEMORY, "File test message 3");

    PT_LogFlush(log);

    /* Verify file exists and has content */
    FILE *f = fopen(filename, "r");
    assert(f != NULL);

    char line[512];
    int line_count = 0;
    while (fgets(line, sizeof(line), f)) {
        line_count++;
        /* Check format: [timestamp][LEVEL] message */
        assert(line[0] == '[');
        assert(strstr(line, "][") != NULL);
    }
    fclose(f);

    assert(line_count == 3);

    PT_LogDestroy(log);
    unlink(filename);

    printf(" OK\n");
}

void test_elapsed_time(void) {
    printf("test_elapsed_time...");

    PT_Log *log = PT_LogCreate();
    assert(log != NULL);

    uint32_t t1 = PT_LogElapsedMs(log);
    assert(t1 == 0 || t1 < 10);  /* Should be very close to 0 */

    /* Sleep 50ms */
    usleep(50000);

    uint32_t t2 = PT_LogElapsedMs(log);
    assert(t2 >= 40 && t2 <= 100);  /* Should be around 50ms (allow variance) */

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_sequence_numbers(void) {
    printf("test_sequence_numbers...");

    PT_Log *log = PT_LogCreate();
    assert(log != NULL);

    uint32_t seq1 = PT_LogNextSeq(log);
    uint32_t seq2 = PT_LogNextSeq(log);
    uint32_t seq3 = PT_LogNextSeq(log);

    assert(seq1 == 1);
    assert(seq2 == 2);
    assert(seq3 == 3);

    PT_LogDestroy(log);
    printf(" OK\n");
}

/* Performance logging test callback */
static int g_perf_callback_count = 0;
static PT_LogPerfEntry g_last_perf_entry;

static void test_perf_callback(
    const PT_LogPerfEntry *entry,
    const char            *label,
    void                  *user_data
) {
    g_perf_callback_count++;
    g_last_perf_entry = *entry;
    (void)label;
    (void)user_data;
}

void test_performance_logging(void) {
    printf("test_performance_logging...");

    PT_Log *log = PT_LogCreate();
    assert(log != NULL);

    g_perf_callback_count = 0;
    PT_LogSetPerfCallback(log, test_perf_callback, NULL);
    PT_LogSetCategories(log, PT_LOG_CAT_ALL);

    PT_LogPerfEntry entry = {0};
    entry.seq_num = PT_LogNextSeq(log);
    entry.timestamp_ms = PT_LogElapsedMs(log);
    entry.event_type = 1;
    entry.value1 = 100;
    entry.value2 = 200;
    entry.flags = 0x42;
    entry.category = PT_LOG_CAT_PERF;

    PT_LOG_PERF(log, &entry, "Test event");

    assert(g_perf_callback_count == 1);
    assert(g_last_perf_entry.seq_num == entry.seq_num);
    assert(g_last_perf_entry.event_type == 1);
    assert(g_last_perf_entry.value1 == 100);
    assert(g_last_perf_entry.value2 == 200);
    assert(g_last_perf_entry.flags == 0x42);
    assert(g_last_perf_entry.category == PT_LOG_CAT_PERF);

    /* Test category filtering */
    PT_LogSetCategories(log, PT_LOG_CAT_NETWORK);  /* Exclude PERF */
    g_perf_callback_count = 0;

    entry.category = PT_LOG_CAT_PERF;
    PT_LOG_PERF(log, &entry, "Should be filtered");
    assert(g_perf_callback_count == 0);  /* Filtered */

    entry.category = PT_LOG_CAT_NETWORK;
    PT_LOG_PERF(log, &entry, "Should pass");
    assert(g_perf_callback_count == 1);  /* Passed */

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_level_names(void) {
    printf("test_level_names...");

    assert(strcmp(PT_LogLevelName(PT_LOG_NONE), "---") == 0);
    assert(strcmp(PT_LogLevelName(PT_LOG_ERR), "ERR") == 0);
    assert(strcmp(PT_LogLevelName(PT_LOG_WARN), "WRN") == 0);
    assert(strcmp(PT_LogLevelName(PT_LOG_INFO), "INF") == 0);
    assert(strcmp(PT_LogLevelName(PT_LOG_DEBUG), "DBG") == 0);

    printf(" OK\n");
}

void test_version(void) {
    printf("test_version...");

    const char *ver = PT_LogVersion();
    assert(ver != NULL);
    assert(strlen(ver) > 0);
    assert(strstr(ver, ".") != NULL);  /* Should have dots */

    printf(" OK\n");
}

int main(void) {
    printf("PT_Log POSIX Tests\n");
    printf("==================\n\n");

    test_create_destroy();
    test_level_filtering();
    test_category_filtering();
    test_formatting();
    test_file_output();
    test_elapsed_time();
    test_sequence_numbers();
    test_performance_logging();
    test_level_names();
    test_version();

    printf("\n==================\n");
    printf("All tests PASSED\n");
    return 0;
}
