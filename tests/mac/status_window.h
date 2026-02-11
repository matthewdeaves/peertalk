/**
 * @file status_window.h
 * @brief Minimal window-based progress display for Mac test apps
 *
 * Provides a simple status window that displays real-time test progress
 * without requiring complex console emulation or dialogs.
 *
 * Usage:
 *   status_init("My Test");         // Create window
 *   status_line("Starting...");     // Add text lines
 *   status_linef("Count: %d", n);   // Formatted text
 *   status_clear();                 // Clear and reset
 *   status_cleanup();               // Dispose window
 */

#ifndef STATUS_WINDOW_H
#define STATUS_WINDOW_H

/**
 * Initialize status window
 *
 * Creates a simple window with the given title. Call after Toolbox init.
 *
 * @param test_name  Window title (e.g., "Latency Test")
 */
void status_init(const char *test_name);

/**
 * Clear window and reset to top
 *
 * Erases all text and moves cursor back to top of window.
 */
void status_clear(void);

/**
 * Add a line of status text
 *
 * Appends text at current Y position, then advances to next line.
 * Automatically wraps to top if window is full.
 *
 * @param text  Text to display (C string)
 */
void status_line(const char *text);

/**
 * Formatted status line
 *
 * Like status_line but with printf-style formatting.
 *
 * @param fmt   Format string
 * @param ...   Format arguments
 */
void status_linef(const char *fmt, ...);

/**
 * Cleanup status window
 *
 * Disposes window. Call before exit.
 */
void status_cleanup(void);

#endif /* STATUS_WINDOW_H */
