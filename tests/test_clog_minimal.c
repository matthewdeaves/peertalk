/*
 * test_clog_minimal.c -- Minimal clog test for Classic Mac (T044)
 *
 * Verifies that clog can init, write to PT_Log file, and shutdown.
 * If this crashes, the problem is in clog/File Manager, not peertalk.
 * If PT_Log is created with output, clog works on this hardware.
 */

#include "clog.h"
#include <stdio.h>

#if !defined(PT_PLATFORM_POSIX)
#include <Timer.h>
#endif

int main(void)
{
#if !defined(PT_PLATFORM_POSIX)
    /* Redirect clog to file on Classic Mac */
    clog_set_file("PT_Log");
#endif

    clog_init("clog_test", CLOG_LVL_INFO);

    printf("clog_minimal: init OK\n");
    CLOG_INFO("clog_minimal: write test line 1");
    CLOG_INFO("clog_minimal: write test line 2");
    CLOG_INFO("clog_minimal: ALL OK");
    printf("clog_minimal: logged 3 lines\n");

    clog_shutdown();
    printf("clog_minimal: shutdown OK\n");

#if !defined(PT_PLATFORM_POSIX)
    /* Brief pause so output flushes to LaunchAPPL */
    {
        long final_ticks;
        Delay(120, &final_ticks);  /* 2 seconds */
    }
#endif

    return 0;
}
