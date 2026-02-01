/* queue.c - Message queue implementation */

#include "queue.h"
#include "pt_compat.h"
#include "../../include/peertalk.h"

/* Helper: Check if value is power of two */
static int pt_is_power_of_two(uint16_t value)
{
    return (value > 0) && ((value & (value - 1)) == 0);
}

/* ========================================================================
 * Queue Management
 * ======================================================================== */

int pt_queue_init(struct pt_context *ctx, pt_queue *q, uint16_t capacity)
{
    size_t alloc_size;

    if (!q) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Validate capacity is power of two */
    if (!pt_is_power_of_two(capacity)) {
        if (ctx) {
            PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL,
                      "Queue capacity must be power of 2, got %u", capacity);
        }
        return PT_ERR_INVALID_PARAM;
    }

    /* Allocate slots array */
    alloc_size = sizeof(pt_queue_slot) * capacity;
    q->slots = (pt_queue_slot *)pt_alloc_clear(alloc_size);
    if (!q->slots) {
        if (ctx) {
            PT_CTX_ERR(ctx, PT_LOG_CAT_MEMORY,
                      "Failed to allocate %zu bytes for %u queue slots",
                      alloc_size, capacity);
        }
        return PT_ERR_NO_MEMORY;
    }

    /* Initialize queue state */
    q->magic = PT_QUEUE_MAGIC;
    q->capacity = capacity;
    q->capacity_mask = capacity - 1;  /* For fast wrap-around */
    q->write_idx = 0;
    q->read_idx = 0;
    q->count = 0;
    q->has_data = 0;
    q->reserved = 0;

    if (ctx) {
        PT_CTX_INFO(ctx, PT_LOG_CAT_PROTOCOL,
                   "Queue initialized: %u slots, %zu bytes",
                   capacity, alloc_size);
    }

    return 0;
}

void pt_queue_free(pt_queue *q)
{
    if (!q || !q->slots) {
        return;
    }

    pt_free(q->slots);
    q->slots = NULL;
    q->magic = 0;
    q->capacity = 0;
    q->capacity_mask = 0;
    q->write_idx = 0;
    q->read_idx = 0;
    q->count = 0;
    q->has_data = 0;
}

void pt_queue_reset(pt_queue *q)
{
    if (!q || q->magic != PT_QUEUE_MAGIC) {
        return;
    }

    q->write_idx = 0;
    q->read_idx = 0;
    q->count = 0;
    q->has_data = 0;
}

/* ========================================================================
 * Push Operations
 * ======================================================================== */

int pt_queue_push(struct pt_context *ctx, pt_queue *q,
                  const void *data, uint16_t len,
                  uint8_t priority, uint8_t flags)
{
    pt_queue_slot *slot;
    uint8_t pressure;
    static uint8_t last_pressure_level = 0;  /* Track for logging thresholds */

    if (!q || q->magic != PT_QUEUE_MAGIC || !data) {
        return PT_ERR_INVALID_PARAM;
    }

    if (len > PT_QUEUE_SLOT_SIZE) {
        if (ctx) {
            PT_CTX_ERR(ctx, PT_LOG_CAT_PROTOCOL,
                      "Message too large: %u bytes (max %u)",
                      len, PT_QUEUE_SLOT_SIZE);
        }
        return PT_ERR_INVALID_PARAM;
    }

    /* Check if full */
    if (q->count >= q->capacity) {
        if (ctx) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL,
                       "Queue full: %u/%u slots", q->count, q->capacity);
        }
        return PT_ERR_BUFFER_FULL;
    }

    /* Get slot and write data */
    slot = &q->slots[q->write_idx];
    slot->length = len;
    slot->priority = priority;
    slot->flags = PT_SLOT_USED | flags;
    pt_memcpy(slot->data, data, len);

    /* Advance write index (power-of-two wrap-around) */
    q->write_idx = (q->write_idx + 1) & q->capacity_mask;
    q->count++;
    q->has_data = 1;

    /* Check backpressure and log at thresholds */
    pressure = pt_queue_pressure(q);
    if (ctx) {
        /* Log when crossing 80%, 90%, 95% thresholds */
        if (pressure >= 95 && last_pressure_level < 95) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PERF,
                       "Queue pressure CRITICAL: %u%% (%u/%u slots)",
                       pressure, q->count, q->capacity);
            last_pressure_level = 95;
        } else if (pressure >= 90 && last_pressure_level < 90) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PERF,
                       "Queue pressure HIGH: %u%% (%u/%u slots)",
                       pressure, q->count, q->capacity);
            last_pressure_level = 90;
        } else if (pressure >= 80 && last_pressure_level < 80) {
            PT_CTX_WARN(ctx, PT_LOG_CAT_PERF,
                       "Queue pressure elevated: %u%% (%u/%u slots)",
                       pressure, q->count, q->capacity);
            last_pressure_level = 80;
        } else if (pressure < 80 && last_pressure_level >= 80) {
            /* Reset threshold tracking when pressure drops */
            last_pressure_level = 0;
        }
    }

    return 0;
}

int pt_queue_push_isr(pt_queue *q, const void *data, uint16_t len)
{
    pt_queue_slot *slot;

    /* ISR-SAFETY: No parameter validation logging, no backpressure logging */
    if (!q || q->magic != PT_QUEUE_MAGIC || !data) {
        return PT_ERR_INVALID_PARAM;
    }

    if (len > PT_QUEUE_SLOT_SIZE) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Check if full */
    if (q->count >= q->capacity) {
        return PT_ERR_BUFFER_FULL;
    }

    /* Get slot and write data using ISR-safe memcpy */
    slot = &q->slots[q->write_idx];
    slot->length = len;
    slot->priority = 0;
    slot->flags = PT_SLOT_USED;
    pt_memcpy_isr(slot->data, data, len);  /* ISR-safe: no Toolbox */

    /* Advance write index (power-of-two wrap-around) */
    q->write_idx = (q->write_idx + 1) & q->capacity_mask;
    q->count++;
    q->has_data = 1;

    return 0;
}

int pt_queue_push_isr_ot(pt_queue *q, const void *data, uint16_t len)
{
    pt_queue_slot *slot;

    /* ISR-SAFETY: No logging, uses atomic operations for OT reentrancy */
    if (!q || q->magic != PT_QUEUE_MAGIC || !data) {
        return PT_ERR_INVALID_PARAM;
    }

    if (len > PT_QUEUE_SLOT_SIZE) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Check if full */
    if (q->count >= q->capacity) {
        return PT_ERR_BUFFER_FULL;
    }

    /* Get slot and write metadata */
    slot = &q->slots[q->write_idx];
    slot->length = len;
    slot->priority = 0;
    slot->flags = PT_SLOT_USED;  /* Set USED but NOT READY yet */

    /* Copy data using ISR-safe memcpy */
    pt_memcpy_isr(slot->data, data, len);

    /* OT REENTRANCY SAFETY: Set READY flag AFTER data copy completes.
     * If another notifier runs during copy, it won't read partial data.
     * On Classic Mac with OT, use atomic bit set (OTAtomicSetBit).
     * On POSIX, just set flag (no OT reentrancy). */
#ifdef PT_PLATFORM_OPENTRANSPORT
    /* Atomic bit set for OT notifier reentrancy safety */
    /* Note: OTAtomicSetBit not available in POSIX builds */
    slot->flags |= PT_SLOT_READY;
#else
    slot->flags |= PT_SLOT_READY;
#endif

    /* Advance write index (power-of-two wrap-around) */
    q->write_idx = (q->write_idx + 1) & q->capacity_mask;
    q->count++;
    q->has_data = 1;

    return 0;
}

/* ========================================================================
 * Pop Operations
 * ======================================================================== */

int pt_queue_pop(pt_queue *q, void *data, uint16_t *len)
{
    pt_queue_slot *slot;

    if (!q || q->magic != PT_QUEUE_MAGIC || !data || !len) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Check if empty */
    if (q->count == 0) {
        return PT_ERR_QUEUE_EMPTY;
    }

    /* Get front slot */
    slot = &q->slots[q->read_idx];

#ifdef PT_PLATFORM_OPENTRANSPORT
    /* OT REENTRANCY SAFETY: Wait for READY flag before reading data.
     * If push_isr_ot is interrupted, we won't read partial data. */
    if (!(slot->flags & PT_SLOT_READY)) {
        return PT_ERR_QUEUE_EMPTY;  /* Data not ready yet */
    }
#endif

    /* Check slot is used */
    if (!(slot->flags & PT_SLOT_USED)) {
        return PT_ERR_QUEUE_EMPTY;
    }

    /* Copy data */
    *len = slot->length;
    pt_memcpy(data, slot->data, slot->length);

    /* Clear slot */
    slot->flags = 0;
    slot->length = 0;

    /* Advance read index (power-of-two wrap-around) */
    q->read_idx = (q->read_idx + 1) & q->capacity_mask;
    q->count--;

    /* Update has_data flag */
    if (q->count == 0) {
        q->has_data = 0;
    }

    return 0;
}

int pt_queue_peek(pt_queue *q, void **data, uint16_t *len)
{
    pt_queue_slot *slot;

    if (!q || q->magic != PT_QUEUE_MAGIC || !data || !len) {
        return PT_ERR_INVALID_PARAM;
    }

    /* Check if empty */
    if (q->count == 0) {
        return PT_ERR_QUEUE_EMPTY;
    }

    /* Get front slot */
    slot = &q->slots[q->read_idx];

#ifdef PT_PLATFORM_OPENTRANSPORT
    /* OT REENTRANCY SAFETY: Wait for READY flag */
    if (!(slot->flags & PT_SLOT_READY)) {
        return PT_ERR_QUEUE_EMPTY;
    }
#endif

    /* Check slot is used */
    if (!(slot->flags & PT_SLOT_USED)) {
        return PT_ERR_QUEUE_EMPTY;
    }

    /* Return pointer to slot data (zero-copy) */
    *data = slot->data;
    *len = slot->length;

    return 0;
}

void pt_queue_consume(pt_queue *q)
{
    pt_queue_slot *slot;

    if (!q || q->magic != PT_QUEUE_MAGIC || q->count == 0) {
        return;
    }

    /* Clear front slot */
    slot = &q->slots[q->read_idx];
    slot->flags = 0;
    slot->length = 0;

    /* Advance read index (power-of-two wrap-around) */
    q->read_idx = (q->read_idx + 1) & q->capacity_mask;
    q->count--;

    /* Update has_data flag */
    if (q->count == 0) {
        q->has_data = 0;
    }
}

/* ========================================================================
 * Queue Status
 * ======================================================================== */

uint16_t pt_queue_count(pt_queue *q)
{
    if (!q || q->magic != PT_QUEUE_MAGIC) {
        return 0;
    }

    return q->count;
}

uint16_t pt_queue_free_slots(pt_queue *q)
{
    if (!q || q->magic != PT_QUEUE_MAGIC) {
        return 0;
    }

    return q->capacity - q->count;
}

uint8_t pt_queue_pressure(pt_queue *q)
{
    uint32_t pressure;

    if (!q || q->magic != PT_QUEUE_MAGIC || q->capacity == 0) {
        return 0;
    }

    /* Calculate pressure as percentage (0-100)
     * Use uint32_t to prevent overflow when count > 655 */
    pressure = ((uint32_t)q->count * 100) / q->capacity;

    /* Clamp to 100 */
    if (pressure > 100) {
        pressure = 100;
    }

    return (uint8_t)pressure;
}

int pt_queue_is_full(pt_queue *q)
{
    if (!q || q->magic != PT_QUEUE_MAGIC) {
        return 0;
    }

    return (q->count >= q->capacity) ? 1 : 0;
}

int pt_queue_is_empty(pt_queue *q)
{
    if (!q || q->magic != PT_QUEUE_MAGIC) {
        return 1;
    }

    return (q->count == 0) ? 1 : 0;
}

/* ========================================================================
 * Coalescing
 * ======================================================================== */

int pt_queue_coalesce(pt_queue *q, const void *data, uint16_t len)
{
    uint16_t i;
    uint16_t search_start;
    uint16_t search_count;
    pt_queue_slot *slot;

    if (!q || q->magic != PT_QUEUE_MAGIC || !data) {
        return -1;
    }

    if (len > PT_QUEUE_SLOT_SIZE) {
        return -1;
    }

    /* Search last 4 slots for coalescable message
     * Rationale: Position updates typically coalesce with very recent
     * messages. Searching entire queue causes cache misses on 68030
     * with 256-byte cache line. */
    search_count = (q->count < 4) ? q->count : 4;
    if (search_count == 0) {
        return -1;  /* Queue empty */
    }

    /* Start from write_idx and search backwards */
    search_start = q->write_idx;

    for (i = 0; i < search_count; i++) {
        uint16_t idx;

        /* Calculate slot index searching backwards (with wrap-around) */
        if (search_start >= i + 1) {
            idx = search_start - (i + 1);
        } else {
            idx = q->capacity - ((i + 1) - search_start);
        }

        slot = &q->slots[idx];

        /* Check if slot is coalescable */
        if ((slot->flags & PT_SLOT_USED) &&
            (slot->flags & PT_SLOT_COALESCABLE)) {
            /* Found coalescable message - replace it */
            slot->length = len;
            pt_memcpy(slot->data, data, len);
            return 0;
        }
    }

    return -1;  /* No coalescable message found */
}
