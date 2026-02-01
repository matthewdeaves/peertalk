/*
 * PT_Log Performance Logging Tests
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "pt_log.h"

/* Event types for testing */
#define EVENT_SEND      1
#define EVENT_RECV      2
#define EVENT_CONNECT   3

static int g_perf_count = 0;
static PT_LogPerfEntry g_entries[100];

static void collect_perf(const PT_LogPerfEntry *entry,
                         const char *label, void *ud) {
    if (g_perf_count < 100) {
        g_entries[g_perf_count++] = *entry;
    }
    (void)label; (void)ud;
}

void test_perf_sequence(void) {
    printf("  test_perf_sequence...");

    PT_Log *log = PT_LogCreate();
    PT_LogSetPerfCallback(log, collect_perf, NULL);
    PT_LogSetCategories(log, PT_LOG_CAT_ALL);  /* Enable all categories */
    g_perf_count = 0;

    /* Log a sequence of events */
    for (int i = 0; i < 10; i++) {
        PT_LogPerfEntry entry = {0};
        entry.seq_num = PT_LogNextSeq(log);
        entry.timestamp_ms = PT_LogElapsedMs(log);
        entry.event_type = (i % 2 == 0) ? EVENT_SEND : EVENT_RECV;
        entry.value1 = i * 100;
        entry.value2 = i * 10;
        entry.flags = 0x01;
        entry.category = PT_LOG_CAT_NETWORK;  /* Set category */

        PT_LogPerf(log, &entry, NULL);
    }

    assert(g_perf_count == 10);

    /* Verify sequence numbers are monotonic */
    for (int i = 1; i < 10; i++) {
        assert(g_entries[i].seq_num > g_entries[i-1].seq_num);
    }

    /* Verify timestamps are non-decreasing */
    for (int i = 1; i < 10; i++) {
        assert(g_entries[i].timestamp_ms >= g_entries[i-1].timestamp_ms);
    }

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_perf_with_text_log(void) {
    printf("  test_perf_with_text_log...");

    PT_Log *log = PT_LogCreate();
    PT_LogSetCategories(log, PT_LOG_CAT_ALL);
    PT_LogSetLevel(log, PT_LOG_DEBUG);
    PT_LogSetOutput(log, PT_LOG_OUT_FILE);
    PT_LogSetFile(log, "/tmp/pt_log_perf_test.log");

    PT_LogPerfEntry entry = {0};
    entry.seq_num = PT_LogNextSeq(log);
    entry.timestamp_ms = PT_LogElapsedMs(log);
    entry.event_type = EVENT_CONNECT;
    entry.value1 = 1234;
    entry.value2 = 5678;
    entry.flags = 0xFF;
    entry.category = PT_LOG_CAT_NETWORK;  /* Test category metadata */

    PT_LogPerf(log, &entry, "connection_established");
    PT_LogFlush(log);
    PT_LogDestroy(log);

    /* Verify file contains perf entry with category */
    FILE *f = fopen("/tmp/pt_log_perf_test.log", "r");
    assert(f != NULL);
    char buf[512];
    assert(fgets(buf, sizeof(buf), f) != NULL);
    assert(strstr(buf, "PERF") != NULL);
    assert(strstr(buf, "connection_established") != NULL);
    assert(strstr(buf, "cat=0x0002") != NULL);  /* PT_LOG_CAT_NETWORK = 0x0002 */
    fclose(f);

    printf(" OK\n");
}

/* Callback for test_multiple_outputs - must be at file scope for C89 */
static int g_multi_callback_count = 0;
static void multi_count_callback(PT_LogLevel l, PT_LogCategory c,
                                 uint32_t t, const char *m, void *u) {
    g_multi_callback_count++;
    (void)l; (void)c; (void)t; (void)m; (void)u;
}

void test_multiple_outputs(void) {
    printf("  test_multiple_outputs...");

    PT_Log *log = PT_LogCreate();
    PT_LogSetFile(log, "/tmp/pt_log_multi_test.log");
    PT_LogSetCallback(log, multi_count_callback, NULL);
    PT_LogSetOutput(log, PT_LOG_OUT_FILE | PT_LOG_OUT_CALLBACK);

    g_multi_callback_count = 0;
    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "Test message");

    assert(g_multi_callback_count == 1);  /* Callback was called */

    PT_LogFlush(log);
    PT_LogDestroy(log);

    /* File was also written */
    FILE *f = fopen("/tmp/pt_log_multi_test.log", "r");
    assert(f != NULL);
    char buf[256];
    assert(fgets(buf, sizeof(buf), f) != NULL);
    assert(strstr(buf, "Test message") != NULL);
    fclose(f);

    printf(" OK\n");
}

/* Callback for test_app_categories - must be at file scope for C89 */
static int g_cat_count = 0;
static void cat_count_cb(PT_LogLevel l, PT_LogCategory c,
                         uint32_t t, const char *m, void *u) {
    g_cat_count++;
    (void)l; (void)c; (void)t; (void)m; (void)u;
}

void test_app_categories(void) {
    printf("  test_app_categories...");

    #define MY_CAT_UI     PT_LOG_CAT_APP1
    #define MY_CAT_GAME   PT_LOG_CAT_APP2
    #define MY_CAT_AUDIO  PT_LOG_CAT_APP3

    PT_Log *log = PT_LogCreate();
    PT_LogSetOutput(log, PT_LOG_OUT_CALLBACK);
    PT_LogSetCallback(log, cat_count_cb, NULL);

    /* Enable only UI and GAME */
    PT_LogSetCategories(log, MY_CAT_UI | MY_CAT_GAME);
    g_cat_count = 0;

    PT_LOG_INFO(log, MY_CAT_UI, "UI event");
    PT_LOG_INFO(log, MY_CAT_GAME, "Game event");
    PT_LOG_INFO(log, MY_CAT_AUDIO, "Audio event");  /* Should be filtered */

    assert(g_cat_count == 2);

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_perf_category_field(void) {
    printf("  test_perf_category_field...");

    PT_Log *log = PT_LogCreate();
    PT_LogSetPerfCallback(log, collect_perf, NULL);
    PT_LogSetCategories(log, PT_LOG_CAT_ALL);  /* Enable all categories */
    g_perf_count = 0;

    /* Log perf entries with different categories */
    PT_LogPerfEntry entry1 = {0};
    entry1.seq_num = 1;
    entry1.event_type = 1;
    entry1.category = PT_LOG_CAT_NETWORK;
    PT_LogPerf(log, &entry1, NULL);

    PT_LogPerfEntry entry2 = {0};
    entry2.seq_num = 2;
    entry2.event_type = 2;
    entry2.category = PT_LOG_CAT_PROTOCOL;
    PT_LogPerf(log, &entry2, NULL);

    assert(g_perf_count == 2);
    assert(g_entries[0].category == PT_LOG_CAT_NETWORK);
    assert(g_entries[1].category == PT_LOG_CAT_PROTOCOL);

    /* Verify struct size is exactly 16 bytes (cache-optimal) */
    assert(sizeof(PT_LogPerfEntry) == 16);

    PT_LogDestroy(log);
    printf(" OK\n");
}

int main(void) {
    printf("PT_Log Performance Tests\n");
    printf("========================\n\n");

    test_perf_sequence();
    test_perf_with_text_log();
    test_multiple_outputs();
    test_app_categories();
    test_perf_category_field();

    printf("\n========================\n");
    printf("All tests PASSED\n");
    return 0;
}
