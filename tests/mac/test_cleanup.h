/**
 * @file test_cleanup.h
 * @brief File Cleanup Helper for Mac Test Apps
 *
 * Deletes temporary files from the Mac after test logs have been
 * streamed to the test partner. This prevents accumulation of
 * log files and stdout captures on the Mac filesystem.
 *
 * Usage:
 *   1. After log_stream_complete() returns true
 *   2. Call test_cleanup_files("PT_TestName") with your test's log name
 *   3. This deletes: PT_Log, PT_LibDebug, PT_<TestName>, out
 *
 * Files preserved:
 *   - LaunchAPPLServer (execution server)
 *   - LaunchAPPLServer Preferences
 *   - Retro68App (gets overwritten on next upload)
 */

#ifndef TEST_CLEANUP_H
#define TEST_CLEANUP_H

#include <Files.h>
#include <string.h>

/**
 * Delete a file by C string name (internal helper)
 *
 * @param filename  C string filename
 * @return 0 on success, File Manager error on failure
 */
static OSErr test_cleanup_delete_cstr(const char *filename)
{
    Str255 pname;
    size_t len = strlen(filename);

    if (len > 255) {
        len = 255;
    }

    pname[0] = (unsigned char)len;
    memcpy(&pname[1], filename, len);

    return FSDelete(pname, 0);
}

/**
 * Clean up all temporary files after test completion
 *
 * Call this after logs have been successfully streamed to the partner.
 * Deletes:
 *   - PT_Log (general application log)
 *   - PT_LibDebug (library debug log)
 *   - The test-specific log file (e.g., PT_Throughput)
 *   - out (stdout capture from LaunchAPPL)
 *
 * @param test_log_name  Name of test-specific log file (e.g., "PT_Throughput")
 *                       Pass NULL to skip test-specific log deletion
 */
static void test_cleanup_files(const char *test_log_name)
{
    /* Delete common log files - ignore errors (file may not exist) */
    (void)test_cleanup_delete_cstr("PT_Log");
    (void)test_cleanup_delete_cstr("PT_LibDebug");
    (void)test_cleanup_delete_cstr("out");

    /* Delete test-specific log if provided */
    if (test_log_name != NULL && test_log_name[0] != '\0') {
        (void)test_cleanup_delete_cstr(test_log_name);
    }
}

#endif /* TEST_CLEANUP_H */
