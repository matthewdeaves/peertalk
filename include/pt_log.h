/*
 * PT_Log - Cross-Platform C Logging Library
 *
 * A standalone logging library that works on POSIX and Classic Mac.
 * Used by PeerTalk internally, but can be used independently by any
 * application.
 *
 * FEATURES:
 *  - Level filtering (ERR, WARN, INFO, DEBUG)
 *  - Category filtering (bitmask, app-extensible)
 *  - Multiple outputs (file, console, callback)
 *  - Structured performance logging
 *  - Production-ready (included by default, opt-in stripping)
 *  - Zero overhead when stripped
 *
 * BUILD OPTIONS:
 *  -DPT_LOG_STRIP         Remove all logging code (zero overhead)
 *  -DPT_LOG_MIN_LEVEL=2   Only include ERR and WARN (optional)
 *
 * THREAD SAFETY:
 *  POSIX: PT_Log is thread-safe with pthread_mutex serialization
 *  Classic Mac: NOT thread-safe (not needed—single-threaded cooperative multitasking)
 *
 * CRITICAL ISR SAFETY NOTES:
 *  PT_Log is NOT interrupt-safe. Do NOT call PT_Log from:
 *   - MacTCP ASR callbacks
 *   - Open Transport notifiers
 *   - ADSP completion routines
 *   - Time Manager or VBL tasks
 *
 *  Use the "set flag, process later" pattern instead:
 *
 *    // In interrupt callback
 *    static pascal void my_asr(...) {
 *        state->flags.event_occurred = 1;  // OK: atomic flag set
 *        // NO PT_Log calls here
 *    }
 *
 *    // In main loop
 *    if (state->flags.event_occurred) {
 *        state->flags.event_occurred = 0;
 *        PT_LOG_INFO(log, CAT, "Event occurred");  // Safe here
 *    }
 *
 *  Why: TickCount() and File Manager calls are NOT in Inside Macintosh
 *  Volume VI Table B-3 (interrupt-safe routines).
 *
 * MESSAGE LENGTH CONSTRAINT (Classic Mac):
 *  Keep messages under PT_LOG_LINE_MAX (192 bytes) after printf expansion.
 *  Longer messages are safely truncated.
 *  Fits in 68030's 256-byte data cache.
 *
 *  Classic Mac vsprintf() has NO bounds checking - overflow causes crash!
 *
 *  Example:
 *    // WRONG - will crash if filename is long
 *    PT_LOG_INFO(log, CAT, "File: %s", long_path);
 *
 *    // RIGHT - truncate before logging
 *    char truncated[32];
 *    strncpy(truncated, long_path, 31);
 *    truncated[31] = '\0';
 *    PT_LOG_INFO(log, CAT, "File: %s", truncated);
 *
 * CONTEXT OWNERSHIP:
 *  PT_Log contexts are owned by their creator.
 *  - Creator is responsible for destroying it
 *  - Can be provided to PeerTalk or created independently
 *  - Reference-counted by ownership (not internally)
 *
 * ISR-SAFETY GUARD:
 *  Define PT_ISR_CONTEXT before including pt_log.h to disable all
 *  logging macros in interrupt handler files. This allows static
 *  analysis to verify no PT_Log calls from interrupt context.
 *
 *    #define PT_ISR_CONTEXT
 *    #include "pt_log.h"
 *    // All PT_LOG_* macros are now disabled
 */

#ifndef PT_LOG_H
#define PT_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ===================================================================
 * PORTABILITY TYPEDEFS
 * =================================================================== */

/* Standard int types - fallback for MPW, CodeWarrior, THINK C */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
    #include <stdint.h>
#elif defined(__GNUC__) || defined(__clang__)
    #include <stdint.h>
#else
    /* Classic Mac compilers without stdint.h */
    typedef signed char        int8_t;
    typedef unsigned char      uint8_t;
    typedef signed short       int16_t;
    typedef unsigned short     uint16_t;
    typedef signed long        int32_t;
    typedef unsigned long      uint32_t;
#endif

#include <stdarg.h>  /* va_list - C89+ */

/* ===================================================================
 * COMPILER DETECTION
 * =================================================================== */

#if defined(__GNUC__) || defined(__clang__)
    #define PT_LOG_PACKED __attribute__((packed))
#elif defined(__MWERKS__)
    #pragma options align=packed
    #define PT_LOG_PACKED
#elif defined(THINK_C) || defined(__SC__)
    #pragma options align=mac68k
    #define PT_LOG_PACKED
#else
    #define PT_LOG_PACKED
#endif

/* ===================================================================
 * TYPES
 * =================================================================== */

/* Opaque context */
typedef struct pt_log PT_Log;

/* Log levels (hierarchical) */
typedef enum {
    PT_LOG_NONE  = 0,  /* Disable all logging */
    PT_LOG_ERR   = 1,  /* Errors only */
    PT_LOG_WARN  = 2,  /* Warnings and errors */
    PT_LOG_INFO  = 3,  /* Info, warnings, and errors */
    PT_LOG_DEBUG = 4   /* Everything */
} PT_LogLevel;

/* Categories (bitmask, 16-bit) */
typedef enum {
    /* Reserved PeerTalk categories */
    PT_LOG_CAT_GENERAL   = 0x0001,
    PT_LOG_CAT_NETWORK   = 0x0002,
    PT_LOG_CAT_PROTOCOL  = 0x0004,
    PT_LOG_CAT_MEMORY    = 0x0008,
    PT_LOG_CAT_PLATFORM  = 0x0010,
    PT_LOG_CAT_PERF      = 0x0020,
    PT_LOG_CAT_CONNECT   = 0x0040,
    PT_LOG_CAT_DISCOVERY = 0x0080,
    PT_LOG_CAT_SEND      = 0x0100,
    PT_LOG_CAT_RECV      = 0x0200,
    PT_LOG_CAT_INIT      = 0x0400,

    /* Application categories (user-extensible) */
    PT_LOG_CAT_APP1      = 0x0800,
    PT_LOG_CAT_APP2      = 0x1000,
    PT_LOG_CAT_APP3      = 0x2000,
    PT_LOG_CAT_APP4      = 0x4000,
    PT_LOG_CAT_APP5      = 0x8000,

    PT_LOG_CAT_ALL       = 0xFFFF
} PT_LogCategory;

/* Output destinations (bitmask) */
typedef enum {
    PT_LOG_OUT_NONE     = 0x00,
    PT_LOG_OUT_FILE     = 0x01,
    PT_LOG_OUT_CONSOLE  = 0x02,
    PT_LOG_OUT_CALLBACK = 0x04
} PT_LogOutput;

/* Structured performance entry (exactly 16 bytes for cache efficiency) */
typedef struct PT_LOG_PACKED {
    uint32_t    seq_num;        /* Sequence number */
    uint32_t    timestamp_ms;   /* Milliseconds since PT_LogCreate */
    uint16_t    value1;         /* User-defined metric 1 */
    uint16_t    value2;         /* User-defined metric 2 */
    uint8_t     event_type;     /* User-defined event type */
    uint8_t     flags;          /* User-defined flags */
    uint16_t    category;       /* PT_LogCategory for filtering */
} PT_LogPerfEntry;

/* Callbacks */
typedef void (*PT_LogCallback)(
    PT_LogLevel     level,
    PT_LogCategory  category,
    uint32_t        timestamp_ms,
    const char     *message,
    void           *user_data
);

typedef void (*PT_LogPerfCallback)(
    const PT_LogPerfEntry *entry,
    const char            *label,
    void                  *user_data
);

/* ===================================================================
 * LIFECYCLE
 * =================================================================== */

/*
 * Create a new logging context.
 *
 * Returns: Opaque PT_Log handle, or NULL on allocation failure.
 *
 * Default configuration:
 *  - Level: PT_LOG_INFO
 *  - Categories: PT_LOG_CAT_ALL
 *  - Output: PT_LOG_OUT_CONSOLE
 *  - Auto-flush: disabled
 */
PT_Log *PT_LogCreate(void);

/*
 * Destroy a logging context and flush any buffered output.
 *
 * Safe to pass NULL.
 */
void PT_LogDestroy(PT_Log *log);

/* ===================================================================
 * CONFIGURATION
 * =================================================================== */

/*
 * Set the minimum log level.
 *
 * Messages below this level are discarded before formatting.
 */
void PT_LogSetLevel(PT_Log *log, PT_LogLevel level);

/*
 * Get the current log level.
 */
PT_LogLevel PT_LogGetLevel(PT_Log *log);

/*
 * Set the category filter (bitmask).
 *
 * Only messages matching these categories are logged.
 */
void PT_LogSetCategories(PT_Log *log, uint16_t categories);

/*
 * Get the current category filter.
 */
uint16_t PT_LogGetCategories(PT_Log *log);

/*
 * Set the output destinations (bitmask).
 *
 * Can combine PT_LOG_OUT_FILE | PT_LOG_OUT_CONSOLE | PT_LOG_OUT_CALLBACK.
 */
void PT_LogSetOutput(PT_Log *log, uint8_t outputs);

/*
 * Get the current output destinations.
 */
uint8_t PT_LogGetOutput(PT_Log *log);

/*
 * Set the log file path.
 *
 * Returns: 0 on success, -1 on error (file open failed).
 *
 * On Classic Mac, creates a file with creator='PTLg', type='TEXT'.
 * POSIX: Opens with O_CREAT | O_WRONLY | O_APPEND.
 */
int PT_LogSetFile(PT_Log *log, const char *filename);

/*
 * Set the message callback.
 *
 * Callback is invoked for each log message when PT_LOG_OUT_CALLBACK is set.
 *
 * CRITICAL: On POSIX, callbacks hold the mutex during dispatch.
 * Calling PT_Log from the callback causes deadlock!
 *
 * On Classic Mac, this is the only way to display logs in the UI
 * (console output is a no-op).
 */
void PT_LogSetCallback(PT_Log *log, PT_LogCallback callback, void *user_data);

/*
 * Set the performance logging callback.
 *
 * Callback is invoked for each PT_LogPerf call when category matches.
 */
void PT_LogSetPerfCallback(PT_Log *log, PT_LogPerfCallback callback, void *user_data);

/*
 * Enable or disable auto-flush.
 *
 * When enabled, file output is flushed after every write.
 * Useful for crash resilience but impacts performance.
 */
void PT_LogSetAutoFlush(PT_Log *log, int enabled);

/* ===================================================================
 * LOGGING
 * =================================================================== */

/*
 * Write a log message.
 *
 * Messages are filtered by level and category before formatting.
 *
 * IMPORTANT: Keep format strings and expanded output under 192 bytes
 * on Classic Mac (no vsnprintf bounds checking).
 */
void PT_LogWrite(
    PT_Log         *log,
    PT_LogLevel     level,
    PT_LogCategory  category,
    const char     *fmt,
    ...
);

/*
 * Write a log message with va_list.
 *
 * Same as PT_LogWrite but accepts a va_list.
 */
void PT_LogWriteV(
    PT_Log         *log,
    PT_LogLevel     level,
    PT_LogCategory  category,
    const char     *fmt,
    va_list         args
);

/*
 * Log a structured performance entry.
 *
 * Filtered by entry->category. If callback is set and category matches,
 * callback is invoked.
 *
 * IMPORTANT: PT_LogPerfEntry must be exactly 16 bytes.
 * Compile-time assertion included in implementation.
 */
void PT_LogPerf(PT_Log *log, const PT_LogPerfEntry *entry, const char *label);

/*
 * Flush any buffered output to file.
 *
 * Safe to call even if file output is disabled.
 */
void PT_LogFlush(PT_Log *log);

/* ===================================================================
 * UTILITIES
 * =================================================================== */

/*
 * Get milliseconds elapsed since PT_LogCreate.
 *
 * Useful for setting PT_LogPerfEntry.timestamp_ms.
 */
uint32_t PT_LogElapsedMs(PT_Log *log);

/*
 * Get the next sequence number and increment.
 *
 * Useful for setting PT_LogPerfEntry.seq_num.
 */
uint32_t PT_LogNextSeq(PT_Log *log);

/*
 * Get the human-readable name for a log level.
 *
 * Returns: "ERR", "WARN", "INFO", "DEBUG", or "NONE".
 */
const char *PT_LogLevelName(PT_LogLevel level);

/*
 * Get the PT_Log version string.
 *
 * Returns: "PT_Log v1.0.0"
 */
const char *PT_LogVersion(void);

/* ===================================================================
 * CONVENIENCE MACROS
 * =================================================================== */

#ifndef PT_LOG_STRIP

/* Standard logging macros */
#define PT_LOG_ERR(log, cat, ...)   PT_LogWrite((log), PT_LOG_ERR, (cat), __VA_ARGS__)
#define PT_LOG_WARN(log, cat, ...)  PT_LogWrite((log), PT_LOG_WARN, (cat), __VA_ARGS__)
#define PT_LOG_INFO(log, cat, ...)  PT_LogWrite((log), PT_LOG_INFO, (cat), __VA_ARGS__)
#define PT_LOG_DEBUG(log, cat, ...) PT_LogWrite((log), PT_LOG_DEBUG, (cat), __VA_ARGS__)

/* Performance logging macro */
#define PT_LOG_PERF(log, entry, label) PT_LogPerf((log), (entry), (label))

#else  /* PT_LOG_STRIP defined */

/* Strip all logging code at compile time (zero overhead) */
#define PT_LOG_ERR(log, cat, ...)   ((void)0)
#define PT_LOG_WARN(log, cat, ...)  ((void)0)
#define PT_LOG_INFO(log, cat, ...)  ((void)0)
#define PT_LOG_DEBUG(log, cat, ...) ((void)0)
#define PT_LOG_PERF(log, entry, label) ((void)0)

#endif  /* PT_LOG_STRIP */

/* ===================================================================
 * ISR-SAFETY GUARD
 * =================================================================== */

#ifdef PT_ISR_CONTEXT
    /* Disable all logging macros in interrupt handler files */
    #undef PT_LOG_ERR
    #undef PT_LOG_WARN
    #undef PT_LOG_INFO
    #undef PT_LOG_DEBUG
    #undef PT_LOG_PERF

    #define PT_LOG_ERR(log, cat, ...)   _PT_ISR_ERROR_DO_NOT_CALL_PT_LOG_FROM_INTERRUPT()
    #define PT_LOG_WARN(log, cat, ...)  _PT_ISR_ERROR_DO_NOT_CALL_PT_LOG_FROM_INTERRUPT()
    #define PT_LOG_INFO(log, cat, ...)  _PT_ISR_ERROR_DO_NOT_CALL_PT_LOG_FROM_INTERRUPT()
    #define PT_LOG_DEBUG(log, cat, ...) _PT_ISR_ERROR_DO_NOT_CALL_PT_LOG_FROM_INTERRUPT()
    #define PT_LOG_PERF(log, entry, label) _PT_ISR_ERROR_DO_NOT_CALL_PT_LOG_FROM_INTERRUPT()

    /* Undefined function to trigger compile error */
    void _PT_ISR_ERROR_DO_NOT_CALL_PT_LOG_FROM_INTERRUPT(void);
#endif  /* PT_ISR_CONTEXT */

/* ===================================================================
 * COMPILER CLEANUP
 * =================================================================== */

#if defined(__MWERKS__) || defined(THINK_C) || defined(__SC__)
    #pragma options align=reset
#endif

#ifdef __cplusplus
}
#endif

#endif  /* PT_LOG_H */
