/*
 * PT_Log - Classic Mac Implementation
 *
 * For System 6.0.8 through Mac OS 9 using File Manager for file I/O.
 *
 * CRITICAL LIMITATIONS:
 * - vsprintf() has NO bounds checking - keep messages under 192 bytes
 * - TickCount() NOT ISR-safe - do NOT call PT_Log from interrupt handlers
 * - File Manager NOT ISR-safe - use "set flag, process later" pattern
 */

#include "../../include/pt_log.h"

#ifndef PT_LOG_STRIP

#include <Files.h>
#include <Memory.h>
#include <OSUtils.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/* Compile-time assertion for PT_LogPerfEntry size */
typedef char PT_LogPerfEntry_must_be_16_bytes[
    (sizeof(PT_LogPerfEntry) == 16) ? 1 : -1
];

/*============================================================================
 * Constants
 *
 * BUFFER SIZE RATIONALE (Classic Mac):
 * 256 bytes chosen to fit in 68030's 256-byte data cache:
 * - Reduces cache pollution on memory-constrained systems
 * - 68000 has no cache, but 256 bytes is still reasonable
 * - PowerPC Macs have larger caches but 256 is fine for logging
 *
 * LINE MAX CONSTRAINT:
 * 192 bytes is the HARD LIMIT for log messages after printf expansion.
 * Classic Mac vsprintf() has NO bounds checking - overflow will crash!
 *============================================================================*/

#define PT_LOG_BUFFER_SIZE  256
#define PT_LOG_LINE_MAX     192
#define PT_LOG_CREATOR      'PTLg'
#define PT_LOG_TYPE         'TEXT'
#define PT_LOG_INLINE_COPY_THRESHOLD  32

/*============================================================================
 * Log Context Structure
 *
 * Field ordering optimized for cache efficiency AND minimal padding:
 * - 4-byte fields first (pointers on Classic Mac are 4 bytes)
 * - 2-byte fields next
 * - 1-byte fields last (grouped to avoid padding)
 *
 * On 68030: First 32 bytes contain all hot fields (cache-friendly)
 *============================================================================*/

struct pt_log {
    /* === 4-BYTE FIELDS (pointers, longs) === */
    PT_LogCallback      msg_callback;   /* 4 bytes */
    void               *msg_user_data;  /* 4 bytes */
    PT_LogPerfCallback  perf_callback;  /* 4 bytes */
    void               *perf_user_data; /* 4 bytes */
    unsigned long       start_ticks;    /* 4 bytes - TickCount base */
    uint32_t            next_seq;       /* 4 bytes */

    /* === 2-BYTE FIELDS === */
    int16_t             buffer_pos;     /* 2 bytes - max 256 */
    uint16_t            categories;     /* 2 bytes */
    short               file_refnum;    /* 2 bytes - File Manager refnum */

    /* === 1-BYTE FIELDS (grouped) === */
    uint8_t             level;          /* 1 byte - stores PT_LogLevel */
    uint8_t             outputs;        /* 1 byte */
    uint8_t             auto_flush;     /* 1 byte */
    uint8_t             _pad;           /* 1 byte - explicit padding */

    /* === BUFFER: At end to avoid cache pollution === */
    char                buffer[PT_LOG_BUFFER_SIZE];  /* 256 bytes */
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
 * C String to Pascal String Conversion
 *============================================================================*/

static void c_to_pstr(const char *c_str, Str255 p_str) {
    int len = 0;
    while (c_str[len] && len < 255) {
        p_str[len + 1] = c_str[len];
        len++;
    }
    p_str[0] = (unsigned char)len;
}

/*============================================================================
 * File Manager Helpers (Parameter Block Versions)
 *
 * These use low-level PB calls instead of high-level glue routines
 * for maximum compatibility across Classic Mac toolboxes.
 *============================================================================*/

static OSErr pt_create_file(ConstStr255Param name, short vRefNum,
                            OSType creator, OSType fileType) {
    HParamBlockRec pb;
    OSErr err;

    memset(&pb, 0, sizeof(pb));
    pb.fileParam.ioNamePtr = (StringPtr)name;
    pb.fileParam.ioVRefNum = vRefNum;
    pb.fileParam.ioDirID = 0;
    err = PBHCreateSync(&pb);

    (void)creator;
    (void)fileType;

    return err;
}

static OSErr pt_set_file_info(ConstStr255Param name, short vRefNum,
                               OSType creator, OSType fileType) {
    HParamBlockRec pb;
    OSErr err;

    memset(&pb, 0, sizeof(pb));
    pb.fileParam.ioNamePtr = (StringPtr)name;
    pb.fileParam.ioVRefNum = vRefNum;
    pb.fileParam.ioFDirIndex = 0;
    pb.fileParam.ioDirID = 0;

    err = PBHGetFInfoSync(&pb);
    if (err != noErr) return err;

    pb.fileParam.ioFlFndrInfo.fdType = fileType;
    pb.fileParam.ioFlFndrInfo.fdCreator = creator;

    return PBHSetFInfoSync(&pb);
}

static OSErr pt_seek_to_end(short refNum) {
    ParamBlockRec pb;

    memset(&pb, 0, sizeof(pb));
    pb.ioParam.ioRefNum = refNum;
    pb.ioParam.ioPosMode = fsFromLEOF;
    pb.ioParam.ioPosOffset = 0;

    return PBSetFPosSync(&pb);
}

/*============================================================================
 * Lifecycle
 *============================================================================*/

PT_Log *PT_LogCreate(void) {
    PT_Log *log;

    log = (PT_Log *)NewPtrClear(sizeof(PT_Log));
    if (!log) return NULL;

    /* Defaults */
    log->level = PT_LOG_INFO;
    log->categories = PT_LOG_CAT_ALL;
    log->outputs = PT_LOG_OUT_CONSOLE;
    log->auto_flush = 0;
    log->next_seq = 1;
    log->file_refnum = 0;

    /* Record start time in ticks (60ths of a second) */
    log->start_ticks = TickCount();

    return log;
}

void PT_LogDestroy(PT_Log *log) {
    if (!log) return;

    PT_LogFlush(log);

    if (log->file_refnum) {
        FSClose(log->file_refnum);
    }

    DisposePtr((Ptr)log);
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
    Str255 pname;
    OSErr err;
    short refnum;

    if (!log) return -1;

    /* Close existing file */
    if (log->file_refnum) {
        FSClose(log->file_refnum);
        log->file_refnum = 0;
    }

    if (!filename) return 0;

    c_to_pstr(filename, pname);

    /* Create file if it doesn't exist (ignore dupFNErr if it does) */
    err = pt_create_file(pname, 0, PT_LOG_CREATOR, PT_LOG_TYPE);
    if (err != noErr && err != dupFNErr) {
        return -1;
    }

    /* Set file type/creator */
    if (err == noErr) {
        pt_set_file_info(pname, 0, PT_LOG_CREATOR, PT_LOG_TYPE);
    }

    /* Open for writing */
    err = FSOpen(pname, 0, &refnum);
    if (err != noErr) {
        return -1;
    }

    /* Seek to end for append (preserves existing log content) */
    pt_seek_to_end(refnum);

    log->file_refnum = refnum;
    log->outputs |= PT_LOG_OUT_FILE;

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
 * Utilities
 *============================================================================*/

uint32_t PT_LogElapsedMs(PT_Log *log) {
    unsigned long now;
    unsigned long elapsed_ticks;

    if (!log) return 0;

    now = TickCount();
    elapsed_ticks = now - log->start_ticks;

    /* Convert ticks (60ths of second) to milliseconds.
     * 68000/68010 have NO hardware divide - use shift-based approximation.
     * ticks * 16.625 = (ticks << 4) + (ticks >> 1) + (ticks >> 3)
     * Error: -0.25% (acceptable for logging) */
#if defined(__MC68000__) || defined(__MC68010__)
    return (uint32_t)((elapsed_ticks << 4) + (elapsed_ticks >> 1) + (elapsed_ticks >> 3));
#else
    /* PowerPC or 68020+: division is available */
    return (uint32_t)((elapsed_ticks * 1000UL) / 60UL);
#endif
}

uint32_t PT_LogNextSeq(PT_Log *log) {
    if (!log) return 0;
    return log->next_seq++;
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

/*============================================================================
 * Internal Helpers
 *============================================================================*/

static void flush_buffer(PT_Log *log) {
    if (log->file_refnum && log->buffer_pos > 0) {
        long count = log->buffer_pos;
        FSWrite(log->file_refnum, &count, log->buffer);
        log->buffer_pos = 0;
    }
}

static void write_to_buffer(PT_Log *log, const char *str, int len) {
    int i;
    char *dst;
    const char *src;

    /* Buffer full? Flush it */
    if (log->buffer_pos + len >= PT_LOG_BUFFER_SIZE) {
        flush_buffer(log);
    }

    /* Still doesn't fit? Write directly */
    if (len >= PT_LOG_BUFFER_SIZE) {
        if (log->file_refnum) {
            long count = len;
            FSWrite(log->file_refnum, &count, str);
        }
        return;
    }

    /* Inline copy for small strings (faster than BlockMoveData on 68k) */
    if (len <= PT_LOG_INLINE_COPY_THRESHOLD) {
        dst = log->buffer + log->buffer_pos;
        src = str;
        for (i = 0; i < len; i++) {
            dst[i] = src[i];
        }
    } else {
        BlockMoveData(str, log->buffer + log->buffer_pos, len);
    }

    log->buffer_pos += len;
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
    if (!log || !fmt) return;

    /* Filter by level (early exit before allocation) */
    if (level > log->level) return;

    /* Filter by category (early exit before allocation) */
    if (!(category & log->categories)) return;

    /* NOW allocate stack buffers (only if passing filters) */
    {
        char line[PT_LOG_LINE_MAX];
        char formatted[PT_LOG_LINE_MAX];
        int len;
        uint32_t timestamp;
        const char *level_name;

        timestamp = PT_LogElapsedMs(log);
        level_name = PT_LogLevelName(level);

        /* Use vsprintf - Classic Mac has NO bounds checking!
         * User MUST keep messages under 192 bytes after expansion.
         * This is a documented platform limitation. */
        vsprintf(formatted, fmt, args);

        /* Ensure null termination and leave room for prefix (safety) */
        formatted[160] = '\0';  /* Max formatted msg: 160 bytes + 15-byte prefix = 175 total */

        /* Disable format-overflow warning - this is a documented constraint */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-overflow"
        /* Use sprintf RETURN VALUE (not strlen) */
        len = sprintf(line, "[%08lu][%s] %s\r",
                      (unsigned long)timestamp, level_name, formatted);
#pragma GCC diagnostic pop

        /* Write to outputs */
        if (log->outputs & PT_LOG_OUT_FILE) {
            write_to_buffer(log, line, len);
            if (log->auto_flush) {
                flush_buffer(log);
            }
        }

        /* Console output is a no-op on Classic Mac (use callback for UI) */

        /* Callback dispatch (pass formatted message, NOT line with prefix) */
        if ((log->outputs & PT_LOG_OUT_CALLBACK) && log->msg_callback) {
            log->msg_callback(level, category, timestamp, formatted, log->msg_user_data);
        }
    }
}

void PT_LogPerf(PT_Log *log, const PT_LogPerfEntry *entry, const char *label) {
    if (!log || !entry) return;

    /* Filter by category */
    if (!(entry->category & log->categories)) return;

    /* Callback dispatch */
    if (log->perf_callback) {
        log->perf_callback(entry, label, log->perf_user_data);
    }

    /* Text output (optional) */
    if ((log->outputs & PT_LOG_OUT_FILE) && (log->level >= PT_LOG_INFO)) {
        char line[PT_LOG_LINE_MAX];
        int len;

        /* Use sprintf return value instead of strlen() */
        if (label && *label) {
            len = sprintf(line,
                "[%08lu][INF] PERF %s: seq=%lu type=%u v1=%u v2=%u flags=0x%02X cat=0x%04X\r",
                (unsigned long)entry->timestamp_ms,
                label,
                (unsigned long)entry->seq_num,
                (unsigned)entry->event_type,
                (unsigned)entry->value1,
                (unsigned)entry->value2,
                (unsigned)entry->flags,
                (unsigned)entry->category);
        } else {
            len = sprintf(line,
                "[%08lu][INF] PERF seq=%lu type=%u v1=%u v2=%u flags=0x%02X cat=0x%04X\r",
                (unsigned long)entry->timestamp_ms,
                (unsigned long)entry->seq_num,
                (unsigned)entry->event_type,
                (unsigned)entry->value1,
                (unsigned)entry->value2,
                (unsigned)entry->flags,
                (unsigned)entry->category);
        }

        write_to_buffer(log, line, len);
        if (log->auto_flush) {
            flush_buffer(log);
        }
    }
}

void PT_LogFlush(PT_Log *log) {
    if (!log) return;
    flush_buffer(log);
}

#endif  /* PT_LOG_STRIP */
