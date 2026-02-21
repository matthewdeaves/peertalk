/**
 * @file status_window.c
 * @brief Status window implementation for Mac test apps
 *
 * Provides a simple window for displaying test progress without requiring
 * complex console emulation. Works on all Classic Mac systems.
 */

#include "status_window.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>

/* Window state */
static WindowPtr g_status_win = NULL;
static int g_status_y = 20;        /* Current Y position for text */
static int g_line_height = 14;     /* Pixels per line */

/* Window dimensions */
#define STATUS_WIN_TOP      40
#define STATUS_WIN_LEFT     40
#define STATUS_WIN_BOTTOM   320
#define STATUS_WIN_RIGHT    500
#define STATUS_TEXT_LEFT    8
#define STATUS_TEXT_TOP     16

void status_init(const char *test_name)
{
    Rect bounds;
    Str255 title;
    size_t len;

    if (g_status_win) {
        /* Already initialized */
        return;
    }

    /* Set window bounds */
    SetRect(&bounds, STATUS_WIN_LEFT, STATUS_WIN_TOP,
            STATUS_WIN_RIGHT, STATUS_WIN_BOTTOM);

    /* Convert C string to Pascal string */
    len = strlen(test_name);
    if (len > 254) len = 254;
    title[0] = (unsigned char)len;
    memcpy(title + 1, test_name, len);

    /* Create window - documentProc for System 6.0.8 compatibility
     * (noGrowDocProc may not be available on older systems) */
    g_status_win = NewWindow(NULL, &bounds, title, true,
                              documentProc, (WindowPtr)-1L, false, 0);
    if (!g_status_win) {
        return;
    }

    /* Set up drawing - System 6.0.8 compatible
     * Use TextSize/TextFace instead of TextFont for better compatibility */
    SetPort(g_status_win);
    TextSize(9);       /* 9pt is readable on all systems */
    g_line_height = 12;
    g_status_y = STATUS_TEXT_TOP;
}

void status_clear(void)
{
    if (!g_status_win) return;

    SetPort(g_status_win);
    EraseRect(&g_status_win->portRect);
    g_status_y = STATUS_TEXT_TOP;
}

void status_line(const char *text)
{
    Str255 pstr;
    size_t len;

    if (!g_status_win) return;

    SetPort(g_status_win);
    MoveTo(STATUS_TEXT_LEFT, g_status_y);

    /* Convert C string to Pascal string */
    len = strlen(text);
    if (len > 255) len = 255;
    pstr[0] = (unsigned char)len;
    memcpy(pstr + 1, text, len);

    DrawString(pstr);
    g_status_y += g_line_height;

    /* Wrap to top if exceeded window */
    if (g_status_y > (STATUS_WIN_BOTTOM - STATUS_WIN_TOP - 20)) {
        status_clear();
    }
}

void status_linef(const char *fmt, ...)
{
    char buf[256];
    va_list args;

    va_start(args, fmt);
    /* Use vsprintf instead of vsnprintf for System 6.0.8 compatibility.
     * CRITICAL: Caller must ensure formatted output < 256 bytes! */
    vsprintf(buf, fmt, args);
    va_end(args);

    status_line(buf);
}

void status_cleanup(void)
{
    if (g_status_win) {
        DisposeWindow(g_status_win);
        g_status_win = NULL;
    }
}
