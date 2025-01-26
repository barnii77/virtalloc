#include <assert.h>
#include <stdio.h>
#include <memory.h>
#include <stddef.h>
#include "virtalloc/virtual_allocator.h"
#include "virtalloc/memory_slot_meta.h"
#include "virtalloc/allocator.h"
#include "virtalloc/allocator_utils.h"
#include "virtalloc/alloc_settings.h"
#include "virtalloc/math_utils.h"

void virtalloc_dump_allocator_to_file_impl(FILE *file, VirtualAllocator *allocator) {
    assert(allocator && "illegal usage: allocator must not be NULL");
    fprintf(file, "\n===== ALLOCATOR (%p) =====\n", allocator);
    fprintf(file, "First Slot: %p\n", allocator->first_slot);
    fprintf(file, "Num Buckets: %zu\n", allocator->num_buckets);
    fprintf(file, "Bucket Sizes: ");
    for (size_t i = 0; i < min(16, allocator->num_buckets); i++)
        fprintf(file, "%zu ", allocator->bucket_sizes[i]);
    fprintf(file, " ......\n");
    fprintf(file, "Bucket Values: ");
    for (size_t i = 0; i < min(16, allocator->num_buckets); i++)
        fprintf(file, "%p ", allocator->bucket_values[i]);
    fprintf(file, " ......\n");

    // print all slots
    fprintf(file, "\n");
    size_t i = 1;
    MemorySlotMeta *meta = get_meta(allocator, allocator->first_slot, NO_EXPECTATION);
    void *start = meta->data;
    int first_iter = 1;
    while (meta->data != start || first_iter) {
        first_iter = 0;
        fprintf(file, "===== SLOT %4zu (%p) =====\n", i, meta->data);
        fprintf(file, "Size: %zu\n", meta->size);
        fprintf(file, "Free: %s\n", meta->is_free ? "Yes" : "No");
        fprintf(file, "Data: ");
        for (size_t j = 0; j < min(16, MIN_ALLOCATION_SIZE); j++)
            fprintf(file, "%x ", ((unsigned char *) meta->data)[j] % 256);
        fprintf(file, " ......\n");

        // advance
        meta = get_meta(allocator, meta->next, NO_EXPECTATION);
        i++;
    }

    fprintf(file, "\n===== ////////////////////////// =====\n");
}

int try_add_new_memory(VirtualAllocator *allocator, size_t min_size) {
    assert(min_size >= 8 && "unreachable");
    if (allocator->add_new_memory && allocator->request_new_memory) {
        void *mem = allocator->request_new_memory(min_size);
        if (!mem)
            return 0;
        size_t size = *(size_t *) mem;
        allocator->add_new_memory(allocator, mem, size);
        return 1;
    }
    return 0;
}

void *virtalloc_malloc_impl(VirtualAllocator *allocator, size_t size, int is_retry_run) {
    allocator->pre_alloc_op(allocator);

    size = align_to(size < MIN_ALLOCATION_SIZE ? MIN_ALLOCATION_SIZE : size, ALLOCATION_ALIGN);
    const size_t bucket_idx = get_bucket_index(allocator, size);
    void *attempted_slot = allocator->bucket_values[bucket_idx];
    if (!attempted_slot)
        // no slot of that size or bigger is available
        goto oom;
    if (bucket_idx == allocator->num_buckets - 1) {
        void *smallest_slot = allocator->bucket_values[0];
        if (!smallest_slot)
            // no free slot available
            goto oom;
        const MemorySlotMeta *smallest_meta = get_meta(allocator, smallest_slot, EXPECT_IS_FREE);
        // get the biggest free slot (the next smaller one links to the biggest one because the list is circular)
        MemorySlotMeta *biggest_meta = get_meta(allocator, smallest_meta->next_smaller_free, EXPECT_IS_FREE);
        if (biggest_meta->size < size)
            // cannot allocate that much memory (not enough space)
            goto oom;
    }
    MemorySlotMeta *meta = get_meta(allocator, attempted_slot, EXPECT_IS_FREE);

    // try to find the smallest free slot to consume that still fits
    int is_first_iter = 1;
    void *starting_slot = meta->data;
    while (meta->size < size && (meta->data != starting_slot || is_first_iter)) {
        is_first_iter = 0;
        meta = get_meta(allocator, meta->next_bigger_free, EXPECT_IS_FREE);
    }
    if (meta->data == starting_slot && !is_first_iter)
        // no slot that is big enough was found
        goto oom;

    assert(meta && meta->size >= size && "unreachable");

    const size_t remaining_bytes = meta->size - size;
    if (remaining_bytes < sizeof(MemorySlotMeta) + MIN_ALLOCATION_SIZE) {
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
                .next_smaller_free = NULL, .is_free = 1, .memory_is_owned = 0,
                .memory_pointer_right_adjustment = 0, .meta_type = NORMAL_MEMORY_SLOT_META_TYPE
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
    }

    allocator->post_alloc_op(allocator);
    return meta->data;

oom:
    if (!is_retry_run && try_add_new_memory(allocator, size + sizeof(MemorySlotMeta) + ALLOCATION_ALIGN - 1)) {
        // retry by requesting new memory and re-running (can only retry once)
        void *mem = virtalloc_malloc_impl(allocator, size, 1);
        allocator->post_alloc_op(allocator);
        return mem;
    }
    goto fail;

fail:
    allocator->post_alloc_op(allocator);
    return NULL;
}

void virtalloc_free_impl(VirtualAllocator *allocator, void *p) {
    allocator->pre_alloc_op(allocator);

    MemorySlotMeta *meta = get_meta(allocator, p, EXPECT_IS_ALLOCATED);

    meta->is_free = 1;
    refresh_checksum_of(allocator, meta);

    coalesce_memory_slots(allocator, meta, 0);

    refresh_checksum_of(allocator, meta);

    allocator->post_alloc_op(allocator);
}

void *virtalloc_realloc_impl(VirtualAllocator *allocator, void *p, size_t size) {
    allocator->pre_alloc_op(allocator);

    if (!p)
        return virtalloc_malloc_impl(allocator, size, 0);

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
            allocator->post_alloc_op(allocator);
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
                allocator->post_alloc_op(allocator);
                return p;
            }
            // create a new free slot
            meta->size = size;
            void *new_slot_data = meta->data + size + sizeof(MemorySlotMeta);
            const MemorySlotMeta new_slot_meta_content = {
                    .sizeof_meta = sizeof(MemorySlotMeta), .checksum = 0, .size = shaved_off - sizeof(MemorySlotMeta),
                    .data = new_slot_data, .next = meta->next, .prev = meta->data, .next_bigger_free = NULL,
                    .next_smaller_free = NULL, .is_free = 1, .memory_is_owned = 0,
                    .memory_pointer_right_adjustment = 0, .meta_type = NORMAL_MEMORY_SLOT_META_TYPE
            };
            MemorySlotMeta *new_slot_meta_ptr = new_slot_data - sizeof(MemorySlotMeta);
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
    } else if (next_meta->is_free && next_meta->size + sizeof(MemorySlotMeta) >= growth_bytes
               && next_meta->data - sizeof(*next_meta) == meta->data + meta->size) {
        // can simply grow into the adjacent free space
        consume_next_slot(allocator, meta, growth_bytes);
        allocator->post_alloc_op(allocator);
        return p;
    } else {
        // must relocate the memory
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
}

void virtalloc_pre_op_callback_impl(VirtualAllocator *allocator) {
    assert(allocator->intra_thread_lock_count >= 0);
    if (!allocator->assume_thread_safe_usage)
        lock_virtual_allocator(allocator);
}

void virtalloc_post_op_callback_impl(VirtualAllocator *allocator) {
    assert(allocator->intra_thread_lock_count >= 0);
    if (!allocator->assume_thread_safe_usage)
        unlock_virtual_allocator(allocator);
}

void virtalloc_add_new_memory_impl(VirtualAllocator *allocator, void *p, size_t size) {
    assert(size >= sizeof(MemorySlotMeta) + MIN_ALLOCATION_SIZE);
    allocator->pre_alloc_op(allocator);

    void *slot = p + sizeof(MemorySlotMeta);

    size_t right_adjustment = ALLOCATION_ALIGN - (size_t) slot % ALLOCATION_ALIGN;
    slot += right_adjustment;
    p += right_adjustment;
    size -= right_adjustment;

    MemorySlotMeta *first_meta = NULL;
    MemorySlotMeta *last_meta = NULL;
    if (allocator->first_slot) {
        first_meta = get_meta(allocator, allocator->first_slot, NO_EXPECTATION);
        last_meta = get_meta(allocator, first_meta->prev, NO_EXPECTATION);
    }
    const MemorySlotMeta new_slot_meta_content = {
            .sizeof_meta = sizeof(MemorySlotMeta), .checksum = 0, .size = size - sizeof(MemorySlotMeta),
            .data = slot, .next = first_meta ? first_meta->data : slot, .prev = last_meta ? last_meta->data : slot,
            .next_bigger_free = NULL, .next_smaller_free = NULL, .is_free = 1, .memory_is_owned = 1,
            .memory_pointer_right_adjustment = right_adjustment, .meta_type = NORMAL_MEMORY_SLOT_META_TYPE
    };
    *(MemorySlotMeta *) p = new_slot_meta_content;

    // insert slot into normal linked list
    if (first_meta) {
        assert(last_meta && "unreachable");
        last_meta->next = slot;
        first_meta->prev = slot;
        refresh_checksum_of(allocator, first_meta);
        refresh_checksum_of(allocator, last_meta);
    } else {
        allocator->first_slot = slot;
    }

    coalesce_memory_slots(allocator, p, 0);

    // since owned slots have been added separately, the heap must be scanned at destroy time for those slots and
    // the release callback must be called on them -> enable that behavior
    allocator->release_only_allocator = 0;

    allocator->post_alloc_op(allocator);
}
