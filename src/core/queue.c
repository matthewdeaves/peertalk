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

    /* Initialize Phase 3 extensions (priority freelists, coalesce hash) */
    pt_queue_ext_init(q);

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
    /* cppcheck-suppress variableScope ; static var must persist across calls for threshold tracking */
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
    slot->coalesce_key = PT_COALESCE_NONE;  /* No coalescing for plain push */
    slot->next_slot = PT_SLOT_NONE;
    pt_memcpy(slot->data, data, len);

    /* Add to priority freelist (Phase 3 enhancement) */
    {
        pt_queue_ext *ext = &q->ext;
        uint16_t slot_idx = q->write_idx;

        if (ext->prio_tail[priority] != PT_SLOT_NONE) {
            /* List has items - append to tail */
            q->slots[ext->prio_tail[priority]].next_slot = slot_idx;
        } else {
            /* List empty - set head */
            ext->prio_head[priority] = slot_idx;
        }
        ext->prio_tail[priority] = slot_idx;
        ext->prio_count[priority]++;
    }

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
    /* cppcheck-suppress variableScope ; C89 style for Classic Mac compiler compatibility */
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

/* ========================================================================
 * Phase 3: Priority & Coalescing Operations
 * ======================================================================== */

/*
 * Initialize extended queue data structures
 *
 * Call this after pt_queue_init() from Phase 2 to set up
 * priority free-lists and coalesce hash table.
 */
void pt_queue_ext_init(pt_queue *q) {
    pt_queue_ext *ext;
    uint16_t i;
    uint16_t max_slots;  /* Loop invariant hoisted for optimization */

    if (!q) {
        /* Cannot log without context, but defend against NULL */
        return;
    }

#ifdef PT_QUEUE_SLOT_EXPECTED_SIZE
    /* Runtime size check for C89/C99 (C11 uses _Static_assert) */
    if (sizeof(pt_queue_slot) != PT_QUEUE_SLOT_EXPECTED_SIZE) {
        /* Size mismatch - likely compiler padding issue.
         * Use #pragma pack on Classic Mac compilers if this fails. */
        return;  /* Fail-safe: don't corrupt memory */
    }
#endif

    /* Capacity validation - queue capacity must not exceed PT_QUEUE_MAX_SLOTS
     * because next_slot pointers are only initialized for slots 0..MAX_SLOTS-1 */
    if (q->capacity > PT_QUEUE_MAX_SLOTS) {
        /* Configuration error - slots beyond MAX_SLOTS have uninitialized next_slot.
         * Caller should use smaller capacity or increase PT_QUEUE_MAX_SLOTS.
         * NOTE: Cannot log here since we don't have context - caller should check. */
        return;  /* Fail-safe: don't use queue with uninitialized slots */
    }

    ext = &q->ext;

    /* Initialize priority free-lists as empty */
    for (i = 0; i < PT_PRIO_COUNT; i++) {
        ext->prio_head[i] = PT_SLOT_NONE;
        ext->prio_tail[i] = PT_SLOT_NONE;
        ext->prio_count[i] = 0;
    }

    /* Initialize slot next_slot pointers (stored in slots themselves)
     * OPTIMIZATION: Hoist loop invariant min(capacity, MAX_SLOTS) */
    max_slots = (q->capacity < PT_QUEUE_MAX_SLOTS) ? q->capacity : PT_QUEUE_MAX_SLOTS;
    for (i = 0; i < max_slots; i++) {
        q->slots[i].next_slot = PT_SLOT_NONE;
    }

    /* Initialize coalesce hash table as empty */
    for (i = 0; i < PT_COALESCE_HASH_SIZE; i++) {
        ext->coalesce_hash[i] = PT_SLOT_NONE;
    }

    /* Initialize pending pop state */
    q->pending_pop_prio = 0;
    q->pending_pop_slot = PT_SLOT_NONE;

    /* Initialize ISR flags (for deferred logging from interrupt context) */
    q->isr_flags.queue_full = 0;
    q->isr_flags.coalesce_hit = 0;
    q->isr_flags.hash_collision = 0;

    /* NOTE: Logging should be done by caller who has context:
     * PT_LOG_DEBUG(ctx->log, PT_LOG_CAT_PROTOCOL, "Queue ext initialized: capacity=%u", q->capacity);
     */
}

/*
 * Pop highest priority message - O(1) using priority free-lists
 */
int pt_queue_pop_priority(pt_queue *q, void *data, uint16_t *len) {
    pt_queue_ext *ext;
    pt_queue_slot *slot;
    uint16_t slot_idx;
    int prio;

    if (!q || q->count == 0)
        return -1;

    ext = &q->ext;  /* Extended queue data */

    /* Find highest non-empty priority level */
    for (prio = PT_PRIO_CRITICAL; prio >= PT_PRIO_LOW; prio--) {
        if (ext->prio_head[prio] != PT_SLOT_NONE)
            break;
    }

    if (prio < PT_PRIO_LOW)
        return -1;  /* All lists empty (shouldn't happen if count > 0) */

    /* Pop from head of this priority's list */
    slot_idx = ext->prio_head[prio];
    slot = &q->slots[slot_idx];

    /* Update list head (next_slot is IN the slot for traversal locality) */
    ext->prio_head[prio] = slot->next_slot;
    if (ext->prio_head[prio] == PT_SLOT_NONE)
        ext->prio_tail[prio] = PT_SLOT_NONE;  /* List now empty */
    ext->prio_count[prio]--;

    /* Remove from coalesce hash if present */
    if (slot->coalesce_key != PT_COALESCE_NONE) {
        uint16_t hash_idx = PT_COALESCE_HASH(slot->coalesce_key);
        if (ext->coalesce_hash[hash_idx] == slot_idx)
            ext->coalesce_hash[hash_idx] = PT_SLOT_NONE;
    }

    /* Copy data out */
    if (data && len) {
        pt_memcpy(data, slot->data, slot->length);
        *len = slot->length;
    }

    /* Clear slot */
    slot->flags = 0;
    slot->length = 0;
    slot->next_slot = PT_SLOT_NONE;
    q->count--;

    if (q->count == 0)
        q->has_data = 0;

    return 0;
}

/*
 * Direct pop - returns pointer to slot data without copying (ZERO-COPY)
 */
int pt_queue_pop_priority_direct(pt_queue *q, const void **data_out,
                                  uint16_t *len_out) {
    pt_queue_ext *ext;
    pt_queue_slot *slot;
    uint16_t slot_idx;
    int prio;

    if (!q || q->count == 0 || !data_out || !len_out)
        return -1;

    ext = &q->ext;

    /* Find highest non-empty priority level */
    for (prio = PT_PRIO_CRITICAL; prio >= PT_PRIO_LOW; prio--) {
        if (ext->prio_head[prio] != PT_SLOT_NONE)
            break;
    }

    if (prio < PT_PRIO_LOW)
        return -1;

    /* Return pointer to head slot's data */
    slot_idx = ext->prio_head[prio];
    slot = &q->slots[slot_idx];

    *data_out = slot->data;
    *len_out = slot->length;

    /* Store pending pop info for commit (use reserved field or add to ext) */
    q->pending_pop_prio = (uint8_t)prio;
    q->pending_pop_slot = slot_idx;

    return 0;
}

/*
 * Commit a direct pop - actually removes the slot from queue
 */
void pt_queue_pop_priority_commit(pt_queue *q) {
    pt_queue_ext *ext;
    pt_queue_slot *slot;
    uint16_t slot_idx;
    int prio;

    if (!q)
        return;

    ext = &q->ext;
    prio = q->pending_pop_prio;
    slot_idx = q->pending_pop_slot;
    slot = &q->slots[slot_idx];

    /* Update list head */
    ext->prio_head[prio] = slot->next_slot;
    if (ext->prio_head[prio] == PT_SLOT_NONE)
        ext->prio_tail[prio] = PT_SLOT_NONE;
    ext->prio_count[prio]--;

    /* Remove from coalesce hash if present */
    if (slot->coalesce_key != PT_COALESCE_NONE) {
        uint16_t hash_idx = PT_COALESCE_HASH(slot->coalesce_key);
        if (ext->coalesce_hash[hash_idx] == slot_idx)
            ext->coalesce_hash[hash_idx] = PT_SLOT_NONE;
    }

    /* Clear slot */
    slot->flags = 0;
    slot->length = 0;
    slot->next_slot = PT_SLOT_NONE;
    q->count--;

    if (q->count == 0)
        q->has_data = 0;
}

/*
 * Push with coalescing - O(1) using hash table lookup
 */
int pt_queue_push_coalesce(pt_queue *q, const void *data, uint16_t len,
                            uint8_t priority, pt_coalesce_key key) {
    pt_queue_ext *ext;
    pt_queue_slot *slot;
    uint16_t slot_idx;
    uint16_t hash_idx;

    if (!q || !data || len == 0 || len > PT_QUEUE_SLOT_SIZE)
        return -1;

    ext = &q->ext;

    /* Check hash table for existing message with same key - O(1) */
    if (key != PT_COALESCE_NONE) {
        hash_idx = PT_COALESCE_HASH(key);
        slot_idx = ext->coalesce_hash[hash_idx];

        if (slot_idx != PT_SLOT_NONE) {
            slot = &q->slots[slot_idx];
            /* Verify it's actually our key (hash collision check) */
            if ((slot->flags & PT_SLOT_USED) && slot->coalesce_key == key) {
                /* Found - replace data in place */
                /* LOGGING: Caller can log "Coalesce hit: key=0x%04X" at DEBUG level */
                pt_memcpy(slot->data, data, len);
                slot->length = len;
                /* Note: priority and list position don't change on coalesce */
                slot->timestamp = pt_get_ticks();  /* Update timestamp */
                return 0;  /* Coalesced */
            }
            /* Hash collision - different key at same bucket */
            /* LOGGING: Caller can log "Hash collision at bucket %u" at DEBUG level */
        }
    }

    /* No existing message - allocate new slot */
    if (q->count >= q->capacity)
        return -1;  /* Full */

    /* Find free slot (use write_idx as starting point) */
    slot_idx = q->write_idx;
    slot = &q->slots[slot_idx];

    /* Fill slot */
    pt_memcpy(slot->data, data, len);
    slot->length = len;
    slot->priority = priority;
    slot->flags = PT_SLOT_USED;
    slot->coalesce_key = key;
    slot->timestamp = pt_get_ticks();

    /* Add to priority list (append to tail for FIFO within priority)
     * NOTE: next_slot is IN the slot (not ext) for traversal locality */
    slot->next_slot = PT_SLOT_NONE;
    if (ext->prio_tail[priority] == PT_SLOT_NONE) {
        /* List was empty */
        ext->prio_head[priority] = slot_idx;
    } else {
        /* Append to tail - update previous tail's next_slot */
        q->slots[ext->prio_tail[priority]].next_slot = slot_idx;
    }
    ext->prio_tail[priority] = slot_idx;
    ext->prio_count[priority]++;

    /* Add to coalesce hash table */
    if (key != PT_COALESCE_NONE) {
        hash_idx = PT_COALESCE_HASH(key);
        ext->coalesce_hash[hash_idx] = slot_idx;
    }

    q->write_idx = (q->write_idx + 1) & q->capacity_mask;
    q->count++;
    q->has_data = 1;

    return 0;
}

/*
 * ISR-safe push with coalescing (for MacTCP ASR) - O(1) hash lookup
 */
int pt_queue_push_coalesce_isr(pt_queue *q, const void *data, uint16_t len,
                                uint8_t priority, pt_coalesce_key key) {
    pt_queue_ext *ext;
    pt_queue_slot *slot;
    uint16_t slot_idx;
    uint16_t hash_idx;

    if (!q || !data || len == 0 || len > PT_QUEUE_SLOT_SIZE)
        return -1;

    ext = &q->ext;

    /* Check hash table for existing message with same key - O(1) */
    if (key != PT_COALESCE_NONE) {
        hash_idx = PT_COALESCE_HASH(key);
        slot_idx = ext->coalesce_hash[hash_idx];

        if (slot_idx != PT_SLOT_NONE) {
            slot = &q->slots[slot_idx];
            if ((slot->flags & PT_SLOT_USED) && slot->coalesce_key == key) {
                /* Found - replace using ISR-safe copy */
                pt_memcpy_isr(slot->data, data, len);
                slot->length = len;
                /* Don't update timestamp in ISR - no TickCount() call */
                q->isr_flags.coalesce_hit = 1;  /* Signal main loop for logging */
                return 0;  /* Coalesced */
            }
            /* Hash collision - set flag for logging */
            q->isr_flags.hash_collision = 1;
        }
    }

    /* No existing message - allocate new slot */
    if (q->count >= q->capacity) {
        q->isr_flags.queue_full = 1;  /* Signal main loop for logging */
        return -1;
    }

    slot_idx = q->write_idx;
    slot = &q->slots[slot_idx];

    /* Fill slot using ISR-safe copy */
    pt_memcpy_isr(slot->data, data, len);
    slot->length = len;
    slot->priority = priority;
    slot->flags = PT_SLOT_USED;
    slot->coalesce_key = key;
    slot->timestamp = 0;  /* No TickCount() in ISR */

    /* Add to priority list (next_slot is IN the slot for traversal locality) */
    slot->next_slot = PT_SLOT_NONE;
    if (ext->prio_tail[priority] == PT_SLOT_NONE) {
        ext->prio_head[priority] = slot_idx;
    } else {
        q->slots[ext->prio_tail[priority]].next_slot = slot_idx;
    }
    ext->prio_tail[priority] = slot_idx;
    ext->prio_count[priority]++;

    /* Add to coalesce hash table */
    if (key != PT_COALESCE_NONE) {
        hash_idx = PT_COALESCE_HASH(key);
        ext->coalesce_hash[hash_idx] = slot_idx;
    }

    q->write_idx = (q->write_idx + 1) & q->capacity_mask;
    q->count++;
    q->has_data = 1;

    return 0;
}

/*
 * Check and log ISR flags from main loop
 */
void pt_check_queue_isr_flags(struct pt_context *ctx, pt_queue *q) {
    if (!ctx || !ctx->log || !q)
        return;

    if (q->isr_flags.queue_full) {
        PT_CTX_WARN(ctx, PT_LOG_CAT_PROTOCOL, "Queue full during ISR");
        q->isr_flags.queue_full = 0;
    }
    if (q->isr_flags.coalesce_hit) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL, "Coalesce hit during ISR");
        q->isr_flags.coalesce_hit = 0;
    }
    if (q->isr_flags.hash_collision) {
        PT_CTX_DEBUG(ctx, PT_LOG_CAT_PROTOCOL, "Hash collision during ISR");
        q->isr_flags.hash_collision = 0;
    }
}

/* ========================================================================
 * Phase 3: Backpressure & Batch Operations
 * ======================================================================== */

/*
 * Get current backpressure level.
 */
pt_backpressure pt_queue_backpressure(pt_queue *q) {
    uint8_t pressure;

    if (!q)
        return PT_BACKPRESSURE_BLOCKING;

    pressure = pt_queue_pressure(q);

    if (pressure >= PT_PRESSURE_CRITICAL)
        return PT_BACKPRESSURE_BLOCKING;
    if (pressure >= PT_PRESSURE_HIGH)
        return PT_BACKPRESSURE_HEAVY;
    if (pressure >= PT_PRESSURE_MEDIUM)
        return PT_BACKPRESSURE_LIGHT;

    return PT_BACKPRESSURE_NONE;
}

/*
 * Try to push with backpressure awareness.
 */
int pt_queue_try_push(pt_queue *q, const void *data, uint16_t len,
                      uint8_t priority, pt_coalesce_key key,
                      pt_backpressure *pressure_out) {
    pt_backpressure bp;
    int result;

    bp = pt_queue_backpressure(q);

    if (pressure_out)
        *pressure_out = bp;

    /* Apply backpressure policy */
    switch (bp) {
    case PT_BACKPRESSURE_BLOCKING:
        if (priority < PT_PRIO_CRITICAL)
            return -1;  /* Drop - caller should log at WARN level */
        break;

    case PT_BACKPRESSURE_HEAVY:
        if (priority < PT_PRIO_HIGH)
            return -1;  /* Drop - caller should log at WARN level */
        break;

    case PT_BACKPRESSURE_LIGHT:
        /* Allow but signal caller to slow down */
        break;

    case PT_BACKPRESSURE_NONE:
        /* All clear */
        break;
    }

    result = pt_queue_push_coalesce(q, data, len, priority, key);

    /* Update pressure after push */
    if (pressure_out)
        *pressure_out = pt_queue_backpressure(q);

    return result;
}
