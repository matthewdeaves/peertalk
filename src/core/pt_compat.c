/**
 * @file pt_compat.c
 * @brief Cross-platform compatibility layer implementation
 */

#include "pt_compat.h"

#if defined(PT_PLATFORM_POSIX)
    #include <stdlib.h>
    #include <string.h>
    #include <stdio.h>
#elif defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
    #include <MacMemory.h>
    #ifdef PT_PLATFORM_OT
        #include <OpenTransport.h>
    #endif
#endif

/* ========================================================================== */
/* Memory Allocation - POSIX                                                  */
/* ========================================================================== */

#if defined(PT_PLATFORM_POSIX)

void *pt_alloc(size_t size) {
    return malloc(size);
}

void pt_free(void *ptr) {
    free(ptr);
}

void *pt_alloc_clear(size_t size) {
    return calloc(1, size);
}

size_t pt_get_free_mem(void) {
    /* POSIX: return effectively unlimited */
    return 1024UL * 1024UL * 1024UL; /* 1GB */
}

size_t pt_get_max_block(void) {
    /* POSIX: return effectively unlimited */
    return 1024UL * 1024UL * 1024UL; /* 1GB */
}

#endif /* PT_PLATFORM_POSIX */

/* ========================================================================== */
/* Memory Allocation - Classic Mac                                            */
/* ========================================================================== */

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)

void *pt_alloc(size_t size) {
    return NewPtr((Size)size);
}

void pt_free(void *ptr) {
    if (ptr != NULL) {
        DisposePtr((Ptr)ptr);
    }
}

void *pt_alloc_clear(size_t size) {
    return NewPtrClear((Size)size);
}

size_t pt_get_free_mem(void) {
    return (size_t)FreeMem();
}

size_t pt_get_max_block(void) {
    return (size_t)MaxBlock();
}

#endif /* PT_PLATFORM_MACTCP || PT_PLATFORM_OT */

/* ========================================================================== */
/* Atomic Operations - POSIX                                                  */
/* ========================================================================== */

#if defined(PT_PLATFORM_POSIX)

/**
 * IMPORTANT: POSIX 'atomic' operations - NOT thread-safe for true
 * multi-threading!
 *
 * PeerTalk uses a single-threaded, non-blocking event loop model.
 * These volatile operations are sufficient for that use case.
 *
 * For applications that use PeerTalk from multiple threads, external
 * synchronization (e.g., pthread_mutex) is required around PeerTalk
 * API calls.
 */

void pt_atomic_set_bit(pt_atomic_t *flags, int bit) {
    *flags |= (1U << bit);
}

void pt_atomic_clear_bit(pt_atomic_t *flags, int bit) {
    *flags &= ~(1U << bit);
}

int pt_atomic_test_bit(pt_atomic_t *flags, int bit) {
    return (*flags & (1U << bit)) != 0;
}

int pt_atomic_test_and_clear_bit(pt_atomic_t *flags, int bit) {
    uint32_t mask = (1U << bit);
    int was_set = (*flags & mask) != 0;
    *flags &= ~mask;
    return was_set;
}

#endif /* PT_PLATFORM_POSIX */

/* ========================================================================== */
/* Atomic Operations - MacTCP (68k)                                           */
/* ========================================================================== */

#if defined(PT_PLATFORM_MACTCP)

/**
 * 68k atomic operations using volatile.
 *
 * Safe because:
 * 1. ASR only SETS bits (via OR): *flags |= mask
 * 2. Main loop only CLEARS bits (via AND): *flags &= ~mask
 * 3. No read-modify-write race (operations are one-way)
 * 4. 32-bit aligned access is atomic on 68000+ (hardware guarantee)
 *
 * Based on Motorola 68000 User's Manual, Section 8:
 * "32-bit aligned accesses complete in a single bus cycle and cannot
 * be interrupted mid-operation."
 *
 * Does NOT disable interrupts - unnecessary for this pattern.
 */

void pt_atomic_set_bit(pt_atomic_t *flags, int bit) {
    *flags |= (1U << bit);
}

void pt_atomic_clear_bit(pt_atomic_t *flags, int bit) {
    *flags &= ~(1U << bit);
}

int pt_atomic_test_bit(pt_atomic_t *flags, int bit) {
    return (*flags & (1U << bit)) != 0;
}

int pt_atomic_test_and_clear_bit(pt_atomic_t *flags, int bit) {
    uint32_t mask = (1U << bit);
    int was_set = (*flags & mask) != 0;
    *flags &= ~mask;
    return was_set;
}

#endif /* PT_PLATFORM_MACTCP */

/* ========================================================================== */
/* Atomic Operations - Open Transport (PPC)                                   */
/* ========================================================================== */

#if defined(PT_PLATFORM_OT)

/**
 * Open Transport atomic operations using OTAtomic* functions.
 *
 * OTAtomic functions operate on BYTES with bit indices 0-7.
 * For a 32-bit flags word, we need to calculate which byte contains
 * the bit we want to manipulate.
 *
 * Big-endian byte layout (32-bit word at address 0x1000):
 *   0x1000: byte 0 (bits 24-31) - OT bits 0-7
 *   0x1001: byte 1 (bits 16-23) - OT bits 0-7
 *   0x1002: byte 2 (bits 8-15)  - OT bits 0-7
 *   0x1003: byte 3 (bits 0-7)   - OT bits 0-7
 *
 * To set logical bit N (0-31):
 *   byte_offset = 3 - (N / 8)
 *   bit_in_byte = N % 8
 */

void pt_atomic_set_bit(pt_atomic_t *flags, int bit) {
    int byte_offset = 3 - (bit / 8);
    int bit_in_byte = bit % 8;
    UInt8 *byte_ptr = ((UInt8 *)flags) + byte_offset;
    OTAtomicSetBit(byte_ptr, (UInt8)bit_in_byte);
}

void pt_atomic_clear_bit(pt_atomic_t *flags, int bit) {
    int byte_offset = 3 - (bit / 8);
    int bit_in_byte = bit % 8;
    UInt8 *byte_ptr = ((UInt8 *)flags) + byte_offset;
    OTAtomicClearBit(byte_ptr, (UInt8)bit_in_byte);
}

int pt_atomic_test_bit(pt_atomic_t *flags, int bit) {
    int byte_offset = 3 - (bit / 8);
    int bit_in_byte = bit % 8;
    UInt8 *byte_ptr = ((UInt8 *)flags) + byte_offset;
    return OTAtomicTestBit(byte_ptr, (UInt8)bit_in_byte);
}

int pt_atomic_test_and_clear_bit(pt_atomic_t *flags, int bit) {
    int byte_offset = 3 - (bit / 8);
    int bit_in_byte = bit % 8;
    UInt8 *byte_ptr = ((UInt8 *)flags) + byte_offset;
    Boolean was_set = OTAtomicTestBit(byte_ptr, (UInt8)bit_in_byte);
    if (was_set) {
        OTAtomicClearBit(byte_ptr, (UInt8)bit_in_byte);
    }
    return was_set ? 1 : 0;
}

#endif /* PT_PLATFORM_OT */

/* ========================================================================== */
/* Memory Utilities - POSIX                                                   */
/* ========================================================================== */

#if defined(PT_PLATFORM_POSIX)

void *pt_memcpy(void *dest, const void *src, size_t n) {
    return memcpy(dest, src, n);
}

void *pt_memset(void *dest, int c, size_t n) {
    return memset(dest, c, n);
}

int pt_memcmp(const void *a, const void *b, size_t n) {
    return memcmp(a, b, n);
}

size_t pt_strlen(const char *s) {
    return strlen(s);
}

char *pt_strncpy(char *dest, const char *src, size_t n) {
    strncpy(dest, src, n);
    if (n > 0) {
        dest[n - 1] = '\0'; /* Always null-terminate */
    }
    return dest;
}

#endif /* PT_PLATFORM_POSIX */

/* ========================================================================== */
/* Memory Utilities - Classic Mac                                             */
/* ========================================================================== */

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)

void *pt_memcpy(void *dest, const void *src, size_t n) {
    BlockMoveData(src, dest, (Size)n);
    return dest;
}

void *pt_memset(void *dest, int c, size_t n) {
    unsigned char *p = (unsigned char *)dest;
    unsigned char value = (unsigned char)c;
    size_t i;
    for (i = 0; i < n; i++) {
        p[i] = value;
    }
    return dest;
}

int pt_memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    size_t i;
    for (i = 0; i < n; i++) {
        if (pa[i] != pb[i]) {
            return (pa[i] < pb[i]) ? -1 : 1;
        }
    }
    return 0;
}

size_t pt_strlen(const char *s) {
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}

char *pt_strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    /* Pad with nulls if src is shorter than n */
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    /* Always null-terminate if buffer size > 0 */
    if (n > 0) {
        dest[n - 1] = '\0';
    }
    return dest;
}

#endif /* PT_PLATFORM_MACTCP || PT_PLATFORM_OT */

/* ========================================================================== */
/* ISR-Safe Memory Copy (All Platforms)                                       */
/* ========================================================================== */

void *pt_memcpy_isr(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    size_t i;
    for (i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

/* ========================================================================== */
/* Formatted Output - POSIX                                                   */
/* ========================================================================== */

#if defined(PT_PLATFORM_POSIX)

int pt_vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    return vsnprintf(buf, size, fmt, args);
}

int pt_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    int result;
    va_start(args, fmt);
    result = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return result;
}

#endif /* PT_PLATFORM_POSIX */

/* ========================================================================== */
/* Formatted Output - Classic Mac (Limited Implementation)                    */
/* ========================================================================== */

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)

/**
 * Helper: Format an integer to a string buffer.
 * @param value Integer value
 * @param base Base (10 or 16)
 * @param width Field width (0 = no padding)
 * @param zero_pad 1 to pad with zeros, 0 to pad with spaces
 * @param uppercase 1 for uppercase hex (A-F), 0 for lowercase (a-f)
 * @param is_signed 1 if value should be treated as signed
 * @param buf Output buffer
 * @param bufsize Size of output buffer
 * @return Number of characters written
 */
static int pt_format_int(long value, int base, int width, int zero_pad,
                         int uppercase, int is_signed, char *buf,
                         size_t bufsize) {
    char temp[32];
    int len = 0;
    int negative = 0;
    unsigned long uvalue;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (bufsize == 0) return 0;

    /* Handle signed values */
    if (is_signed && value < 0) {
        negative = 1;
        uvalue = (unsigned long)(-value);
    } else {
        uvalue = (unsigned long)value;
    }

    /* Convert to string (reversed) */
    if (uvalue == 0) {
        temp[len++] = '0';
    } else {
        while (uvalue > 0 && len < (int)sizeof(temp)) {
            temp[len++] = digits[uvalue % base];
            uvalue /= base;
        }
    }

    /* Add sign if negative */
    if (negative) {
        temp[len++] = '-';
    }

    /* Calculate padding */
    int pad = width - len;
    if (pad < 0) pad = 0;

    int pos = 0;

    /* Pad with spaces (if not zero-padding) */
    if (!zero_pad) {
        while (pad > 0 && pos < (int)bufsize - 1) {
            buf[pos++] = ' ';
            pad--;
        }
    }

    /* If zero-padding and negative, write sign first */
    if (zero_pad && negative && pos < (int)bufsize - 1) {
        buf[pos++] = '-';
        len--; /* Sign already written */
    }

    /* Pad with zeros */
    if (zero_pad) {
        while (pad > 0 && pos < (int)bufsize - 1) {
            buf[pos++] = '0';
            pad--;
        }
    }

    /* Write digits (reversed) */
    int start = negative && zero_pad ? 1 : (negative ? 1 : 0);
    for (int i = len - 1; i >= start && pos < (int)bufsize - 1; i--) {
        buf[pos++] = temp[i];
    }

    buf[pos] = '\0';
    return pos;
}

int pt_vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    size_t pos = 0;
    const char *p = fmt;

    if (size == 0) return 0;

    while (*p && pos < size - 1) {
        if (*p != '%') {
            buf[pos++] = *p++;
            continue;
        }

        p++; /* Skip '%' */

        /* Parse flags and width */
        int zero_pad = 0;
        int width = 0;

        if (*p == '0') {
            zero_pad = 1;
            p++;
        }

        while (*p >= '0' && *p <= '9') {
            width = width * 10 + (*p - '0');
            p++;
        }

        /* Parse length modifier */
        int is_long = 0;
        if (*p == 'l') {
            is_long = 1;
            p++;
        }

        /* Parse format specifier */
        switch (*p) {
            case 'd': {
                long val = is_long ? va_arg(args, long) : va_arg(args, int);
                int written = pt_format_int(val, 10, width, zero_pad, 0, 1,
                                           buf + pos, size - pos);
                pos += written;
                break;
            }
            case 'u': {
                unsigned long val = is_long ? va_arg(args, unsigned long)
                                            : va_arg(args, unsigned int);
                int written = pt_format_int((long)val, 10, width, zero_pad,
                                           0, 0, buf + pos, size - pos);
                pos += written;
                break;
            }
            case 'x': {
                unsigned long val = is_long ? va_arg(args, unsigned long)
                                            : va_arg(args, unsigned int);
                int written = pt_format_int((long)val, 16, width, zero_pad,
                                           0, 0, buf + pos, size - pos);
                pos += written;
                break;
            }
            case 'X': {
                unsigned long val = is_long ? va_arg(args, unsigned long)
                                            : va_arg(args, unsigned int);
                int written = pt_format_int((long)val, 16, width, zero_pad,
                                           1, 0, buf + pos, size - pos);
                pos += written;
                break;
            }
            case 'p': {
                void *ptr = va_arg(args, void *);
                if (pos < size - 3) {
                    buf[pos++] = '0';
                    buf[pos++] = 'x';
                }
                int written = pt_format_int((long)ptr, 16, 8, 1, 0, 0,
                                           buf + pos, size - pos);
                pos += written;
                break;
            }
            case 's': {
                const char *str = va_arg(args, const char *);
                if (str == NULL) str = "(null)";
                while (*str && pos < size - 1) {
                    buf[pos++] = *str++;
                }
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                if (pos < size - 1) {
                    buf[pos++] = c;
                }
                break;
            }
            case '%': {
                if (pos < size - 1) {
                    buf[pos++] = '%';
                }
                break;
            }
            default:
                /* Unknown format - just copy it */
                if (pos < size - 1) {
                    buf[pos++] = '%';
                }
                if (pos < size - 1 && *p) {
                    buf[pos++] = *p;
                }
                break;
        }

        if (*p) p++;
    }

    buf[pos] = '\0';
    return (int)pos;
}

int pt_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    int result;
    va_start(args, fmt);
    result = pt_vsnprintf(buf, size, fmt, args);
    va_end(args);
    return result;
}

#endif /* PT_PLATFORM_MACTCP || PT_PLATFORM_OT */
