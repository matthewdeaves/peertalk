/* test_backpressure.c - Phase 3 backpressure and batch tests
 *
 * Tests for:
 * - Backpressure level detection (NONE/LIGHT/HEAVY/BLOCKING)
 * - try_push backpressure policy enforcement
 * - Batch buffer operations (init, add)
 */

#include <stdio.h>
#include <assert.h>
#include "../src/core/queue.h"
#include "../src/core/send.h"

void test_backpressure_levels(void) {
    pt_queue q;
    int i;

    /* Use 32-slot capacity (PT_QUEUE_MAX_SLOTS limit from Phase 3)
     * Capacity must be power-of-2 AND <= PT_QUEUE_MAX_SLOTS */
    pt_queue_init(NULL, &q, 32);
    pt_queue_ext_init(&q);  /* Initialize O(1) data structures */

    /* Empty - no pressure */
    assert(pt_queue_backpressure(&q) == PT_BACKPRESSURE_NONE);

    /* Fill to 25% (8/32) */
    for (i = 0; i < 8; i++) {
        pt_queue_push_coalesce(&q, "x", 1, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    }
    assert(pt_queue_backpressure(&q) == PT_BACKPRESSURE_NONE);

    /* Fill to 50% (16/32) */
    for (i = 0; i < 8; i++) {
        pt_queue_push_coalesce(&q, "x", 1, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    }
    assert(pt_queue_backpressure(&q) == PT_BACKPRESSURE_LIGHT);

    /* Fill to 75% (24/32) */
    for (i = 0; i < 8; i++) {
        pt_queue_push_coalesce(&q, "x", 1, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    }
    assert(pt_queue_backpressure(&q) == PT_BACKPRESSURE_HEAVY);

    /* Fill to >90% (29/32 = 90.6%) to trigger BLOCKING */
    for (i = 0; i < 5; i++) {
        pt_queue_push_coalesce(&q, "x", 1, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    }
    assert(pt_queue_backpressure(&q) == PT_BACKPRESSURE_BLOCKING);

    pt_queue_free(&q);
    printf("test_backpressure_levels: PASSED\n");
}

void test_try_push_policy(void) {
    pt_queue q;
    int i;
    pt_backpressure bp;

    /* Use 32-slot capacity (PT_QUEUE_MAX_SLOTS limit from Phase 3) */
    pt_queue_init(NULL, &q, 32);
    pt_queue_ext_init(&q);  /* Initialize O(1) data structures */

    /* Fill to blocking (>90% = 29/32) */
    for (i = 0; i < 29; i++) {
        pt_queue_push_coalesce(&q, "x", 1, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    }

    /* Low priority should fail */
    assert(pt_queue_try_push(&q, "y", 1, PT_PRIO_LOW, 0, &bp) != 0);
    assert(bp == PT_BACKPRESSURE_BLOCKING);

    /* Critical priority should succeed */
    assert(pt_queue_try_push(&q, "z", 1, PT_PRIO_CRITICAL, 0, &bp) == 0);

    pt_queue_free(&q);
    printf("test_try_push_policy: PASSED\n");
}

void test_batch_send(void) {
    pt_batch batch;

    pt_batch_init(&batch);

    assert(pt_batch_add(&batch, "hello", 5) == 0);
    assert(pt_batch_add(&batch, "world", 5) == 0);

    assert(batch.count == 2);
    assert(batch.used == 2 * (4 + 5));  /* 2 * (header + data) */

    printf("test_batch_send: PASSED\n");
}

int main(void) {
    test_backpressure_levels();
    test_try_push_policy();
    test_batch_send();
    printf("\nAll backpressure tests PASSED!\n");
    return 0;
}
