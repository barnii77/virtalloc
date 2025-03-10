#include <stdio.h>
#include <memory.h>
#include <stddef.h>
#include "virtalloc/allocator.h"
#include "virtalloc/gp_memory_slot_meta.h"
#include "virtalloc/small_rr_memory_slot_meta.h"
#include "virtalloc/allocator_impl.h"
#include "virtalloc/allocator_utils.h"
#include "virtalloc/allocator_settings.h"
#include "virtalloc/math_utils.h"
#include "virtalloc/helper_macros.h"
#include "virtalloc/check_allocator.h"

/// pad to alignment requirement and add safety padding to prevent off-by-1 bugs on the user end
static size_t get_gpa_compatible_size(const Allocator *allocator, size_t requested_size) {
    requested_size += allocator->get_gpa_padding_lines
                          ? allocator->get_gpa_padding_lines(requested_size) * LARGE_ALLOCATION_ALIGN
                          : 0;
    return align_to(requested_size < MIN_LARGE_ALLOCATION_SIZE ? MIN_LARGE_ALLOCATION_SIZE : requested_size,
                    LARGE_ALLOCATION_ALIGN);
}

static void dump_sorted_free_list(FILE *file, const Allocator *allocator) {
    fprintf(file, "\nSORTED FREE LIST:\n");
    if (allocator->bucket_strategy == BUCKET_ARENAS) {
        fprintf(file, "YOU ARE USING ARENAS, THERE ARE A LOT OF SORTED FREE LISTS AND I REFUSE TO PRINT ALL\n");
    } else {
        const int num = 1000;
        int n_iters_left = num;
        int first_iter = 1;
        const GPMemorySlotMeta *meta = (GPMemorySlotMeta *) (get_bucket_entry(allocator, 0) - sizeof(GPMemorySlotMeta));
        size_t cnt = 0;
        for (void *i = meta->data; --n_iters_left && i != meta->data || first_iter;
             i = ((GPMemorySlotMeta *) (i - sizeof(GPMemorySlotMeta)))->next_bigger_free, cnt++) {
            first_iter = 0;
            GPMemorySlotMeta *im = i - sizeof(GPMemorySlotMeta);
            validate_checksum_of(allocator, im, 1);
            dump_gp_slot_meta_to_file(file, im, cnt + 1);
        }
        if (n_iters_left == 0)
            fprintf(file, ".... (there's more)\n");
    }
}

static void dump_bucket_binary_tree_to_file(FILE *file, const Allocator *allocator) {
    assert_internal(file && allocator && allocator->bucket_strategy == BUCKET_TREE && "illegal usage");
    fprintf(file, "digraph G {\n");
    for (size_t i = 0; i < 2 * allocator->gpa.rounded_num_buckets_pow_2 - 1; i++) {
        // dump bucket tree node to file (generate graph structure)
        const GPBucketTreeNode *node = &allocator->gpa.bucket_tree[i];
        const GPBucketTreeNode *left = get_bbt_child(allocator, node, 0);
        const GPBucketTreeNode *right = get_bbt_child(allocator, node, 1);
        if (!left || !right) {
            assert_internal((!left && !right) && "unreachable");
            continue;
        }
        fprintf(file, "    node%zu -> node%zu;\n", i, (size_t) (left - allocator->gpa.bucket_tree));
        fprintf(file, "    node%zu -> node%zu;\n", i, (size_t) (right - allocator->gpa.bucket_tree));
    }
    for (size_t i = 0; i < 2 * allocator->gpa.rounded_num_buckets_pow_2 - 1; i++) {
        // dump bucket tree node to file (generate node styles)
        const GPBucketTreeNode *node = &allocator->gpa.bucket_tree[i];
        const GPBucketTreeNode *left = get_bbt_child(allocator, node, 0);
        const GPBucketTreeNode *right = get_bbt_child(allocator, node, 1);
        size_t stride;
        if (!left || !right) {
            assert_internal((!left && !right) && "unreachable");
            stride = 1;
        } else {
            stride = 2 * (right->bucket_idx - left->bucket_idx);
        }
        const char *style = node->is_active ? "color=yellow, style=filled" : "color=grey, style=filled";
        fprintf(file, "    node%zu [label=\"node%zu (stride %zu)\", %s];\n", i, i, stride, style);
    }
    fprintf(file, "}\n");
}

void virtalloc_dump_allocator_to_file_impl(FILE *file, Allocator *allocator) {
    assert_external(allocator && "illegal usage: allocator must not be NULL");
    allocator->block_logging = 1; // this function is itself logging, so disable logging
    fprintf(file, "\n===== ALLOCATOR (%p) =====\n", allocator);
    fprintf(file, "First General Purpose Slot: %p\n", allocator->gpa.first_slot);
    fprintf(file, "First Small Slot: %p\n", allocator->sma.first_slot);
    fprintf(file, "Num Buckets: %zu\n", allocator->gpa.num_buckets);
    fprintf(file, "Bucket Strategy: %s\n", allocator->bucket_strategy == BUCKET_ARENAS
                                               ? "Arenas"
                                               : allocator->bucket_strategy == BUCKET_TREE
                                                     ? "Bucket Tree"
                                                     : "Disable Buckets");
    fprintf(file, "Bucket Sizes: ");
    for (size_t i = 0; i < min(16, allocator->gpa.num_buckets); i++)
        fprintf(file, "%zu ", allocator->gpa.bucket_sizes[i]);
    fprintf(file, " ......\n");
    fprintf(file, "Bucket Values: ");
    for (size_t i = 0; i < min(16, allocator->gpa.num_buckets); i++)
        fprintf(file, "%p ", get_bucket_entry(allocator, i));
    fprintf(file, " ......\n\n");

    // print all the non-null bucket sizes/values
    fprintf(file, "PHYSICAL BUCKET VALUES:\n");
    for (size_t i = 0; i < allocator->gpa.num_buckets; i++) {
        if (!allocator->gpa.bucket_values[i]) {
            fprintf(file, "BUCKET %zu: size %zu\nNULL ENTRY\n", i + 1, allocator->gpa.bucket_sizes[i]);
        } else {
            fprintf(file, "BUCKET %zu: size %zu\n", i + 1, allocator->gpa.bucket_sizes[i]);
            if (allocator->bucket_strategy != BUCKET_TREE)
                dump_gp_slot_meta_to_file(file, get_meta(allocator, allocator->gpa.bucket_values[i], NO_EXPECTATION),
                                          i + 1);
            else
                fprintf(
                    file,
                    "===== GENERAL PURPOSE SLOT %4zu (%p) =====\nCANNOT PRINT MORE INFO BECAUSE WITH BUCKET TREE IT MIGHT BE INVALID\n",
                    i + 1, allocator->gpa.bucket_values[i]);
        }
    }

    fprintf(file, "\nSEMANTIC BUCKET VALUES:\n");
    for (size_t i = 0; i < allocator->gpa.num_buckets; i++) {
        if (!get_bucket_entry(allocator, i)) {
            fprintf(file, "BUCKET %zu: size %zu\nNULL ENTRY\n", i + 1, allocator->gpa.bucket_sizes[i]);
        } else {
            fprintf(file, "BUCKET %zu: size %zu\n", i + 1, allocator->gpa.bucket_sizes[i]);
            dump_gp_slot_meta_to_file(file, get_meta(allocator, get_bucket_entry(allocator, i), NO_EXPECTATION), i + 1);
        }
    }

    if (allocator->bucket_strategy == BUCKET_TREE) {
        fprintf(file, "\nBUCKET TREE (in graphviz format):\n");
        dump_bucket_binary_tree_to_file(file, allocator);
    }

    dump_sorted_free_list(file, allocator);

    // print all slots
    fprintf(file, "\nGENERAL PURPOSE SLOTS:\n");
    size_t i = 1;
    GPMemorySlotMeta *gp_meta = get_meta(allocator, allocator->gpa.first_slot, NO_EXPECTATION);
    const void *gp_start = gp_meta->data;
    int first_iter = 1;
    while (gp_meta->data != gp_start || first_iter) {
        first_iter = 0;
        dump_gp_slot_meta_to_file(file, gp_meta, i);
        // advance
        gp_meta = get_meta(allocator, gp_meta->next, NO_EXPECTATION);
        i++;
    }

    if (allocator->sma.first_slot) {
        fprintf(file, "\nSMALL RR SLOTS:\n");
        i = 1;
        SmallRRMemorySlotMeta *sm_meta = allocator->sma.first_slot - sizeof(SmallRRMemorySlotMeta);
        const SmallRRMemorySlotMeta *sm_start = sm_meta;
        first_iter = 1;
        while (sm_meta != sm_start || first_iter) {
            first_iter = 0;
            dump_sm_slot_meta_to_file(file, sm_meta, i);
            // advance
            sm_meta = get_next_rr_slot(allocator, (void *) sm_meta + sizeof(SmallRRMemorySlotMeta))
                      - sizeof(SmallRRMemorySlotMeta);
            i++;
        }
    }

    fprintf(file, "\n===== ////////////////////////// =====\n");
    allocator->block_logging = 0; // re-enable logging (does nothing if the project is not compiled with it)
}

static int try_add_new_memory(Allocator *allocator, const size_t min_size, const int using_rr_allocator) {
    debug_print_enter_fn(allocator->block_logging, "try_add_new_memory");
    assert_internal(min_size >= 8 && "unreachable");
    if (!using_rr_allocator && allocator->gpa_add_new_memory && allocator->request_new_memory) {
        void *mem = allocator->request_new_memory(min_size);
        if (!mem) {
            debug_print_leave_fn(allocator->block_logging, "try_add_new_memory");
            return 0;
        }
        const size_t size = *(size_t *) mem;
        allocator->gpa_add_new_memory(allocator, mem, size);
        debug_print_leave_fn(allocator->block_logging, "try_add_new_memory");
        return 1;
    }
    if (using_rr_allocator && allocator->sma_add_new_memory && (
            allocator->request_new_memory || allocator->sma_request_mem_from_gpa)) {
        void *mem;
        if (allocator->sma_request_mem_from_gpa)
            mem = virtalloc_malloc_impl(allocator, max(min_size, MAX_TINY_ALLOCATION_SIZE), 0);
        else
            mem = allocator->request_new_memory(min_size);

        if (!mem) {
            debug_print_leave_fn(allocator->block_logging, "try_add_new_memory");
            return 0;
        }
        const size_t size = allocator->sma_request_mem_from_gpa
                                ? max(min_size, MAX_TINY_ALLOCATION_SIZE)
                                : *(size_t *) mem; // request_new_memory writes buffer capacity to first 8 buf bytes
        allocator->sma_add_new_memory(allocator, mem, size, !allocator->sma_request_mem_from_gpa);
        debug_print_leave_fn(allocator->block_logging, "try_add_new_memory");
        return 1;
    }
    debug_print_leave_fn(allocator->block_logging, "try_add_new_memory");
    return 0;
}

void *virtalloc_malloc_impl(Allocator *allocator, size_t size, const int is_retry_run) {
    check_allocator(allocator);
    debug_print_enter_fn(allocator->block_logging, "virtalloc_malloc_impl");
    allocator->pre_alloc_op(allocator);

    int using_rr_allocator = 0;
    if (!allocator->no_rr_allocator && size < MAX_TINY_ALLOCATION_SIZE - sizeof(SmallRRMemorySlotMeta)) {
        // use the small round-robin allocator
        using_rr_allocator = 1;
        void *rr_slot = allocator->sma.rr_slot;
        if (!rr_slot)
            goto oom;
        const void *starting_rr_slot = allocator->sma.rr_slot;
        int is_first_iter = 1;
        size_t ic = 0;
        while (((rr_slot = get_next_rr_slot(allocator, rr_slot)) != starting_rr_slot && ic < allocator->sma.
                max_slot_checks_before_oom) || is_first_iter) {
            is_first_iter = 0;
            const SmallRRMemorySlotMeta *meta = rr_slot - sizeof(SmallRRMemorySlotMeta);
            if (meta->meta_type == RR_META_TYPE_LINK) {
                rr_slot = get_next_rr_slot(allocator, rr_slot);
                continue;
            }
            if (meta->is_free)
                break;
            ic++;
        }
        SmallRRMemorySlotMeta *meta = rr_slot - sizeof(SmallRRMemorySlotMeta);
        if (meta->meta_type == RR_META_TYPE_SLOT && meta->is_free) {
            meta->is_free = 0;
            allocator->sma.rr_slot = rr_slot;
            allocator->post_alloc_op(allocator);
            return rr_slot;
        }
        goto oom;
    }

    // pad to alignment requirement and add safety padding to prevent off-by-1 bugs on the user end
    size = is_retry_run ? size : get_gpa_compatible_size(allocator, size);

    // check if the size exceeds a certain limit and if it does, use early release mechanism for the allocation
    if (size >= allocator->gpa.min_size_for_early_release && allocator->request_new_memory) {
        size = round_to_power_of_2(size); // should make realloc much more efficient
        void *mem = allocator->request_new_memory(sizeof(GPEarlyReleaseMeta) + size);
        if (!mem)
            return NULL;
        const size_t granted_size = *(size_t *) mem;
        assert_external(granted_size >= sizeof(GPEarlyReleaseMeta) + size);
        GPEarlyReleaseMeta meta_content = {
            .time_to_checksum_check = 0, .checksum = 0, .data = mem, .size = granted_size - sizeof(GPEarlyReleaseMeta),
            .__padding = 0, .__bit_padding2 = 0, .meta_type = GP_META_TYPE_EARLY_RELEASE_SLOT
        };
        refresh_checksum_of(allocator, &meta_content);
        *(GPEarlyReleaseMeta *) mem = meta_content;
        return mem + sizeof(GPEarlyReleaseMeta);
    }

    // find the bucket that fits the size (the largest bucket that is still smaller)
    const size_t bucket_idx = get_bucket_index(allocator, size);
    void *attempted_slot = get_bucket_entry(allocator, bucket_idx);
    if (!attempted_slot)
        // no slot of that size or bigger is available
        goto oom;
    GPMemorySlotMeta *meta = get_meta(allocator, attempted_slot, EXPECT_IS_FREE);

    const GPMemorySlotMeta *biggest_meta;
    const void *biggest_slot;
    void *smallest_slot;
    if (allocator->bucket_strategy == BUCKET_ARENAS) {
        smallest_slot = NULL;
        biggest_meta = NULL;
        biggest_slot = NULL;
    } else {
        smallest_slot = get_bucket_entry(allocator, 0);
        const GPMemorySlotMeta *smallest_meta = get_meta(allocator, smallest_slot, EXPECT_IS_FREE);
        // get the biggest free slot (the next smaller one links to the biggest one because the list is circular)
        biggest_meta = get_meta(allocator, smallest_meta->next_smaller_free, EXPECT_IS_FREE);
        biggest_slot = biggest_meta->data;
    }

    // try to find the smallest free slot to consume that still fits through forwards exploration (best-fit strategy)
    int is_first_iter = 1;
    const void *starting_slot = meta->data;
    size_t ic = 0;
    while (meta->size < size
           && ((meta->data != starting_slot && meta->data != smallest_slot && ic < allocator->gpa.
                max_slot_checks_before_oom) || is_first_iter)) {
        ic++;
        is_first_iter = 0;
        meta = get_meta(allocator, meta->next_bigger_free, EXPECT_IS_FREE);
    }
    if (meta->size >= size)
        // no slot that is big enough was found
        goto found;
    if (allocator->bucket_strategy != BUCKET_ARENAS && (meta->data == smallest_slot || meta->data == starting_slot))
        // the biggest slot was definitely checked, and it is not big enough
        goto oom;

    // this loop avoids code duplication
    for (int iter_type = 0; iter_type < 2; iter_type++) {
        switch (iter_type) {
            case 0:
                // max slot checks exceeded, try to go down from the next bigger bucket instead (backwards exploration)
                attempted_slot = bucket_idx == allocator->gpa.num_buckets - 1 || allocator->bucket_strategy ==
                                 BUCKET_ARENAS
                                     ? NULL
                                     : get_bucket_entry(allocator, bucket_idx + 1);
                break;
            case 1:
                // the next bigger slot isn't populated, check the biggest slot
                attempted_slot = biggest_meta ? biggest_meta->data : NULL;
                break;
            default:
                assert_internal(0 && "unreachable");
                return NULL;
        }

        if (attempted_slot) {
            meta = get_meta(allocator, attempted_slot, EXPECT_IS_FREE);
            is_first_iter = 1;
            starting_slot = meta->data;
            ic = 0;
            while (meta->size > size
                   && ((meta->data != starting_slot && meta->data != biggest_slot && ic < allocator->gpa.
                        max_slot_checks_before_oom) || is_first_iter)) {
                ic++;
                is_first_iter = 0;
                meta = get_meta(allocator, meta->next_smaller_free, EXPECT_IS_FREE);
            }
            if (meta->size < size)
                // meta currently refers to the next smaller one after the last match -> advance back to the last match
                meta = get_meta(allocator, meta->next_bigger_free, EXPECT_IS_FREE);
            if (meta->size >= size)
                goto found;
            if (meta->data == biggest_slot || meta->data == starting_slot)
                // the biggest slot was definitely checked, and it is not big enough
                goto oom;
        }
    }

    // no slot that is big enough was found
    goto oom;

found:
    assert_internal(meta && meta->size >= size && "unreachable");

    const size_t remaining_bytes = meta->size - size;
    if (remaining_bytes < sizeof(GPMemorySlotMeta) + MIN_LARGE_ALLOCATION_SIZE) {
        // remaining slot would be too small, just convert the current slot to an allocated one
        unbind_from_sorted_free_list(allocator, meta);
        meta->is_free = 0;
        refresh_checksum_of(allocator, meta);
    } else {
        // split into 2 slots
        GPMemorySlotMeta *next_meta = get_meta(allocator, meta->next, NO_EXPECTATION);

        unbind_from_sorted_free_list(allocator, meta);
        meta->is_free = 0;
        meta->size = size;

        void *new_slot_data = meta->data + size + sizeof(GPMemorySlotMeta);
        GPMemorySlotMeta *new_slot_meta_ptr = new_slot_data - sizeof(GPMemorySlotMeta);
        *new_slot_meta_ptr = (GPMemorySlotMeta){
            .checksum = 0, .size = remaining_bytes - sizeof(GPMemorySlotMeta), .data = new_slot_data,
            .next = meta->next, .prev = meta->data, .next_bigger_free = NULL, .next_smaller_free = NULL,
            .time_to_checksum_check = 0, .memory_pointer_right_adjustment = 0, .is_free = 1, .memory_is_owned = 0,
            .__bit_padding1 = 0, .__padding = {0}, .__bit_padding2 = 0, .meta_type = GP_META_TYPE_SLOT
        };

        // insert slot into normal linked list
        meta->next = new_slot_data;
        next_meta->prev = new_slot_data;

        refresh_checksum_of(allocator, meta);
        refresh_checksum_of(allocator, next_meta);
        // redundant because insert_into_sorted_free_list does this anyway
        // refresh_checksum_of(allocator, new_slot_meta_ptr);

        insert_into_sorted_free_list(allocator, new_slot_meta_ptr);
    }

    // success
    allocator->post_alloc_op(allocator);
    debug_print_leave_fn(allocator->block_logging, "virtalloc_malloc_impl");
    return meta->data;

oom:
    // out of memory (try to request more)
    if (!is_retry_run && try_add_new_memory(
            allocator,
            max(size, max(allocator->bucket_strategy == BUCKET_ARENAS ? allocator->gpa.bucket_sizes[allocator->gpa.
                    num_buckets - 1] : 0, MIN_NEW_MEM_REQUEST_SIZE)) + sizeof(GPMemorySlotMeta) + LARGE_ALLOCATION_ALIGN
            - 1, using_rr_allocator)) {
        // retry by requesting new memory and re-running (can only retry once)
        void *mem = virtalloc_malloc_impl(allocator, size, 1);
        allocator->post_alloc_op(allocator);
        debug_print_leave_fn(allocator->block_logging, "virtalloc_malloc_impl");
        return mem;
    }

    // failure
    allocator->post_alloc_op(allocator);
    debug_print_leave_fn(allocator->block_logging, "virtalloc_malloc_impl");
    return NULL;
}

void virtalloc_free_impl(Allocator *allocator, void *p) {
    check_allocator(allocator);
    assert_external(p && "Illegal argument: p (pointer) parameter in virtalloc_free call must be non-null");
    debug_print_enter_fn(allocator->block_logging, "virtalloc_free_impl");
    allocator->pre_alloc_op(allocator);

    const GenericMemorySlotMeta *gm = p - sizeof(GenericMemorySlotMeta);
    if (gm->meta_type == GP_META_TYPE_SLOT) {
        GPMemorySlotMeta *meta = get_meta(allocator, p, EXPECT_IS_ALLOCATED);
        validate_checksum_of(allocator, meta, 1); // force validate the checksum (makes sense here)
        meta->is_free = 1;
        refresh_checksum_of(allocator, meta);
        coalesce_memory_slots(allocator, meta, 0);
        refresh_checksum_of(allocator, meta);
    } else if (gm->meta_type == GP_META_TYPE_EARLY_RELEASE_SLOT) {
        GPEarlyReleaseMeta *meta = get_early_rel_meta(allocator, p);
        validate_checksum_of(allocator, meta, 1);
        assert_internal(meta->data == p && "unreachable");
        allocator->release_memory(p);
    } else if (gm->meta_type == RR_META_TYPE_SLOT) {
        SmallRRMemorySlotMeta *meta = p - sizeof(SmallRRMemorySlotMeta);
        assert_external(!meta->is_free && "attempted to free an already free slot (double free)");
        meta->is_free = 1;
    } else {
        assert_external(0 && "invalid pointer passed to free: not associated with any allocation");
    }

    allocator->post_alloc_op(allocator);
    debug_print_leave_fn(allocator->block_logging, "virtalloc_free_impl");
}

void *virtalloc_realloc_impl(Allocator *allocator, void *p, size_t size) {
    check_allocator(allocator);
    debug_print_enter_fn(allocator->block_logging, "virtalloc_realloc_impl");
    allocator->pre_alloc_op(allocator);

    if (!p)
        return virtalloc_malloc_impl(allocator, size, 0);

    const GenericMemorySlotMeta *gm = p - sizeof(GenericMemorySlotMeta);
    if (gm->meta_type != RR_META_TYPE_SLOT && gm->meta_type != GP_META_TYPE_SLOT && gm->meta_type !=
        GP_META_TYPE_EARLY_RELEASE_SLOT) {
        assert_external(0 && "invalid pointer: does not correspond to allocation");
        return NULL;
    }

    if (gm->meta_type == RR_META_TYPE_SLOT) {
        if (size <= MAX_TINY_ALLOCATION_SIZE - sizeof(SmallRRMemorySlotMeta)) {
            debug_print_leave_fn(allocator->block_logging, "virtalloc_realloc_impl");
            return p; // since all slots in RR allocator are the same size, no action is required
        }
        // must relocate the memory to general purpose allocator
        void *new_memory = virtalloc_malloc_impl(allocator, size, 0);
        if (!new_memory) {
            allocator->post_alloc_op(allocator);
            debug_print_leave_fn(allocator->block_logging, "virtalloc_realloc_impl");
            return NULL;
        }
        memmove(new_memory, p, MAX_TINY_ALLOCATION_SIZE - sizeof(SmallRRMemorySlotMeta));
        virtalloc_free_impl(allocator, p);
        allocator->post_alloc_op(allocator);
        return new_memory;
    }

    if (!size) {
        // free the slot
        virtalloc_free_impl(allocator, p);
        allocator->post_alloc_op(allocator);
        debug_print_leave_fn(allocator->block_logging, "virtalloc_realloc_impl");
        return NULL;
    }

    // pad to alignment requirement and add safety padding to prevent off-by-1 bugs on the user end
    const size_t og_size = size;
    size = get_gpa_compatible_size(allocator, size);

    // normal slots (smaller than the release early size limit) can be grown or shrunk without relocation
    if (gm->meta_type == GP_META_TYPE_SLOT) {
        GPMemorySlotMeta *meta = get_meta(allocator, p, EXPECT_IS_ALLOCATED);
        GPMemorySlotMeta *next_meta = get_meta(allocator, meta->next, NO_EXPECTATION);
        const size_t growth_bytes = size - meta->size;
        assert_internal(
            meta->size >= MIN_LARGE_ALLOCATION_SIZE && "this allocation is smaller than the minimum allocation size");

        if (size < meta->size) {
            // downsize the slot
            const size_t shaved_off = meta->size - size;
            if (next_meta->is_free && next_meta->data - sizeof(*next_meta) == meta->data + meta->size) {
                // merge it into the next slot because it is a free, contiguous neighbour slot
                consume_prev_slot(allocator, next_meta, meta->size - size);
            } else {
                if (shaved_off < sizeof(GPMemorySlotMeta) + MIN_LARGE_ALLOCATION_SIZE) {
                    // cannot realloc: would not create a usable memory slot (because it would be smaller than allowed)
                    allocator->post_alloc_op(allocator);
                    debug_print_leave_fn(allocator->block_logging, "virtalloc_realloc_impl");
                    return p;
                }
                // create a new free slot
                meta->size = size;
                void *new_slot_data = meta->data + size + sizeof(GPMemorySlotMeta);
                const GPMemorySlotMeta new_slot_meta_content = {
                    .checksum = 0, .size = shaved_off - sizeof(GPMemorySlotMeta), .data = new_slot_data,
                    .next = meta->next,
                    .prev = meta->data, .next_bigger_free = NULL, .next_smaller_free = NULL,
                    .time_to_checksum_check = 0,
                    .memory_pointer_right_adjustment = 0, .is_free = 1, .memory_is_owned = 0, .__bit_padding1 = 0,
                    .__padding = {0}, .__bit_padding2 = 0, .meta_type = GP_META_TYPE_SLOT
                };
                GPMemorySlotMeta *new_slot_meta_ptr = new_slot_data - sizeof(GPMemorySlotMeta);
                *new_slot_meta_ptr = new_slot_meta_content;

                // insert slot into normal linked list
                meta->next = new_slot_data;
                next_meta->prev = new_slot_data;

                refresh_checksum_of(allocator, meta);
                refresh_checksum_of(allocator, next_meta);
                // redundant because insert_into_sorted_free_list does this anyway
                // refresh_checksum_of(allocator, new_slot_meta_ptr);

                insert_into_sorted_free_list(allocator, new_slot_meta_ptr);
            }
            allocator->post_alloc_op(allocator);
            debug_print_leave_fn(allocator->block_logging, "virtalloc_realloc_impl");
            return p;
        } else if (size == meta->size && (og_size >= MIN_LARGE_ALLOCATION_SIZE || allocator->no_rr_allocator)) {
            // no need to do anything, except when og_size < MIN_LARGE_ALLOCATION_SIZE. In that case, we want to move
            // the data to an RR slot (if RRA is enabled) to reduce metadata overhead for small allocations.
            return p;
        } else if (size > meta->size && next_meta->is_free && next_meta->size + sizeof(GPMemorySlotMeta) >= growth_bytes
                   && next_meta->data - sizeof(*next_meta) == meta->data + meta->size) {
            // trying to grow slot (and there is adjacent free space to grow into)
            consume_next_slot(allocator, meta, growth_bytes);
            allocator->post_alloc_op(allocator);
            debug_print_leave_fn(allocator->block_logging, "virtalloc_realloc_impl");
            return p;
        }
    } else {
        assert_internal(gm->meta_type == GP_META_TYPE_EARLY_RELEASE_SLOT && "unreachable");
        const GPEarlyReleaseMeta *germ = get_early_rel_meta(allocator, p);
        size = round_to_power_of_2(size);
        if (size == germ->size)
            // no need to relocate or resize, the buffer capacity is already available
            return p;
    }

    // must relocate the memory to grow the slot
    void *new_memory = virtalloc_malloc_impl(allocator, og_size, 0);
    if (!new_memory) {
        allocator->post_alloc_op(allocator);
        debug_print_leave_fn(allocator->block_logging, "virtalloc_realloc_impl");
        return NULL;
    }
    if (gm->meta_type == GP_META_TYPE_SLOT) {
        const GPMemorySlotMeta *meta = get_meta(allocator, p, EXPECT_IS_ALLOCATED);
        memmove(new_memory, p, min(meta->size, og_size));
    } else {
        const GPEarlyReleaseMeta *meta = get_early_rel_meta(allocator, p);
        memmove(new_memory, p, min(meta->size, og_size));
    }
    virtalloc_free_impl(allocator, p);
    allocator->post_alloc_op(allocator);
    debug_print_leave_fn(allocator->block_logging, "virtalloc_realloc_impl");
    return new_memory;
}

void virtalloc_pre_op_callback_impl(Allocator *allocator) {
    assert_internal(allocator->intra_thread_lock_count >= 0);
    if (!allocator->assume_thread_safe_usage)
        lock_virtual_allocator(allocator);
}

void virtalloc_post_op_callback_impl(Allocator *allocator) {
    assert_internal(allocator->intra_thread_lock_count >= 0);
    if (!allocator->assume_thread_safe_usage)
        unlock_virtual_allocator(allocator);
}

void virtalloc_gpa_add_new_memory_impl(Allocator *allocator, void *p, size_t size) {
    assert_external(size >= sizeof(GPMemorySlotMeta) + MIN_LARGE_ALLOCATION_SIZE);
    allocator->pre_alloc_op(allocator);

    const size_t right_adjustment = (LARGE_ALLOCATION_ALIGN - (size_t) p % LARGE_ALLOCATION_ALIGN) %
                                    LARGE_ALLOCATION_ALIGN;
    p += right_adjustment;
    size -= right_adjustment;
    void *slot = p + sizeof(GPMemorySlotMeta);

    GPMemorySlotMeta *first_meta = NULL;
    GPMemorySlotMeta *last_meta = NULL;
    if (allocator->gpa.first_slot) {
        first_meta = get_meta(allocator, allocator->gpa.first_slot, NO_EXPECTATION);
        last_meta = get_meta(allocator, first_meta->prev, NO_EXPECTATION);
    }
    const GPMemorySlotMeta new_slot_meta_content = {
        .checksum = 0, .size = size - sizeof(GPMemorySlotMeta), .data = slot,
        .next = first_meta ? first_meta->data : slot, .prev = last_meta ? last_meta->data : slot,
        .next_bigger_free = NULL, .next_smaller_free = NULL, .time_to_checksum_check = 0,
        .memory_pointer_right_adjustment = right_adjustment, .is_free = 1, .memory_is_owned = 1, .__bit_padding1 = 0,
        .__padding = {0}, .__bit_padding2 = 0, .meta_type = GP_META_TYPE_SLOT
    };
    *(GPMemorySlotMeta *) p = new_slot_meta_content;

    // insert slot into normal linked list
    if (first_meta) {
        assert_internal(last_meta && "unreachable");
        last_meta->next = slot;
        first_meta->prev = slot;
        refresh_checksum_of(allocator, first_meta);
        refresh_checksum_of(allocator, last_meta);
    } else {
        allocator->gpa.first_slot = slot;
    }

    coalesce_memory_slots(allocator, p, 0);

    // since owned slots have been added separately, the heap must be scanned at destroy time for those slots and
    // the release callback must be called on them -> enable that behavior
    allocator->release_only_allocator = 0;

    allocator->post_alloc_op(allocator);
}

void virtalloc_sma_add_new_memory_impl(Allocator *allocator, void *p, size_t size, const int must_free_later) {
    assert_external(
        size >= sizeof(SmallRRNextSlotLinkMeta) + sizeof(SmallRRStartOfMemoryChunkMeta) + sizeof(SmallRRMemorySlotMeta)
        + MAX_TINY_ALLOCATION_SIZE);
    allocator->pre_alloc_op(allocator);

    void *og_p = p;

    // align p
    const size_t right_adjustment = (MAX_TINY_ALLOCATION_SIZE - (size_t) p % MAX_TINY_ALLOCATION_SIZE) %
                                    MAX_TINY_ALLOCATION_SIZE;
    p += right_adjustment;
    size -= right_adjustment;
    void *aligned_p = p;

    // add the SmallRRStartOfMemoryChunkMeta
    SmallRRStartOfMemoryChunkMeta mcm = {.must_release_chunk_on_destroy = must_free_later, .__padding = {0}};
    memmove(&mcm.memory_chunk_ptr_raw_bytes, &og_p, sizeof(og_p));
    *(SmallRRStartOfMemoryChunkMeta *) p = mcm;
    p += sizeof(SmallRRStartOfMemoryChunkMeta);
    size -= sizeof(SmallRRStartOfMemoryChunkMeta);

    // add as many slots as possible
    while (size >= MAX_TINY_ALLOCATION_SIZE) {
        *(SmallRRMemorySlotMeta *) p = (SmallRRMemorySlotMeta){.is_free = 1, .meta_type = RR_META_TYPE_SLOT};
        p += MAX_TINY_ALLOCATION_SIZE;
        size -= MAX_TINY_ALLOCATION_SIZE;
    }

    // replace the last added slot with a link slot (for linking to the next section)
    p -= MAX_TINY_ALLOCATION_SIZE;
    *(SmallRRNextSlotLinkMeta *) p = (SmallRRNextSlotLinkMeta){
        .__bit_padding = 0, .meta_type = RR_META_TYPE_LINK
    };
    p += sizeof(SmallRRNextSlotLinkMeta);
    // link back to the first slot (because that's what the last link in the chain does)
    *(void **) p = allocator->sma.first_slot
                       ? allocator->sma.first_slot
                       : aligned_p + sizeof(SmallRRStartOfMemoryChunkMeta) + sizeof(SmallRRMemorySlotMeta);

    // link the new slots to the existing chain
    if (allocator->sma.first_slot) {
        assert_internal(allocator->sma.last_slot && allocator->sma.rr_slot && "unreachable");
        *(void **) allocator->sma.last_slot = aligned_p + sizeof(SmallRRStartOfMemoryChunkMeta) + sizeof(
                                                  SmallRRMemorySlotMeta);
        allocator->sma.last_slot = p;
    } else {
        assert_internal(!allocator->sma.last_slot && !allocator->sma.rr_slot && "unreachable");
        allocator->sma.first_slot = aligned_p + sizeof(SmallRRStartOfMemoryChunkMeta) + sizeof(SmallRRMemorySlotMeta);
        allocator->sma.last_slot = p;
    }
    // guaranteed free memory (also happens to be an easy OOM fix)
    allocator->sma.rr_slot = aligned_p + sizeof(SmallRRStartOfMemoryChunkMeta) + sizeof(SmallRRMemorySlotMeta);

    // since owned slots have been added separately, the heap must be scanned at destroy time for those slots and
    // the release callback must be called on them -> enable that behavior
    allocator->release_only_allocator = 0;

    allocator->post_alloc_op(allocator);
}
