#include <stdlib.h>
#include <stddef.h>
#include <memory.h>
#include <assert.h>
#include <stdio.h>
#include "virtalloc.h"
#include "virtalloc/allocator_impl.h"
#include "virtalloc/gp_memory_slot_meta.h"
#include "virtalloc/allocator.h"
#include "virtalloc/allocator_settings.h"
#include "virtalloc/allocator_utils.h"
#include "virtalloc/small_rr_memory_slot_meta.h"
#include "virtalloc/helper_macros.h"

static size_t get_padding_lines_impl(const size_t allocation_size) {
    if (allocation_size < MIN_SIZE_FOR_SAFETY_PADDING)
        return 0;
    return 1;
}

// TODO I should temporarily go back to the previous version with exponential bucket gaps to profile that again to see where the hot loops were in that case and
// where they are now.
// FIXME I must have screwed something up very deeply because my optimization that should be a 10x speedup has made things slower... oh is this going to be annoying

static size_t get_min_size_for_early_release_from_flags(const int flags) {
    return flags & VIRTALLOC_FLAG_VA_KEEP_SIZE_TINY
               ? EARLY_RELEASE_SIZE_TINY
               : flags & VIRTALLOC_FLAG_VA_KEEP_SIZE_SMALL
                     ? EARLY_RELEASE_SIZE_SMALL
                     : flags & VIRTALLOC_FLAG_VA_KEEP_SIZE_LARGE
                           ? EARLY_RELEASE_SIZE_LARGE
                           : EARLY_RELEASE_SIZE_NORMAL;
}

static vap_t new_virtual_allocator_from_impl(size_t size, char memory[static size], const int flags,
                                             const int memory_is_owned) {
    const size_t min_size_for_early_release = get_min_size_for_early_release_from_flags(flags);
    const size_t num_buckets = min_size_for_early_release / LARGE_ALLOCATION_ALIGN;

    const size_t right_adjustment = (LARGE_ALLOCATION_ALIGN - (size_t) memory % LARGE_ALLOCATION_ALIGN) %
                                    LARGE_ALLOCATION_ALIGN;
    memory += right_adjustment;
    size -= right_adjustment;

    if (size < sizeof(Allocator) + num_buckets * sizeof(size_t) + num_buckets * sizeof(void *))
        return NULL;
    ThreadLock tl;
    init_lock(&tl);
    int disable_buckets = (flags & VIRTALLOC_FLAG_VA_DISABLE_BUCKETS) != 0;
    Allocator va = {
        .lock = tl,
        .gpa = {
            .max_slot_checks_before_oom = (size_t) -1, .first_slot = NULL,
            .num_buckets = disable_buckets ? 1 : num_buckets, .min_size_for_early_release = min_size_for_early_release,
            .bucket_sizes = NULL, .bucket_values = NULL
        },
        .sma = {
            .max_slot_checks_before_oom = (size_t) DEFAULT_EXPLORATION_STEPS_BEFORE_RR_OOM, .first_slot = NULL,
            .last_slot = NULL, .rr_slot = NULL
        },
        .malloc = virtalloc_malloc_impl, .free = virtalloc_free_impl, .realloc = virtalloc_realloc_impl,
        .gpa_add_new_memory = virtalloc_gpa_add_new_memory_impl,
        .sma_add_new_memory = virtalloc_sma_add_new_memory_impl, .release_memory = NULL, .request_new_memory = NULL,
        .pre_alloc_op = virtalloc_pre_op_callback_impl, .post_alloc_op = virtalloc_post_op_callback_impl,
        .intra_thread_lock_count = 0,
        .steps_per_checksum_check = flags & VIRTALLOC_FLAG_VA_DENSE_CHECKSUM_CHECKS ? 1 : STEPS_PER_CHECKSUM_CHECK,
        .memory_pointer_right_adjustment = right_adjustment,
        .get_gpa_padding_lines = flags & VIRTALLOC_FLAG_VA_HAS_SAFETY_PADDING_LINE ? get_padding_lines_impl : NULL,
        .has_checksum = (flags & VIRTALLOC_FLAG_VA_HAS_CHECKSUM) != 0,
        .enable_safety_checks = (flags & VIRTALLOC_FLAG_VA_HAS_NON_CHECKSUM_SAFETY_CHECKS) != 0,
        .memory_is_owned = memory_is_owned, .release_only_allocator = 1, .assume_thread_safe_usage = 0,
        .no_rr_allocator = (flags & VIRTALLOC_FLAG_VA_NO_RR_ALLOCATOR) != 0, .block_logging = 0,
        .sma_request_mem_from_gpa = (flags & VIRTALLOC_FLAG_VA_SMA_REQUEST_MEM_FROM_GPA) != 0,
        .disable_bucket_mechanism = disable_buckets
    };
    va.gpa.bucket_sizes = (size_t *) &memory[sizeof(Allocator)];
    va.gpa.bucket_values = (void **) &memory[sizeof(Allocator) + va.gpa.num_buckets * sizeof(size_t)];
    va.gpa.first_slot = memory + sizeof(Allocator) + va.gpa.num_buckets * sizeof(size_t)
                        + va.gpa.num_buckets * sizeof(void *) + sizeof(GPMemorySlotMeta);
    const size_t remaining_slot_size = (void *) memory + size - va.gpa.first_slot;
    if (remaining_slot_size < MIN_LARGE_ALLOCATION_SIZE)
        va.gpa.first_slot = NULL;
    *(Allocator *) memory = va;

    // initialize bucket sizes
    for (size_t i = 0; i < va.gpa.num_buckets; i++)
        // this initializes them linearly with a step size of ALIGN which should lead to O(1) malloc/free
        va.gpa.bucket_sizes[i] = MIN_LARGE_ALLOCATION_SIZE + i * LARGE_ALLOCATION_ALIGN;

    // another approach for initializing:
    // double current_bucket_size = MIN_LARGE_ALLOCATION_SIZE;
    // double bucket_growth_factor = /* magic constant like 1.1 here */;
    // for (size_t i = 0; i < va.gpa.num_buckets; i++) {
    //     va.gpa.bucket_sizes[i] = (size_t) current_bucket_size;
    //     current_bucket_size *= bucket_growth_factor;
    // }

    // initialize bucket values
    memset(va.gpa.bucket_values, 0, va.gpa.num_buckets * sizeof(void *));

    // if the remaining memory can be used for a memory slot, initialize it
    if (va.gpa.first_slot) {
        GPMemorySlotMeta *first_slot_meta_ptr = va.gpa.first_slot - sizeof(GPMemorySlotMeta);
        const GPMemorySlotMeta first_slot_meta_content = {
            .checksum = 0, .size = remaining_slot_size, .data = va.gpa.first_slot, .next = va.gpa.first_slot,
            .prev = va.gpa.first_slot, .next_bigger_free = NULL, .next_smaller_free = NULL, .time_to_checksum_check = 0,
            .memory_pointer_right_adjustment = 0, .is_free = 1, .memory_is_owned = 0, .__bit_padding1 = 0,
            .__padding = {0}, .__bit_padding2 = 0, .meta_type = GP_META_TYPE_SLOT
        };
        *first_slot_meta_ptr = first_slot_meta_content;
        insert_into_sorted_free_list((Allocator *) memory, first_slot_meta_ptr);
    }

    return memory;
}

vap_t virtalloc_new_allocator_in(const size_t size, char memory[static size], const int flags) {
    return new_virtual_allocator_from_impl(size, memory, flags, 0);
}

vap_t virtalloc_new_allocator(size_t size, const int flags) {
    const size_t min_size_for_early_release = get_min_size_for_early_release_from_flags(flags);
    const size_t num_buckets = min_size_for_early_release / LARGE_ALLOCATION_ALIGN;

    size += sizeof(Allocator) + num_buckets * sizeof(size_t) + num_buckets * sizeof(void *) + LARGE_ALLOCATION_ALIGN;
    char *memory = malloc(size);
    if (!memory)
        return NULL;

    vap_t alloc = new_virtual_allocator_from_impl(size, memory, flags, 1);
    if (!alloc)
        return NULL;
    virtalloc_set_release_mechanism(alloc, free);
    return alloc;
}

void virtalloc_destroy_allocator(vap_t allocator) {
    Allocator *alloc = allocator;
    lock_virtual_allocator(alloc);

    if (!alloc->release_memory || alloc->release_only_allocator)
        goto finalize;

    // release the general purpose allocators memory
    int is_first_iter = 1;
    void *starting_slot = alloc->gpa.first_slot;
    if (starting_slot) {
        GPMemorySlotMeta *gpa_meta = get_meta(alloc, starting_slot, NO_EXPECTATION);
        void *next_to_dealloc = NULL;
        while (gpa_meta->data != starting_slot || is_first_iter) {
            is_first_iter = 0;
            if (gpa_meta->memory_is_owned) {
                if (next_to_dealloc)
                    alloc->release_memory(next_to_dealloc);
                next_to_dealloc = (void *) gpa_meta - gpa_meta->memory_pointer_right_adjustment;
            }
            assert_internal(gpa_meta->next && "encountered NULL where it should never happen");
            gpa_meta = get_meta(allocator, gpa_meta->next, NO_EXPECTATION);
        }
        if (next_to_dealloc)
            alloc->release_memory(next_to_dealloc);
    }

    if (alloc->no_rr_allocator)
        goto finalize;

    // release the RR allocator's memory
    is_first_iter = 1;
    starting_slot = alloc->sma.first_slot;
    if (starting_slot) {
        void *slot = starting_slot;
        void *next_to_dealloc = NULL;
        while (slot != starting_slot || is_first_iter) {
            is_first_iter = 0;
            const SmallRRMemorySlotMeta *meta = slot - sizeof(SmallRRMemorySlotMeta);
            if (meta->meta_type == RR_META_TYPE_LINK) {
                void *next_slot = *(void **) slot;
                if (next_to_dealloc)
                    alloc->release_memory(next_to_dealloc);
                const SmallRRStartOfMemoryChunkMeta *mcm =
                        next_slot - sizeof(SmallRRMemorySlotMeta) - sizeof(SmallRRStartOfMemoryChunkMeta);
                next_to_dealloc = mcm->must_release_chunk_on_destroy
                                      ? *(void **) &mcm->memory_chunk_ptr_raw_bytes
                                      : NULL;
                slot = next_slot;
            } else {
                assert_internal(meta->meta_type == RR_META_TYPE_SLOT && "unreachable");
                slot = get_next_rr_slot(alloc, slot);
            }
        }
        if (next_to_dealloc)
            alloc->release_memory(next_to_dealloc);
    }

finalize:
    unlock_virtual_allocator(alloc);
    destroy_lock(&alloc->lock);
    if (alloc->release_memory && alloc->memory_is_owned)
        alloc->release_memory(allocator - alloc->memory_pointer_right_adjustment);
}

void *virtalloc_realloc(vap_t allocator, void *p, const size_t size) {
    Allocator *alloc = allocator;
    return alloc->realloc(alloc, p, size);
}

void virtalloc_free(vap_t allocator, void *p) {
    Allocator *alloc = allocator;
    alloc->free(alloc, p);
}

void *virtalloc_malloc(vap_t allocator, const size_t size) {
    Allocator *alloc = allocator;
    return alloc->malloc(alloc, size, 0);
}

void virtalloc_set_release_mechanism(vap_t allocator, void (*release_memory)(void *p)) {
    Allocator *alloc = allocator;
    alloc->release_memory = release_memory;
}

void virtalloc_unset_release_mechanism(vap_t allocator) {
    Allocator *alloc = allocator;
    alloc->release_memory = NULL;
}

void virtalloc_set_request_mechanism(vap_t allocator, void *(*request_new_memory)(size_t min_size)) {
    Allocator *alloc = allocator;
    alloc->request_new_memory = request_new_memory;
}

void virtalloc_unset_request_mechanism(vap_t allocator) {
    Allocator *alloc = allocator;
    alloc->request_new_memory = NULL;
}

void virtalloc_set_max_gpa_slot_checks_before_oom(vap_t allocator, const size_t max_slot_checks) {
    Allocator *alloc = allocator;
    alloc->gpa.max_slot_checks_before_oom = max_slot_checks;
}

void virtalloc_set_max_sma_slot_checks_before_oom(vap_t allocator, const size_t max_slot_checks) {
    Allocator *alloc = allocator;
    alloc->sma.max_slot_checks_before_oom = max_slot_checks;
}

void virtalloc_dump_allocator_metadata_to_file(FILE *file, vap_t allocator) {
    virtalloc_dump_allocator_to_file_impl(file, allocator);
}
