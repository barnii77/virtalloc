#include <stdlib.h>
#include <stddef.h>
#include <memory.h>
#include <assert.h>
#include "virtalloc.h"
#include "virtalloc/allocator.h"
#include "virtalloc/memory_slot_meta.h"
#include "virtalloc/virtual_allocator.h"
#include "virtalloc/alloc_settings.h"
#include "virtalloc/allocator_utils.h"

vap_t new_virtual_allocator_from_impl(size_t size, char memory[static size], const int flags,
                                      const int memory_is_owned) {
    const size_t num_buckets = flags & VIRTALLOC_FLAG_VA_FEW_BUCKETS ? NUM_BUCKETS_FEW_BUCKET_MODE : NUM_BUCKETS;
    const double bucket_growth_factor = flags & VIRTALLOC_FLAG_VA_FEW_BUCKETS
                                        ? BUCKET_SIZE_GROWTH_FACTOR_FEW_BUCKET_MODE
                                        : BUCKET_SIZE_GROWTH_FACTOR;

    size_t right_adjustment = ALLOCATION_ALIGN - (size_t) memory % ALLOCATION_ALIGN;
    memory += right_adjustment;
    size -= right_adjustment;

    if (size < sizeof(VirtualAllocator) + num_buckets * sizeof(size_t) + num_buckets * sizeof(void *))
        return NULL;
    ThreadLock tl;
    init_lock(&tl);
    VirtualAllocator va = {
        .lock = tl, .self = (VirtualAllocator *) memory, .first_slot = NULL, .num_buckets = num_buckets,
        .bucket_sizes = NULL, .bucket_values = NULL, .malloc = virtalloc_malloc_impl, .free = virtalloc_free_impl,
        .realloc = virtalloc_realloc_impl, .add_new_memory = virtalloc_add_new_memory_impl, .release_memory = NULL,
        .pre_alloc_op_callback = virtalloc_pre_op_callback_impl,
        .post_alloc_op_callback = virtalloc_post_op_callback_impl, .intra_thread_lock_count = 0,
        .memory_pointer_right_adjustment = right_adjustment,
        .has_checksum = (flags & VIRTALLOC_FLAG_VA_HAS_CHECKSUM) != 0,
        .enable_safety_checks = (flags & VIRTALLOC_FLAG_VA_HAS_NON_CHECKSUM_SAFETY_CHECKS) != 0,
        .memory_is_owned = memory_is_owned, .release_only_allocator = 1, .assume_thread_safe_usage = 0,
    };
    va.bucket_sizes = (size_t *) &memory[sizeof(VirtualAllocator)];
    va.bucket_values = (void **) &memory[sizeof(VirtualAllocator) + va.num_buckets * sizeof(size_t)];
    va.first_slot = memory + sizeof(VirtualAllocator) + va.num_buckets * sizeof(size_t)
                    + va.num_buckets * sizeof(void *) + sizeof(MemorySlotMeta);
    const size_t remaining_slot_size = (void *) memory + size - va.first_slot;
    if (remaining_slot_size < MIN_ALLOCATION_SIZE)
        va.first_slot = NULL;
    *(VirtualAllocator *) memory = va;

    // initialize bucket sizes and values
    double current_bucket_size = MIN_ALLOCATION_SIZE;
    for (size_t i = 0; i < va.num_buckets; i++) {
        va.bucket_sizes[i] = (size_t) current_bucket_size;
        current_bucket_size *= bucket_growth_factor;
    }
    memset(va.bucket_values, 0, va.num_buckets * sizeof(void *));

    // if the remaining memory can be used for a memory slot, initialize it
    if (va.first_slot) {
        MemorySlotMeta *first_slot_meta_ptr = va.first_slot - sizeof(MemorySlotMeta);
        const MemorySlotMeta first_slot_meta_content = {
            .sizeof_meta = sizeof(MemorySlotMeta), .checksum = 0, .size = remaining_slot_size, .data = va.first_slot,
            .next = va.first_slot, .prev = va.first_slot, .next_bigger_free = va.first_slot,
            .next_smaller_free = va.first_slot, .is_free = 1, .memory_is_owned = 0,
            .memory_pointer_right_adjustment = 0, .meta_type = NORMAL_MEMORY_SLOT_META_TYPE
        };
        *first_slot_meta_ptr = first_slot_meta_content;
        refresh_checksum_of(&va, first_slot_meta_ptr);

        for (size_t bi = 0; bi < va.num_buckets; bi++)
            if (va.bucket_sizes[bi] <= remaining_slot_size)
                va.bucket_values[bi] = va.first_slot;
    }

    return memory;
}

vap_t virtalloc_new_virtual_allocator_from(const size_t size, char memory[static size], const int flags) {
    return new_virtual_allocator_from_impl(size, memory, flags, 0);
}

vap_t virtalloc_new_virtual_allocator(const size_t size, const int flags) {
    if (size < sizeof(VirtualAllocator))
        return NULL;
    char *memory = malloc(size + ALLOCATION_ALIGN - 1);
    if (!memory)
        return NULL;
    vap_t alloc = new_virtual_allocator_from_impl(size, memory, flags, 1);
    virtalloc_set_release_mechanism(alloc, free);
    return alloc;
}

void virtalloc_destroy_virtual_allocator(vap_t allocator) {
    VirtualAllocator *alloc = allocator;
    lock_virtual_allocator(alloc);
    if (alloc->release_memory) {
        if (alloc->memory_is_owned)
            alloc->release_memory(allocator - alloc->memory_pointer_right_adjustment);
        if (!alloc->release_only_allocator) {
            int is_first_iter = 1;
            void *starting_slot = alloc->first_slot;
            MemorySlotMeta *meta = get_meta(alloc, starting_slot, NO_EXPECTATION);
            while (meta->data != starting_slot || is_first_iter) {
                if (meta->memory_is_owned)
                    alloc->release_memory((void *) meta - meta->memory_pointer_right_adjustment);
                is_first_iter = 0;
                assert(meta->next && "encountered NULL where it should never happen");
                meta = get_meta(allocator, meta->next, NO_EXPECTATION);
            }
        }
    }
    unlock_virtual_allocator(alloc);
    destroy_lock(&alloc->lock);
}

void *virtalloc_realloc(vap_t allocator, void *p, const size_t size) {
    VirtualAllocator *alloc = allocator;
    return alloc->realloc(alloc, p, size);
}

void virtalloc_free(vap_t allocator, void *p) {
    VirtualAllocator *alloc = allocator;
    alloc->free(alloc, p);
}

void *virtalloc_malloc(vap_t allocator, const size_t size) {
    VirtualAllocator *alloc = allocator;
    return alloc->malloc(alloc, size);
}

void virtalloc_add_new_memory(vap_t allocator, void *memory, const size_t size) {
    VirtualAllocator *alloc = allocator;
    alloc->add_new_memory(alloc, memory, size);
}

void virtalloc_set_release_mechanism(vap_t allocator, void (*release_memory)(void *p)) {
    VirtualAllocator *alloc = allocator;
    alloc->release_memory = release_memory;
}

void virtalloc_unset_release_mechanism(vap_t allocator) {
    VirtualAllocator *alloc = allocator;
    alloc->release_memory = NULL;
}
