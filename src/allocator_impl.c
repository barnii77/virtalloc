#include <assert.h>
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

void virtalloc_dump_allocator_to_file_impl(FILE *file, Allocator *allocator) {
    assert(allocator && "illegal usage: allocator must not be NULL");
    fprintf(file, "\n===== ALLOCATOR (%p) =====\n", allocator);
    fprintf(file, "First Slot: %p\n", allocator->gpa.first_slot);
    fprintf(file, "Num Buckets: %zu\n", allocator->gpa.num_buckets);
    fprintf(file, "Bucket Sizes: ");
    for (size_t i = 0; i < min(16, allocator->gpa.num_buckets); i++)
        fprintf(file, "%zu ", allocator->gpa.bucket_sizes[i]);
    fprintf(file, " ......\n");
    fprintf(file, "Bucket Values: ");
    for (size_t i = 0; i < min(16, allocator->gpa.num_buckets); i++)
        fprintf(file, "%p ", allocator->gpa.bucket_values[i]);
    fprintf(file, " ......\n");

    // print all slots
    fprintf(file, "\n");
    size_t i = 1;
    GPMemorySlotMeta *meta = get_meta(allocator, allocator->gpa.first_slot, NO_EXPECTATION);
    void *start = meta->data;
    int first_iter = 1;
    while (meta->data != start || first_iter) {
        first_iter = 0;
        fprintf(file, "===== SLOT %4zu (%p) =====\n", i, meta->data);
        fprintf(file, "Size: %zu\n", meta->size);
        fprintf(file, "Free: %s\n", meta->is_free ? "Yes" : "No");
        fprintf(file, "Data: ");
        for (size_t j = 0; j < min(16, MIN_LARGE_ALLOCATION_SIZE); j++)
            fprintf(file, "%x ", ((unsigned char *) meta->data)[j] % 256);
        fprintf(file, " ......\n");

        // advance
        meta = get_meta(allocator, meta->next, NO_EXPECTATION);
        i++;
    }

    fprintf(file, "\n===== ////////////////////////// =====\n");
}

int try_add_new_memory(Allocator *allocator, const size_t min_size, const int using_rr_allocator) {
    assert(min_size >= 8 && "unreachable");
    if (!using_rr_allocator && allocator->gpa_add_new_memory && allocator->request_new_memory) {
        void *mem = allocator->request_new_memory(min_size);
        if (!mem)
            return 0;
        const size_t size = *(size_t *) mem;
        allocator->gpa_add_new_memory(allocator, mem, size);
        return 1;
    }
    if (using_rr_allocator && allocator->sma_add_new_memory && allocator->request_new_memory) {
        void *mem = allocator->request_new_memory(min_size);
        if (!mem)
            return 0;
        const size_t size = *(size_t *) mem;
        allocator->sma_add_new_memory(allocator, mem, size);
        return 1;
    }
    return 0;
}

void *virtalloc_malloc_impl(Allocator *allocator, size_t size, const int is_retry_run) {
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

    size = align_to(size < MIN_LARGE_ALLOCATION_SIZE ? MIN_LARGE_ALLOCATION_SIZE : size, LARGE_ALLOCATION_ALIGN);
    const size_t bucket_idx = get_bucket_index(allocator, size);
    void *attempted_slot = allocator->gpa.bucket_values[bucket_idx];
    if (!attempted_slot)
        // no slot of that size or bigger is available
        goto oom;
    if (bucket_idx == allocator->gpa.num_buckets - 1) {
        void *smallest_slot = allocator->gpa.bucket_values[0];
        if (!smallest_slot)
            // no free slot available
            goto oom;
        const GPMemorySlotMeta *smallest_meta = get_meta(allocator, smallest_slot, EXPECT_IS_FREE);
        // get the biggest free slot (the next smaller one links to the biggest one because the list is circular)
        const GPMemorySlotMeta *biggest_meta = get_meta(allocator, smallest_meta->next_smaller_free, EXPECT_IS_FREE);
        if (biggest_meta->size < size)
            // cannot allocate that much memory (not enough space)
            goto oom;
    }
    GPMemorySlotMeta *meta = get_meta(allocator, attempted_slot, EXPECT_IS_FREE);

    // try to find the smallest free slot to consume that still fits
    int is_first_iter = 1;
    const void *starting_slot = meta->data;
    size_t ic = 0;
    while (meta->size < size
           && ((meta->data != starting_slot && ic < allocator->gpa.max_slot_checks_before_oom) || is_first_iter)) {
        ic++;
        is_first_iter = 0;
        meta = get_meta(allocator, meta->next_bigger_free, EXPECT_IS_FREE);
    }
    if (meta->size < size)
        // no slot that is big enough was found
        goto oom;

    assert(meta && meta->size >= size && "unreachable");

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
        const GPMemorySlotMeta new_slot_meta_content = {
            .checksum = 0, .size = remaining_bytes - sizeof(GPMemorySlotMeta), .data = new_slot_data,
            .next = meta->next, .prev = meta->data, .next_bigger_free = NULL, .next_smaller_free = NULL, .is_free = 1,
            .memory_is_owned = 0, .memory_pointer_right_adjustment = 0, .__bit_padding1 = 0, .__padding = {0},
            .__bit_padding2 = 0, .meta_type = GP_META_TYPE_SLOT
        };
        GPMemorySlotMeta *new_slot_meta_ptr = new_slot_data - sizeof(GPMemorySlotMeta);
        *new_slot_meta_ptr = new_slot_meta_content;

        // insert slot into normal linked list
        meta->next = new_slot_data;
        next_meta->prev = new_slot_data;

        refresh_checksum_of(allocator, meta);
        refresh_checksum_of(allocator, next_meta);
        refresh_checksum_of(allocator, new_slot_meta_ptr);

        insert_into_sorted_free_list(allocator, new_slot_meta_ptr);
    }

    // success
    allocator->post_alloc_op(allocator);
    return meta->data;

oom:
    // out of memory (try to request more)
    if (!is_retry_run && try_add_new_memory(
            allocator, max(size, MIN_NEW_MEM_REQUEST_SIZE) + sizeof(GPMemorySlotMeta) + LARGE_ALLOCATION_ALIGN - 1,
            using_rr_allocator)) {
        // retry by requesting new memory and re-running (can only retry once)
        void *mem = virtalloc_malloc_impl(allocator, size, 1);
        allocator->post_alloc_op(allocator);
        return mem;
    }

    // failure
    allocator->post_alloc_op(allocator);
    return NULL;
}

void virtalloc_free_impl(Allocator *allocator, void *p) {
    allocator->pre_alloc_op(allocator);

    const GenericMemorySlotMeta *gm = p - sizeof(GenericMemorySlotMeta);
    if (gm->meta_type == GP_META_TYPE_SLOT) {
        GPMemorySlotMeta *meta = get_meta(allocator, p, EXPECT_IS_ALLOCATED);

        meta->is_free = 1;
        refresh_checksum_of(allocator, meta);

        coalesce_memory_slots(allocator, meta, 0);

        refresh_checksum_of(allocator, meta);
    } else if (gm->meta_type == RR_META_TYPE_SLOT) {
        SmallRRMemorySlotMeta *meta = p - sizeof(SmallRRMemorySlotMeta);
        assert(!meta->is_free && "attempted to free an already free slot (double free)");
        meta->is_free = 1;
    } else {
        assert(0 && "invalid pointer passed to free: not associated with any allocation");
    }

    allocator->post_alloc_op(allocator);
}

void *virtalloc_realloc_impl(Allocator *allocator, void *p, size_t size) {
    allocator->pre_alloc_op(allocator);

    if (!p)
        return virtalloc_malloc_impl(allocator, size, 0);

    const GenericMemorySlotMeta *gm = p - sizeof(GenericMemorySlotMeta);
    if (gm->meta_type == RR_META_TYPE_LINK)
        assert(0 && "invalid pointer: does not correspond to allocation");

    if (gm->meta_type == RR_META_TYPE_SLOT) {
        if (size <= MAX_TINY_ALLOCATION_SIZE - sizeof(SmallRRMemorySlotMeta))
            return p; // since all slots in RR allocator are the same size, no action is required
        // must relocate the memory to general purpose allocator
        void *new_memory = virtalloc_malloc_impl(allocator, size, 0);
        if (!new_memory) {
            allocator->post_alloc_op(allocator);
            return NULL;
        }
        memmove(new_memory, p, MAX_TINY_ALLOCATION_SIZE - sizeof(SmallRRMemorySlotMeta));
        virtalloc_free_impl(allocator, p);
        allocator->post_alloc_op(allocator);
        return new_memory;
    }

    size = align_to(size < MIN_LARGE_ALLOCATION_SIZE ? MIN_LARGE_ALLOCATION_SIZE : size, LARGE_ALLOCATION_ALIGN);
    GPMemorySlotMeta *meta = get_meta(allocator, p, EXPECT_IS_ALLOCATED);
    GPMemorySlotMeta *next_meta = get_meta(allocator, meta->next, NO_EXPECTATION);
    const size_t growth_bytes = size - meta->size;
    assert(meta->size >= MIN_LARGE_ALLOCATION_SIZE && "this allocation is smaller than the minimum allocation size");

    if (size < meta->size) {
        // downsize or free the slot
        if (!size) {
            // free the slot
            virtalloc_free_impl(allocator, p);
            allocator->post_alloc_op(allocator);
            return NULL;
        }
        // downsize the slot
        const size_t shaved_off = meta->size - size;
        if (next_meta->is_free && next_meta->data - sizeof(*next_meta) == meta->data + meta->size) {
            // merge it into the next slot because it is a free, contiguous neighbour slot
            consume_prev_slot(allocator, next_meta, meta->size - size);
        } else {
            if (shaved_off < sizeof(GPMemorySlotMeta) + MIN_LARGE_ALLOCATION_SIZE) {
                // cannot realloc: would not create a usable memory slot (because it would be smaller than allowed)
                allocator->post_alloc_op(allocator);
                return p;
            }
            // create a new free slot
            meta->size = size;
            void *new_slot_data = meta->data + size + sizeof(GPMemorySlotMeta);
            const GPMemorySlotMeta new_slot_meta_content = {
                .checksum = 0, .size = shaved_off - sizeof(GPMemorySlotMeta), .data = new_slot_data, .next = meta->next,
                .prev = meta->data, .next_bigger_free = NULL, .next_smaller_free = NULL, .is_free = 1,
                .memory_is_owned = 0, .memory_pointer_right_adjustment = 0, .__bit_padding1 = 0, .__padding = {0},
                .__bit_padding2 = 0, .meta_type = GP_META_TYPE_SLOT
            };
            GPMemorySlotMeta *new_slot_meta_ptr = new_slot_data - sizeof(GPMemorySlotMeta);
            *new_slot_meta_ptr = new_slot_meta_content;

            // insert slot into normal linked list
            meta->next = new_slot_data;
            next_meta->prev = new_slot_data;

            refresh_checksum_of(allocator, meta);
            refresh_checksum_of(allocator, next_meta);

            insert_into_sorted_free_list(allocator, new_slot_meta_ptr);
        }
        allocator->post_alloc_op(allocator);
        return p;
    }

    // trying to grow slot
    if (next_meta->is_free && next_meta->size + sizeof(GPMemorySlotMeta) >= growth_bytes
        && next_meta->data - sizeof(*next_meta) == meta->data + meta->size) {
        // can simply grow into the adjacent free space
        consume_next_slot(allocator, meta, growth_bytes);
        allocator->post_alloc_op(allocator);
        return p;
    }

    // must relocate the memory to grow the slot
    void *new_memory = virtalloc_malloc_impl(allocator, size, 0);
    if (!new_memory) {
        allocator->post_alloc_op(allocator);
        return NULL;
    }
    memmove(new_memory, p, meta->size);
    virtalloc_free_impl(allocator, p);
    allocator->post_alloc_op(allocator);
    return new_memory;
}

void virtalloc_pre_op_callback_impl(Allocator *allocator) {
    assert(allocator->intra_thread_lock_count >= 0);
    if (!allocator->assume_thread_safe_usage)
        lock_virtual_allocator(allocator);
}

void virtalloc_post_op_callback_impl(Allocator *allocator) {
    assert(allocator->intra_thread_lock_count >= 0);
    if (!allocator->assume_thread_safe_usage)
        unlock_virtual_allocator(allocator);
}

void virtalloc_gpa_add_new_memory_impl(Allocator *allocator, void *p, size_t size) {
    assert(size >= sizeof(GPMemorySlotMeta) + MIN_LARGE_ALLOCATION_SIZE);
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
        .next_bigger_free = NULL, .next_smaller_free = NULL, .is_free = 1, .memory_is_owned = 1,
        .memory_pointer_right_adjustment = right_adjustment, .__bit_padding1 = 0, .__padding = {0}, .__bit_padding2 = 0,
        .meta_type = GP_META_TYPE_SLOT
    };
    *(GPMemorySlotMeta *) p = new_slot_meta_content;

    // insert slot into normal linked list
    if (first_meta) {
        assert(last_meta && "unreachable");
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

void virtalloc_sma_add_new_memory_impl(Allocator *allocator, void *p, size_t size) {
    assert(
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
    SmallRRStartOfMemoryChunkMeta mcm = {.__padding = {0}};
    memcpy(&mcm.memory_chunk_ptr_raw_bytes, &og_p, sizeof(og_p));
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
        assert(allocator->sma.last_slot && allocator->sma.rr_slot && "unreachable");
        *(void **) allocator->sma.last_slot = aligned_p + sizeof(SmallRRStartOfMemoryChunkMeta) + sizeof(
                                                  SmallRRMemorySlotMeta);
        allocator->sma.last_slot = p;
    } else {
        assert(!allocator->sma.last_slot && !allocator->sma.rr_slot && "unreachable");
        allocator->sma.first_slot = aligned_p + sizeof(SmallRRStartOfMemoryChunkMeta) + sizeof(SmallRRMemorySlotMeta);
        allocator->sma.last_slot = p;
        allocator->sma.rr_slot = allocator->sma.first_slot;
    }

    // since owned slots have been added separately, the heap must be scanned at destroy time for those slots and
    // the release callback must be called on them -> enable that behavior
    allocator->release_only_allocator = 0;

    allocator->post_alloc_op(allocator);
}
