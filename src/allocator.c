#include <stdlib.h>
#include <assert.h>
#include <memory.h>
#include <stddef.h>
#include "virtalloc/checksum.h"
#include "virtalloc/virtual_allocator.h"
#include "virtalloc/memory_slot_meta.h"
#include "virtalloc/allocator.h"
#include "virtalloc/allocator_utils.h"
#include "virtalloc/alloc_settings.h"
#include "virtalloc/math_utils.h"

void *virtalloc_malloc_impl(VirtualAllocator *allocator, size_t size, size_t max_backward_exploration_steps) {
    allocator->pre_alloc_op_callback(allocator);

    size = align_to(size < MIN_ALLOCATION_SIZE ? MIN_ALLOCATION_SIZE : size, ALLOCATION_ALIGN);
    const size_t bucket_idx = get_bucket_index(allocator, size);
    MemorySlotMeta *meta = NULL;
    if (bucket_idx == allocator->num_buckets - 1 && allocator->bucket_sizes[bucket_idx] < size) {
        // TODO here it may actually make sense to do forward exploration as a special case before trying backward expl
        const MemorySlotMeta *smallest_meta = get_meta(allocator, allocator->bucket_values[0], EXPECT_IS_FREE);
        // get the biggest free slot (the next smaller one links to the biggest one because the list is circular)
        meta = get_meta(allocator, smallest_meta->next_smaller_free, EXPECT_IS_FREE);
        if (meta->size < size) {
            // cannot allocate that much memory (not enough space)
            allocator->post_alloc_op_callback(allocator);
            return NULL;
        }
    } else {
        void *slot_from_bucket = allocator->bucket_values[bucket_idx];
        if (!slot_from_bucket)
            // no slot of that size or bigger is available
            return NULL;
        meta = get_meta(allocator, slot_from_bucket, EXPECT_IS_FREE);
        assert(size <= meta->size && "unreachable");
    }

    // try to find the smallest free slot to consume that still fits
    int stopped_because_too_small = 0;
    while (max_backward_exploration_steps--) {
        meta = get_meta(allocator, meta->next_smaller_free, EXPECT_IS_FREE);
        if (meta->size < size) {
            stopped_because_too_small = 1;
            break;
        }
    }

    if (stopped_because_too_small)
        // the current slot meta refers to is too small, but the next bigger one fits
        meta = get_meta(allocator, meta->next_bigger_free, EXPECT_IS_FREE);

    assert(meta && meta->size >= size && "unreachable");

    const size_t remaining_bytes = meta->size - size;
    if (remaining_bytes < sizeof(MemorySlotMeta)) {
        // remaining slot would be too small, just convert the current slot to an allocated one
        unbind_from_sorted_free_list(allocator, meta);
        meta->is_free = 0;
        refresh_checksum_of(allocator, meta);
    } else {
        // split into 2 slots
        MemorySlotMeta *next_meta = get_meta(allocator, meta->next, NO_EXPECTATION);

        unbind_from_sorted_free_list(allocator, meta);
        meta->is_free = 0;
        meta->size = size;

        void *new_slot_data = meta->data + size + sizeof(MemorySlotMeta);
        const MemorySlotMeta new_slot_meta_content = {
            .sizeof_meta = sizeof(MemorySlotMeta), .checksum = 0, .size = remaining_bytes - sizeof(MemorySlotMeta),
            .data = new_slot_data, .next = meta->next, .prev = meta->data, .next_bigger_free = NULL,
            .next_smaller_free = NULL, .is_free = 1, .meta_type = NORMAL_MEMORY_SLOT_META_TYPE
        };
        MemorySlotMeta *new_slot_meta_ptr = new_slot_data - sizeof(MemorySlotMeta);
        *new_slot_meta_ptr = new_slot_meta_content;

        // insert slot into normal linked list
        meta->next = new_slot_data;
        next_meta->prev = new_slot_data;

        refresh_checksum_of(allocator, meta);
        refresh_checksum_of(allocator, next_meta);
        refresh_checksum_of(allocator, new_slot_meta_ptr);

        insert_into_sorted_free_list(allocator, new_slot_meta_ptr);

        refresh_checksum_of(allocator, meta);
        refresh_checksum_of(allocator, next_meta);
        refresh_checksum_of(allocator, new_slot_meta_ptr);
    }
    allocator->post_alloc_op_callback(allocator);
    return meta->data;
}

void virtalloc_free_impl(VirtualAllocator *allocator, void *p) {
    allocator->pre_alloc_op_callback(allocator);

    MemorySlotMeta *meta = get_meta(allocator, p, EXPECT_IS_ALLOCATED);

    meta->is_free = 1;
    refresh_checksum_of(allocator, meta);

    coalesce_memory_slots(allocator, meta, 0);

    refresh_checksum_of(allocator, meta);

    allocator->post_alloc_op_callback(allocator);
}

void *virtalloc_realloc_impl(VirtualAllocator *allocator, void *p, size_t size,
                             const size_t max_backward_exploration_steps) {
    allocator->pre_alloc_op_callback(allocator);

    if (!p)
        return virtalloc_malloc_impl(allocator, size, max_backward_exploration_steps);

    size = align_to(size < MIN_ALLOCATION_SIZE ? MIN_ALLOCATION_SIZE : size, ALLOCATION_ALIGN);
    MemorySlotMeta *meta = get_meta(allocator, p, EXPECT_IS_ALLOCATED);
    MemorySlotMeta *next_meta = get_meta(allocator, meta->next, NO_EXPECTATION);
    const size_t growth_bytes = size - meta->size;
    assert(meta->size >= MIN_ALLOCATION_SIZE && "this allocation is smaller than the minimum allocation size");

    if (size < meta->size) {
        // downsize or free the slot
        if (!size) {
            // free the slot
            virtalloc_free_impl(allocator, p);
            allocator->post_alloc_op_callback(allocator);
            return NULL;
        }
        // downsize the slot
        const size_t shaved_off = meta->size - size;
        if (next_meta->is_free && next_meta->data - sizeof(*next_meta) == meta->data + meta->size) {
            // merge it into the next slot because it is a free, contiguous neighbour slot
            consume_prev_slot(allocator, next_meta, meta->size - size);
        } else {
            if (shaved_off < sizeof(MemorySlotMeta) + MIN_ALLOCATION_SIZE) {
                // cannot realloc: would not create a usable memory slot (because it would be smaller than allowed)
                allocator->post_alloc_op_callback(allocator);
                return p;
            }
            // create a new free slot
            meta->size = size;
            void *new_slot_data = meta->data + size + sizeof(MemorySlotMeta);
            const MemorySlotMeta new_slot_meta_content = {
                .sizeof_meta = sizeof(MemorySlotMeta), .checksum = 0, .size = shaved_off - sizeof(MemorySlotMeta),
                .data = new_slot_data, .next = meta->next, .prev = meta->data, .next_bigger_free = NULL,
                .next_smaller_free = NULL, .is_free = 1, .meta_type = NORMAL_MEMORY_SLOT_META_TYPE
            };
            MemorySlotMeta *new_slot_meta_ptr = new_slot_data - sizeof(MemorySlotMeta);
            *new_slot_meta_ptr = new_slot_meta_content;

            // insert slot into normal linked list
            meta->next = new_slot_data;
            next_meta->prev = new_slot_data;

            refresh_checksum_of(allocator, meta);
            refresh_checksum_of(allocator, next_meta);

            insert_into_sorted_free_list(allocator, new_slot_meta_ptr);

            refresh_checksum_of(allocator, meta);
            refresh_checksum_of(allocator, next_meta);
            refresh_checksum_of(allocator, new_slot_meta_ptr);
        }
        allocator->post_alloc_op_callback(allocator);
        return p;
    } else if (next_meta->is_free && next_meta->size + sizeof(MemorySlotMeta) >= growth_bytes
               && next_meta->data - sizeof(*next_meta) == meta->data + meta->size) {
        // can simply grow into the adjacent free space
        consume_next_slot(allocator, meta, growth_bytes);
        allocator->post_alloc_op_callback(allocator);
        return p;
    } else {
        // must relocate the memory
        void *new_memory = virtalloc_malloc_impl(allocator, size, max_backward_exploration_steps);
        memmove(new_memory, p, meta->size);
        virtalloc_free_impl(allocator, p);
        allocator->post_alloc_op_callback(allocator);
        return new_memory;
    }
}

void virtalloc_pre_op_callback(VirtualAllocator *allocator) {
    assert(allocator->intra_thread_lock_count >= 0);
    if (!allocator->assume_thread_safe_usage)
        lock_virtual_allocator(allocator);
}

void virtalloc_post_op_callback(VirtualAllocator *allocator) {
    assert(allocator->intra_thread_lock_count >= 0);
    if (!allocator->assume_thread_safe_usage)
        unlock_virtual_allocator(allocator);
}
