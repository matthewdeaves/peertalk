/*
 * PT_Log - POSIX Implementation
 *
 * Thread-safe logging for Linux, macOS, BSD with pthread_mutex.
 */

#include "../../include/pt_log.h"

#ifndef PT_LOG_STRIP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <pthread.h>
#include <time.h>

/* Compile-time assertion for PT_LogPerfEntry size */
typedef char PT_LogPerfEntry_must_be_16_bytes[
    (sizeof(PT_LogPerfEntry) == 16) ? 1 : -1
];

/*============================================================================
 * Constants
 *
 * BUFFER SIZE RATIONALE (POSIX):
 * 512 bytes chosen for POSIX systems to balance throughput vs memory:
 * - Larger than Classic Mac's 256 bytes (no 68030 cache constraint)
 * - Not so large as to waste memory in multi-context scenarios
 * - 8x a typical 64-byte cache line (good prefetch behavior)
 * - Matches common filesystem block sizes (512 bytes)
 *
 * For very high-throughput logging, applications can increase this by
 * modifying the source. For memory-constrained POSIX systems, reduce to 256.
 *============================================================================*/

#define PT_LOG_BUFFER_SIZE  512
#define PT_LOG_LINE_MAX     256

/*============================================================================
 * Log Context Structure
 *
 * Field ordering is optimized for cache efficiency AND minimal padding:
 * - Fields ordered largest-to-smallest to eliminate implicit padding
 * - 8-byte pointers first, then 4-byte, then 2-byte, then 1-byte
 * - Hot path fields grouped together in first cache line
 * - Large buffer at end (cold on non-write paths)
 *
 * On 64-bit: First 64 bytes contain all hot/warm fields (one cache line)
 *============================================================================*/

struct pt_log {
    /* === 8-BYTE FIELDS (pointers, timeval) === */
    FILE               *file;           /* 8 bytes - file handle */
    PT_LogCallback      msg_callback;   /* 8 bytes - callback pointer */
    void               *msg_user_data;  /* 8 bytes - callback context */
    PT_LogPerfCallback  perf_callback;  /* 8 bytes */
    void               *perf_user_data; /* 8 bytes */
    struct timeval      start_time;     /* 16 bytes - timestamp base */

    /* === 4-BYTE FIELDS === */
    int                 buffer_pos;     /* 4 bytes - buffer write position */
    uint32_t            next_seq;       /* 4 bytes - sequence counter */

    /* === 2-BYTE FIELDS === */
    uint16_t            categories;     /* 2 bytes - filter check */

    /* === 1-BYTE FIELDS (grouped to avoid padding) === */
    uint8_t             level;          /* 1 byte - stores PT_LogLevel enum value */
    uint8_t             outputs;        /* 1 byte - output routing */
    uint8_t             auto_flush;     /* 1 byte - flush decision */
    uint8_t             _pad[1];        /* 1 byte - explicit padding for alignment */

    /* === MUTEX (platform-dependent size, typically 40 bytes) === */
    pthread_mutex_t     mutex;

    /* === BUFFER: Large, at end to avoid polluting cache === */
    char                buffer[PT_LOG_BUFFER_SIZE];  /* 512 bytes */
};

/*============================================================================
 * Static Data
 *============================================================================*/

static const char *g_level_names[] = {
    "---",  /* NONE */
    "ERR",
    "WRN",
    "INF",
    "DBG"
};

static const char *g_version = "1.0.0";

/*============================================================================
 * Lifecycle
 *============================================================================*/

PT_Log *PT_LogCreate(void) {
    PT_Log *log = (PT_Log *)calloc(1, sizeof(PT_Log));
    if (!log) return NULL;

    /* Defaults */
    log->level = PT_LOG_INFO;
    log->categories = PT_LOG_CAT_ALL;
    log->outputs = PT_LOG_OUT_CONSOLE;
    log->auto_flush = 0;
    log->next_seq = 1;

    /* Record start time */
    gettimeofday(&log->start_time, NULL);

    /* Initialize mutex */
    pthread_mutex_init(&log->mutex, NULL);

    return log;
}

void PT_LogDestroy(PT_Log *log) {
    if (!log) return;

    PT_LogFlush(log);

    if (log->file) {
        fclose(log->file);
    }

    pthread_mutex_destroy(&log->mutex);
    free(log);
}

/*============================================================================
 * Configuration
 *============================================================================*/

void PT_LogSetLevel(PT_Log *log, PT_LogLevel level) {
    if (log) log->level = (uint8_t)level;
}

PT_LogLevel PT_LogGetLevel(PT_Log *log) {
    return log ? (PT_LogLevel)log->level : PT_LOG_NONE;
}

void PT_LogSetCategories(PT_Log *log, uint16_t categories) {
    if (log) log->categories = categories;
}

uint16_t PT_LogGetCategories(PT_Log *log) {
    return log ? log->categories : 0;
}

void PT_LogSetOutput(PT_Log *log, uint8_t outputs) {
    if (log) log->outputs = outputs;
}

uint8_t PT_LogGetOutput(PT_Log *log) {
    return log ? log->outputs : 0;
}

int PT_LogSetFile(PT_Log *log, const char *filename) {
    if (!log) return -1;

    pthread_mutex_lock(&log->mutex);

    /* Close existing file */
    if (log->file) {
        fclose(log->file);
        log->file = NULL;
    }

    if (filename) {
        log->file = fopen(filename, "a");
        if (!log->file) {
            pthread_mutex_unlock(&log->mutex);
            return -1;
        }
        log->outputs |= PT_LOG_OUT_FILE;
    }

    pthread_mutex_unlock(&log->mutex);
    return 0;
}

void PT_LogSetCallback(PT_Log *log, PT_LogCallback callback, void *user_data) {
    if (log) {
        log->msg_callback = callback;
        log->msg_user_data = user_data;
    }
}

void PT_LogSetPerfCallback(PT_Log *log, PT_LogPerfCallback callback, void *user_data) {
    if (log) {
        log->perf_callback = callback;
        log->perf_user_data = user_data;
    }
}

void PT_LogSetAutoFlush(PT_Log *log, int enabled) {
    if (log) log->auto_flush = enabled ? 1 : 0;
}

/*============================================================================
 * Internal Helpers
 *============================================================================*/

static void flush_to_file(PT_Log *log) {
    if (log->file && log->buffer_pos > 0) {
        fwrite(log->buffer, 1, log->buffer_pos, log->file);
        log->buffer_pos = 0;
    }
    if (log->file && log->auto_flush) {
        fflush(log->file);
    }
}

static void write_line(PT_Log *log, const char *line, int len) {
    /* Buffer full? Flush it */
    if (log->buffer_pos + len > PT_LOG_BUFFER_SIZE) {
        flush_to_file(log);
    }

    /* Still doesn't fit? Write directly */
    if (len > PT_LOG_BUFFER_SIZE) {
        if (log->file) {
            fwrite(line, 1, len, log->file);
            if (log->auto_flush) {
                fflush(log->file);
            }
        }
        if (log->outputs & PT_LOG_OUT_CONSOLE) {
            fwrite(line, 1, len, stderr);
        }
        return;
    }

    /* Append to buffer */
    if (log->outputs & PT_LOG_OUT_FILE) {
        memcpy(log->buffer + log->buffer_pos, line, len);
        log->buffer_pos += len;
    }

    /* Console output (unbuffered) */
    if (log->outputs & PT_LOG_OUT_CONSOLE) {
        fwrite(line, 1, len, stderr);
    }
}

/*============================================================================
 * Logging
 *============================================================================*/

void PT_LogWrite(
    PT_Log         *log,
    PT_LogLevel     level,
    PT_LogCategory  category,
    const char     *fmt,
    ...
) {
    va_list args;
    va_start(args, fmt);
    PT_LogWriteV(log, level, category, fmt, args);
    va_end(args);
}

void PT_LogWriteV(
    PT_Log         *log,
    PT_LogLevel     level,
    PT_LogCategory  category,
    const char     *fmt,
    va_list         args
) {
    if (!log) return;

    /* Filter by level (early exit before allocation) */
    if (level > log->level) return;

    /* Filter by category (early exit before allocation) */
    if (!(category & log->categories)) return;

    /* Now allocate stack buffer (only if passing filters) */
    char line[PT_LOG_LINE_MAX];
    uint32_t timestamp_ms;
    const char *level_name;
    int prefix_len, total_len;

    pthread_mutex_lock(&log->mutex);

    /* Get timestamp */
    struct timeval now;
    gettimeofday(&now, NULL);
    timestamp_ms = (uint32_t)(
        (now.tv_sec - log->start_time.tv_sec) * 1000 +
        (now.tv_usec - log->start_time.tv_usec) / 1000
    );

    /* Format: [timestamp][LEVEL] message\n */
    level_name = PT_LogLevelName(level);
    /* cppcheck-suppress invalidPrintfArgType_uint ; uint32_t is unsigned int on unix64 */
    prefix_len = snprintf(line, PT_LOG_LINE_MAX, "[%08u][%s] ",
                          timestamp_ms, level_name);

    /* Format user message */
    vsnprintf(line + prefix_len, PT_LOG_LINE_MAX - prefix_len - 1, fmt, args);
    total_len = strlen(line);

    /* Ensure newline */
    if (total_len < PT_LOG_LINE_MAX - 1 && line[total_len - 1] != '\n') {
        line[total_len++] = '\n';
        line[total_len] = '\0';
    }

    /* Write to outputs */
    write_line(log, line, total_len);

    /* Callback dispatch (WARNING: callback holds mutex - don't call PT_Log!) */
    if ((log->outputs & PT_LOG_OUT_CALLBACK) && log->msg_callback) {
        log->msg_callback(level, category, timestamp_ms, line, log->msg_user_data);
    }

    pthread_mutex_unlock(&log->mutex);
}

void PT_LogPerf(PT_Log *log, const PT_LogPerfEntry *entry, const char *label) {
    if (!log || !entry) return;

    /* Filter by category */
    if (!(entry->category & log->categories)) return;

    pthread_mutex_lock(&log->mutex);

    /* Callback dispatch */
    if (log->perf_callback) {
        log->perf_callback(entry, label, log->perf_user_data);
    }

    /* Text output (optional) */
    if (log->outputs & (PT_LOG_OUT_FILE | PT_LOG_OUT_CONSOLE)) {
        char line[PT_LOG_LINE_MAX];
        int len;

        if (label && *label) {
            /* cppcheck-suppress invalidPrintfArgType_uint ; uint32_t/uint16_t are unsigned int compatible */
            len = snprintf(line, PT_LOG_LINE_MAX,
                "[%08u][INF] PERF %s: seq=%u type=%u v1=%u v2=%u flags=0x%02X cat=0x%04X\n",
                entry->timestamp_ms,
                label,
                entry->seq_num,
                entry->event_type,
                entry->value1,
                entry->value2,
                entry->flags,
                entry->category);
        } else {
            /* cppcheck-suppress invalidPrintfArgType_uint ; uint32_t/uint16_t are unsigned int compatible */
            len = snprintf(line, PT_LOG_LINE_MAX,
                "[%08u][INF] PERF seq=%u type=%u v1=%u v2=%u flags=0x%02X cat=0x%04X\n",
                entry->timestamp_ms,
                entry->seq_num,
                entry->event_type,
                entry->value1,
                entry->value2,
                entry->flags,
                entry->category);
        }

        write_line(log, line, len);
    }

    pthread_mutex_unlock(&log->mutex);
}

void PT_LogFlush(PT_Log *log) {
    if (!log) return;

    pthread_mutex_lock(&log->mutex);
    flush_to_file(log);
    if (log->file) {
        fflush(log->file);
    }
    pthread_mutex_unlock(&log->mutex);
}

/*============================================================================
 * Utilities
 *============================================================================*/

uint32_t PT_LogElapsedMs(PT_Log *log) {
    if (!log) return 0;

    struct timeval now;
    gettimeofday(&now, NULL);

    return (uint32_t)(
        (now.tv_sec - log->start_time.tv_sec) * 1000 +
        (now.tv_usec - log->start_time.tv_usec) / 1000
    );
}

uint32_t PT_LogNextSeq(PT_Log *log) {
    if (!log) return 0;

    uint32_t seq;
    pthread_mutex_lock(&log->mutex);
    seq = log->next_seq++;
    pthread_mutex_unlock(&log->mutex);

    return seq;
}

const char *PT_LogLevelName(PT_LogLevel level) {
    if (level < 0 || level > PT_LOG_DEBUG) {
        return "???";
    }
    return g_level_names[level];
}

const char *PT_LogVersion(void) {
    return g_version;
}

#endif  /* PT_LOG_STRIP */
