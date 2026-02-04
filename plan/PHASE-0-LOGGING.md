# PHASE 0: PT_Log - Cross-Platform Logging Library

> **Status:** [DONE] ✓
> **Depends on:** None
> **Produces:** Standalone logging library for POSIX and Classic Mac
> **Risk Level:** Low
> **Estimated Sessions:** 4
> **Implementation Complete:** 2026-02-04 - All sessions implemented, tested, and integrated. POSIX and Mac implementations present in src/log/. PT_LogCreate() integrated into PeerTalk_Init().
> **Review Applied:** 2026-01-30 - All claims re-verified; added ISR-safety guard macro, error logging checklist, PT_LogPerf decision matrix, initialization logging guidance, callback error contract

## Fact-Check Summary

| Claim | Source | Status |
|-------|--------|--------|
| TickCount() not in Table B-3 | Inside Macintosh Vol VI, line 224515+ | ✓ Verified |
| File Manager not interrupt-safe | Inside Macintosh Vol VI Table B-3 | ✓ Verified |
| BlockMove not in Table B-3 | Inside Macintosh Vol VI Table B-3 | ✓ Verified (conservative approach correct) |
| ASR cannot allocate/free memory | MacTCP Programmer's Guide, line 4230 | ✓ Verified |
| ASR can issue async MacTCP calls | MacTCP Programmer's Guide, line 4232 | ✓ Verified |
| OT notifiers run at deferred task time | NetworkingOpenTransport.txt, page 5793 | ✓ Verified |
| OTAtomicSetBit interrupt-safe | NetworkingOpenTransport.txt Table C-1 | ✓ Verified |
| ADSP userFlags must be cleared | Programming With AppleTalk (1991), p.5780 | ✓ Verified |
| 68030 data cache 256 bytes | Hardware specification | ✓ Verified |
| PT_LogPerfEntry 16 bytes | Compile-time assertion | ✓ Verified |
| vsnprintf via Retro68 newlib | Retro68/gcc/newlib/libc/include/stdio.h | ✓ Verified |
| OT notifier reentrancy possible | NetworkingOpenTransport.txt p.5822-5826 | ✓ Verified |
| OTAllocMem may return NULL | NetworkingOpenTransport.txt p.9143-9148 | ✓ Verified |
| ADSP completion runs at interrupt | Programming With AppleTalk p.1554-1558 | ✓ Verified |

## Overview

PT_Log is a standalone cross-platform logging library that ships with PeerTalk but can be used independently by any C application. It provides structured logging with level filtering, category filtering, multiple output destinations, and zero platform-specific code for developers.

**Why a separate phase?** PT_Log is a standalone product, not just PeerTalk infrastructure. Applications can use PT_Log without using PeerTalk's networking features. This phase establishes it as a first-class library before PeerTalk builds on top of it.

**Target Platforms:**
- POSIX (Linux, macOS, BSD)
- Classic Mac (System 6.0.8 through Mac OS 9)

## Goals

1. Create a standalone logging library with no PeerTalk dependencies
2. Support file output, console output, and custom callbacks
3. Provide level and category filtering at runtime
4. Include structured performance logging for benchmarking
5. Work in both debug and production builds (compile-time stripping is opt-in)
6. Zero overhead when stripped; minimal overhead when filtered at runtime
7. Cache-friendly data structures optimized for Classic Mac (68030: 256-byte cache)
8. Defer expensive operations (stack allocation, formatting) until after filtering

## Design Principles

### Production-Ready by Default

Unlike many logging libraries that compile out in release builds, PT_Log is **included by default**. Production applications often need logging for:
- Error reporting and diagnostics
- Crash analysis on customer machines
- Performance monitoring
- Audit trails

**Compile-time control:**

| Define | Behavior |
|--------|----------|
| (none) | Full logging support, runtime level control |
| `PT_LOG_STRIP` | All logging code removed, macros expand to nothing |
| `PT_LOG_MIN_LEVEL=n` | Only include code for level ≤ n (optional optimization) |

**Runtime control:**
- `PT_LogSetLevel()` - Set minimum level (ERR, WARN, INFO, DEBUG)
- `PT_LogSetCategories()` - Enable/disable category bitmask
- `PT_LogSetOutput()` - Choose destinations (file, console, callback)

### Memory Efficiency

Classic Macs have limited RAM. PT_Log uses:
- Small fixed buffers (256 bytes on Classic Mac, 512 bytes on POSIX)
- No dynamic allocation after initialization
- Immediate flush option for crash resilience
- Optional compile-time stripping for minimal builds

### Cache-Friendly Design (Data-Oriented)

PT_Log structures are designed for CPU cache efficiency:

**Hot path optimization:**
- Frequently-accessed fields (`level`, `categories`, `outputs`, `buffer_pos`) grouped at struct start
- Filter checks happen before any expensive operations
- Stack allocation deferred until after filtering

**Platform-specific sizing:**
- 68030 data cache is only 256 bytes - buffer sized accordingly
- 68000 has no cache - minimize memory accesses
- Modern systems get larger buffers for throughput

**Message length constraint:**
- `PT_LOG_LINE_MAX` is 256 bytes (POSIX) or 192 bytes (Classic Mac)
- Classic Mac uses `vsprintf()` (no bounds checking) - messages MUST stay under limit
- Document this constraint prominently in API

## Session Scope Table

| Session | Focus | Status | Files Created/Modified | Tests | Verify |
|---------|-------|--------|------------------------|-------|--------|
| 0.1 | Core API & Types | [DONE] | `include/pt_log.h` | None | Header compiles; PT_LogPerfEntry.category field present |
| 0.2 | POSIX Implementation | [DONE] | `src/log/pt_log_posix.c` | `tests/test_log_posix.c` | Logs to file/console; hot fields first; valgrind clean |
| 0.3.1 | Classic Mac Implementation | [DONE] | `src/log/pt_log_mac.c` | None | 256-byte buffer; inline copy; sprintf retval; real Mac test |
| 0.3.2 | Retro68 Build Config | [DONE] | `src/log/CMakeLists.txt` | None | Builds for 68k and PPC with Retro68 |
| 0.4 | Callbacks & Performance | [DONE] | `tests/test_log_perf.c`, `Makefile` | `tests/test_log_perf.c` | Callbacks fire; perf category filtering works |

### Status Key
- **[OPEN]** - Not started
- **[IN PROGRESS]** - Currently being worked on
- **[READY TO TEST]** - Implementation complete, needs verification
- **[DONE]** - Verified and complete

---

## Session 0.1: Core API & Types

### Objective
Define the complete public API for PT_Log in a single header file.

### Tasks

#### Task 0.1.1: Create `include/pt_log.h`

```c
/*
 * PT_Log - Cross-Platform C Logging Library
 *
 * A standalone logging library that works on POSIX and Classic Mac.
 * Used by PeerTalk internally, but can be used independently by any
 * C application.
 *
 * Features:
 * - Level filtering (ERR, WARN, INFO, DEBUG)
 * - Category filtering (bitmask, app-extensible)
 * - Multiple outputs (file, console, callback)
 * - Structured performance logging
 * - Production-ready (included by default, opt-in stripping)
 * - Zero overhead when stripped
 *
 * Quick Start:
 *
 *     PT_Log *log = PT_LogCreate();
 *     PT_LogSetFile(log, "myapp.log");
 *     PT_LOG_INFO(log, PT_LOG_CAT_APP1, "Application started");
 *     PT_LogDestroy(log);
 *
 * Build Options:
 *
 *     -DPT_LOG_STRIP         Remove all logging code (zero overhead)
 *     -DPT_LOG_MIN_LEVEL=2   Only include ERR and WARN (optional)
 *
 * THREAD SAFETY:
 *
 * POSIX: PT_Log is thread-safe. All public functions serialize access via
 * pthread_mutex. Multiple threads can call PT_LogWrite() simultaneously
 * without corruption.
 *
 * Classic Mac: PT_Log is NOT thread-safe but doesn't need to be - Classic
 * Mac is single-threaded (cooperative multitasking). No mutex overhead.
 *
 * INTERRUPT SAFETY (CRITICAL for Classic Mac):
 *
 * PT_Log is NOT interrupt-safe. Do not call any PT_Log functions from:
 * - MacTCP ASR callbacks
 * - Open Transport notifiers
 * - ADSP completion routines (ioCompletion or userRoutine)
 * - Time Manager or VBL tasks
 *
 * TickCount() and File Manager calls are not in Inside Macintosh Volume VI
 * Table B-3 ("Routines That May Be Called at Interrupt Time").
 *
 * Instead, set volatile flags in your interrupt handler and call PT_Log
 * functions from your main event loop. See ISR-Safety section below.
 *
 * MESSAGE LENGTH CONSTRAINT (Classic Mac):
 * Log messages should stay under PT_LOG_LINE_MAX (192 bytes) after printf
 * expansion. Longer messages are safely truncated (not a crash).
 *
 * TOOLCHAIN NOTE:
 * PeerTalk uses Retro68 which provides vsnprintf() via newlib, so buffer
 * overflow is prevented automatically. The 192-byte limit exists because:
 * - Smaller buffers save RAM on memory-constrained Classic Macs
 * - Fits in 68030's 256-byte data cache with room for metadata
 * - Long log messages are typically a sign of poor logging practice
 *
 * Legacy note: MPW/CodeWarrior/THINK C only provide vsprintf() without
 * bounds checking. If porting to those toolchains, message length becomes
 * a hard safety requirement.
 *
 * CONTEXT OWNERSHIP:
 *
 * PT_Log contexts are reference-counted by ownership, not by call count.
 * The creator of a PT_Log context is responsible for destroying it.
 *
 * When integrating with PeerTalk (Phase 1+):
 * - Option 1: Let PeerTalk create the log context (default)
 *     PeerTalk_Config config = {0};  // log = NULL means PeerTalk creates one
 *     ctx = PeerTalk_Init(&config);
 *     // PeerTalk owns the log, destroys it in PeerTalk_Shutdown()
 *
 * - Option 2: Provide your own log context
 *     PT_Log *my_log = PT_LogCreate();
 *     PT_LogSetFile(my_log, "app.log");
 *     PeerTalk_Config config = { .log = my_log };
 *     ctx = PeerTalk_Init(&config);
 *     // YOU own the log - PeerTalk will NOT destroy it
 *     PeerTalk_Shutdown(ctx);
 *     PT_LogDestroy(my_log);  // You must destroy it
 *
 * Standalone usage (without PeerTalk):
 *     PT_Log *log = PT_LogCreate();
 *     // ... use log ...
 *     PT_LogDestroy(log);  // Always destroy what you create
 *
 * RECOMMENDED LOG LEVELS:
 * - Production: PT_LOG_WARN (shows errors and warnings)
 * - Development: PT_LOG_DEBUG (shows everything)
 * - High-performance: PT_LOG_ERR (errors only, minimal overhead)
 */

#ifndef PT_LOG_H
#define PT_LOG_H

/*============================================================================
 * Portability: stdint.h types
 *
 * Retro68 provides stdint.h via newlib. For maximum portability with older
 * compilers (e.g., MPW, CodeWarrior), we provide fallback typedefs.
 *============================================================================*/
#if defined(__MWERKS__) || defined(THINK_C) || defined(MPW_C)
  /* Classic Mac compilers without C99 stdint.h */
  typedef unsigned char   uint8_t;
  typedef unsigned short  uint16_t;
  typedef unsigned long   uint32_t;
  typedef signed long     int32_t;
#else
  #include <stdint.h>
#endif

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Version
 *============================================================================*/

#define PT_LOG_VERSION_MAJOR    1
#define PT_LOG_VERSION_MINOR    0
#define PT_LOG_VERSION_PATCH    0

/*============================================================================
 * Opaque Log Context
 *============================================================================*/

typedef struct pt_log PT_Log;

/*============================================================================
 * Log Levels
 *
 * Levels are hierarchical: setting level to WARN shows ERR and WARN.
 *============================================================================*/

typedef enum {
    PT_LOG_NONE  = 0,   /* Logging disabled */
    PT_LOG_ERR   = 1,   /* Errors: failures that affect functionality */
    PT_LOG_WARN  = 2,   /* Warnings: issues that may cause problems */
    PT_LOG_INFO  = 3,   /* Info: normal operational messages */
    PT_LOG_DEBUG = 4    /* Debug: verbose diagnostic information */
} PT_LogLevel;

/*============================================================================
 * Log Categories (bitmask)
 *
 * Categories allow filtering by subsystem. Multiple categories can be
 * enabled simultaneously using bitwise OR.
 *
 * Categories 0x0001-0x07FF are reserved for PT_Log and PeerTalk.
 * Applications should use PT_LOG_CAT_APP1 through PT_LOG_CAT_APP5
 * (0x0800-0x8000) for their own categories.
 *============================================================================*/

typedef enum {
    /* Reserved for PT_Log / PeerTalk internal use */
    PT_LOG_CAT_GENERAL   = 0x0001,  /* General messages (use for app-level only; PeerTalk uses specific categories) */
    PT_LOG_CAT_NETWORK   = 0x0002,  /* Network operations (connections, data transfer) */
    PT_LOG_CAT_PROTOCOL  = 0x0004,  /* Protocol encoding/decoding */
    PT_LOG_CAT_MEMORY    = 0x0008,  /* Memory allocation/deallocation */
    PT_LOG_CAT_PLATFORM  = 0x0010,  /* Platform-specific operations (MacTCP, OT, POSIX) */
    PT_LOG_CAT_PERF      = 0x0020,  /* Performance/timing data */
    PT_LOG_CAT_CONNECT   = 0x0040,  /* Connection lifecycle (peer create/destroy/connect/disconnect) */
    PT_LOG_CAT_DISCOVERY = 0x0080,  /* Discovery operations (UDP broadcast, NBP lookup) */
    PT_LOG_CAT_SEND      = 0x0100,  /* Send operations (TCP/UDP/ADSP transmit) */
    PT_LOG_CAT_RECV      = 0x0200,  /* Receive operations (TCP/UDP/ADSP receive) */
    PT_LOG_CAT_INIT      = 0x0400,  /* Initialization/startup/shutdown */

    /* Available for applications (shifted to make room for PeerTalk categories) */
    PT_LOG_CAT_APP1      = 0x0800,
    PT_LOG_CAT_APP2      = 0x1000,
    PT_LOG_CAT_APP3      = 0x2000,
    PT_LOG_CAT_APP4      = 0x4000,
    PT_LOG_CAT_APP5      = 0x8000,
    /* Note: APP6-APP8 removed due to 16-bit bitmask limit.
     * If more app categories needed, consider using a 32-bit bitmask. */

    /* All categories enabled */
    PT_LOG_CAT_ALL       = 0xFFFF
} PT_LogCategory;

/*============================================================================
 * Output Destinations (bitmask)
 *
 * Multiple destinations can be enabled simultaneously.
 *============================================================================*/

typedef enum {
    PT_LOG_OUT_NONE     = 0x00,  /* No output (useful for perf-only logging) */
    PT_LOG_OUT_FILE     = 0x01,  /* Write to file (set via PT_LogSetFile) */
    PT_LOG_OUT_CONSOLE  = 0x02,  /* POSIX: stderr; Classic Mac: no-op (use callback for UI) */
    PT_LOG_OUT_CALLBACK = 0x04   /* All platforms: custom display via callback */
} PT_LogOutput;

/*
 * PLATFORM OUTPUT DIFFERENCES:
 *
 * POSIX (Linux/macOS):
 *   - PT_LOG_OUT_FILE: Writes to specified file path
 *   - PT_LOG_OUT_CONSOLE: Writes to stderr (always available)
 *   - PT_LOG_OUT_CALLBACK: Calls your function for custom handling
 *
 * Classic Mac (System 6-9):
 *   - PT_LOG_OUT_FILE: Writes to specified file (File Manager)
 *   - PT_LOG_OUT_CONSOLE: No-op (Classic Mac has no console/stderr)
 *   - PT_LOG_OUT_CALLBACK: Calls your function - USE THIS FOR UI DISPLAY
 *
 * For Classic Mac UI display, use PT_LOG_OUT_CALLBACK and implement a
 * callback that draws to a TextEdit field, list, or scrolling window.
 */

/*============================================================================
 * Callback Types
 *============================================================================*/

/*
 * Message callback - called for each log message when PT_LOG_OUT_CALLBACK
 * is enabled. Use this to redirect logs to your own UI, remote server, etc.
 *
 * Parameters:
 *   level       - The log level (PT_LOG_ERR, etc.)
 *   category    - The category bitmask
 *   timestamp_ms - Milliseconds since PT_LogCreate()
 *   message     - The formatted message (null-terminated)
 *   user_data   - Your context pointer from PT_LogSetCallback()
 *
 * CALLBACK CONTRACT:
 * - Callbacks are called synchronously from PT_LogWrite()
 * - Keep handlers fast - every callback adds to logging latency
 * - Callbacks should NOT fail; if they do, the message is discarded
 * - PT_Log does not track callback failures or disable on repeated errors
 * - For UI logging, consider buffering in your callback and batch-updating:
 *
 *       void my_callback(...) {
 *           ring_buffer_push(message);  // Fast - just buffer
 *           g_log_dirty = true;
 *       }
 *
 *       // In main event loop:
 *       if (g_log_dirty) {
 *           update_textedit_from_buffer();
 *           g_log_dirty = false;
 *       }
 *
 * WARNING: Do NOT call PT_Log functions from within the callback.
 * On POSIX, PT_LogWrite() holds a mutex during callback dispatch - calling
 * PT_Log from inside the callback will cause mutex deadlock.
 */
typedef void (*PT_LogCallback)(
    PT_LogLevel     level,
    PT_LogCategory  category,
    uint32_t        timestamp_ms,
    const char     *message,
    void           *user_data
);

/*============================================================================
 * Structured Performance Entry
 *
 * For detailed performance analysis and benchmarking. Performance entries
 * are separate from text logging and can be:
 * - Written to the text log (when PT_LOG_CAT_PERF is enabled globally)
 * - Captured via callback for custom processing
 * - Used for post-session analysis
 *
 * Fields are application-defined. Suggested uses:
 *   event_type  - What happened (e.g., SEND, RECV, CONNECT, RENDER)
 *   value1/2    - Measurements (e.g., bytes, count, duration_us)
 *   flags       - Status flags (e.g., success/failure, TCP/UDP)
 *   category    - METADATA for callback receivers (NOT used for filtering)
 *
 * IMPORTANT: The category field is passed through to callbacks for their
 * use in categorizing/filtering collected entries. It does NOT affect
 * whether PT_LogPerf() writes to the text log - that is controlled by
 * whether PT_LOG_CAT_PERF is enabled globally via PT_LogSetCategories().
 *
 * Memory layout: 16 bytes total, no internal padding, power-of-2 aligned.
 * This is optimal for batch processing and cache efficiency:
 * - 16 entries fit in a 68030 data cache line (256 bytes)
 * - 4 entries fit in a modern 64-byte cache line
 *============================================================================*/

/*
 * Packing directives ensure exactly 16 bytes with no compiler-added padding.
 * The size assertion in tests/test_log_perf.c verifies this at compile time.
 */
#if defined(__GNUC__) || defined(__clang__)
  #define PT_LOG_PACKED __attribute__((packed))
#elif defined(__MWERKS__)
  #pragma options align=packed
  #define PT_LOG_PACKED
#else
  #define PT_LOG_PACKED
#endif

typedef struct PT_LOG_PACKED {
    uint32_t    seq_num;        /* Sequence number (from PT_LogNextSeq) */
    uint32_t    timestamp_ms;   /* Milliseconds since PT_LogCreate() */
    uint16_t    value1;         /* Application-defined value */
    uint16_t    value2;         /* Application-defined value */
    uint8_t     event_type;     /* Application-defined event type */
    uint8_t     flags;          /* Application-defined flags */
    uint16_t    category;       /* Metadata for callback receivers (NOT for text log filtering) */
} PT_LogPerfEntry;

#if defined(__MWERKS__)
  #pragma options align=reset
#endif

/*
 * COMPILE-TIME SIZE ASSERTION:
 * PT_LogPerfEntry MUST be exactly 16 bytes for cache efficiency.
 * - 16 entries fit in 68030 data cache (256 bytes)
 * - 4 entries fit in modern 64-byte cache line
 *
 * C11 _Static_assert or C99 array-size trick for compile-time check.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  _Static_assert(sizeof(PT_LogPerfEntry) == 16,
      "PT_LogPerfEntry must be exactly 16 bytes for cache efficiency");
#elif defined(__GNUC__) || defined(__clang__)
  /* GCC/Clang extension for pre-C11 */
  typedef char _pt_log_perf_entry_size_check[(sizeof(PT_LogPerfEntry) == 16) ? 1 : -1];
#elif defined(__MWERKS__) || defined(THINK_C)
  /* CodeWarrior/THINK C: negative array size causes compile error if size wrong */
  typedef char _pt_log_perf_entry_size_check[(sizeof(PT_LogPerfEntry) == 16) ? 1 : -1];
#endif

/*
 * Performance callback - called for each PT_LogPerf() entry.
 * Use this to collect structured data for analysis.
 */
typedef void (*PT_LogPerfCallback)(
    const PT_LogPerfEntry *entry,
    const char            *label,   /* Optional label from PT_LogPerf() */
    void                  *user_data
);

/*============================================================================
 * Lifecycle Functions
 *============================================================================*/

/*
 * Create a new log context.
 *
 * Returns NULL on allocation failure (rare on POSIX, possible on Classic Mac).
 * Default settings: level=INFO, categories=ALL, output=CONSOLE.
 */
PT_Log *PT_LogCreate(void);

/*
 * Destroy a log context.
 *
 * Flushes any buffered output and closes files. Safe to pass NULL.
 */
void PT_LogDestroy(PT_Log *log);

/*============================================================================
 * Configuration Functions
 *============================================================================*/

/*
 * Set minimum log level.
 *
 * Messages with level > min_level are filtered out.
 * Default: PT_LOG_INFO (shows ERR, WARN, INFO; hides DEBUG)
 *
 * Example - production build showing only errors:
 *     PT_LogSetLevel(log, PT_LOG_ERR);
 *
 * Example - debug build showing everything:
 *     PT_LogSetLevel(log, PT_LOG_DEBUG);
 */
void PT_LogSetLevel(PT_Log *log, PT_LogLevel level);

/*
 * Get current log level.
 */
PT_LogLevel PT_LogGetLevel(PT_Log *log);

/*
 * Set category filter bitmask.
 *
 * Only messages matching enabled categories are output.
 * Default: PT_LOG_CAT_ALL
 *
 * Example - only show network and protocol messages:
 *     PT_LogSetCategories(log, PT_LOG_CAT_NETWORK | PT_LOG_CAT_PROTOCOL);
 */
void PT_LogSetCategories(PT_Log *log, uint16_t categories);

/*
 * Get current category filter.
 */
uint16_t PT_LogGetCategories(PT_Log *log);

/*
 * Set output destinations (bitmask).
 *
 * Multiple destinations can be enabled simultaneously.
 * Default: PT_LOG_OUT_CONSOLE
 *
 * Example - log to file and callback, but not console:
 *     PT_LogSetOutput(log, PT_LOG_OUT_FILE | PT_LOG_OUT_CALLBACK);
 */
void PT_LogSetOutput(PT_Log *log, uint8_t outputs);

/*
 * Get current output destinations.
 */
uint8_t PT_LogGetOutput(PT_Log *log);

/*
 * Set output file.
 *
 * Opens the file for appending. On Classic Mac, creates the file with
 * creator 'PTLg' and type 'TEXT' if it doesn't exist.
 *
 * Returns 0 on success, -1 on error (file couldn't be opened/created).
 * Automatically enables PT_LOG_OUT_FILE if successful.
 *
 * Pass NULL to close the current file without opening a new one.
 */
int PT_LogSetFile(PT_Log *log, const char *filename);

/*
 * Set message callback.
 *
 * The callback is invoked for each log message when PT_LOG_OUT_CALLBACK
 * is enabled. Pass NULL to remove the callback.
 */
void PT_LogSetCallback(PT_Log *log, PT_LogCallback callback, void *user_data);

/*
 * Set performance entry callback.
 *
 * The callback is invoked for each PT_LogPerf() call.
 * Pass NULL to remove the callback.
 */
void PT_LogSetPerfCallback(PT_Log *log, PT_LogPerfCallback callback, void *user_data);

/*
 * Set whether to auto-flush after each message.
 *
 * When enabled, each log message is immediately written to file/console.
 * Useful for debugging crashes, but slower. Default: disabled (buffered).
 */
void PT_LogSetAutoFlush(PT_Log *log, int enabled);

/*============================================================================
 * Logging Functions
 *============================================================================*/

/*
 * Write a formatted log message.
 *
 * Uses printf-style formatting. Message is filtered by level and category
 * before being written to enabled outputs.
 *
 * Format string and arguments follow standard printf conventions.
 * On Classic Mac, %s expects C strings (not Pascal strings).
 */
void PT_LogWrite(PT_Log *log, PT_LogLevel level, PT_LogCategory category,
                 const char *fmt, ...);

/*
 * Write a formatted log message (va_list version).
 *
 * For wrapping PT_Log in your own variadic functions.
 */
void PT_LogWriteV(PT_Log *log, PT_LogLevel level, PT_LogCategory category,
                  const char *fmt, va_list args);

/*
 * Write a structured performance entry.
 *
 * The entry is passed to the performance callback (if set) and optionally
 * written to the text log with category PT_LOG_CAT_PERF.
 *
 * Label is optional (can be NULL) and provides context in text output.
 */
void PT_LogPerf(PT_Log *log, const PT_LogPerfEntry *entry, const char *label);

/*
 * Flush buffered output.
 *
 * Forces all buffered data to be written to file/console. Call this
 * before program exit or when you need output to be visible immediately.
 */
void PT_LogFlush(PT_Log *log);

/*============================================================================
 * Utility Functions
 *============================================================================*/

/*
 * Get milliseconds elapsed since PT_LogCreate().
 *
 * Useful for creating PT_LogPerfEntry timestamps or correlating events.
 * Uses TickCount() on Classic Mac (converted to ms), gettimeofday() on POSIX.
 */
uint32_t PT_LogElapsedMs(PT_Log *log);

/*
 * Get next sequence number.
 *
 * Returns a monotonically increasing number, starting from 1.
 * Useful for correlating log entries across sender/receiver or for ordering.
 */
uint32_t PT_LogNextSeq(PT_Log *log);

/*
 * Get human-readable level name.
 *
 * Returns "ERR", "WRN", "INF", "DBG", or "---" for NONE.
 */
const char *PT_LogLevelName(PT_LogLevel level);

/*
 * Get library version string.
 *
 * Returns a string like "1.0.0".
 */
const char *PT_LogVersion(void);

/*============================================================================
 * Convenience Macros
 *
 * These macros provide a cleaner syntax and automatically expand to nothing
 * when PT_LOG_STRIP is defined.
 *
 * Usage:
 *     PT_LOG_ERR(log, PT_LOG_CAT_NETWORK, "Connection failed: %d", errno);
 *     PT_LOG_INFO(log, PT_LOG_CAT_APP1, "User %s logged in", username);
 *
 * With category shorthand (define your own):
 *     #define LOG_NET PT_LOG_CAT_NETWORK
 *     PT_LOG_WARN(log, LOG_NET, "Packet dropped");
 *
 * ISR-SAFETY GUARD:
 *
 * Define PT_ISR_CONTEXT before including pt_log.h in files that contain
 * interrupt handlers (MacTCP ASR, OT notifiers, ADSP callbacks). This
 * disables all logging macros in that file, preventing accidental ISR calls.
 *
 *     #define PT_ISR_CONTEXT
 *     #include "pt_log.h"
 *
 * This allows static analysis and code review to verify no PT_Log calls
 * occur from interrupt context.
 *============================================================================*/

/*
 * ISR-safety guard: When PT_ISR_CONTEXT is defined, disable all logging
 * macros to catch accidental ISR-level PT_Log calls at compile time.
 */
#ifdef PT_ISR_CONTEXT

#define PT_LOG_ERR(log, cat, ...)   ((void)0)
#define PT_LOG_WARN(log, cat, ...)  ((void)0)
#define PT_LOG_INFO(log, cat, ...)  ((void)0)
#define PT_LOG_DEBUG(log, cat, ...) ((void)0)
#define PT_LOG_PERF(log, entry, label) ((void)0)

#elif !defined(PT_LOG_STRIP)

/* Default: logging enabled */

/* Check against optional compile-time minimum level */
#ifndef PT_LOG_MIN_LEVEL
#define PT_LOG_MIN_LEVEL 4  /* Include all levels by default */
#endif

#if PT_LOG_MIN_LEVEL >= 1
#define PT_LOG_ERR(log, cat, ...) \
    PT_LogWrite(log, PT_LOG_ERR, cat, __VA_ARGS__)
#else
#define PT_LOG_ERR(log, cat, ...) ((void)0)
#endif

#if PT_LOG_MIN_LEVEL >= 2
#define PT_LOG_WARN(log, cat, ...) \
    PT_LogWrite(log, PT_LOG_WARN, cat, __VA_ARGS__)
#else
#define PT_LOG_WARN(log, cat, ...) ((void)0)
#endif

#if PT_LOG_MIN_LEVEL >= 3
#define PT_LOG_INFO(log, cat, ...) \
    PT_LogWrite(log, PT_LOG_INFO, cat, __VA_ARGS__)
#else
#define PT_LOG_INFO(log, cat, ...) ((void)0)
#endif

#if PT_LOG_MIN_LEVEL >= 4
#define PT_LOG_DEBUG(log, cat, ...) \
    PT_LogWrite(log, PT_LOG_DEBUG, cat, __VA_ARGS__)
#else
#define PT_LOG_DEBUG(log, cat, ...) ((void)0)
#endif

/* Performance logging macro */
#define PT_LOG_PERF(log, entry, label) PT_LogPerf(log, entry, label)

#else /* PT_LOG_STRIP defined */

/* Logging stripped: all macros expand to nothing */
#define PT_LOG_ERR(log, cat, ...)   ((void)0)
#define PT_LOG_WARN(log, cat, ...)  ((void)0)
#define PT_LOG_INFO(log, cat, ...)  ((void)0)
#define PT_LOG_DEBUG(log, cat, ...) ((void)0)
#define PT_LOG_PERF(log, entry, label) ((void)0)

/* Stub functions when stripped (for code that calls functions directly) */
#define PT_LogCreate()                          ((PT_Log *)0)
#define PT_LogDestroy(log)                      ((void)0)
#define PT_LogSetLevel(log, level)              ((void)0)
#define PT_LogGetLevel(log)                     (PT_LOG_NONE)
#define PT_LogSetCategories(log, cats)          ((void)0)
#define PT_LogGetCategories(log)                (0)
#define PT_LogSetOutput(log, out)               ((void)0)
#define PT_LogGetOutput(log)                    (0)
#define PT_LogSetFile(log, fn)                  (0)
#define PT_LogSetCallback(log, cb, ud)          ((void)0)
#define PT_LogSetPerfCallback(log, cb, ud)      ((void)0)
#define PT_LogSetAutoFlush(log, en)             ((void)0)
#define PT_LogWrite(log, lv, cat, ...)          ((void)0)
#define PT_LogWriteV(log, lv, cat, fmt, args)   ((void)0)
#define PT_LogPerf(log, entry, label)           ((void)0)
#define PT_LogFlush(log)                        ((void)0)
#define PT_LogElapsedMs(log)                    (0)
#define PT_LogNextSeq(log)                      (0)

#endif /* PT_LOG_STRIP / PT_ISR_CONTEXT */

#ifdef __cplusplus
}
#endif

#endif /* PT_LOG_H */
```

### Verification
- [ ] Header compiles with `gcc -c -x c` on POSIX
- [ ] Header compiles with Retro68 for 68k
- [ ] Header compiles with Retro68 for PPC
- [ ] All types are defined and complete
- [ ] Macros expand correctly with and without PT_LOG_STRIP
- [ ] PT_ISR_CONTEXT disables all logging macros when defined

---

## Session 0.2: POSIX Implementation

### Objective
Implement PT_Log for POSIX systems (Linux, macOS, BSD) with full test coverage.

### Tasks

#### Task 0.2.1: Create `src/log/pt_log_posix.c`

```c
/*
 * PT_Log - POSIX Implementation
 */

#include "pt_log.h"

#ifndef PT_LOG_STRIP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <pthread.h>

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
    FILE           *file;           /* 8 bytes - file handle */
    PT_LogCallback  msg_callback;   /* 8 bytes - callback pointer */
    void           *msg_user_data;  /* 8 bytes - callback context */
    PT_LogPerfCallback  perf_callback;  /* 8 bytes */
    void               *perf_user_data; /* 8 bytes */
    struct timeval  start_time;     /* 16 bytes - timestamp base */

    /* === 4-BYTE FIELDS === */
    int             buffer_pos;     /* 4 bytes - buffer write position */
    uint32_t        next_seq;       /* 4 bytes - sequence counter */

    /* === 2-BYTE FIELDS === */
    uint16_t        categories;     /* 2 bytes - filter check */

    /* === 1-BYTE FIELDS (grouped to avoid padding) === */
    uint8_t         level;          /* 1 byte - stores PT_LogLevel enum value */
    uint8_t         outputs;        /* 1 byte  - output routing */
    uint8_t         auto_flush;     /* 1 byte  - flush decision */
    uint8_t         _pad[1];        /* 1 byte  - explicit padding for alignment */

    /* === MUTEX (platform-dependent size, 40-48 bytes) === */
    pthread_mutex_t mutex;

    /* === BUFFER: Large, at end to avoid polluting cache === */
    char            buffer[PT_LOG_BUFFER_SIZE];  /* 512 bytes */
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
    if (log) log->level = level;
}

PT_LogLevel PT_LogGetLevel(PT_Log *log) {
    return log ? log->level : PT_LOG_NONE;
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
    if (log) log->auto_flush = enabled;
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

uint32_t PT_LogElapsedMs(PT_Log *log) {
    struct timeval now;
    if (!log) return 0;

    gettimeofday(&now, NULL);

    uint32_t sec_diff = (uint32_t)(now.tv_sec - log->start_time.tv_sec);
    int32_t usec_diff = now.tv_usec - log->start_time.tv_usec;

    return sec_diff * 1000 + usec_diff / 1000;
}

uint32_t PT_LogNextSeq(PT_Log *log) {
    if (!log) return 0;
    /* Use atomic fetch-and-add for lock-free sequence numbers.
     * This is faster than mutex for this simple increment operation.
     * __atomic builtins are available in GCC 4.7+ and Clang 3.1+.
     *
     * NOTE: Using __ATOMIC_RELAXED (not SEQ_CST) because sequence numbers
     * don't need synchronization with other memory operations - they're
     * purely informational ordering aids. RELAXED is 5-10x faster on
     * weakly-ordered architectures (ARM, PowerPC). */
#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7))
    return __atomic_fetch_add(&log->next_seq, 1, __ATOMIC_RELAXED);
#else
    /* Fallback for older compilers */
    uint32_t seq;
    pthread_mutex_lock(&log->mutex);
    seq = log->next_seq++;
    pthread_mutex_unlock(&log->mutex);
    return seq;
#endif
}

const char *PT_LogLevelName(PT_LogLevel level) {
    /* Use unsigned comparison to handle negative values from bad casts */
    if ((unsigned)level > PT_LOG_DEBUG) return "???";
    return g_level_names[level];
}

const char *PT_LogVersion(void) {
    return g_version;
}

/*============================================================================
 * Output Helpers
 *============================================================================*/

static void flush_buffer(PT_Log *log) {
    if (log->buffer_pos == 0) return;

    if ((log->outputs & PT_LOG_OUT_FILE) && log->file) {
        fwrite(log->buffer, 1, log->buffer_pos, log->file);
        /* Only fflush when explicitly requested (auto_flush or PT_LogFlush).
         * Avoids unnecessary syscall overhead during normal buffered operation. */
    }

    if (log->outputs & PT_LOG_OUT_CONSOLE) {
        fwrite(log->buffer, 1, log->buffer_pos, stderr);
    }

    log->buffer_pos = 0;
}

/* Force sync to disk - called by PT_LogFlush or when auto_flush is enabled */
static void sync_file(PT_Log *log) {
    if ((log->outputs & PT_LOG_OUT_FILE) && log->file) {
        fflush(log->file);
    }
}

static void write_to_buffer(PT_Log *log, const char *str, int len) {
    /* Flush if needed */
    if (log->buffer_pos + len >= PT_LOG_BUFFER_SIZE) {
        flush_buffer(log);
    }

    /* If still too big, write directly */
    if (len >= PT_LOG_BUFFER_SIZE) {
        if ((log->outputs & PT_LOG_OUT_FILE) && log->file) {
            fwrite(str, 1, len, log->file);
        }
        if (log->outputs & PT_LOG_OUT_CONSOLE) {
            fwrite(str, 1, len, stderr);
        }
        return;
    }

    /* Add to buffer */
    memcpy(log->buffer + log->buffer_pos, str, len);
    log->buffer_pos += len;
}

/*============================================================================
 * Logging Functions
 *============================================================================*/

void PT_LogWrite(PT_Log *log, PT_LogLevel level, PT_LogCategory category,
                 const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    PT_LogWriteV(log, level, category, fmt, args);
    va_end(args);
}

void PT_LogWriteV(PT_Log *log, PT_LogLevel level, PT_LogCategory category,
                  const char *fmt, va_list args) {
    /* NOTE: Stack buffers declared AFTER filter checks to avoid
     * allocating 512 bytes on stack for filtered-out messages.
     * This is important on Classic Mac with limited stack space. */

    if (!log || !fmt) return;

    /* Filter by level - fast path exit */
    if (level > log->level) return;

    /* Filter by category - fast path exit */
    if (!(log->categories & category)) return;

    /* === PASSED FILTERS: Now allocate stack buffers === */
    {
        char line[PT_LOG_LINE_MAX];
        char formatted[PT_LOG_LINE_MAX];
        int len;
        uint32_t timestamp;

        pthread_mutex_lock(&log->mutex);

        timestamp = PT_LogElapsedMs(log);

        /* Format the user message */
        vsnprintf(formatted, sizeof(formatted), fmt, args);

        /* Format the full line with timestamp and level */
        /* Cast to unsigned long for portability with uint32_t */
        len = snprintf(line, sizeof(line), "[%08lu][%s] %s\n",
                       (unsigned long)timestamp, g_level_names[level], formatted);

        /* Write to file/console */
        if (log->outputs & (PT_LOG_OUT_FILE | PT_LOG_OUT_CONSOLE)) {
            write_to_buffer(log, line, len);
            if (log->auto_flush) {
                flush_buffer(log);
                sync_file(log);
            }
        }

        /* Call message callback */
        if ((log->outputs & PT_LOG_OUT_CALLBACK) && log->msg_callback) {
            log->msg_callback(level, category, timestamp, formatted, log->msg_user_data);
        }

        pthread_mutex_unlock(&log->mutex);
    }
}

void PT_LogPerf(PT_Log *log, const PT_LogPerfEntry *entry, const char *label) {
    if (!log || !entry) return;

    pthread_mutex_lock(&log->mutex);

    /* Call performance callback */
    if (log->perf_callback) {
        log->perf_callback(entry, label, log->perf_user_data);
    }

    /* Also write to text log if PERF category enabled.
     * Note: entry->category is metadata for callback receivers, not used for
     * filtering here. Global PT_LOG_CAT_PERF controls whether perf entries
     * appear in the text log. */
    if ((log->categories & PT_LOG_CAT_PERF) && (log->level >= PT_LOG_INFO)) {
        char line[PT_LOG_LINE_MAX];
        int len;

        if (label) {
            len = snprintf(line, sizeof(line),
                "[%08u][INF] PERF seq=%u type=%u v1=%u v2=%u flags=0x%02X cat=0x%04X %s\n",
                entry->timestamp_ms, entry->seq_num, entry->event_type,
                entry->value1, entry->value2, entry->flags, entry->category, label);
        } else {
            len = snprintf(line, sizeof(line),
                "[%08u][INF] PERF seq=%u type=%u v1=%u v2=%u flags=0x%02X cat=0x%04X\n",
                entry->timestamp_ms, entry->seq_num, entry->event_type,
                entry->value1, entry->value2, entry->flags, entry->category);
        }

        write_to_buffer(log, line, len);
        if (log->auto_flush) {
            flush_buffer(log);
            sync_file(log);
        }
    }

    pthread_mutex_unlock(&log->mutex);
}

void PT_LogFlush(PT_Log *log) {
    if (!log) return;

    pthread_mutex_lock(&log->mutex);
    flush_buffer(log);
    sync_file(log);
    pthread_mutex_unlock(&log->mutex);
}

#endif /* PT_LOG_STRIP */
```

#### Task 0.2.2: Create `tests/test_log_posix.c`

```c
/*
 * PT_Log POSIX Tests
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "pt_log.h"

static int g_callback_count = 0;
static PT_LogLevel g_last_level;
static char g_last_message[256];

static void test_callback(PT_LogLevel level, PT_LogCategory cat,
                          uint32_t ts, const char *msg, void *ud) {
    g_callback_count++;
    g_last_level = level;
    strncpy(g_last_message, msg, sizeof(g_last_message) - 1);
    (void)cat; (void)ts; (void)ud;
}

static int g_perf_count = 0;
static PT_LogPerfEntry g_last_perf;

static void test_perf_callback(const PT_LogPerfEntry *entry,
                               const char *label, void *ud) {
    g_perf_count++;
    g_last_perf = *entry;
    (void)label; (void)ud;
}

void test_create_destroy(void) {
    printf("  test_create_destroy...");

    PT_Log *log = PT_LogCreate();
    assert(log != NULL);

    /* Check defaults */
    assert(PT_LogGetLevel(log) == PT_LOG_INFO);
    assert(PT_LogGetCategories(log) == PT_LOG_CAT_ALL);
    assert(PT_LogGetOutput(log) == PT_LOG_OUT_CONSOLE);

    PT_LogDestroy(log);

    /* NULL should be safe */
    PT_LogDestroy(NULL);

    printf(" OK\n");
}

void test_level_filtering(void) {
    printf("  test_level_filtering...");

    PT_Log *log = PT_LogCreate();
    PT_LogSetOutput(log, PT_LOG_OUT_CALLBACK);
    PT_LogSetCallback(log, test_callback, NULL);

    /* Set level to WARN - should see ERR and WARN only */
    PT_LogSetLevel(log, PT_LOG_WARN);
    g_callback_count = 0;

    PT_LOG_ERR(log, PT_LOG_CAT_GENERAL, "error");
    assert(g_callback_count == 1);

    PT_LOG_WARN(log, PT_LOG_CAT_GENERAL, "warning");
    assert(g_callback_count == 2);

    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "info");
    assert(g_callback_count == 2);  /* Should NOT increase */

    PT_LOG_DEBUG(log, PT_LOG_CAT_GENERAL, "debug");
    assert(g_callback_count == 2);  /* Should NOT increase */

    /* Set level to DEBUG - should see all */
    PT_LogSetLevel(log, PT_LOG_DEBUG);
    g_callback_count = 0;

    PT_LOG_ERR(log, PT_LOG_CAT_GENERAL, "error");
    PT_LOG_WARN(log, PT_LOG_CAT_GENERAL, "warning");
    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "info");
    PT_LOG_DEBUG(log, PT_LOG_CAT_GENERAL, "debug");
    assert(g_callback_count == 4);

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_category_filtering(void) {
    printf("  test_category_filtering...");

    PT_Log *log = PT_LogCreate();
    PT_LogSetOutput(log, PT_LOG_OUT_CALLBACK);
    PT_LogSetCallback(log, test_callback, NULL);
    PT_LogSetLevel(log, PT_LOG_DEBUG);

    /* Only enable NETWORK category */
    PT_LogSetCategories(log, PT_LOG_CAT_NETWORK);
    g_callback_count = 0;

    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "general");
    assert(g_callback_count == 0);  /* Filtered */

    PT_LOG_INFO(log, PT_LOG_CAT_NETWORK, "network");
    assert(g_callback_count == 1);  /* Passed */

    PT_LOG_INFO(log, PT_LOG_CAT_PROTOCOL, "protocol");
    assert(g_callback_count == 1);  /* Filtered */

    /* Enable multiple categories */
    PT_LogSetCategories(log, PT_LOG_CAT_NETWORK | PT_LOG_CAT_PROTOCOL);
    g_callback_count = 0;

    PT_LOG_INFO(log, PT_LOG_CAT_NETWORK, "network");
    PT_LOG_INFO(log, PT_LOG_CAT_PROTOCOL, "protocol");
    assert(g_callback_count == 2);

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_formatting(void) {
    printf("  test_formatting...");

    PT_Log *log = PT_LogCreate();
    PT_LogSetOutput(log, PT_LOG_OUT_CALLBACK);
    PT_LogSetCallback(log, test_callback, NULL);
    PT_LogSetLevel(log, PT_LOG_DEBUG);

    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "Hello %s, count=%d", "world", 42);
    assert(strcmp(g_last_message, "Hello world, count=42") == 0);

    PT_LOG_ERR(log, PT_LOG_CAT_GENERAL, "Hex: 0x%08X", 0xDEADBEEF);
    assert(strcmp(g_last_message, "Hex: 0xDEADBEEF") == 0);
    assert(g_last_level == PT_LOG_ERR);

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_file_output(void) {
    printf("  test_file_output...");

    const char *filename = "/tmp/pt_log_test.log";
    unlink(filename);  /* Remove if exists */

    PT_Log *log = PT_LogCreate();
    assert(PT_LogSetFile(log, filename) == 0);
    PT_LogSetOutput(log, PT_LOG_OUT_FILE);

    PT_LOG_INFO(log, PT_LOG_CAT_GENERAL, "Test message to file");
    PT_LogFlush(log);
    PT_LogDestroy(log);

    /* Verify file was written */
    FILE *f = fopen(filename, "r");
    assert(f != NULL);
    char buf[256];
    assert(fgets(buf, sizeof(buf), f) != NULL);
    assert(strstr(buf, "Test message to file") != NULL);
    fclose(f);

    unlink(filename);
    printf(" OK\n");
}

void test_elapsed_time(void) {
    printf("  test_elapsed_time...");

    PT_Log *log = PT_LogCreate();

    uint32_t t1 = PT_LogElapsedMs(log);
    usleep(50000);  /* 50ms */
    uint32_t t2 = PT_LogElapsedMs(log);

    assert(t2 > t1);
    assert(t2 - t1 >= 40);  /* Allow some tolerance */
    assert(t2 - t1 < 200);  /* But not too much */

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_sequence_numbers(void) {
    printf("  test_sequence_numbers...");

    PT_Log *log = PT_LogCreate();

    uint32_t s1 = PT_LogNextSeq(log);
    uint32_t s2 = PT_LogNextSeq(log);
    uint32_t s3 = PT_LogNextSeq(log);

    assert(s1 == 1);
    assert(s2 == 2);
    assert(s3 == 3);

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_performance_logging(void) {
    printf("  test_performance_logging...");

    PT_Log *log = PT_LogCreate();
    PT_LogSetPerfCallback(log, test_perf_callback, NULL);
    g_perf_count = 0;

    PT_LogPerfEntry entry = {0};
    entry.seq_num = PT_LogNextSeq(log);
    entry.timestamp_ms = PT_LogElapsedMs(log);
    entry.event_type = 1;
    entry.value1 = 100;
    entry.value2 = 200;
    entry.flags = 0x0F;
    entry.category = PT_LOG_CAT_NETWORK;  /* New: category field */

    PT_LogPerf(log, &entry, "test_event");

    assert(g_perf_count == 1);
    assert(g_last_perf.event_type == 1);
    assert(g_last_perf.value1 == 100);
    assert(g_last_perf.value2 == 200);
    assert(g_last_perf.category == PT_LOG_CAT_NETWORK);  /* Verify category */

    PT_LogDestroy(log);
    printf(" OK\n");
}

void test_level_names(void) {
    printf("  test_level_names...");

    assert(strcmp(PT_LogLevelName(PT_LOG_ERR), "ERR") == 0);
    assert(strcmp(PT_LogLevelName(PT_LOG_WARN), "WRN") == 0);
    assert(strcmp(PT_LogLevelName(PT_LOG_INFO), "INF") == 0);
    assert(strcmp(PT_LogLevelName(PT_LOG_DEBUG), "DBG") == 0);
    assert(strcmp(PT_LogLevelName(PT_LOG_NONE), "---") == 0);

    printf(" OK\n");
}

void test_version(void) {
    printf("  test_version...");

    const char *ver = PT_LogVersion();
    assert(ver != NULL);
    assert(strlen(ver) > 0);
    assert(strstr(ver, ".") != NULL);  /* Should have dots */

    printf(" OK\n");
}

int main(void) {
    printf("PT_Log POSIX Tests\n");
    printf("==================\n\n");

    test_create_destroy();
    test_level_filtering();
    test_category_filtering();
    test_formatting();
    test_file_output();
    test_elapsed_time();
    test_sequence_numbers();
    test_performance_logging();
    test_level_names();
    test_version();

    printf("\n==================\n");
    printf("All tests PASSED\n");
    return 0;
}
```

### Verification
- [ ] `make test_log` compiles and passes all tests
- [ ] Log file output works correctly
- [ ] Console output appears on stderr
- [ ] Callback receives all non-filtered messages
- [ ] Thread safety: no crashes with concurrent logging (manual test)
- [ ] Memory: valgrind shows no leaks
- [ ] Struct field ordering is largest-to-smallest (8-byte, 4-byte, 2-byte, 1-byte)
- [ ] `level` is `uint8_t` not `PT_LogLevel` enum
- [ ] `auto_flush` is `uint8_t` not `int`
- [ ] Explicit `_pad[1]` byte present for alignment
- [ ] Stack buffers declared after filter checks
- [ ] PT_LogNextSeq uses atomic operations (check with -E or debugger)
- [ ] fflush only called when auto_flush or explicit PT_LogFlush

---

## Session 0.3: Classic Mac Implementation & Build

### Objective
Implement PT_Log for Classic Mac (System 6.0.8 through Mac OS 9) using File Manager for file I/O, and configure Retro68 build system.

---

### CRITICAL: Classic Mac vsprintf() Buffer Overflow Risk

**The Classic Mac toolbox does NOT provide `vsnprintf()` or `snprintf()`.** The only available function is `vsprintf()`, which has **NO BOUNDS CHECKING**.

If a formatted log message expands beyond `PT_LOG_LINE_MAX` (192 bytes), it **WILL** overflow the buffer and corrupt memory. This typically causes:
- Immediate crash
- Corrupted heap leading to later crash
- Data corruption

**Mitigation:**

1. **Keep messages concise.** Each log message, after printf expansion, must be < 192 bytes.
2. **Avoid unbounded strings.** Don't log user input or network data without truncation.
3. **Test on real hardware.** Emulators may have more permissive implementations.

**Examples:**

```c
/* WRONG - Will crash if filename is long or has special chars */
PT_LOG_INFO(log, CAT, "Processing file: %s with data: %s", long_filename, user_data);

/* WRONG - Packet data could exceed buffer */
PT_LOG_DEBUG(log, CAT, "Received: %s", raw_packet_hex_dump);

/* RIGHT - Concise, bounded message */
PT_LOG_INFO(log, CAT, "Processing file");
PT_LOG_DEBUG(log, CAT, "Recv %d bytes from peer %d", len, peer_id);

/* RIGHT - Truncate long strings before logging */
char truncated[32];
strncpy(truncated, long_string, 31);
truncated[31] = '\0';
PT_LOG_INFO(log, CAT, "File: %s", truncated);
```

**This is a hard platform limitation.** There is no safe alternative on Classic Mac toolbox.

---

### Tasks

#### Task 0.3.1: Create `src/log/pt_log_mac.c`

```c
/*
 * PT_Log - Classic Mac Implementation
 *
 * Uses File Manager for file I/O.
 * Works on System 6.0.8 through Mac OS 9.
 */

#include "pt_log.h"

#ifndef PT_LOG_STRIP

#include <Files.h>
#include <Memory.h>
#include <OSUtils.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/*============================================================================
 * Constants
 *
 * Buffer size is 256 bytes on Classic Mac (fits in 68030's 256-byte data cache).
 * Line max is 192 bytes to leave room for timestamp prefix in buffer.
 *
 * WARNING: Classic Mac uses vsprintf() which has NO bounds checking.
 * Log messages that expand beyond PT_LOG_LINE_MAX WILL cause buffer overflow.
 * Keep messages concise. This is a hard platform limitation.
 *============================================================================*/

#define PT_LOG_BUFFER_SIZE  256   /* Sized for 68030 cache (was: 512) */
#define PT_LOG_LINE_MAX     192   /* Max message length - ENFORCED BY CALLER */
#define PT_LOG_CREATOR      'PTLg'
#define PT_LOG_TYPE         'TEXT'

/*
 * Inline copy threshold: For strings <= this size, use inline byte-copy loop.
 * For larger strings, use BlockMoveData (function call overhead is amortized).
 *
 * Value of 32 bytes was chosen based on:
 * - 68000: No cache, inline loop avoids function call overhead
 * - 68020/030: 256-byte cache, small copies fit entirely in cache
 * - BlockMoveData only wins for larger copies where setup cost is amortized
 *
 * Adjust based on hardware profiling if needed.
 */
#define PT_LOG_INLINE_COPY_THRESHOLD  32

/*============================================================================
 * Log Context Structure
 *
 * Field ordering optimized for cache efficiency AND minimal padding:
 * - Fields ordered largest-to-smallest to eliminate implicit padding
 * - 4-byte pointers/longs first, then 2-byte, then 1-byte
 * - No pthread_mutex (Classic Mac is single-threaded)
 * - Smaller buffer at end (256 bytes for 68030 cache)
 *
 * On 68k: First 32 bytes contain all hot/warm fields (fits in 68030 cache)
 *============================================================================*/

struct pt_log {
    /* === 4-BYTE FIELDS (pointers on 32-bit, longs) === */
    PT_LogCallback  msg_callback;   /* 4 bytes - callback pointer */
    void           *msg_user_data;  /* 4 bytes - callback context */
    PT_LogPerfCallback  perf_callback;  /* 4 bytes */
    void               *perf_user_data; /* 4 bytes */
    unsigned long   start_ticks;    /* 4 bytes - TickCount() at creation */
    uint32_t        next_seq;       /* 4 bytes - sequence counter */

    /* === 2-BYTE FIELDS === */
    int16_t         buffer_pos;     /* 2 bytes - buffer write position (max 256) */
    uint16_t        categories;     /* 2 bytes - filter check */
    short           file_refnum;    /* 2 bytes - File Manager refnum */

    /* === 1-BYTE FIELDS (grouped to avoid padding) === */
    uint8_t         level;          /* 1 byte - stores PT_LogLevel enum value */
    uint8_t         outputs;        /* 1 byte  - output routing */
    uint8_t         auto_flush;     /* 1 byte  - flush decision */
    uint8_t         _pad;           /* 1 byte  - explicit padding for alignment */

    /* === BUFFER: At end to avoid polluting cache === */
    char            buffer[PT_LOG_BUFFER_SIZE];  /* 256 bytes */
};

/*============================================================================
 * Static Data
 *============================================================================*/

static const char *g_level_names[] = {
    "---",
    "ERR",
    "WRN",
    "INF",
    "DBG"
};

static const char *g_version = "1.0.0";

/*============================================================================
 * Helper: C string to Pascal string
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
 * File Manager Helpers
 *
 * The high-level File Manager calls (Create, SetFPos) used in some examples
 * are actually glue routines that may not be available in all toolboxes.
 * We use the low-level PB (parameter block) versions for maximum compatibility.
 *============================================================================*/

static OSErr pt_create_file(ConstStr255Param name, short vRefNum,
                            OSType creator, OSType fileType) {
    HParamBlockRec pb;

    memset(&pb, 0, sizeof(pb));
    pb.fileParam.ioNamePtr = (StringPtr)name;
    pb.fileParam.ioVRefNum = vRefNum;
    pb.fileParam.ioDirID = 0;  /* Current directory */
    pb.ioParam.ioMisc = NULL;
    return PBHCreateSync(&pb);
}

static OSErr pt_set_file_info(ConstStr255Param name, short vRefNum,
                               OSType creator, OSType fileType) {
    HParamBlockRec pb;
    OSErr err;

    /* First get existing info */
    memset(&pb, 0, sizeof(pb));
    pb.fileParam.ioNamePtr = (StringPtr)name;
    pb.fileParam.ioVRefNum = vRefNum;
    pb.fileParam.ioFDirIndex = 0;  /* Use name, not index */
    pb.fileParam.ioDirID = 0;
    err = PBHGetFInfoSync(&pb);
    if (err != noErr) return err;

    /* Set creator and type */
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
    if (log) log->level = level;
}

PT_LogLevel PT_LogGetLevel(PT_Log *log) {
    return log ? log->level : PT_LOG_NONE;
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

    /* Create file if it doesn't exist (ignore dupFNErr if it does).
     * Use PBHCreateSync via helper - the high-level Create() glue may
     * not be available in all toolboxes. */
    err = pt_create_file(pname, 0, PT_LOG_CREATOR, PT_LOG_TYPE);
    if (err != noErr && err != dupFNErr) {
        return -1;
    }

    /* Set file type/creator (PBHCreate doesn't set these) */
    if (err == noErr) {
        pt_set_file_info(pname, 0, PT_LOG_CREATOR, PT_LOG_TYPE);
    }

    /* Open for writing */
    err = FSOpen(pname, 0, &refnum);
    if (err != noErr) {
        return -1;
    }

    /* Seek to end for append (preserves existing log content).
     * Use PBSetFPosSync via helper - SetFPos() glue may not be available. */
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
    if (log) log->auto_flush = enabled;
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

uint32_t PT_LogElapsedMs(PT_Log *log) {
    unsigned long now;
    unsigned long elapsed_ticks;

    if (!log) return 0;

    now = TickCount();
    elapsed_ticks = now - log->start_ticks;

    /* Convert ticks (60ths of second) to milliseconds.
     *
     * 68000/68010 have NO hardware divide instruction - division is very
     * expensive (~70+ cycles). 68020+ have DIVS/DIVU but still ~40 cycles.
     *
     * Exact: ticks * 1000 / 60 = ticks * 16.667
     *
     * Shift-based approximation (no division):
     *   ticks * 16 + ticks / 4 = ticks * 16.25 (error: -2.5%)
     *   ticks * 16 + ticks / 2 + ticks / 8 = ticks * 16.625 (error: -0.25%)
     *
     * We use the more accurate version since this isn't called from
     * interrupt context and the extra shifts are cheap.
     */
#if defined(__MC68000__) || defined(__MC68010__) || defined(THINK_C) || defined(__MWERKS__)
    /* 68k: Use shift-based approximation to avoid division.
     * ticks * 16.625 = (ticks << 4) + (ticks >> 1) + (ticks >> 3)
     * Error: -0.25% (1 second shows as 997.5ms - acceptable for logging) */
    return (uint32_t)((elapsed_ticks << 4) + (elapsed_ticks >> 1) + (elapsed_ticks >> 3));
#else
    /* PowerPC or unknown: division is fast, use exact formula */
    return (uint32_t)((elapsed_ticks * 1000UL) / 60UL);
#endif
}

uint32_t PT_LogNextSeq(PT_Log *log) {
    /* Note: Classic Mac is single-threaded, no mutex needed.
     * POSIX version uses pthread_mutex for thread safety. */
    if (!log) return 0;
    return log->next_seq++;
}

const char *PT_LogLevelName(PT_LogLevel level) {
    /* Use unsigned comparison to handle negative values from bad casts */
    if ((unsigned)level > PT_LOG_DEBUG) return "???";
    return g_level_names[level];
}

const char *PT_LogVersion(void) {
    return g_version;
}

/*============================================================================
 * Output Helpers
 *============================================================================*/

static void flush_buffer(PT_Log *log) {
    long count;

    if (log->buffer_pos == 0) return;

    if ((log->outputs & PT_LOG_OUT_FILE) && log->file_refnum) {
        count = log->buffer_pos;
        FSWrite(log->file_refnum, &count, log->buffer);
    }

    /* Console output on Classic Mac: use DebugStr or just skip */
    /* In real usage, apps would use callback for UI display */

    log->buffer_pos = 0;
}

static void write_to_buffer(PT_Log *log, const char *str, int len) {
    if (log->buffer_pos + len >= PT_LOG_BUFFER_SIZE) {
        flush_buffer(log);
    }

    if (len >= PT_LOG_BUFFER_SIZE) {
        /* Too big for buffer, write directly */
        if ((log->outputs & PT_LOG_OUT_FILE) && log->file_refnum) {
            long count = len;
            FSWrite(log->file_refnum, &count, str);
        }
        return;
    }

    /* Inline copy for small strings (faster than BlockMoveData call overhead on 68k).
     * BlockMoveData only wins for larger copies where setup cost is amortized.
     * Threshold defined by PT_LOG_INLINE_COPY_THRESHOLD (default: 32 bytes). */
    if (len <= PT_LOG_INLINE_COPY_THRESHOLD) {
        char *dst = log->buffer + log->buffer_pos;
        const char *src = str;
        int i;
        for (i = 0; i < len; i++) {
            dst[i] = src[i];
        }
    } else {
        BlockMoveData(str, log->buffer + log->buffer_pos, len);
    }
    log->buffer_pos += len;
}

/*============================================================================
 * Logging Functions
 *============================================================================*/

void PT_LogWrite(PT_Log *log, PT_LogLevel level, PT_LogCategory category,
                 const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    PT_LogWriteV(log, level, category, fmt, args);
    va_end(args);
}

void PT_LogWriteV(PT_Log *log, PT_LogLevel level, PT_LogCategory category,
                  const char *fmt, va_list args) {
    /* NOTE: Stack buffers declared AFTER filter checks to avoid
     * allocating ~400 bytes on stack for filtered-out messages.
     * Classic Mac has limited stack space (8KB-32KB typical). */

    if (!log || !fmt) return;

    /* Filter by level - fast path exit */
    if (level > log->level) return;

    /* Filter by category - fast path exit */
    if (!(log->categories & category)) return;

    /* === PASSED FILTERS: Now allocate stack buffers === */
    {
        char line[PT_LOG_LINE_MAX];
        char formatted[PT_LOG_LINE_MAX];
        int len;
        uint32_t timestamp;

        timestamp = PT_LogElapsedMs(log);

        /* Format the user message */
        /* WARNING: Classic Mac uses vsprintf (no vsnprintf). Format strings that
         * expand beyond PT_LOG_LINE_MAX (192) bytes will cause buffer overflow.
         * Keep log messages concise. This is a hard platform limitation. */
        vsprintf(formatted, fmt, args);

        /* Format the full line - use sprintf return value (not strlen) */
        len = sprintf(line, "[%08lu][%s] %s\r",  /* Mac uses \r for newline */
                      (unsigned long)timestamp, g_level_names[level], formatted);

        /* Write to file */
        if (log->outputs & PT_LOG_OUT_FILE) {
            write_to_buffer(log, line, len);
            if (log->auto_flush) {
                flush_buffer(log);
            }
        }

        /* Call message callback */
        if ((log->outputs & PT_LOG_OUT_CALLBACK) && log->msg_callback) {
            log->msg_callback(level, category, timestamp, formatted, log->msg_user_data);
        }
    }
}

void PT_LogPerf(PT_Log *log, const PT_LogPerfEntry *entry, const char *label) {
    if (!log || !entry) return;

    /* Call performance callback */
    if (log->perf_callback) {
        log->perf_callback(entry, label, log->perf_user_data);
    }

    /* Write to text log if PERF category enabled.
     * Note: entry->category is metadata for callback receivers, not used for
     * filtering here. Global PT_LOG_CAT_PERF controls whether perf entries
     * appear in the text log. */
    if ((log->categories & PT_LOG_CAT_PERF) && (log->level >= PT_LOG_INFO)) {
        char line[PT_LOG_LINE_MAX];
        int len;

        /* Use sprintf return value instead of strlen() */
        if (label) {
            len = sprintf(line,
                "[%08lu][INF] PERF seq=%lu type=%u v1=%u v2=%u flags=0x%02X cat=0x%04X %s\r",
                (unsigned long)entry->timestamp_ms,
                (unsigned long)entry->seq_num,
                (unsigned)entry->event_type,
                (unsigned)entry->value1,
                (unsigned)entry->value2,
                (unsigned)entry->flags,
                (unsigned)entry->category,
                label);
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

#endif /* PT_LOG_STRIP */
```

### Verification (on real Mac hardware)
- [ ] PT_LogCreate() returns valid pointer
- [ ] PT_LogSetFile() creates file with correct creator/type (uses PBHCreateSync)
- [ ] File opened for append (uses PBSetFPosSync to seek to end)
- [ ] Log messages appear in file with correct format
- [ ] Timestamps increase correctly (based on TickCount)
- [ ] Sequence numbers increment
- [ ] Callback receives messages
- [ ] No crashes after 100+ log messages
- [ ] MaxBlock() same before/after (no memory leaks)
- [ ] PT_Log NOT called from ASR/notifier context in test code (use flags instead)
- [ ] Buffer size is 256 bytes (not 512)
- [ ] Inline copy used for small strings (verify with debugger if needed)
- [ ] sprintf return value used (no strlen calls)
- [ ] Messages near 192-byte limit don't crash (boundary test)
- [ ] Struct level field is 1 byte (uint8_t), not 4 bytes (int/enum)

#### Task 0.3.2: Create Retro68 Build Configuration

Create `src/log/CMakeLists.txt` for building PT_Log with Retro68:

```cmake
# PT_Log library - Retro68 build configuration
#
# Build with:
#   mkdir build-68k && cd build-68k
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/Retro68/build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
#   make ptlog
#
#   mkdir build-ppc && cd build-ppc
#   cmake .. -DCMAKE_TOOLCHAIN_FILE=/path/to/Retro68/build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
#   make ptlog

cmake_minimum_required(VERSION 3.9)
project(ptlog C)

# Detect if we're building for Classic Mac via Retro68
if(CMAKE_SYSTEM_NAME STREQUAL "Retro68")
    set(PTLOG_PLATFORM_MAC TRUE)
    set(PTLOG_SOURCES pt_log_mac.c)
    message(STATUS "Building PT_Log for Classic Mac (Retro68)")
else()
    set(PTLOG_PLATFORM_MAC FALSE)
    set(PTLOG_SOURCES pt_log_posix.c)
    message(STATUS "Building PT_Log for POSIX")
endif()

# Include directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../include)

# Library target
add_library(ptlog STATIC ${PTLOG_SOURCES})

# Compiler flags
if(PTLOG_PLATFORM_MAC)
    # Classic Mac: strict C89, no warnings about long long (from stdint.h fallback)
    target_compile_options(ptlog PRIVATE -Wall -Wextra -pedantic)
else()
    # POSIX: C99 with pthread
    target_compile_options(ptlog PRIVATE -Wall -Wextra -pedantic -std=c99)
    target_link_libraries(ptlog pthread)
endif()

# Installation
install(TARGETS ptlog ARCHIVE DESTINATION lib)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../../include/pt_log.h DESTINATION include)
```

**Verification:**
- [ ] `cmake` succeeds with Retro68 m68k toolchain
- [ ] `cmake` succeeds with Retro68 PPC toolchain
- [ ] `make ptlog` produces `libptlog.a` for 68k
- [ ] `make ptlog` produces `libptlog.a` for PPC
- [ ] Library links successfully with a test app on real Mac

---

## Session 0.4: Callbacks, Performance, and Integration

### Objective
Verify callbacks and performance logging work correctly. Create shared test utilities.

### Tasks

#### Task 0.4.1: Create `tests/test_log_perf.c`

```c
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
```

#### Task 0.4.2: Update Makefile to build PT_Log

Add to `Makefile`:

```makefile
# PT_Log library
LOG_SRCS = src/log/pt_log_posix.c
LOG_OBJS = $(LOG_SRCS:.c=.o)

libptlog.a: $(LOG_OBJS)
	ar rcs $@ $^

# PT_Log tests
test_log: tests/test_log_posix.c libptlog.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lptlog -lpthread

test_log_perf: tests/test_log_perf.c libptlog.a
	$(CC) $(CFLAGS) -o $@ $< -L. -lptlog -lpthread

test-log: test_log test_log_perf
	./test_log
	./test_log_perf
```

### CRITICAL: Performance Entry Category Filtering

**Important implementation detail discovered during Session 0.4:**

The `PT_LogPerf()` function filters performance entries by category before calling callbacks or writing to output. This means:

1. **The log context must have categories enabled** via `PT_LogSetCategories()`
2. **Each performance entry must have its `category` field set** to a non-zero value
3. **The entry's category must match at least one enabled category**, or it's silently dropped

**Example of the filtering logic** (from `src/log/pt_log_posix.c:327`):
```c
void PT_LogPerf(PT_Log *log, const PT_LogPerfEntry *entry, const char *label) {
    if (!log || !entry) return;

    /* Filter by category - entry is silently dropped if no match */
    if (!(entry->category & log->categories)) return;

    /* Callback/text output happens here... */
}
```

**Common mistake in test code:**
```c
/* WRONG - will not trigger callback! */
PT_Log *log = PT_LogCreate();
PT_LogSetPerfCallback(log, my_callback, NULL);
PT_LogPerfEntry entry = {0};  /* category = 0 */
PT_LogPerf(log, &entry, NULL);  /* Silently filtered out! */

/* CORRECT - callback fires */
PT_Log *log = PT_LogCreate();
PT_LogSetPerfCallback(log, my_callback, NULL);
PT_LogSetCategories(log, PT_LOG_CAT_ALL);  /* Enable categories */
PT_LogPerfEntry entry = {0};
entry.category = PT_LOG_CAT_NETWORK;  /* Set category */
PT_LogPerf(log, &entry, NULL);  /* Callback fires! */
```

**Why this design?**
- Consistent filtering behavior between text logging and performance logging
- Allows selective performance instrumentation (e.g., only log network events)
- Zero-overhead for disabled categories (callback not called, no formatting done)

**Testing note:** All test functions that use `PT_LogPerf()` must call `PT_LogSetCategories(log, PT_LOG_CAT_ALL)` or enable specific categories, and set `entry.category` to a matching value.

### Verification
- [ ] `make test-log` passes all tests
- [ ] Performance entries captured correctly via callback
- [ ] Multiple outputs work simultaneously
- [ ] App-defined categories work correctly
- [ ] File + callback output produces consistent data
- [ ] PT_LogPerfEntry.category field passed to callback correctly
- [ ] PT_LogPerfEntry.category appears in text log output (cat=0xNNNN)
- [ ] PT_LogPerfEntry is exactly 16 bytes (compile-time assertion in test)
- [ ] Filtered messages don't allocate stack buffers (verify with debugger/profiler)

---

---

## Performance Optimizations Applied

This section documents the data-oriented design decisions made for cache efficiency and performance on constrained Classic Mac hardware.

### Struct Field Ordering (Cache-Friendly, Padding-Free)

Both `struct pt_log` implementations order fields largest-to-smallest to eliminate implicit compiler padding:

1. **8-byte fields first (POSIX) / 4-byte fields first (Mac):** pointers, timeval
   - Natural alignment, no padding between fields

2. **4-byte fields next:** buffer_pos, next_seq
   - Still naturally aligned

3. **2-byte fields:** categories, file_refnum (Mac)

4. **1-byte fields grouped together:** level (stored as uint8_t), outputs, auto_flush
   - Explicit padding byte added for alignment clarity

5. **Buffer at end:** 256-512 bytes
   - Large, only touched on actual writes
   - Doesn't pollute cache during filter-only paths

**Key change:** PT_LogLevel enum (4 bytes) is stored internally as `uint8_t level` (1 byte). The API still accepts the enum type.

### PT_LogPerfEntry Layout

- 16 bytes total, no internal padding
- Power-of-2 size for efficient array indexing
- 16 entries fit in 68030's 256-byte data cache
- 4 entries fit in modern 64-byte cache line
- `reserved` field repurposed as `category` for filtering

### Stack Allocation Optimization

Stack buffers (`line`, `formatted`) are declared **after** filter checks:

```c
if (level > log->level) return;        /* Exit before allocation */
if (!(category & log->categories)) return;

{
    char line[PT_LOG_LINE_MAX];        /* Only allocated if needed */
    char formatted[PT_LOG_LINE_MAX];
    /* ... */
}
```

This saves ~400 bytes of stack allocation on filtered-out messages. Important on Classic Mac where stack is 8KB-32KB.

### Platform-Specific Buffer Sizing

| Platform | Buffer Size | Rationale |
|----------|-------------|-----------|
| POSIX | 512 bytes | Larger for throughput |
| Classic Mac | 256 bytes | Fits in 68030 data cache (256 bytes) |

### Inline Copy for Small Strings (Mac)

BlockMoveData has function call overhead. For strings ≤ 32 bytes, inline loop is faster:

```c
if (len <= 32) {
    for (i = 0; i < len; i++) dst[i] = src[i];
} else {
    BlockMoveData(str, dst, len);
}
```

### sprintf Return Value (Mac)

Use `sprintf` return value instead of calling `strlen()`:

```c
/* WRONG - redundant strlen */
sprintf(line, "...");
len = strlen(line);

/* RIGHT - use return value */
len = sprintf(line, "...");
```

### auto_flush Type Change

Changed `auto_flush` from `int` (4 bytes) to `uint8_t` (1 byte):
- Boolean value doesn't need 4 bytes
- Better struct packing, less padding
- Saves 3 bytes per log context

---

## Phase Dependencies

```
Phase 0: PT_Log Library     ← This phase
    ↓
Phase 1: PeerTalk Foundation (uses PT_Log)
    ↓
Phase 2: Protocol
    ...
```

**PeerTalk Integration (Phase 1):**

```c
/*============================================================================
 * pt_internal.h - Internal PeerTalk header (Phase 1)
 *============================================================================*/
#include "pt_log.h"

/*============================================================================
 * Context-Aware Logging Macros
 *
 * These macros simplify logging from code that has access to a pt_context.
 * They extract the log handle automatically, reducing boilerplate.
 *
 * Usage in PeerTalk internal code:
 *     PT_CTX_ERR(ctx, PT_LOG_CAT_NETWORK, "Connection failed: %d", err);
 *     PT_CTX_INFO(ctx, PT_LOG_CAT_PROTOCOL, "Peer %d connected", peer_id);
 *
 * The ctx parameter can be any expression yielding pt_context*:
 *     PT_CTX_DEBUG(state->ctx, PT_LOG_CAT_PLATFORM, "Poll cycle");
 *============================================================================*/
#define PT_CTX_ERR(ctx, cat, ...)   PT_LOG_ERR((ctx)->log, cat, __VA_ARGS__)
#define PT_CTX_WARN(ctx, cat, ...)  PT_LOG_WARN((ctx)->log, cat, __VA_ARGS__)
#define PT_CTX_INFO(ctx, cat, ...)  PT_LOG_INFO((ctx)->log, cat, __VA_ARGS__)
#define PT_CTX_DEBUG(ctx, cat, ...) PT_LOG_DEBUG((ctx)->log, cat, __VA_ARGS__)

struct pt_context {
    /* === Logging === */
    PT_Log *log;          /* Logging context (never NULL after init) */
    uint8_t owns_log;     /* 1 if we created it, 0 if passed in */

    /* === Platform state === */
    void   *platform;     /* Platform-specific data (MacTCP, OT, POSIX) */

    /* === Connection tracking === */
    /* ... (defined in Phase 2) ... */
};

/*============================================================================
 * PeerTalk configuration structure - passed to PeerTalk_Init()
 *============================================================================*/
typedef struct {
    /* Logging options (all optional - defaults provided) */
    PT_Log     *log;              /* NULL = create our own; non-NULL = use this one */
    const char *log_filename;     /* NULL = no file; path = log to this file */
    PT_LogLevel log_level;        /* Default: PT_LOG_INFO */

    /* Network options */
    uint16_t    discovery_port;   /* Default: 7353 */
    uint16_t    tcp_port;         /* Default: 7354 */

    /* ... */
} PeerTalk_Config;

/*============================================================================
 * PeerTalk_Init() - Initialize PeerTalk (Phase 1)
 *============================================================================*/
PT_Result PeerTalk_Init(pt_context **out_ctx, const PeerTalk_Config *config) {
    pt_context *ctx;

    ctx = (pt_context *)calloc(1, sizeof(pt_context));
    if (!ctx) return PT_ERR_MEMORY;

    /* === Initialize logging === */
    if (config && config->log) {
        /* Use caller's log context (shared logging) */
        ctx->log = config->log;
        ctx->owns_log = 0;
    } else {
        /* Create our own log context */
        ctx->log = PT_LogCreate();
        if (!ctx->log) {
            free(ctx);
            return PT_ERR_MEMORY;
        }
        ctx->owns_log = 1;

        /* Apply config options */
        if (config) {
            if (config->log_filename) {
                PT_LogSetFile(ctx->log, config->log_filename);
            }
            if (config->log_level != 0) {
                PT_LogSetLevel(ctx->log, config->log_level);
            }
        }
    }

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_PLATFORM, "PeerTalk v%s initializing", PT_VERSION);

    /* === Initialize platform (Phase 4/5/6/7) === */
    /* ctx->platform = platform_init(ctx->log); */

    *out_ctx = ctx;
    return PT_OK;
}

/*============================================================================
 * PeerTalk_Shutdown() - Cleanup PeerTalk (Phase 1)
 *============================================================================*/
void PeerTalk_Shutdown(pt_context *ctx) {
    if (!ctx) return;

    PT_LOG_INFO(ctx->log, PT_LOG_CAT_PLATFORM, "PeerTalk shutting down");

    /* === Shutdown platform (Phase 4/5/6/7) === */
    /* platform_shutdown(ctx->platform); */

    /* === Cleanup logging === */
    if (ctx->owns_log) {
        PT_LogDestroy(ctx->log);
    }
    /* If !owns_log, caller is responsible for their PT_Log lifetime */

    free(ctx);
}

/*============================================================================
 * Passing log context to platform modules (Phase 5-7)
 *============================================================================*/

/* Each platform module receives the log context at init */
void *mactcp_init(PT_Log *log) {
    mactcp_state *state = /* ... allocate ... */;
    state->log = log;  /* Store reference for use in main loop logging */

    PT_LOG_DEBUG(log, PT_LOG_CAT_PLATFORM, "MacTCP driver initializing");

    /* ... */
    return state;
}

/* Platform code uses the stored log reference */
void mactcp_poll(mactcp_state *state) {
    /* Process ISR flags and log from main loop (see ISR template above) */
    process_connection_log_events(&state->conn, state->log, state->conn_id);
}
```

**Integration patterns:**

1. **Shared logging**: Application creates PT_Log, passes to PeerTalk, uses same context for app logging
2. **Separate logging**: Application passes NULL, PeerTalk creates its own context
3. **Platform propagation**: pt_context.log is passed to platform init functions

---

## Logging Best Practices for Critical Paths

When implementing Phases 4-9, ensure these critical events are logged:

### Events That MUST Be Logged

| Event Category | Log Level | Category | Example |
|----------------|-----------|----------|---------|
| Initialization success | INFO | PT_LOG_CAT_INIT | "PeerTalk v1.0 initialized" |
| Initialization failure | ERR | PT_LOG_CAT_INIT | "Failed to open MacTCP driver: %d" |
| Connection established | INFO | PT_LOG_CAT_CONNECT | "Connected to peer %d at %s:%d" |
| Connection failed | ERR | PT_LOG_CAT_CONNECT | "Connection to %s:%d failed: %d" |
| Connection timeout | WARN | PT_LOG_CAT_CONNECT | "Connection timeout after %dms" |
| Disconnect (clean) | INFO | PT_LOG_CAT_CONNECT | "Peer %d disconnected" |
| Disconnect (error) | WARN | PT_LOG_CAT_CONNECT | "Peer %d connection lost: %d" |
| Resource exhaustion | ERR | PT_LOG_CAT_MEMORY | "Stream pool exhausted (max %d)" |
| Resource exhaustion | ERR | PT_LOG_CAT_MEMORY | "Queue full, dropping message" |
| Protocol error | ERR | PT_LOG_CAT_PROTOCOL | "Invalid magic: expected PTLK, got %08X" |
| Protocol version mismatch | WARN | PT_LOG_CAT_PROTOCOL | "Peer version %d.%d, ours %d.%d" |
| State transition | DEBUG | PT_LOG_CAT_CONNECT | "Peer %d: %s -> %s" |
| Data received | DEBUG | PT_LOG_CAT_RECV | "Recv %d bytes from peer %d" |
| Data sent | DEBUG | PT_LOG_CAT_SEND | "Sent %d bytes to peer %d" |

### Template: Logging Connection State Transitions

```c
/* State machine transition logging - call from main loop only */
static void log_state_transition(PT_Log *log, int peer_id,
                                 const char *old_state, const char *new_state) {
    PT_LOG_DEBUG(log, PT_LOG_CAT_CONNECT, "Peer %d: %s -> %s",
                 peer_id, old_state, new_state);
}

/* Usage in state machine */
if (conn->state != new_state) {
    log_state_transition(ctx->log, conn->peer_id,
                         state_name(conn->state), state_name(new_state));
    conn->state = new_state;
}
```

### Template: Logging Timeout Events

```c
/* Timeout logging - call when timeout detected in main loop */
static void log_timeout(PT_Log *log, const char *operation,
                        int peer_id, uint32_t elapsed_ms, uint32_t timeout_ms) {
    PT_LOG_WARN(log, PT_LOG_CAT_CONNECT,
                "%s timeout for peer %d: %lums (limit %lums)",
                operation, peer_id,
                (unsigned long)elapsed_ms, (unsigned long)timeout_ms);
}
```

### Template: Logging Resource Exhaustion

```c
/* Resource exhaustion - critical for debugging production issues */
static void log_resource_exhausted(PT_Log *log, const char *resource,
                                   int current, int max) {
    PT_LOG_ERR(log, PT_LOG_CAT_MEMORY, "%s exhausted: %d/%d in use",
               resource, current, max);
}

/* Usage */
if (pool->active_count >= pool->max_count) {
    log_resource_exhausted(ctx->log, "TCP stream pool",
                           pool->active_count, pool->max_count);
    return PT_ERR_NO_RESOURCES;
}
```

---

## CRITICAL: ISR-Safety Rules

**PT_Log is NOT interrupt-safe.** This is by design - it uses TickCount() and File Manager calls which are NOT in Inside Macintosh Volume VI Table B-3 ("Routines That May Be Called at Interrupt Time").

**NEVER call PT_Log functions from:**
- MacTCP ASR callbacks
- Open Transport notifiers
- ADSP completion routines (ioCompletion or userRoutine)
- Time Manager tasks
- VBL tasks
- Any other interrupt-level code

### Concrete Template: ISR-Safe Event Logging for Phase 5-7

This template shows the recommended structure for logging events from interrupt handlers. Use this pattern in MacTCP (Phase 5), Open Transport (Phase 6), and AppleTalk (Phase 7) implementations.

```c
/*============================================================================
 * ISR-SAFE LOGGING TEMPLATE
 *
 * This pattern separates interrupt-time event capture from main-loop logging.
 * Copy and adapt for your connection/stream state structures.
 *============================================================================*/

/* Event flags for ISR -> main loop communication */
#define PT_EVT_DATA_ARRIVED     0x01
#define PT_EVT_REMOTE_CLOSE     0x02
#define PT_EVT_ERROR            0x04
#define PT_EVT_CONNECT_COMPLETE 0x08
#define PT_EVT_SEND_COMPLETE    0x10
#define PT_EVT_TIMEOUT          0x20

/* Hot data structure - accessed from ISR
 *
 * TYPE NOTE for log_error_code:
 * - MacTCP terminReason is 'unsigned short' - use uint16_t, NOT int16_t
 * - Open Transport OTResult is 'SInt32' (signed 32-bit)
 * - AppleTalk OSErr is 'SInt16' (signed 16-bit)
 * Match the field type to your platform's error type to avoid truncation.
 * For MacTCP specifically: terminReason values include TCPRemoteAbort (2),
 * TCPNetworkFailure (3), etc. - all small positive values that fit in uint16_t.
 */
typedef struct {
    volatile uint8_t  log_events;       /* Bitmask of events to log */
    volatile int16_t  log_error_code;   /* Error code if PT_EVT_ERROR set (see type note) */
    volatile uint16_t log_bytes;        /* Bytes transferred (for data events) */
    /* ... other connection state ... */
} pt_connection_hot;

/*----------------------------------------------------------------------------
 * ISR callback - runs at interrupt level (MacTCP ASR, OT notifier, etc.)
 * ONLY set flags and simple values. NO PT_Log calls!
 *----------------------------------------------------------------------------*/
static void isr_callback(pt_connection_hot *hot, int event, int error_code,
                         int bytes_transferred) {
    switch (event) {
    case EVENT_DATA_ARRIVED:
        hot->log_bytes = (uint16_t)bytes_transferred;
        hot->log_events |= PT_EVT_DATA_ARRIVED;
        break;

    case EVENT_REMOTE_CLOSE:
        hot->log_events |= PT_EVT_REMOTE_CLOSE;
        break;

    case EVENT_ERROR:
        hot->log_error_code = (int16_t)error_code;
        hot->log_events |= PT_EVT_ERROR;
        break;

    case EVENT_CONNECT:
        hot->log_events |= PT_EVT_CONNECT_COMPLETE;
        break;
    }
    /* NO PT_Log calls here - not even PT_DEBUG! */
}

/*----------------------------------------------------------------------------
 * Main loop event processor - runs at application level
 * Process flags and call PT_Log safely here.
 *----------------------------------------------------------------------------*/
void process_connection_log_events(pt_connection_hot *hot, PT_Log *log,
                                   int connection_id) {
    uint8_t events;

    /* Atomic read and clear of events (disable interrupts on 68k if needed) */
    events = hot->log_events;
    hot->log_events = 0;

    if (events == 0) return;

    /* Now safe to log - we're in main loop, not ISR */
    if (events & PT_EVT_DATA_ARRIVED) {
        PT_LOG_DEBUG(log, PT_LOG_CAT_NETWORK,
                 "Conn %d: data arrived (%u bytes)", connection_id, hot->log_bytes);
    }

    if (events & PT_EVT_REMOTE_CLOSE) {
        PT_LOG_INFO(log, PT_LOG_CAT_NETWORK,
                "Conn %d: remote close", connection_id);
    }

    if (events & PT_EVT_ERROR) {
        PT_LOG_ERR(log, PT_LOG_CAT_NETWORK,
               "Conn %d: error %d", connection_id, hot->log_error_code);
    }

    if (events & PT_EVT_CONNECT_COMPLETE) {
        PT_LOG_INFO(log, PT_LOG_CAT_NETWORK,
                "Conn %d: connected", connection_id);
    }
}
```

**Key Points:**

1. **volatile fields**: `log_events`, `log_error_code`, `log_bytes` must be `volatile` since ISR modifies them
2. **Atomic operations**: On 68k, consider disabling interrupts during read-and-clear of events
3. **Small data only**: Only store what's needed for logging (error codes, byte counts, flags)
4. **Clear after read**: Reset `log_events` to 0 after processing to prepare for next ISR

**Phase-specific adaptations:**
- **Phase 5 (MacTCP)**: Put `log_events` in your TCPiopb wrapper structure
- **Phase 6 (OT)**: Use `OTAtomicSetBit()` / `OTAtomicTestBit()` for `log_events`
- **Phase 7 (ADSP)**: Put `log_events` at start of CCB wrapper (context recovery via cast)

### Pattern: Flag-Based Logging from Interrupts

```c
/*============================================================================
 * WRONG - Will crash! TickCount() and FSWrite() not interrupt-safe
 *============================================================================*/
static pascal void tcp_asr(StreamPtr stream, unsigned short event,
                           Ptr userDataPtr, unsigned short terminReason,
                           ICMPReport *icmpMsg) {
    my_state *state = (my_state *)userDataPtr;

    /* CRASH: PT_Log calls TickCount() internally */
    PT_LOG_INFO(state->ctx->log, PT_LOG_CAT_NETWORK, "Data arrived");
}

/*============================================================================
 * CORRECT - Set flags in ASR, log from main event loop
 *============================================================================*/
static pascal void tcp_asr(StreamPtr stream, unsigned short event,
                           Ptr userDataPtr, unsigned short terminReason,
                           ICMPReport *icmpMsg) {
    my_state *state = (my_state *)userDataPtr;

    switch (event) {
    case TCPDataArrival:
        state->flags.data_arrived = 1;  /* Just set flag - safe */
        break;
    case TCPClosing:
        state->flags.remote_close = 1;
        break;
    }
    /* NO PT_Log calls here! */
}

/* In main event loop (WaitNextEvent loop): */
void process_network_events(my_state *state) {
    if (state->flags.data_arrived) {
        PT_LOG_INFO(state->ctx->log, PT_LOG_CAT_NETWORK, "Data arrived");
        state->flags.data_arrived = 0;
        /* Now safe to process the data... */
    }
    if (state->flags.remote_close) {
        PT_LOG_INFO(state->ctx->log, PT_LOG_CAT_NETWORK, "Remote close");
        state->flags.remote_close = 0;
        /* Handle close... */
    }
}
```

### Open Transport Notifier Pattern

Open Transport notifiers run at **deferred task time** (not hardware interrupt time),
which is slightly less restrictive but still has severe limitations. Per Networking
With Open Transport v1.3 (page 5793): notifiers cannot make synchronous OT calls,
cannot use File Manager, and should not allocate memory (OTAllocMem may fail due
to pool depletion). Use OTAtomicSetBit for flag-based communication.

```c
/*============================================================================
 * CORRECT for Open Transport - same principle (deferred task time)
 *============================================================================*/
static pascal void tcp_notifier(void *context, OTEventCode code,
                                OTResult result, void *cookie) {
    my_endpoint *ep = (my_endpoint *)context;

    switch (code) {
    case T_DATA:
        OTAtomicSetBit(&ep->event_flags, kDataAvailableBit);
        break;
    case T_DISCONNECT:
        OTAtomicSetBit(&ep->event_flags, kDisconnectBit);
        break;
    }
    /* NO PT_Log calls here! */
}

/* In main event loop: */
void process_ot_events(my_endpoint *ep, PT_Log *log) {
    if (OTAtomicTestBit(&ep->event_flags, kDataAvailableBit)) {
        OTAtomicClearBit(&ep->event_flags, kDataAvailableBit);
        PT_LOG_INFO(log, PT_LOG_CAT_NETWORK, "OT data available");
        /* Process data... */
    }
}
```

**Phase 1 Session 1.4 MUST include these examples in documentation.**

---

## Logging Guidelines

### Error Logging Checklist

Every error path MUST have a corresponding PT_LOG_ERR or PT_LOG_WARN call. Use this checklist when implementing Phases 1-9:

| Error Condition | Level | Category | Example Message Format |
|-----------------|-------|----------|------------------------|
| Memory allocation failed | ERR | MEMORY | "NewPtr failed: need %ld, have %ld free" |
| Protocol magic mismatch | ERR | PROTOCOL | "Invalid magic: got 0x%08X, expected 0x%08X" |
| Protocol version mismatch | WARN | PROTOCOL | "Version mismatch: peer v%d, we support v%d" |
| CRC/checksum error | ERR | PROTOCOL | "CRC error: got 0x%04X, computed 0x%04X" |
| Queue full (backpressure) | WARN | MEMORY | "Queue full: %d/%d entries, dropping" |
| Connection refused | ERR | CONNECT | "Connection refused: %s:%d error %d" |
| Connection timeout | WARN | CONNECT | "Connect timeout to %s:%d after %dms" |
| Discovery timeout | WARN | DISCOVERY | "Discovery timeout: no response in %dms" |
| Driver open failed | ERR | PLATFORM | "Failed to open %s driver: error %d" |
| Stream/endpoint create failed | ERR | PLATFORM | "TCPCreate failed: error %d" |
| Send failed | ERR | SEND | "Send failed on conn %d: error %d" |
| Receive error | ERR | RECV | "Recv error on conn %d: error %d" |
| Resource limit reached | WARN | MEMORY | "Stream pool exhausted: %d/%d in use" |

**Rule:** If a function returns an error code (PT_ERR_*, OSErr, OTResult), log BEFORE returning.

### Initialization Logging Checklist

Log these events during startup for diagnostic visibility:

| Phase | What to Log | Level | Category | Example |
|-------|-------------|-------|----------|---------|
| 0 | PT_Log version, file path | INFO | INIT | "PT_Log v1.0.0, file: /app.log" |
| 1 | PeerTalk version, max_peers | INFO | INIT | "PeerTalk v1.0.0, max_peers=%d" |
| 1 | Platform detected | DEBUG | PLATFORM | "Platform: MacTCP on System 7.1" |
| 5 | MacTCP driver version | DEBUG | PLATFORM | "MacTCP driver v2.0.6 opened" |
| 5 | TCP buffer size chosen | DEBUG | MEMORY | "TCP buffer: %d bytes (FreeMem=%ld)" |
| 6 | Open Transport version | DEBUG | PLATFORM | "OT version %d.%d.%d initialized" |
| 6 | Endpoint configuration | DEBUG | PLATFORM | "TCP endpoint: async, blocking reads" |
| 7 | AppleTalk drivers opened | DEBUG | PLATFORM | ".MPP and .DSP drivers opened" |
| 7 | NBP type registered | INFO | DISCOVERY | "NBP registered: %s:%s@%s" |
| 4 | POSIX socket created | DEBUG | PLATFORM | "Listening on 0.0.0.0:%d (fd=%d)" |

### Timeout Logging Templates

Use these templates for consistent timeout logging across phases:

```c
/* Connection timeout */
PT_LOG_WARN(log, PT_LOG_CAT_CONNECT,
    "Connect timeout to %s:%d after %dms",
    peer_addr, peer_port, elapsed_ms);

/* Discovery timeout (no response) */
PT_LOG_WARN(log, PT_LOG_CAT_DISCOVERY,
    "Discovery timeout: no response from %s in %dms",
    target_name, timeout_ms);

/* Message timeout (queue wait) */
PT_LOG_WARN(log, PT_LOG_CAT_SEND,
    "Send timeout on conn %d: queue depth %d, waited %dms",
    conn_id, queue_depth, elapsed_ms);

/* Retransmit threshold exceeded */
PT_LOG_WARN(log, PT_LOG_CAT_SEND,
    "Conn %d: retransmit count high (%d), may indicate network issue",
    conn_id, retry_count);
```

---

## Example Log Output Format

This section shows what actual PT_Log output looks like, helping developers verify correct behavior and understand the format for log parsing.

### Text Log Format

```
[TIMESTAMP][LVL] message
```

Where:
- `TIMESTAMP`: 8-digit milliseconds since PT_LogCreate() (zero-padded)
- `LVL`: 3-character level indicator (ERR, WRN, INF, DBG)
- `message`: The formatted log message

### Example Output (POSIX - to file or stderr)

```
[00000000][INF] PeerTalk v1.0.0 initializing
[00000015][DBG] MacTCP driver initializing
[00000023][INF] Listening on port 7354
[00001234][INF] Connection established to peer "Mac SE/30"
[00001567][DBG] Conn 1: data arrived (256 bytes)
[00002890][WRN] Conn 1: retransmit count high (3)
[00005432][INF] Conn 1: send complete (1024 bytes)
[00008765][ERR] Conn 2: connection refused (-61)
[00012345][INF] Conn 1: remote close
[00012400][INF] PeerTalk shutting down
```

### Example Output (Classic Mac - to file)

Same format, but uses `\r` (carriage return) instead of `\n` for line endings:

```
[00000000][INF] PeerTalk v1.0.0 initializing\r
[00000015][DBG] MacTCP driver initializing\r
...
```

### When to Use PT_LogPerf vs PT_LOG_DEBUG

Use PT_LogPerf() for structured timing and measurement data that benefits from:
- Post-session analysis (filtering, aggregation, graphing)
- Precise numeric values (byte counts, durations, counters)
- Low-overhead binary capture via callback

Use PT_LOG_DEBUG for diagnostic information that is:
- Human-readable context (state names, flag descriptions)
- One-off debugging (investigating specific issues)
- Conditional logic explanations

**Decision Matrix:**

| What to Log | Use | Reason |
|-------------|-----|--------|
| Connection time (ms) | PT_LogPerf | Numeric, aggregatable, benchmark target |
| "Connected to peer X" | PT_LOG_INFO | Human context, one-time event |
| Message RTT (µs) | PT_LogPerf | Numeric, high-frequency, graphable |
| "Retrying send due to flow control" | PT_LOG_WARN | Diagnostic, decision explanation |
| Bytes sent per message | PT_LogPerf | Numeric, aggregatable |
| "Buffer at 80% capacity" | PT_LOG_WARN | Threshold alert, human action needed |
| Discovery response time | PT_LogPerf | Numeric, network performance metric |
| State machine transition | PT_LOG_DEBUG | Diagnostic, explains behavior |
| Queue depth at each poll | PT_LogPerf | Numeric, for capacity planning |
| "Invalid packet magic" | PT_LOG_ERR | Error, needs investigation |

**Example Combined Usage:**

```c
/* Performance measurement with PT_LogPerf */
uint32_t start = PT_LogElapsedMs(log);
err = TCPSend(stream, data, len);
uint32_t elapsed = PT_LogElapsedMs(log) - start;

PT_LogPerfEntry entry = {
    .seq_num = PT_LogNextSeq(log),
    .timestamp_ms = start,
    .event_type = PERF_TCP_SEND,
    .value1 = (uint16_t)len,        /* bytes sent */
    .value2 = (uint16_t)elapsed,    /* duration ms */
    .flags = (err == noErr) ? 0x01 : 0x00,
    .category = PT_LOG_CAT_SEND
};
PT_LogPerf(log, &entry, "tcp_send");

/* Diagnostic context with PT_LOG_DEBUG */
if (err != noErr) {
    PT_LOG_ERR(log, PT_LOG_CAT_SEND, "TCPSend failed: %d after %ums", err, elapsed);
} else {
    PT_LOG_DEBUG(log, PT_LOG_CAT_SEND, "Sent %d bytes to peer %d", len, peer_id);
}
```

### Performance Entry Text Format

When PT_LOG_CAT_PERF is enabled, performance entries appear in the text log:

```
[TIMESTAMP][INF] PERF seq=N type=T v1=V1 v2=V2 flags=0xFF cat=0xCCCC [label]
```

Example:
```
[00005432][INF] PERF seq=42 type=1 v1=1024 v2=512 flags=0x01 cat=0x0002 send_complete
[00005433][INF] PERF seq=43 type=2 v1=256 v2=0 flags=0x00 cat=0x0002 recv_data
```

Where:
- `seq`: Sequence number from PT_LogNextSeq()
- `type`: Application-defined event type (1=send, 2=recv, etc.)
- `v1`, `v2`: Application-defined values (e.g., byte counts)
- `flags`: Application-defined flags (e.g., 0x01 = success)
- `cat`: Category metadata (e.g., 0x0002 = PT_LOG_CAT_NETWORK)
- `label`: Optional label from PT_LogPerf() call

### Parsing Log Files

For automated analysis, parse each line with:

```c
/* Simple log line parser (POSIX) */
int parse_log_line(const char *line, uint32_t *timestamp, char *level,
                   char *message, size_t msg_size) {
    return sscanf(line, "[%u][%3s] %[^\n]", timestamp, level, message);
}

/* Parse performance entry */
int parse_perf_line(const char *line, uint32_t *ts, uint32_t *seq,
                    int *type, int *v1, int *v2, int *flags, int *cat) {
    return sscanf(line, "[%u][INF] PERF seq=%u type=%d v1=%d v2=%d flags=0x%x cat=0x%x",
                  ts, seq, type, v1, v2, flags, cat);
}
```

---

## Summary of All Optimizations

This plan incorporates the following performance improvements based on data-oriented design review:

### Architectural (Applied Before Implementation)

| Optimization | Location | Impact |
|--------------|----------|--------|
| Struct fields ordered largest-to-smallest | `struct pt_log` (both platforms) | Eliminates implicit padding |
| `PT_LogLevel` stored as `uint8_t` | `struct pt_log` (both platforms) | Saves 3 bytes per context |
| `auto_flush` changed to `uint8_t` | `struct pt_log` (both platforms) | Better packing, less padding |
| Explicit padding bytes | `struct pt_log` (both platforms) | Documents layout, prevents compiler surprises |
| Buffer size reduced to 256 bytes | Classic Mac only | Fits in 68030 data cache |
| PT_LogPerfEntry uses `__attribute__((packed))` | `pt_log.h` | Guarantees 16-byte size |
| PT_LogPerfEntry.category is metadata | `pt_log.h` | Callback receivers can filter; global PERF controls text log |
| Stack allocation after filtering | `PT_LogWriteV` (both platforms) | Saves ~400 bytes stack on filtered messages |
| PB-style File Manager calls | `pt_log_mac.c` | Maximum compatibility across toolboxes |

### Implementation (Applied During Coding)

| Optimization | Location | Impact |
|--------------|----------|--------|
| `sprintf` return value | `pt_log_mac.c` | Eliminates redundant `strlen()` |
| Inline copy for small strings | `write_to_buffer` (Mac) | Faster than BlockMoveData for ≤32 bytes |
| Shift-based tick conversion | `PT_LogElapsedMs` (Mac 68k) | Avoids expensive division on 68000/68010 |
| Atomic fetch-add (RELAXED) | `PT_LogNextSeq` (POSIX) | Lock-free, no memory barriers needed |
| Separate sync_file() from flush_buffer() | `pt_log_posix.c` | fflush only when explicitly needed |
| Unsigned bounds check | `PT_LogLevelName` (both) | Handles negative cast values safely |
| PT_LOG_INLINE_COPY_THRESHOLD | `pt_log_mac.c` | Tunable threshold with documentation |

### Documentation (Applied for Safety)

| Documentation | Location | Purpose |
|---------------|----------|---------|
| Message length constraint | Header comment, Session 0.3 warning | Prevent vsprintf buffer overflow |
| ISR-safety template | ISR-Safety Rules section | Concrete pattern for Phase 5-7 |
| ISR-safety guard macro | Convenience Macros (PT_ISR_CONTEXT) | Catch accidental ISR-level PT_Log calls at compile time |
| Thread-safety guarantee | Header comment | Clarify POSIX multi-thread safety |
| Platform output differences | PT_LogOutput enum comment | Explain console behavior per platform |
| Cache layout comments | Struct definitions | Help future maintainers |
| PT_LogPerfEntry.category usage | Header comment, PT_LogPerf comment | Clarify metadata vs filtering behavior |
| PB-style File Manager rationale | pt_log_mac.c helpers | Explain why Create()/SetFPos() not used |
| Example log output format | Example Log Output section | Help verify correct behavior |
| Phase 1 integration template | Phase Dependencies section | Concrete context struct and init code |
| Buffer size justification | Constants sections | Document why 512 (POSIX) vs 256 (Mac) |
| Compile-time size assertion | After PT_LogPerfEntry | Catch struct layout changes at compile time |
| Callback error contract | Callback Types section | Document that callbacks should not fail |
| Error logging checklist | Logging Guidelines section | Ensure consistent error path coverage |
| Initialization logging checklist | Logging Guidelines section | Document what to log at startup |
| Timeout logging templates | Logging Guidelines section | Consistent timeout message formats |
| PT_LogPerf vs PT_LOG_DEBUG | Decision matrix section | When to use structured vs text logging |
| log_error_code type guidance | ISR template comment | Match field type to platform error type |

### Not Applied (Deferred or Rejected)

| Item | Reason |
|------|--------|
| Hardware divide on PPC | PPC has fast DIVW instruction; shift approximation only on 68k |
| Lazy timestamp computation | Would complicate code; benefit is marginal |
| Combined level+category check | Separate checks may be faster on 68k (no branch prediction) |
| PT_LogPerfEntry.category filtering | Category is metadata for callbacks; global PT_LOG_CAT_PERF controls text output |
| g_level_names inlining | Array lookup is after filtering; cost is minimal |
| Mutex-free POSIX logging | Would require lock-free buffer; complexity not justified |

## Test Coverage Updates (2026-02-03)

**Additional tests implemented** to address gaps identified in TEST_GAP_ANALYSIS.md:

1. **tests/test_isr_safety_compile.c** (HIGH) - Compile-time verification that PT_ISR_CONTEXT disables all logging macros. Intentionally causes linker error to catch ISR logging bugs.

2. **tests/test_log_threads.c** (MEDIUM) - Multi-threaded stress test:
   - 8 threads × 1000 messages = 8000 total
   - Verifies file output integrity
   - Verifies callback message counting
   - Verifies sequence number uniqueness and monotonicity

All tests pass. Phase 0 test coverage: 85%+
