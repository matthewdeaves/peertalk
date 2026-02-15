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

#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
#include <MacMemory.h>
#endif

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

/* ========================================================================== */
/* Bootstrap - Early Initialization                                           */
/* ========================================================================== */

/**
 * Bootstrap PeerTalk at the very start of main().
 *
 * CRITICAL: Call this BEFORE any Toolbox initialization!
 *
 * This function:
 *   1. On Classic Mac: Calls MaxApplZone() + MoreMasters() to prepare heap
 *   2. Allocates TCP receive buffers while heap is contiguous
 *   3. Returns pool for use in PeerTalk_Config
 *
 * The earlier this is called, the larger buffers we can allocate:
 *   - Before InitGraf: Can typically get 16-32KB buffers on 8MB Mac
 *   - After InitGraf: May only get 8KB buffers
 *   - After InitWindows: May only get 4KB buffers
 *
 * @param max_peers  Maximum simultaneous peers (determines buffer count)
 * @return Buffer pool handle, or NULL on POSIX/failure
 */
PeerTalk_BufferPool *PeerTalk_Bootstrap(uint16_t max_peers)
{
#if defined(PT_PLATFORM_MACTCP) || defined(PT_PLATFORM_OT)
    PeerTalk_BufferPool *pool;
    unsigned long free_mem;
    uint16_t scaled_peers;

    /*
     * Classic Mac heap preparation:
     *
     * MaxApplZone() - Extends application heap to maximum size.
     *   This must be called BEFORE any allocations for best results.
     *   If toolbox already called, this has no effect (heap already grown).
     *
     * MoreMasters() - Pre-allocates a block of master pointers.
     *   Prevents heap fragmentation from incremental master pointer
     *   allocation. Call 2-4 times for apps with many handles.
     */
    MaxApplZone();
    MoreMasters();
    MoreMasters();
    MoreMasters();
    MoreMasters();  /* 4 calls = plenty of master pointers */

    /* Auto-scale peer count based on available memory.
     *
     * Each peer needs:
     *   - TCP receive buffer (4-32KB, auto-sized)
     *   - Send queue (~4KB)
     *   - Receive queue (~4KB)
     *   - Direct buffers (optional, ~16KB)
     *   Total: ~12-56KB per peer
     *
     * Memory tiers (based on FreeMem after MaxApplZone):
     *   < 200KB: 1 peer  (very low memory - Mac Plus with apps loaded)
     *   < 400KB: 2 peers (low memory - Mac SE 4MB, Classic)
     *   < 800KB: 3 peers (moderate - Mac SE/30, LC)
     *   >= 800KB: Use requested peer count (high memory - Mac II, Quadra, PPC)
     */
    free_mem = (unsigned long)FreeMem();

    if (free_mem < 200UL * 1024UL) {
        scaled_peers = 1;  /* Absolute minimum */
    } else if (free_mem < 400UL * 1024UL) {
        scaled_peers = 2;  /* Low memory (Mac SE) */
    } else if (free_mem < 800UL * 1024UL) {
        scaled_peers = 3;  /* Moderate memory */
    } else {
        scaled_peers = max_peers;  /* High memory - use requested count */
    }

    /* Now allocate buffers while heap is still contiguous */
    pool = PeerTalk_AllocateBuffersAuto(scaled_peers);

    /* Log scaling decision (if allocation succeeded and we scaled down) */
    if (pool && scaled_peers < max_peers) {
        /* Note: Can't log here since PT_Log isn't initialized yet.
         * The test app will log buffer pool info after PT_LogCreate(). */
    }

    return pool;

#else
    /* POSIX: Buffer allocation is not needed (no 25% threshold issue) */
    (void)max_peers;
    return NULL;
#endif
}

/* ========================================================================== */
/* Buffer Pool Allocation                                                     */
/* ========================================================================== */

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
 * Allocate a buffer pool with automatic sizing based on available memory.
 *
 * This function checks available memory and automatically chooses the largest
 * buffer size that will fit. Ideal for apps that want optimal performance
 * without manual memory management.
 *
 * Sizing logic (considers total allocation including overhead):
 *   - If room for 32KB buffers: use PT_TCP_BUF_32K (25% threshold = 8KB)
 *   - If room for 16KB buffers: use PT_TCP_BUF_16K (25% threshold = 4KB)
 *   - If room for 8KB buffers:  use PT_TCP_BUF_8K  (25% threshold = 2KB)
 *   - Otherwise:                use PT_TCP_BUF_4K  (25% threshold = 1KB)
 *
 * Usage:
 *   pool = PeerTalk_AllocateBuffersAuto(4);  // 4 peers, auto-size
 *   if (pool) {
 *       uint32_t size;
 *       PeerTalk_GetBufferPoolInfo(pool, NULL, &size);
 *       printf("Allocated %luKB buffers\n", size/1024);
 *   }
 *
 * @param count  Number of buffers to allocate (should match max_peers)
 * @return Pool handle, or NULL if even minimum allocation failed
 */
PeerTalk_BufferPool *PeerTalk_AllocateBuffersAuto(uint16_t count)
{
    PeerTalk_BufferPool *pool;
    unsigned long max_block;
    size_t overhead;
    size_t total_needed;

    if (count == 0) {
        return NULL;
    }

    /* Get available contiguous memory */
    max_block = (unsigned long)pt_get_max_block();

    /* Calculate overhead (header + pointers + flags + alignment) */
    overhead = sizeof(PeerTalk_BufferPool) +
               (count * sizeof(void *)) +
               ((count + 3) & ~(size_t)3);

    /* Try sizes from largest to smallest */

    /* Try 32KB buffers (best for file transfer, high-throughput) */
    total_needed = overhead + ((size_t)count * PT_TCP_BUF_32K);
    if (max_block >= total_needed + 4096) {  /* Leave 4KB headroom */
        pool = PeerTalk_AllocateBuffers(count, PT_TCP_BUF_32K);
        if (pool) return pool;
    }

    /* Try 16KB buffers (good for most apps) */
    total_needed = overhead + ((size_t)count * PT_TCP_BUF_16K);
    if (max_block >= total_needed + 2048) {  /* Leave 2KB headroom */
        pool = PeerTalk_AllocateBuffers(count, PT_TCP_BUF_16K);
        if (pool) return pool;
    }

    /* Try 8KB buffers (interactive apps, lower memory) */
    total_needed = overhead + ((size_t)count * PT_TCP_BUF_8K);
    if (max_block >= total_needed + 1024) {  /* Leave 1KB headroom */
        pool = PeerTalk_AllocateBuffers(count, PT_TCP_BUF_8K);
        if (pool) return pool;
    }

    /* Fall back to 4KB minimum */
    pool = PeerTalk_AllocateBuffers(count, PT_TCP_BUF_4K);
    return pool;  /* May be NULL if even 4KB fails */
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
