/* test_queue_advanced.c - Phase 3 advanced queue tests
 *
 * Tests for:
 * - Priority-based dequeue (O(1) via free-lists)
 * - FIFO within priority level
 * - Message coalescing (O(1) via hash table)
 * - Hash collision handling
 * - Zero-copy direct pop
 * - Per-peer coalesce keys
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../src/core/queue.h"

void test_priority_order(void) {
    pt_queue q;
    char buf[256];
    uint16_t len;

    pt_queue_init(NULL, &q, 16);
    pt_queue_ext_init(&q);  /* Initialize O(1) data structures */

    /* Push in random priority order - use pt_queue_push_coalesce with PT_COALESCE_NONE */
    pt_queue_push_coalesce(&q, "low", 4, PT_PRIO_LOW, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "high", 5, PT_PRIO_HIGH, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "normal", 7, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "critical", 9, PT_PRIO_CRITICAL, PT_COALESCE_NONE);

    /* Pop should return highest priority first (O(1) operation) */
    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "critical") == 0);

    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "high") == 0);

    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "normal") == 0);

    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "low") == 0);

    pt_queue_free(&q);
    printf("test_priority_order: PASSED\n");
}

void test_priority_fifo_within_level(void) {
    pt_queue q;
    char buf[256];
    uint16_t len;

    pt_queue_init(NULL, &q, 16);
    pt_queue_ext_init(&q);

    /* Push multiple messages at same priority - should be FIFO within level */
    pt_queue_push_coalesce(&q, "first", 6, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "second", 7, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "third", 6, PT_PRIO_NORMAL, PT_COALESCE_NONE);

    /* Pop should return FIFO order within same priority */
    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "first") == 0);

    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "second") == 0);

    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "third") == 0);

    pt_queue_free(&q);
    printf("test_priority_fifo_within_level: PASSED\n");
}

void test_coalescing(void) {
    pt_queue q;
    char buf[256];
    uint16_t len;

    pt_queue_init(NULL, &q, 16);
    pt_queue_ext_init(&q);

    /* Push multiple position updates with same key - O(1) hash lookup */
    pt_queue_push_coalesce(&q, "pos:1,1", 8, PT_PRIO_NORMAL, PT_COALESCE_POSITION);
    pt_queue_push_coalesce(&q, "pos:2,2", 8, PT_PRIO_NORMAL, PT_COALESCE_POSITION);
    pt_queue_push_coalesce(&q, "pos:3,3", 8, PT_PRIO_NORMAL, PT_COALESCE_POSITION);

    /* Should only have 1 message (coalesced in O(1) time) */
    assert(pt_queue_count(&q) == 1);

    /* Should get latest */
    pt_queue_pop_priority(&q, buf, &len);
    assert(strcmp(buf, "pos:3,3") == 0);

    pt_queue_free(&q);
    printf("test_coalescing: PASSED\n");
}

void test_mixed_coalesce_and_normal(void) {
    pt_queue q;

    pt_queue_init(NULL, &q, 16);
    pt_queue_ext_init(&q);

    /* Mix of coalesced and normal messages */
    pt_queue_push_coalesce(&q, "pos:1", 6, PT_PRIO_NORMAL, PT_COALESCE_POSITION);
    pt_queue_push_coalesce(&q, "chat:hi", 8, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "pos:2", 6, PT_PRIO_NORMAL, PT_COALESCE_POSITION);
    pt_queue_push_coalesce(&q, "chat:bye", 9, PT_PRIO_NORMAL, PT_COALESCE_NONE);

    /* Should have 3 messages (pos coalesced to 1, plus 2 chats) */
    assert(pt_queue_count(&q) == 3);

    pt_queue_free(&q);
    printf("test_mixed_coalesce_and_normal: PASSED\n");
}

void test_coalesce_hash_collision(void) {
    pt_queue q;
    uint16_t key_a, key_b;

    pt_queue_init(NULL, &q, 16);
    pt_queue_ext_init(&q);

    /* Create two keys that hash to same bucket using PT_COALESCE_HASH
     * PT_COALESCE_HASH(key) = ((key) ^ ((key) >> 8)) & 0x1F
     * key_a = 0x0001: hash = (0x0001 ^ 0x00) & 0x1F = 1
     * key_b = 0x0021: hash = (0x0021 ^ 0x00) & 0x1F = 0x0021 & 0x1F = 1 (collision!)
     */
    key_a = 0x0001;  /* Hash: (0x0001 ^ 0x00) & 0x1F = 1 */
    key_b = 0x0021;  /* Hash: (0x0021 ^ 0x00) & 0x1F = 1 (collision) */

    pt_queue_push_coalesce(&q, "key_a_v1", 9, PT_PRIO_NORMAL, key_a);
    pt_queue_push_coalesce(&q, "key_b_v1", 9, PT_PRIO_NORMAL, key_b);

    /* Should have 2 messages - collision handled correctly */
    assert(pt_queue_count(&q) == 2);

    /* Update key_b - should coalesce (key_b is in the hash bucket) */
    pt_queue_push_coalesce(&q, "key_b_v2", 9, PT_PRIO_NORMAL, key_b);
    assert(pt_queue_count(&q) == 2);  /* Still 2 - key_b coalesced */

    /* Update key_a - will NOT coalesce because hash bucket contains key_b
     * This is expected behavior for simple direct-mapped hash: only the most
     * recent key at each bucket is tracked for coalescing. */
    pt_queue_push_coalesce(&q, "key_a_v2", 9, PT_PRIO_NORMAL, key_a);
    assert(pt_queue_count(&q) == 3);  /* Now 3 - key_a couldn't coalesce */

    pt_queue_free(&q);
    printf("test_coalesce_hash_collision: PASSED\n");
}

void test_direct_pop(void) {
    pt_queue q;
    const void *data;
    uint16_t len;

    pt_queue_init(NULL, &q, 16);
    pt_queue_ext_init(&q);

    pt_queue_push_coalesce(&q, "hello", 5, PT_PRIO_NORMAL, PT_COALESCE_NONE);
    pt_queue_push_coalesce(&q, "world", 5, PT_PRIO_HIGH, PT_COALESCE_NONE);

    /* Direct pop returns pointer to HIGH priority first */
    assert(pt_queue_pop_priority_direct(&q, &data, &len) == 0);
    assert(len == 5);
    assert(memcmp(data, "world", 5) == 0);

    /* Not committed yet - count unchanged */
    assert(pt_queue_count(&q) == 2);

    /* Commit removes from queue */
    pt_queue_pop_priority_commit(&q);
    assert(pt_queue_count(&q) == 1);

    pt_queue_free(&q);
    printf("test_direct_pop: PASSED\n");
}

int main(void) {
    test_priority_order();
    test_priority_fifo_within_level();
    test_coalescing();
    test_mixed_coalesce_and_normal();
    test_coalesce_hash_collision();
    test_direct_pop();
    printf("\nAll advanced queue tests PASSED!\n");
    return 0;
}
