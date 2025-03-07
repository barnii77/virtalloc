#include <stdio.h>
#include <stddef.h>
#include "virtalloc/allocator.h"
#include "virtalloc/gp_memory_slot_meta.h"
#include "virtalloc/allocator_utils.h"
#include "virtalloc/helper_macros.h"

static void check_allocator_from_meta_root(const Allocator *allocator, const GPMemorySlotMeta *meta,
                                           const int run_sfl_checks) {
    const int num = 1000;
    int n_iters_left;
    int first_iter;
#define RUN_CHECK(update_rule, inverse_update_rule, expect_is_free, sort_expectation) \
    first_iter = 1; \
    n_iters_left = num; \
    for (void *i = meta->data; --n_iters_left && i != meta->data || first_iter; i = ((GPMemorySlotMeta *) (i - sizeof(GPMemorySlotMeta)))->update_rule) { \
        first_iter = 0; \
        GPMemorySlotMeta *im = i - sizeof(GPMemorySlotMeta); \
        validate_checksum_of(allocator, im, 1); \
        if (expect_is_free) \
            assert_external(im->is_free); \
        const GPMemorySlotMeta *other = get_meta(allocator, im->update_rule, NO_EXPECTATION); \
        assert_external(other->inverse_update_rule == im->data); \
        if (im->data != meta->data && sort_expectation == -1) \
            assert_external(other->size <= im->size); \
        else if (other->data != meta->data && sort_expectation == 1) \
            assert_external(other->size >= im->size); \
    }
    if (run_sfl_checks) {
        RUN_CHECK(next_bigger_free, next_smaller_free, 1, 1)
        RUN_CHECK(next_smaller_free, next_bigger_free, 1, -1)
    }
    RUN_CHECK(next, prev, 0, 0)
    RUN_CHECK(prev, next, 0, 0)
#undef RUN_CHECK
}

static void check_allocator_buckets(const Allocator *allocator) {
    // get the size of the largest slot according to sorted free list and compare with bucket entries max size (this is a sfl integrity check)
    size_t largest_size = -1;
    if (allocator->bucket_strategy == BUCKET_TREE || allocator->bucket_strategy == NO_BUCKETS)
        largest_size = get_meta(
            allocator, get_meta(allocator, get_bucket_entry(allocator, 0), EXPECT_IS_FREE)->next_smaller_free,
            EXPECT_IS_FREE)->size;

    // make sure bucket entries are strictly increasing
    size_t last_size = 0;
    int has_encountered_null = 0;

    for (size_t i = 0; i < allocator->gpa.num_buckets; i++) {
        void *bucket_entry = get_bucket_entry(allocator, i);
        if (allocator->bucket_strategy == BUCKET_ARENAS && bucket_entry == get_bucket_entry(allocator, allocator->gpa.num_buckets - 1) && i != allocator->gpa.num_buckets - 1)
            // skip because the entry we retrieved is actually a fallback
            continue;
        if (has_encountered_null && allocator->bucket_strategy != BUCKET_ARENAS) {
            assert_external(!bucket_entry);
            continue;
        }
        if (!bucket_entry) {
            has_encountered_null = 1;
            continue;
        }
        const GPMemorySlotMeta *meta = get_meta(allocator, bucket_entry, EXPECT_IS_FREE);
        assert_external(meta->size >= last_size);
        assert_external(meta->size >= allocator->gpa.bucket_sizes[i]);
        assert_external(meta->size <= largest_size);
        last_size = meta->size;
        const GPMemorySlotMeta *nsf = get_meta(allocator, meta->next_smaller_free, EXPECT_IS_FREE);
        if (allocator->bucket_strategy != BUCKET_ARENAS)
            assert_external(meta == nsf || meta->size < nsf->size || nsf->size < allocator->gpa.bucket_sizes[i]);
    }
}

void check_allocator(const Allocator *allocator) {
    if (!allocator->debug_corruption_checks)
        return;
    check_allocator_from_meta_root(allocator, get_meta(allocator, allocator->gpa.first_slot, NO_EXPECTATION), 0);
    if (allocator->bucket_strategy == BUCKET_ARENAS) {
        for (size_t i = 0; i < allocator->gpa.num_buckets; i++)
            // check every sorted free list individually if it is populated
            if (allocator->gpa.bucket_values[i])
                check_allocator_from_meta_root(allocator, get_meta(allocator, allocator->gpa.bucket_values[i], EXPECT_IS_FREE), 1);
    } else {
        // there is just one large sorted free list to be checked
        check_allocator_from_meta_root(allocator, get_meta(allocator, allocator->gpa.bucket_values[0], EXPECT_IS_FREE), 1);
    }
    check_allocator_buckets(allocator);
}
