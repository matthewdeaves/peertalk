/**
 * @file buffer_pool.c
 * @brief TCP Receive Buffer Pre-Allocation Pool
 *
 * Enables applications to allocate TCP receive buffers early (at the start of
 * main()) before heap fragmentation, then pass them to PeerTalk via config.
 *
 * On Classic Mac, this is critical for throughput because:
 * - MacTCP's 25% threshold rule: receive completes when 25% of buffer fills
 * - Larger buffers = fewer completions = higher throughput
 * - 4KB buffer -> completes at 1KB (slow)
 * - 16KB buffer -> completes at 4KB (4x better)
 * - Early allocation gets larger contiguous blocks
 */

#include "pt_internal.h"
#include "pt_compat.h"

/**
 * Buffer pool structure.
 *
 * Allocated as a single block containing:
 * - This header
 * - Array of buffer pointers
 * - Array of in_use flags
 * - The actual buffers themselves
 *
 * This minimizes fragmentation by keeping everything together.
 */
struct PeerTalk_BufferPool {
    uint16_t    count;          /* Number of buffers */
    uint16_t    _pad;           /* Alignment padding */
    uint32_t    buffer_size;    /* Size of each buffer */
    uint16_t    in_use_count;   /* Number currently in use */
    uint16_t    _pad2;          /* Alignment padding */
    void      **buffers;        /* Array of buffer pointers */
    uint8_t    *in_use;         /* Array of in_use flags */
    /* Actual buffers follow in memory */
};

/**
 * Allocate a pool of TCP receive buffers.
 *
 * Call this at the VERY START of main(), before any other allocations:
 *
 *   int main(void) {
 *       PeerTalk_BufferPool *pool;
 *       PeerTalk_Config config;
 *
 *       // First thing - allocate buffers while heap is contiguous
 *       pool = PeerTalk_AllocateBuffers(4, PT_TCP_BUF_16K);
 *       if (!pool) {
 *           // Try smaller size
 *           pool = PeerTalk_AllocateBuffers(4, PT_TCP_BUF_8K);
 *       }
 *
 *       // Configure PeerTalk with the pool
 *       config.buffer_pool = pool;
 *       PeerTalk_Init(&config, ...);
 *       ...
 *   }
 *
 * @param count       Number of buffers to allocate (should match max_peers)
 * @param buffer_size Size of each buffer (use PT_TCP_BUF_* constants)
 * @return Pool handle, or NULL if allocation failed
 */
PeerTalk_BufferPool *PeerTalk_AllocateBuffers(uint16_t count, uint32_t buffer_size)
{
    PeerTalk_BufferPool *pool;
    size_t header_size;
    size_t pointers_size;
    size_t flags_size;
    size_t buffers_size;
    size_t total_size;
    char *mem;
    char *buf_start;
    uint16_t i;

    if (count == 0 || buffer_size == 0) {
        return NULL;
    }

    /* Calculate sizes with alignment */
    header_size = sizeof(PeerTalk_BufferPool);
    pointers_size = count * sizeof(void *);
    flags_size = count * sizeof(uint8_t);
    /* Align buffers to 4-byte boundary */
    flags_size = (flags_size + 3) & ~(size_t)3;
    buffers_size = (size_t)count * buffer_size;

    total_size = header_size + pointers_size + flags_size + buffers_size;

    /* Allocate as single contiguous block */
    mem = (char *)pt_plat_alloc(total_size);
    if (mem == NULL) {
        return NULL;
    }

    /* Clear the entire block */
    pt_memset(mem, 0, total_size);

    /* Set up structure */
    pool = (PeerTalk_BufferPool *)mem;
    pool->count = count;
    pool->buffer_size = buffer_size;
    pool->in_use_count = 0;

    /* Arrays follow the header */
    pool->buffers = (void **)(mem + header_size);
    pool->in_use = (uint8_t *)(mem + header_size + pointers_size);

    /* Actual buffers follow the arrays */
    buf_start = mem + header_size + pointers_size + flags_size;

    /* Initialize buffer pointers */
    for (i = 0; i < count; i++) {
        pool->buffers[i] = buf_start + ((size_t)i * buffer_size);
        pool->in_use[i] = 0;
    }

    return pool;
}

/**
 * Free a buffer pool.
 *
 * Call this after PeerTalk_Shutdown() when you no longer need the pool.
 * Safe to pass NULL.
 *
 * @param pool Pool to free, or NULL
 */
void PeerTalk_FreeBuffers(PeerTalk_BufferPool *pool)
{
    if (pool != NULL) {
        /* Everything is in one block, so one free handles it all */
        pt_plat_free(pool);
    }
}

/**
 * Query pool properties.
 *
 * @param pool      Pool to query
 * @param out_count Receives number of buffers (may be NULL)
 * @param out_size  Receives size of each buffer (may be NULL)
 */
void PeerTalk_GetBufferPoolInfo(const PeerTalk_BufferPool *pool,
                                 uint16_t *out_count, uint32_t *out_size)
{
    if (pool == NULL) {
        if (out_count) *out_count = 0;
        if (out_size) *out_size = 0;
        return;
    }

    if (out_count) *out_count = pool->count;
    if (out_size) *out_size = pool->buffer_size;
}

/* ========================================================================== */
/* Internal Pool Management (used by MacTCP TCP stream creation)              */
/* ========================================================================== */

/**
 * Get a buffer from the pool.
 *
 * Internal function called by pt_mactcp_tcp_create() when config has a pool.
 *
 * @param pool Pool to get buffer from
 * @return Buffer pointer, or NULL if pool exhausted
 */
void *pt_buffer_pool_get(PeerTalk_BufferPool *pool)
{
    uint16_t i;

    if (pool == NULL) {
        return NULL;
    }

    for (i = 0; i < pool->count; i++) {
        if (!pool->in_use[i]) {
            pool->in_use[i] = 1;
            pool->in_use_count++;
            return pool->buffers[i];
        }
    }

    return NULL;  /* Pool exhausted */
}

/**
 * Return a buffer to the pool.
 *
 * Internal function called when a TCP stream is released.
 *
 * @param pool   Pool the buffer belongs to
 * @param buffer Buffer to return
 * @return 1 if buffer was from this pool and returned, 0 otherwise
 */
int pt_buffer_pool_return(PeerTalk_BufferPool *pool, void *buffer)
{
    uint16_t i;

    if (pool == NULL || buffer == NULL) {
        return 0;
    }

    for (i = 0; i < pool->count; i++) {
        if (pool->buffers[i] == buffer) {
            if (pool->in_use[i]) {
                pool->in_use[i] = 0;
                if (pool->in_use_count > 0) {
                    pool->in_use_count--;
                }
            }
            return 1;
        }
    }

    return 0;  /* Not from this pool */
}

/**
 * Get the buffer size for a pool.
 *
 * @param pool Pool to query
 * @return Buffer size, or 0 if pool is NULL
 */
uint32_t pt_buffer_pool_size(const PeerTalk_BufferPool *pool)
{
    if (pool == NULL) {
        return 0;
    }
    return pool->buffer_size;
}
