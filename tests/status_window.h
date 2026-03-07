/*
 * status_window.h -- Minimal window-based progress display for Mac test apps
 *
 * Provides a simple status window that displays real-time test progress
 * without requiring complex console emulation or dialogs.
 *
 * Usage:
 *   status_init("My Test");
 *   status_line("Starting...");
 *   status_linef("Count: %d", n);
 *   status_clear();
 *   status_cleanup();
 */

#ifndef STATUS_WINDOW_H
#define STATUS_WINDOW_H

/* Initialize status window. Call after Toolbox init. */
void status_init(const char *test_name);

/* Clear window and reset to top */
void status_clear(void);

/* Add a line of status text (auto-wraps to top when full) */
void status_line(const char *text);

/* Formatted status line (printf-style) */
void status_linef(const char *fmt, ...);

/* Cleanup status window. Call before exit. */
void status_cleanup(void);

#endif /* STATUS_WINDOW_H */
