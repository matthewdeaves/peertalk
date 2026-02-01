/**
 * @file pt_compat.h
 * @brief Cross-platform portability layer for PeerTalk
 *
 * Provides portable abstractions for:
 * - Byte order conversion (network/host)
 * - Memory allocation and utilities
 * - Atomic flag operations (ISR-safe for Classic Mac)
 * - String formatting
 */

#ifndef PT_COMPAT_H
#define PT_COMPAT_H

#include "pt_types.h"
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/* Byte Order Conversion                                                      */
/* ========================================================================== */

/**
 * Network byte order is big-endian.
 * 68k/PPC Macs are big-endian - no conversion needed.
 * x86/x64 POSIX is little-endian - conversion needed.
 */

#if defined(PT_PLATFORM_POSIX)
    /* POSIX: use system byte order functions */
    #include <arpa/inet.h>
    #define pt_htons(x) htons(x)
    #define pt_htonl(x) htonl(x)
    #define pt_ntohs(x) ntohs(x)
    #define pt_ntohl(x) ntohl(x)
#else
    /* Classic Mac: big-endian, no conversion needed */
    #define pt_htons(x) (x)
    #define pt_htonl(x) (x)
    #define pt_ntohs(x) (x)
    #define pt_ntohl(x) (x)
#endif

/* ========================================================================== */
/* Memory Allocation                                                          */
/* ========================================================================== */

/**
 * Allocate memory (uninitialized).
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void *pt_alloc(size_t size);

/**
 * Free previously allocated memory.
 * @param ptr Pointer to free (NULL is safe to pass)
 */
void pt_free(void *ptr);

/**
 * Allocate zeroed memory.
 * @param size Number of bytes to allocate
 * @return Pointer to zeroed memory, or NULL on failure
 */
void *pt_alloc_clear(size_t size);

/**
 * Query available free memory (approximate).
 * @return Bytes of free memory, or 0 if unknown
 */
size_t pt_get_free_mem(void);

/**
 * Query largest allocatable block (approximate).
 * @return Bytes in largest contiguous block, or 0 if unknown
 */
size_t pt_get_max_block(void);

/* ========================================================================== */
/* Atomic Flag Operations (ISR-Safe for Classic Mac)                         */
/* ========================================================================== */

/**
 * IMPORTANT: These are NOT true multi-threading atomics!
 *
 * Classic Mac uses cooperative multitasking - only one thread of execution,
 * but ASR/notifiers CAN interrupt the main code at any time.
 *
 * Safe pattern: ASR only SETS bits, main loop only CLEARS bits.
 * - No read-modify-write race because operations are one-way
 * - 68k: 32-bit aligned access is atomic (hardware guarantee)
 * - PPC: OTAtomic* functions provide memory barriers
 *
 * For POSIX: Volatile is sufficient for single-threaded event loop.
 * NOT safe for true multi-threaded applications - use pthread_mutex instead.
 */

typedef volatile uint32_t pt_atomic_t;

/**
 * Set a bit in the flags word (safe from ISR/notifier).
 * @param flags Pointer to flags word
 * @param bit Bit number (0-31)
 */
void pt_atomic_set_bit(pt_atomic_t *flags, int bit);

/**
 * Clear a bit in the flags word (main loop only).
 * @param flags Pointer to flags word
 * @param bit Bit number (0-31)
 */
void pt_atomic_clear_bit(pt_atomic_t *flags, int bit);

/**
 * Test if a bit is set.
 * @param flags Pointer to flags word
 * @param bit Bit number (0-31)
 * @return Non-zero if bit is set, zero otherwise
 */
int pt_atomic_test_bit(pt_atomic_t *flags, int bit);

/**
 * Test and clear a bit atomically (main loop only).
 * @param flags Pointer to flags word
 * @param bit Bit number (0-31)
 * @return Non-zero if bit was set before clearing
 */
int pt_atomic_test_and_clear_bit(pt_atomic_t *flags, int bit);

/* Common flag bit definitions */
#define PT_FLAG_DATA_AVAILABLE      0
#define PT_FLAG_CONNECT_COMPLETE    1
#define PT_FLAG_DISCONNECT          2
#define PT_FLAG_ERROR               3
#define PT_FLAG_LISTEN_PENDING      4
#define PT_FLAG_SEND_COMPLETE       5

/* ========================================================================== */
/* Memory Utilities                                                           */
/* ========================================================================== */

/**
 * Copy memory (uses BlockMoveData on Classic Mac).
 * WARNING: NOT safe to call from ASR/notifier - use pt_memcpy_isr instead.
 * @param dest Destination pointer
 * @param src Source pointer
 * @param n Number of bytes to copy
 * @return dest
 */
void *pt_memcpy(void *dest, const void *src, size_t n);

/**
 * ISR-safe memory copy (byte-by-byte, no Toolbox calls).
 * Safe to call from MacTCP ASR, OT notifier, or ADSP completion.
 * @param dest Destination pointer
 * @param src Source pointer
 * @param n Number of bytes to copy
 * @return dest
 */
void *pt_memcpy_isr(void *dest, const void *src, size_t n);

/**
 * Set memory to a value.
 * @param dest Destination pointer
 * @param c Value to set (as int, converted to unsigned char)
 * @param n Number of bytes to set
 * @return dest
 */
void *pt_memset(void *dest, int c, size_t n);

/**
 * Compare memory regions.
 * @param a First region
 * @param b Second region
 * @param n Number of bytes to compare
 * @return <0 if a<b, 0 if equal, >0 if a>b
 */
int pt_memcmp(const void *a, const void *b, size_t n);

/* ========================================================================== */
/* String Utilities                                                           */
/* ========================================================================== */

/**
 * Calculate string length.
 * @param s Null-terminated string
 * @return Length in bytes (excluding null terminator)
 */
size_t pt_strlen(const char *s);

/**
 * Copy string safely (always null-terminates).
 * @param dest Destination buffer
 * @param src Source string
 * @param n Size of destination buffer
 * @return dest
 */
char *pt_strncpy(char *dest, const char *src, size_t n);

/* ========================================================================== */
/* Formatted Output                                                           */
/* ========================================================================== */

/**
 * Format string with variable arguments (vsnprintf wrapper).
 *
 * POSIX: Uses standard vsnprintf.
 * Classic Mac: Custom implementation with limited format support.
 *
 * Supported formats (Classic Mac):
 * - %d, %u, %x, %X (int)
 * - %ld, %lu, %lx (long)
 * - %s (string)
 * - %c (char)
 * - %p (pointer)
 * - %% (literal %)
 * - Field width and zero-padding (e.g., %08x)
 *
 * NOT supported (Classic Mac):
 * - Floating point (%f, %g, %e)
 * - Precision (%.2f)
 * - Dynamic width (%*)
 *
 * @param buf Destination buffer
 * @param size Size of buffer (including null terminator)
 * @param fmt Format string
 * @param args Variable argument list
 * @return Number of characters written (excluding null), or -1 on error
 */
int pt_vsnprintf(char *buf, size_t size, const char *fmt, va_list args);

/**
 * Format string with variable arguments (snprintf wrapper).
 * See pt_vsnprintf for format support details.
 */
int pt_snprintf(char *buf, size_t size, const char *fmt, ...);

/* ========================================================================== */
/* Platform-Portable Tick Getter (Phase 3)                                   */
/* ========================================================================== */

/**
 * Platform-portable tick getter
 *
 * Returns monotonically increasing tick count.
 * Resolution varies by platform but sufficient for coalescing/priority.
 *
 * Classic Mac: Uses TickCount() from OSUtils.h (60 ticks/second)
 * POSIX: Uses clock_gettime(CLOCK_MONOTONIC) converted to milliseconds
 *
 * CRITICAL: On Classic Mac, TickCount() is NOT documented as interrupt-safe
 * in Inside Macintosh Table B-3. Do NOT call from ASR/notifier - use
 * timestamp=0 instead. Use pt_queue_push_coalesce_isr() which avoids calling
 * this function at interrupt time.
 *
 * @return Tick count (platform-dependent units, monotonically increasing)
 */
uint32_t pt_get_ticks(void);

#ifdef __cplusplus
}
#endif

#endif /* PT_COMPAT_H */
