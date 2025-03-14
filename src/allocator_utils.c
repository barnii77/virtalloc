#include <stdlib.h>
#include <assert.h>
#include <memory.h>
#include <stddef.h>
#include "virtalloc/allocator_utils.h"
#include "virtalloc/allocator.h"
#include "virtalloc/gp_memory_slot_meta.h"
#include "virtalloc/allocator_settings.h"
#include "virtalloc/small_rr_memory_slot_meta.h"
#include "virtalloc/math_utils.h"
#include "virtalloc/helper_macros.h"

void dump_gp_slot_meta_to_file(FILE *file, GPMemorySlotMeta *meta, const size_t slot_num) {
    fprintf(file, "===== GENERAL PURPOSE SLOT %4zu (%p) =====\n", slot_num, meta->data);
    fprintf(file, "Size: %zu\n", meta->size);
    fprintf(file, "Free: %s\n", meta->is_free ? "Yes" : "No");
    fprintf(file, "Data: ");
    for (size_t j = 0; j < min(16, MIN_LARGE_ALLOCATION_SIZE); j++)
        fprintf(file, "%x ", ((unsigned char *) meta->data)[j] % 256);
    fprintf(file, " ......\n");
}

void dump_sm_slot_meta_to_file(FILE *file, SmallRRMemorySlotMeta *meta, const size_t slot_num) {
    fprintf(file, "===== SMALL SLOT %4zu (%p) =====\n", slot_num, (void *) meta + sizeof(SmallRRMemorySlotMeta));
    fprintf(file, "Size: %d\n", MAX_TINY_ALLOCATION_SIZE);
    fprintf(file, "Free: %s\n", meta->is_free ? "Yes" : "No");
    fprintf(file, "Data: ");
    for (size_t j = 0; j < min(16, MAX_TINY_ALLOCATION_SIZE); j++)
        fprintf(file, "%x ", ((unsigned char *) meta + sizeof(SmallRRMemorySlotMeta))[j] % 256);
    fprintf(file, " ......\n");
}

static size_t linear_search(const size_t needle, const size_t array_size, const size_t array[static array_size]) {
    for (size_t i = 0; i < array_size; i++)
        if (array[i] > needle)
            return i - 1;
    return array_size - 1;
}

static size_t binary_search(const size_t needle, const size_t array_size, const size_t array[static array_size]) {
    size_t left = 0;
    size_t right = array_size - 1;
    while (left <= right) {
        const size_t mid = left + (right - left) / 2;
        if (array[mid] == needle)
            return mid;
        if (array[mid] < needle)
            left = mid + 1;
        else
            right = mid - 1;
    }
    return right;
}

size_t get_bucket_index(const Allocator *allocator, const size_t size) {
    assert_internal(size >= MIN_LARGE_ALLOCATION_SIZE && "allocation smaller than smallest allowed allocation size");
    if (allocator->bucket_strategy == NO_BUCKETS)
        return 0;
    return min(allocator->gpa.num_buckets - 1, (size - MIN_LARGE_ALLOCATION_SIZE) / LARGE_ALLOCATION_ALIGN);
    // the more general approach is this binary search, but the above works for how we sample bucket sizes
    // return binary_search(size, allocator->gpa.num_buckets, allocator->gpa.bucket_sizes);
}

GPBucketTreeNode *get_bbt_child(const Allocator *allocator, const GPBucketTreeNode *parent,
                                const int get_right_child) {
    assert_internal(parent && (get_right_child <= 1) && "illegal argument");
    if (!parent->level)
        return NULL;

    const size_t parent_level_offset = (allocator->gpa.rounded_num_buckets_pow_2 >> (parent->level - 1)) - 1;
    const size_t parent_offset_in_level = (size_t) (parent - allocator->gpa.bucket_tree) - parent_level_offset;
    const size_t child_level_offset = ((allocator->gpa.rounded_num_buckets_pow_2 << 1) >> (parent->level - 1)) - 1;
    const size_t child_offset_in_level = parent_offset_in_level * 2 + get_right_child;
    GPBucketTreeNode *child = &allocator->gpa.bucket_tree[child_level_offset + child_offset_in_level];
    return child;
}

void *get_bucket_entry(const Allocator *allocator, const size_t bucket_idx) {
    assert_internal(bucket_idx < allocator->gpa.num_buckets && "unreachable");
    if (allocator->bucket_strategy == NO_BUCKETS) {
        assert_internal(bucket_idx == 0 && "unreachable");
        return allocator->gpa.bucket_values[bucket_idx];
    }

    if (allocator->bucket_strategy == BUCKET_ARENAS) {
        if (allocator->gpa.bucket_values[bucket_idx])
            return allocator->gpa.bucket_values[bucket_idx];
        // for bucket arenas specifically, if there is no slot available in the given arena, pick a slot from the
        // biggest arena, which will probably subsequently be split. This reduces wasted memory while keeping it O(1)
        return allocator->gpa.bucket_values[allocator->gpa.num_buckets - 1];
    }

    // traverse binary bucket tree
    const GPBucketTreeNode *node = allocator->gpa.bucket_tree;
    assert_internal(node && "unreachable");
    const size_t bucket_size = allocator->gpa.bucket_sizes[bucket_idx];
    while (!node->is_active) {
        assert_internal(node->level && "unreachable");
        if (node->bucket_idx >= allocator->gpa.num_buckets)
            return NULL;

        // the smallest size that falls into the right child's region
        const size_t border = allocator->gpa.bucket_sizes[node->bucket_idx + (1 << (node->level - 1))];
        if (bucket_size < border)
            // traverse left child
            node = get_bbt_child(allocator, node, 0);
        else
            // traverse right child
            node = get_bbt_child(allocator, node, 1);
        assert_internal(node && "unreachable");
    }

    return node->bucket_idx < allocator->gpa.num_buckets ? allocator->gpa.bucket_values[node->bucket_idx] : NULL;
}

void validate_checksum_of(const Allocator *allocator, void *meta, const int force_validate) {
    if (!allocator->has_checksum)
        return;
    GenericGPMeta *gm = meta;
    assert_internal(
        (gm->meta_type == GP_META_TYPE_SLOT || gm->meta_type == GP_META_TYPE_EARLY_RELEASE_SLOT) && "unreachable");
    if (force_validate || (gm->time_to_checksum_check =
                           (gm->time_to_checksum_check - 1) % allocator->steps_per_checksum_check) == 0)
        if (get_checksum(gm) != gm->checksum)
            assert_external(
            0 &&
            "checksum incorrect: you likely passed a pointer to free/realloc that does not correspond to an allocation or corrupted the allocator's metadata");
}

GPMemorySlotMeta *get_meta(const Allocator *allocator, void *p, const int should_be_free) {
    assert_internal(p && "illegal argument: p must be non-null");
    debug_print_enter_fn(allocator->block_logging, "get_meta");
    GPMemorySlotMeta *meta = p - sizeof(GPMemorySlotMeta);

#ifdef VIRTALLOC_LOGGING
    if (!allocator->block_logging) {
        // dump the bucket values to stderr
        fprintf(stderr, "++++++++++ DEBUG BUCKET DUMP\n");
        for (size_t i = 0; i < allocator->gpa.num_buckets; i++) {
            void *bucket_entry = get_bucket_entry(allocator, i);
            if (bucket_entry) {
                GPMemorySlotMeta *bvm = bucket_entry - sizeof(GPMemorySlotMeta);
                dump_gp_slot_meta_to_file(stderr, bvm, i + 1);
                if (!bvm->is_free)
                    fprintf(stderr, "WARNING: ALLOCATED SLOT IN FREE LIST\n");
                if (allocator->has_checksum && get_checksum(bvm) != bvm->checksum)
                    fprintf(stderr, "WARNING: CHECKSUM INVALID\n");
            }
        }
        fprintf(stderr, "++++++++++\n");
    }
#endif

    validate_checksum_of(allocator, meta, 0);

    if (!allocator->enable_safety_checks) {
        debug_print_leave_fn(allocator->block_logging, "get_meta");
        return meta;
    }
    assert_external(
        !(should_be_free != NO_EXPECTATION && !!meta->is_free != should_be_free) &&
        "unexpected allocation status: potential double free");
    debug_print_leave_fn(allocator->block_logging, "get_meta");
    return meta;
}

GPEarlyReleaseMeta *get_early_rel_meta(const Allocator *allocator, void *p) {
    debug_print_enter_fn(allocator->block_logging, "get_early_rel_meta");
    GPEarlyReleaseMeta *meta = p - sizeof(GPEarlyReleaseMeta);

#ifdef VIRTALLOC_LOGGING
    if (!allocator->block_logging) {
        // dump the bucket values to stderr
        fprintf(stderr, "++++++++++ DEBUG BUCKET DUMP\n");
        for (size_t i = 0; i < allocator->gpa.num_buckets; i++) {
            void *bucket_entry = get_bucket_entry(allocator, i);
            if (bucket_entry) {
                GPMemorySlotMeta *bvm = bucket_entry - sizeof(GPMemorySlotMeta);
                dump_gp_slot_meta_to_file(stderr, bvm, i + 1);
                if (!bvm->is_free)
                    fprintf(stderr, "WARNING: ALLOCATED SLOT IN FREE LIST\n");
                if (allocator->has_checksum && get_checksum(bvm) != bvm->checksum)
                    fprintf(stderr, "WARNING: CHECKSUM INVALID\n");
            }
        }
        fprintf(stderr, "++++++++++\n");
    }
#endif

    validate_checksum_of(allocator, meta, 0);

    if (!allocator->enable_safety_checks) {
        debug_print_leave_fn(allocator->block_logging, "get_early_rel_meta");
        return meta;
    }
    debug_print_leave_fn(allocator->block_logging, "get_early_rel_meta");
    return meta;
}

void *get_next_rr_slot(const Allocator *allocator, void *rr_slot) {
    assert_internal(!allocator->no_rr_allocator);
    const SmallRRMemorySlotMeta *meta = rr_slot - sizeof(SmallRRMemorySlotMeta);
    if (meta->meta_type == RR_META_TYPE_SLOT)
        return rr_slot + MAX_TINY_ALLOCATION_SIZE;
    if (meta->meta_type == RR_META_TYPE_LINK) {
        void *next_slot = *(void **) rr_slot;
        assert_internal(next_slot && "unreachable");
        // multi-redirect using links isn't allowed
        const SmallRRMemorySlotMeta *next_meta = next_slot - sizeof(SmallRRMemorySlotMeta);
        assert_internal(next_meta->meta_type == RR_META_TYPE_SLOT && "unreachable");
        return next_slot;
    }
    assert_internal(0 && "unreachable");
    return NULL;
}

static void coalesce_slot_with_next(Allocator *allocator, GPMemorySlotMeta *meta, GPMemorySlotMeta *next_meta,
                                    const int meta_requires_unbind, const int next_meta_requires_unbind,
                                    const int out_requires_bind) {
    debug_print_enter_fn(allocator->block_logging, "coalesce_slot_with_next");
    assert_internal(meta->is_free && next_meta->is_free && meta->next == next_meta->data && "illegal usage");

    if (meta_requires_unbind)
        unbind_from_sorted_free_list(allocator, meta);
    if (next_meta_requires_unbind)
        unbind_from_sorted_free_list(allocator, next_meta);

    // remove next_meta from normal linked list
    GPMemorySlotMeta *next_next_meta = get_meta(allocator, next_meta->next, NO_EXPECTATION);
    meta->next = next_meta->next;
    next_next_meta->prev = meta->data;
    // merge
    meta->size += next_meta->size + sizeof(GPMemorySlotMeta);
    // invalidate the checksum of the next meta to catch bugs more easily
    next_meta->checksum = 0;

    // redundant because insert_into_sorted_free_list does this anyway
    // refresh_checksum_of(allocator, meta);
    refresh_checksum_of(allocator, next_next_meta);
    if (out_requires_bind)
        insert_into_sorted_free_list(allocator, meta);
    else
        refresh_checksum_of(allocator, meta);
    debug_print_leave_fn(allocator->block_logging, "coalesce_slot_with_next");
}

void coalesce_memory_slots(Allocator *allocator, GPMemorySlotMeta *meta,
                           const int meta_requires_unbind_from_free_list) {
    debug_print_enter_fn(allocator->block_logging, "coalesce_memory_slots");
    assert_internal(allocator && meta && "illegal usage: allocator and meta must not be NULL");
    assert_internal(
        meta->is_free && "illegal usage: can only coalesce a slot with it's neighbours if the slot is free");
    GPMemorySlotMeta *next_meta = get_meta(allocator, meta->next, NO_EXPECTATION);
    GPMemorySlotMeta *prev_meta = get_meta(allocator, meta->prev, NO_EXPECTATION);

    // can only coalesce with next slot if it is free and forms a contiguous chunk with current slot in memory
    const int coalesce_with_next = next_meta->is_free && next_meta->data - sizeof(*next_meta) == meta->data + meta->
                                   size;
    // can only coalesce with previous slot if it is free and forms a contiguous chunk with current slot in memory
    const int coalesce_with_prev = prev_meta->is_free && meta->data - sizeof(*meta) == prev_meta->data + prev_meta->
                                   size;

    if (coalesce_with_next)
        coalesce_slot_with_next(allocator, meta, next_meta, meta_requires_unbind_from_free_list, 1,
                                !coalesce_with_prev);
    if (coalesce_with_prev)
        coalesce_slot_with_next(allocator, prev_meta, meta, 1,
                                meta_requires_unbind_from_free_list && !coalesce_with_next, 1);

    // if neither of the above branches is reached and the meta passed in is not bound to the sorted free list in the
    // first place, we still need to place it there
    if (!coalesce_with_next && !coalesce_with_prev && !meta_requires_unbind_from_free_list)
        insert_into_sorted_free_list(allocator, meta);
    debug_print_leave_fn(allocator->block_logging, "coalesce_slot_with_next");
}

static void try_coalesce_bbt_children(const Allocator *allocator, GPBucketTreeNode *parent, GPBucketTreeNode *left,
                                      GPBucketTreeNode *right) {
    assert_internal(
        allocator->bucket_strategy == BUCKET_TREE && left->level == right->level && left->level + 1 == parent->level &&
        !parent->is_active);
    if (left->is_active && right->is_active && allocator->gpa.bucket_values[left->bucket_idx] == allocator->gpa.
        bucket_values[right->bucket_idx]) {
        // don't have to copy the value associated with left (and right) to parent because parent shares an entry slot
        // in allocator->gpa.bucket_values with left
        parent->is_active = 1;
        left->is_active = 0;
        right->is_active = 0;
    }
}

static void split_bucket_tree_node(const Allocator *allocator, GPBucketTreeNode *node, GPBucketTreeNode *left,
                                   GPBucketTreeNode *right) {
    left->is_active = 1;
    right->is_active = 1;
    allocator->gpa.bucket_values[right->bucket_idx] = allocator->gpa.bucket_values[node->bucket_idx];
    node->is_active = 0;
}

static size_t get_subtree_min_allowed_size(const Allocator *allocator, const GPBucketTreeNode *node) {
    return allocator->gpa.bucket_sizes[node->bucket_idx];
}

static size_t get_subtree_min_entry_size(const Allocator *allocator, const GPBucketTreeNode *node) {
    void *entry = allocator->gpa.bucket_values[node->bucket_idx];
    if (!entry)
        return get_subtree_min_allowed_size(allocator, node);
    const GPMemorySlotMeta *meta = get_meta(allocator, entry, EXPECT_IS_FREE);
    return meta->size;
}

static void replace_bucket_entry_impl(const Allocator *allocator, const GPMemorySlotMeta *meta,
                                      const GPMemorySlotMeta *replacement, GPBucketTreeNode *node) {
    if (node->bucket_idx >= allocator->gpa.num_buckets)
        return;

    if (node->is_active) {
        const size_t bucket_idx = node->bucket_idx;
        if (allocator->gpa.bucket_values[bucket_idx] == meta->data) {
            if (replacement && replacement->size < meta->size && replacement->size >= allocator->gpa.bucket_sizes[
                    bucket_idx] && replacement->size < allocator->gpa.bucket_sizes[
                    bucket_idx + (1 << node->level) - 1]) {
                // this is reachable if the biggest slot in the sorted free list is removed (e.g. for being split)
                GPBucketTreeNode *left = get_bbt_child(allocator, node, 0);
                GPBucketTreeNode *right = get_bbt_child(allocator, node, 1);
                split_bucket_tree_node(allocator, node, left, right);
                replace_bucket_entry_impl(allocator, meta, replacement, left);
                replace_bucket_entry_impl(allocator, meta, replacement, right);
                assert_internal(
                    !(left->is_active && right->is_active && allocator->gpa.bucket_values[left->bucket_idx] == allocator
                        ->gpa.bucket_values[right->bucket_idx]) && "unreachable");
            } else {
                allocator->gpa.bucket_values[bucket_idx] =
                        replacement && replacement->size >= allocator->gpa.bucket_sizes[bucket_idx]
                            ? replacement->data
                            : NULL;
            }
        }
    } else {
        GPBucketTreeNode *left = get_bbt_child(allocator, node, 0);
        GPBucketTreeNode *right = get_bbt_child(allocator, node, 1);
        assert_internal(left && right && "unreachable");

        // since node is inactive, I can assume that the underlying slots of left and right are populated meaningfully
        // because either they or one of their children must be active. therefore, I can check if the smallest entry
        // that falls into those buckets meets the size criteria on `left` and only if yes, even check them for `right`
        // because the bucket entries are guaranteed to be sorted by size (ascending).
        if (get_subtree_min_entry_size(allocator, left) <= meta->size) {
            replace_bucket_entry_impl(allocator, meta, replacement, left);

            // check same thing for `right`
            if (get_subtree_min_entry_size(allocator, right) <= meta->size)
                replace_bucket_entry_impl(allocator, meta, replacement, right);

            // coalescing only makes sense if something has changed, which only happens inside this if condition body
            try_coalesce_bbt_children(allocator, node, left, right);
        }
    }
}

static void replace_bucket_entry(const Allocator *allocator, const GPMemorySlotMeta *meta, const size_t bucket_idx,
                                 const GPMemorySlotMeta *replacement) {
    assert_internal(meta && "illegal usage");
    if (allocator->bucket_strategy == NO_BUCKETS || allocator->bucket_strategy == BUCKET_ARENAS) {
        if (allocator->bucket_strategy == NO_BUCKETS)
            assert_internal(bucket_idx == 0 && "unreachable");
        else
            assert_internal(
                bucket_idx == get_bucket_index(allocator, meta->size) && (!replacement || bucket_idx == get_bucket_index
                    (
                        allocator, replacement->size)) && "unreachable");

        if (allocator->gpa.bucket_values[bucket_idx] == meta->data)
            allocator->gpa.bucket_values[bucket_idx] = replacement && replacement->size >= allocator->gpa.bucket_sizes[
                                                           bucket_idx]
                                                           ? replacement->data
                                                           : NULL;
    } else {
        replace_bucket_entry_impl(allocator, meta, replacement, allocator->gpa.bucket_tree);
    }
}

static void get_bbt_node_size_bounds_inclusive(const Allocator *allocator, const GPBucketTreeNode *node, size_t *lower,
                                               size_t *upper) {
    *lower = allocator->gpa.bucket_sizes[node->bucket_idx];
    const size_t upper_index = node->bucket_idx + (node->level ? (1 << node->level) - 1 : 0);
    *upper = allocator->gpa.bucket_sizes[min(upper_index, allocator->gpa.num_buckets - 1)];
}

static void add_bucket_entry_impl(const Allocator *allocator, const GPMemorySlotMeta *meta, GPBucketTreeNode *node) {
    if (node->bucket_idx >= allocator->gpa.num_buckets || meta->size < allocator->gpa.bucket_sizes[node->bucket_idx])
        return;

    if (node->is_active) {
        const size_t bucket_idx = node->bucket_idx;
        void *bucket_value = allocator->gpa.bucket_values[bucket_idx];
        const GPMemorySlotMeta *first_in_bucket = bucket_value
                                                      ? get_meta(allocator, bucket_value, EXPECT_IS_FREE)
                                                      : NULL;

        size_t lower, upper;
        get_bbt_node_size_bounds_inclusive(allocator, node, &lower, &upper);

        if (lower < upper && lower <= meta->size && meta->size <= upper) {
            // must split, deactivate node, activate left and right and re-run for both
            GPBucketTreeNode *left = get_bbt_child(allocator, node, 0);
            GPBucketTreeNode *right = get_bbt_child(allocator, node, 1);
            if (!left || !right)
                assert_internal(0 && "unreachable");
            split_bucket_tree_node(allocator, node, left, right);
            add_bucket_entry_impl(allocator, meta, left);
            add_bucket_entry_impl(allocator, meta, right);
        } else if (upper <= meta->size && (!first_in_bucket || meta->size <= first_in_bucket->size)) {
            allocator->gpa.bucket_values[bucket_idx] = meta->data;
        }
    } else {
        GPBucketTreeNode *left = get_bbt_child(allocator, node, 0);
        GPBucketTreeNode *right = get_bbt_child(allocator, node, 1);

        // for the reasoning regarding this pruning condition, see `replace_bucket_entry_impl`
        if (get_subtree_min_allowed_size(allocator, left) <= meta->size) {
            add_bucket_entry_impl(allocator, meta, left);
            if (get_subtree_min_allowed_size(allocator, right) <= meta->size)
                add_bucket_entry_impl(allocator, meta, right);
            // cannot coalesce, such a case would be unreachable
        }
    }
}

static void add_bucket_entry(const Allocator *allocator, const GPMemorySlotMeta *meta, size_t bucket_idx) {
    if (allocator->bucket_strategy == NO_BUCKETS || allocator->bucket_strategy == BUCKET_ARENAS) {
        if (allocator->bucket_strategy == BUCKET_ARENAS) {
            if (allocator->gpa.bucket_sizes[allocator->gpa.num_buckets - 1] <= meta->size)
                // the actual arena this belongs to is the last bucket
                bucket_idx = allocator->gpa.num_buckets - 1;
        } else {
            assert_internal(bucket_idx == 0 && "unreachable");
        }
        void *bucket_value = allocator->gpa.bucket_values[bucket_idx];
        GPMemorySlotMeta *first_in_bucket = bucket_value ? get_meta(allocator, bucket_value, EXPECT_IS_FREE) : NULL;
        if (!first_in_bucket || meta->size <= first_in_bucket->size)
            allocator->gpa.bucket_values[bucket_idx] = meta->data;
    } else {
        add_bucket_entry_impl(allocator, meta, allocator->gpa.bucket_tree);
    }
}

void unbind_from_sorted_free_list(Allocator *allocator, GPMemorySlotMeta *meta) {
    debug_print_enter_fn(allocator->block_logging, "unbind_from_sorted_free_list");
    int is_only_free_slot = 0;
    if (meta->data == meta->next_bigger_free) {
        assert_internal(meta->data == meta->next_smaller_free && "unreachable");
        is_only_free_slot = 1;
    }
    const size_t bucket_idx = get_bucket_index(allocator, meta->size);
    // must replace with next bigger slot
    const GPMemorySlotMeta *replacement = is_only_free_slot
                                              ? NULL
                                              : get_meta(allocator, meta->next_bigger_free, EXPECT_IS_FREE);
    if (replacement && replacement->size < meta->size)
        // meta is the biggest free slot -> the new biggest will be next_smaller_free (next_bigger_free is smallest)
        replacement = get_meta(allocator, meta->next_smaller_free, EXPECT_IS_FREE);
    // must check buckets with size smaller than meta->size if those refer to meta->next_bigger_free
    replace_bucket_entry(allocator, meta, bucket_idx, replacement);

    if (is_only_free_slot) {
        debug_print_leave_fn(allocator->block_logging, "unbind_from_sorted_free_list");
        return;
    }

    // unbind references from sorted free list to consumed slot
    GPMemorySlotMeta *meta_nbf = get_meta(allocator, meta->next_bigger_free, EXPECT_IS_FREE);
    meta_nbf->next_smaller_free = meta->next_smaller_free;
    // have to interleave the refresh calls with the get_meta in case next_bigger_free and next_smaller_free point to
    // the same thing, in which case the first write invalidates the checksum of the second write
    refresh_checksum_of(allocator, meta_nbf);
    GPMemorySlotMeta *meta_nsf = get_meta(allocator, meta->next_smaller_free, EXPECT_IS_FREE);
    meta_nsf->next_bigger_free = meta->next_bigger_free;
    refresh_checksum_of(allocator, meta_nsf);
    debug_print_leave_fn(allocator->block_logging, "unbind_from_sorted_free_list");
}

void insert_into_sorted_free_list(Allocator *allocator, GPMemorySlotMeta *meta) {
    debug_print_enter_fn(allocator->block_logging, "insert_into_sorted_free_list");
    assert_internal(meta->is_free && "illegal usage");
    const size_t bucket_idx = get_bucket_index(allocator, meta->size);

    void *bucket_value = get_bucket_entry(allocator, bucket_idx);
    if (allocator->bucket_strategy == BUCKET_ARENAS && bucket_idx != allocator->gpa.num_buckets - 1 && bucket_value ==
        get_bucket_entry(allocator, allocator->gpa.num_buckets - 1))
        // bucket_value is not the physical entry but rather the fallback to the largest ("all you can eat") arena.
        // thus, we can conclude the actual physical entry in our bucket must be NULL since we observed a fallback
        bucket_value = NULL;

    meta->next_bigger_free = meta->data;
    meta->next_smaller_free = meta->data;
    refresh_checksum_of(allocator, meta);

    GPMemorySlotMeta *next_meta = NULL;
    GPMemorySlotMeta *prev_meta = NULL;
    GPMemorySlotMeta *first_in_bucket = bucket_value ? get_meta(allocator, bucket_value, EXPECT_IS_FREE) : NULL;
    void *smallest_entry = get_bucket_entry(allocator, 0);
    if (!bucket_value) {
        // next bigger one links to the smallest entry to make the sorted linked list circular
        if (smallest_entry && allocator->bucket_strategy != BUCKET_ARENAS)
            next_meta = get_meta(allocator, smallest_entry, EXPECT_IS_FREE);
        else
            next_meta = meta;
        // the next smaller one is just the next_smaller_free of smallest entry
    } else {
        int first_iter = 1;
        next_meta = first_in_bucket;
        if (allocator->bucket_strategy == BUCKET_ARENAS)
            // smallest_entry should point to the smallest entry in the relevant sorted free list (i.e. first_in_bucket)
            smallest_entry = bucket_value;

        // find the smallest heap alloc that is bigger
        while (next_meta->size < meta->size && (next_meta->data != smallest_entry || first_iter)) {
            first_iter = 0;
            GPMemorySlotMeta *next_bigger = get_meta(allocator, next_meta->next_bigger_free, EXPECT_IS_FREE);
            if (next_bigger->size < next_meta->size) {
                // next_bigger points to smallest_entry (i.e. we are inserting the new biggest slot)
                next_meta = next_bigger;
                break;
            }
            next_meta = next_bigger;
        }
        // the previous slot is then the next_smaller_free of that smallest bigger one searched for above
    }
    prev_meta = get_meta(allocator, next_meta->next_smaller_free, EXPECT_IS_FREE);

    assert_internal(next_meta && prev_meta && "unreachable");

    // insert into sorted linked list
    meta->next_bigger_free = next_meta->data;
    meta->next_smaller_free = prev_meta->data;
    next_meta->next_smaller_free = meta->data;
    prev_meta->next_bigger_free = meta->data;

    refresh_checksum_of(allocator, prev_meta);
    refresh_checksum_of(allocator, meta);
    refresh_checksum_of(allocator, next_meta);

    assert_internal(
        (bucket_idx == allocator->gpa.num_buckets - 1 || meta->size < allocator->gpa.bucket_sizes[bucket_idx + 1]) &&
        "unreachable");
    // must check buckets with size smaller than meta->size if those refer to meta->next_bigger_free
    add_bucket_entry(allocator, meta, bucket_idx);
    debug_print_leave_fn(allocator->block_logging, "insert_into_sorted_free_list");
}

void refresh_checksum_of(const Allocator *allocator, void *meta) {
    GenericGPMeta *gm = meta;
    if (allocator->has_checksum)
        gm->checksum = get_checksum(gm);
}

/// grow an allocated slot into a free slot to the right
void consume_next_slot(Allocator *allocator, GPMemorySlotMeta *meta, size_t moved_bytes) {
    debug_print_enter_fn(allocator->block_logging, "consume_next_slot");
    assert_internal(
        !meta->is_free && "only works for allocated slots trying to grow into their free neighbour to the right");
    GPMemorySlotMeta *next_meta = get_meta(allocator, meta->next, EXPECT_IS_FREE);
    const ssize_t remaining_size = (ssize_t) (next_meta->size + sizeof(GPMemorySlotMeta)) - (ssize_t) moved_bytes;
    assert_internal(remaining_size >= 0 && "cannot join: block to join with too small");
    assert_internal(
        next_meta->data - sizeof(*next_meta) == meta->data + meta->size &&
        "cannot coalesce with slot that is not a contiguous neighbour");

    if (remaining_size < sizeof(GPMemorySlotMeta) + MIN_LARGE_ALLOCATION_SIZE) {
        // next slot would become too small, must be consumed completely
        unbind_from_sorted_free_list(allocator, next_meta);

        // invalidate checksum of consumed slot
        next_meta->checksum = 0;

        moved_bytes += remaining_size;

        // unbind references to consumed block in normal linked list
        meta->next = next_meta->next;
        GPMemorySlotMeta *next_next_meta = get_meta(allocator, next_meta->next, NO_EXPECTATION);
        next_next_meta->prev = meta->data;
        // adjust size
        meta->size += moved_bytes;

        refresh_checksum_of(allocator, meta);
        refresh_checksum_of(allocator, next_next_meta);
    } else {
        // reduce next slot size, increase own size
        unbind_from_sorted_free_list(allocator, next_meta);

        // invalidate checksum of slot artifact (so that pre-move slot is invalidated)
        next_meta->checksum = 0;

        // move the metadata of the free slot to the right
        memmove((void *) next_meta + moved_bytes, next_meta, sizeof(*next_meta));
        next_meta = (void *) next_meta + moved_bytes;
        // adjust sizes and pointers
        next_meta->size -= moved_bytes;
        next_meta->data += moved_bytes;
        refresh_checksum_of(allocator, next_meta);

        // adjust sizes and pointers
        meta->next += moved_bytes;
        meta->size += moved_bytes;
        refresh_checksum_of(allocator, meta);

        GPMemorySlotMeta *next_next_meta = get_meta(allocator, next_meta->next, NO_EXPECTATION);
        next_next_meta->prev += moved_bytes;
        refresh_checksum_of(allocator, next_next_meta);

        // insert the free slot to the right back into the sorted free list, just at the now appropriate location
        insert_into_sorted_free_list(allocator, next_meta);
    }
    debug_print_leave_fn(allocator->block_logging, "consume_next_slot");
}

/// grow a free slot into an allocated slot to the left (opposite of consume_next_slot)
void consume_prev_slot(Allocator *allocator, GPMemorySlotMeta *meta, size_t moved_bytes) {
    debug_print_enter_fn(allocator->block_logging, "consume_prev_slot");
    assert_internal(meta->prev != meta->data && "slot cannot consume itself");
    assert_internal(
        meta->is_free && "only works for free slots trying to grow into their allocated neighbour to the left");
    GPMemorySlotMeta *prev_meta = get_meta(allocator, meta->prev, EXPECT_IS_ALLOCATED);
    const ssize_t remaining_size = (ssize_t) (prev_meta->size + sizeof(GPMemorySlotMeta)) - (ssize_t) moved_bytes;
    assert_internal(remaining_size >= 0 && "cannot join: block to join with too small");
    assert_internal(
        meta->data - sizeof(*meta) == prev_meta->data + prev_meta->size &&
        "cannot coalesce with slot that is not a contiguous neighbour");

    if (remaining_size < sizeof(GPMemorySlotMeta) + MIN_LARGE_ALLOCATION_SIZE) {
        unbind_from_sorted_free_list(allocator, meta);

        // invalidate checksum of slot artifact (so that pre-move slot is invalidated)
        meta->checksum = 0;

        // next slot too small, must be consumed completely
        moved_bytes += remaining_size;

        // adjust size and data ptr
        meta->size += moved_bytes;
        meta->data -= moved_bytes;
        // unbind references to consumed block in normal linked list
        meta->prev = prev_meta->prev;

        // move the metadata of the free slot to the left
        memmove((void *) meta - moved_bytes, meta, sizeof(*meta));
        meta = (void *) meta - moved_bytes;
        refresh_checksum_of(allocator, meta);

        // don't have to update prev_meta->prev because meta is moved to exactly where prev_meta used to be

        GPMemorySlotMeta *next_meta = get_meta(allocator, meta->next, NO_EXPECTATION);
        next_meta->prev = meta->data;
        refresh_checksum_of(allocator, next_meta);
    } else {
        unbind_from_sorted_free_list(allocator, meta);

        // invalidate checksum of slot artifact (so that pre-move slot is invalidated)
        meta->checksum = 0;

        // move the metadata of the free slot (meta) to the left
        memmove((void *) meta - moved_bytes, meta, sizeof(*meta));
        meta = (GPMemorySlotMeta *) ((void *) meta - moved_bytes);
        // adjust sizes
        meta->size += moved_bytes;
        meta->data -= moved_bytes;
        prev_meta->size -= moved_bytes;
        prev_meta->next -= moved_bytes;
        // refresh checksums
        refresh_checksum_of(allocator, prev_meta);

        // redundant because insert_into_sorted_free_list does this anyway and meta->next == meta->data is unreachable
        // refresh_checksum_of(allocator, meta);

        GPMemorySlotMeta *next_meta = get_meta(allocator, meta->next, NO_EXPECTATION);
        next_meta->prev -= moved_bytes;
        // refresh checksum
        refresh_checksum_of(allocator, next_meta);

        // redundant because insert_into_sorted_free_list does this anyway
        // refresh_checksum_of(allocator, meta);

        // insert the free slot to the right back into the sorted free list, just at the now appropriate location
        insert_into_sorted_free_list(allocator, meta);
    }
    debug_print_leave_fn(allocator->block_logging, "consume_prev_slot");
}
