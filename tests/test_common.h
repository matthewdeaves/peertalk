/*
 * test_common.h -- Shared test utilities for PeerTalk test apps
 *
 * Classic Mac builds: full Toolbox init, status window GUI, clog to file.
 * POSIX builds: printf + clog, C11.
 */

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include "peertalk.h"
#include "clog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>

#ifdef PT_PLATFORM_POSIX
#include <unistd.h>
#include <time.h>
#else
/* Classic Mac: full Toolbox + Delay/TickCount + status window */
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Memory.h>
#include <Events.h>
#include <Timer.h>
#include "status_window.h"
#endif

/* ------------------------------------------------------------------ */
/* Logging: one helper feeds every sink so a single call reaches the   */
/* local log AND (when enabled) the network.                           */
/*                                                                      */
/*   - clog file (all platforms)                                       */
/*   - printf/stderr (POSIX) or the Toolbox status window (Classic Mac) */
/*   - PeerTalk's UDP debug broadcast, port 7356, when a test app has   */
/*     called test_remote_log_enable() after PT_Init                    */
/*                                                                      */
/* The debug-broadcast sink is how we pull a full run log off a machine */
/* with NO FTP (e.g. the Mac SE): a listener on the host captures the   */
/* stream live.  LaunchAPPL's out-file cannot carry a GUI APPL's stdout */
/* (that path is MPW-tool only), so the network is the portable channel.*/
/* It also dogfoods the debug-broadcast SDK feature (Principle VIII).   */
/* SDK is untouched (Principle VII) -- this is test-harness only.       */
/* ------------------------------------------------------------------ */

/* Set by test_remote_log_enable(); NULL disables the broadcast sink. */
static PT_Context *g_test_log_ctx = NULL;

/* Keep printf-style format checking across every TEST_LOG/TEST_WARN site
   (funnelling through one varargs helper would otherwise lose -Wformat). */
#if defined(__GNUC__)
static void test_emit_log(int is_warn, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#endif

static void test_emit_log(int is_warn, const char *fmt, ...)
{
    char line[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (is_warn) {
        CLOG_WARN("%s", line);
    } else {
        CLOG_INFO("%s", line);
    }

#ifdef PT_PLATFORM_POSIX
    if (is_warn) {
        fprintf(stderr, "%s\n", line);
    } else {
        printf("%s\n", line);
    }
#else
    status_linef("%s", line);
#endif

    if (g_test_log_ctx) {
        PT_DebugSend(g_test_log_ctx, line, strlen(line));
    }
}

#define TEST_LOG(fmt, ...)  test_emit_log(0, fmt, ##__VA_ARGS__)
#define TEST_WARN(fmt, ...) test_emit_log(1, fmt, ##__VA_ARGS__)

/* Enable the UDP debug-broadcast log sink.  Call once, right after
   PT_Init, so subsequent TEST_LOG/TEST_WARN lines also stream to the
   host on port 7356.  Harmless if nobody is listening. */
static __attribute__((unused)) void test_remote_log_enable(PT_Context *ctx)
{
    g_test_log_ctx = ctx;
    PT_EnableDebugBroadcast(ctx, 0);
}

/* ------------------------------------------------------------------ */
/* Message type constants for test apps                                */
/* ------------------------------------------------------------------ */

#define MSG_POSITION  1
#define MSG_MOVE      2
#define MSG_CHAT      3
#define MSG_GAME_OVER 4

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

static volatile int g_running = 1;

/* ------------------------------------------------------------------ */
/* Signal handler for clean shutdown                                   */
/* ------------------------------------------------------------------ */

static void test_signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static void test_install_signal_handler(void)
{
#ifdef PT_PLATFORM_POSIX
    signal(SIGINT, test_signal_handler);
    signal(SIGTERM, test_signal_handler);
#endif
}

/* ------------------------------------------------------------------ */
/* Classic Mac platform init (call FIRST in main, before everything)   */
/* ------------------------------------------------------------------ */

static void test_init_toolbox(void)
{
#if !defined(PT_PLATFORM_POSIX)
#if TARGET_API_MAC_CARBON
    /* Carbon (OS X / CarbonLib): the system initializes the Toolbox for us.
       The classic QuickDraw globals (qd) and the InitGraf/InitFonts/...
       sequence don't exist under Carbon; RetroConsole sets up its own
       window.  MaxApplZone/MoreMasters are classic Memory Manager calls
       that don't apply to the OS X memory model either. */
    InitCursor();
#else
    MaxApplZone();
    MoreMasters();
    MoreMasters();
    MoreMasters();
    MoreMasters();

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
#endif
#endif
}

/* ------------------------------------------------------------------ */
/* Logging init + status window (call after test_init_toolbox)         */
/* ------------------------------------------------------------------ */

static void test_init_logging(const char *app_name)
{
#if !defined(PT_PLATFORM_POSIX)
    {
        /* Use per-app log filenames to avoid collision across test runs.
         * Format: "PT_{Name}" e.g. PT_Lifecycle, PT_Reliable (R20). */
        char logname[48];
        int i;
        logname[0] = 'P'; logname[1] = 'T'; logname[2] = '_';
        for (i = 0; i < 44 && app_name[i]; i++) {
            logname[3 + i] = app_name[i];
        }
        logname[3 + i] = '\0';
        clog_set_file(logname);
        clog_set_flush(CLOG_FLUSH_ALL);
    }
#endif
    clog_init(app_name, CLOG_LVL_INFO);
#if !defined(PT_PLATFORM_POSIX)
    status_init(app_name);
#endif
}

static void test_shutdown_logging(void)
{
    /* Called right after PT_Shutdown, which frees the context. Drop the
       broadcast sink's dangling pointer so any later TEST_LOG can't send
       through a freed ctx. */
    g_test_log_ctx = NULL;
#if !defined(PT_PLATFORM_POSIX)
    /* Pause while window is still visible so user can read results.
     * Must happen BEFORE status_cleanup() disposes the window. */
    {
        EventRecord event;
        int i;
        status_linef("Press any key to exit.");
        for (i = 0; i < 600; i++) {
            WaitNextEvent(everyEvent, &event, 1, NULL);
            if (event.what == keyDown || event.what == autoKey) break;
        }
    }
    status_cleanup();
#endif
    clog_shutdown();
}

/* ------------------------------------------------------------------ */
/* Command-line argument parsing                                       */
/* ------------------------------------------------------------------ */

static const char *test_parse_name(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--name") == 0) {
            return argv[i + 1];
        }
    }
    return "Unnamed";
}

/* ------------------------------------------------------------------ */
/* Platform-appropriate sleep                                          */
/* ------------------------------------------------------------------ */

static void test_sleep_ms(int ms)
{
#ifdef PT_PLATFORM_POSIX
    usleep((unsigned)(ms * 1000));
#else
    {
        EventRecord event;
        long ticks = ms / 16;
        if (ticks < 1) ticks = 1;
        WaitNextEvent(everyEvent, &event, ticks, NULL);
        if (event.what == keyDown || event.what == autoKey) {
            g_running = 0;
        }
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Timestamp (seconds)                                                 */
/* ------------------------------------------------------------------ */

static unsigned long test_time_sec(void)
{
#ifdef PT_PLATFORM_POSIX
    return (unsigned long)time(NULL);
#else
    return (unsigned long)(TickCount() / 60);
#endif
}

/* ------------------------------------------------------------------ */
/* Disconnect reason string                                            */
/* ------------------------------------------------------------------ */

static const char *test_reason_str(PT_DisconnectReason reason)
{
    switch (reason) {
        case PT_QUIT:             return "QUIT";
        case PT_TIMEOUT:          return "TIMEOUT";
        case PT_DISCONNECT_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/* Solo-mode timeout: exit after N seconds if no peer connects.        */
/* ------------------------------------------------------------------ */

#define TEST_SOLO_TIMEOUT_SEC  60

static unsigned long g_test_start_time = 0;
static int g_ever_connected = 0;

static void test_mark_start(void)
{
    g_test_start_time = test_time_sec();
}

static void test_mark_connected(void)
{
    g_ever_connected = 1;
}

/* Returns 1 if the solo timeout has elapsed (no peer ever connected) */
static int test_solo_timeout(void)
{
    if (g_ever_connected) return 0;
    if (g_test_start_time == 0) return 0;
    return (test_time_sec() - g_test_start_time >= TEST_SOLO_TIMEOUT_SEC);
}

/* test_exit_pause removed -- pause is now inside test_shutdown_logging()
 * so the status window stays visible until keypress */

/* ------------------------------------------------------------------ */
/* Helper: should we call PT_Connect on this peer? (R47)               */
/* ------------------------------------------------------------------ */

static __attribute__((unused)) int test_should_connect(const PT_Peer *peer)
{
    PT_PeerState st = PT_GetPeerState(peer);
    return (st == PT_PEER_DISCOVERED || st == PT_PEER_DISCONNECTED);
}

#endif /* TEST_COMMON_H */
