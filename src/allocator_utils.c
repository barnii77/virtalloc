#include <stdlib.h>
#include <assert.h>
#include <memory.h>
#include <stddef.h>
#include "virtalloc/allocator_utils.h"
#include "virtalloc/checksum.h"
#include "virtalloc/virtual_allocator.h"
#include "virtalloc/memory_slot_meta.h"
#include "virtalloc/alloc_settings.h"

// TODO if a specific bucket size pattern can be assumed (eg a specific growth factor per bucket), then you may actually
// be able to do better than linear search through the buckets (e.g. instead of linear search, binary search more or
// less by repeated squaring exponentiation that then tells you how much to advance exponentially).
size_t get_bucket_index(const VirtualAllocator *allocator, const size_t size) {
    for (size_t i = 0; i < allocator->num_buckets; i++) {
        if (allocator->bucket_sizes[i] <= size)
            continue;
        return i - 1;
    }
    return allocator->num_buckets - 1;
}

MemorySlotMeta *get_meta(const VirtualAllocator *allocator, void *p, const int should_be_free) {
    MemorySlotMeta *meta = p - sizeof(MemorySlotMeta);
    assert(
        !(allocator->has_checksum && get_checksum(sizeof(*meta) - offsetof(MemorySlotMeta, checksum) - sizeof(meta->
            checksum), (void *) meta + offsetof(MemorySlotMeta, checksum) + sizeof(meta->checksum)) != meta->checksum)
        && "checksum incorrect: you likely passed a pointer to free/realloc that does not correspond to an allocation");
    if (!allocator->enable_safety_checks)
        return meta;
    assert(!(should_be_free != NO_EXPECTATION && !!meta->is_free != should_be_free) && "unexpected allocation status");
    return meta;
}

void coalesce_slot_with_next(VirtualAllocator *allocator, MemorySlotMeta *meta, MemorySlotMeta *next_meta,
                             const int meta_requires_unbind, const int next_meta_requires_unbind,
                             const int out_requires_bind) {
    assert(meta->is_free && next_meta->is_free && "illegal usage");

    if (meta_requires_unbind)
        unbind_from_sorted_free_list(allocator, meta);
    if (next_meta_requires_unbind)
        unbind_from_sorted_free_list(allocator, next_meta);

    // remove from normal linked list
    meta->next = next_meta->next;
    next_meta->next = meta->data;
    // merge
    meta->size += next_meta->size + sizeof(MemorySlotMeta);

    if (out_requires_bind)
        insert_into_sorted_free_list(allocator, meta);

    refresh_checksum_of(allocator, meta);
}

void coalesce_memory_slots(VirtualAllocator *allocator, MemorySlotMeta *meta, const int meta_requires_unbind) {
    assert(meta->is_free && "illegal usage: can only coalesce a slot with it's neighbours if the slot is free");
    MemorySlotMeta *next_meta = get_meta(allocator, meta->next, NO_EXPECTATION);
    MemorySlotMeta *prev_meta = get_meta(allocator, meta->prev, NO_EXPECTATION);

    // can only coalesce with next slot if it is free and forms a contiguous chunk with current slot in memory
    const int coalesce_with_next = next_meta->is_free && next_meta->data - sizeof(*next_meta) == meta->data + meta->
                                   size;
    // can only coalesce with previous slot if it is free and forms a contiguous chunk with current slot in memory
    const int coalesce_with_prev = prev_meta->is_free && meta->data - sizeof(*meta) == prev_meta->data + prev_meta->
                                   size;

    if (coalesce_with_next)
        coalesce_slot_with_next(allocator, meta, next_meta, meta_requires_unbind, 1, !coalesce_with_prev);
    if (coalesce_with_prev)
        coalesce_slot_with_next(allocator, prev_meta, meta, 1, meta_requires_unbind && !coalesce_with_next, 1);

    // if neither of the above branches is reached and the meta passed in is not bound to the sorted free list in the
    // first place, we still need to place it there
    if (!coalesce_with_next && !coalesce_with_prev && !meta_requires_unbind)
        insert_into_sorted_free_list(allocator, meta);
}

void unbind_from_sorted_free_list(VirtualAllocator *allocator, MemorySlotMeta *meta) {
    int is_only_free_slot = 0;
    if (meta->data == meta->next_bigger_free) {
        assert(meta->data == meta->next_smaller_free && "unreachable");
        is_only_free_slot = 1;
    }
    size_t bucket_idx = get_bucket_index(allocator, meta->size);
    // must check buckets with size smaller than meta->size if those refer to meta->next_bigger_free
    while (allocator->bucket_values[bucket_idx] == meta->data) {
        // must replace with next bigger slot
        const MemorySlotMeta *replacement = is_only_free_slot
                                                ? NULL
                                                : get_meta(allocator, meta->next_bigger_free, EXPECT_IS_FREE);
        if (replacement && replacement->size < meta->size)
            // meta is the biggest free slot -> the new biggest will be next_smaller_free (next_bigger_free is smallest)
            replacement = get_meta(allocator, meta->next_smaller_free, EXPECT_IS_FREE);
        allocator->bucket_values[bucket_idx] = replacement ? replacement->data : NULL;
        if (!bucket_idx--)
            break;
    }

    if (is_only_free_slot)
        return;

    // unbind references from sorted free list to consumed slot
    MemorySlotMeta *meta_nbf = get_meta(allocator, meta->next_bigger_free, EXPECT_IS_FREE);
    meta_nbf->next_smaller_free = meta->next_smaller_free;
    // have to interleave the refresh calls with the get_meta in case next_bigger_free and next_smaller_free point to
    // the same thing, in which case the first write invalidates the checksum of the second write
    refresh_checksum_of(allocator, meta_nbf);
    MemorySlotMeta *meta_nsf = get_meta(allocator, meta->next_smaller_free, EXPECT_IS_FREE);
    meta_nsf->next_bigger_free = meta->next_bigger_free;
    refresh_checksum_of(allocator, meta_nsf);
}

void insert_into_sorted_free_list(VirtualAllocator *allocator, MemorySlotMeta *meta) {
    assert(meta->is_free && "illegal usage");
    size_t bucket_idx = get_bucket_index(allocator, meta->size);
    void *bucket_value = allocator->bucket_values[bucket_idx];
    MemorySlotMeta *next_meta = NULL;
    MemorySlotMeta *prev_meta = NULL;
    MemorySlotMeta *first_in_bucket = bucket_value ? get_meta(allocator, bucket_value, EXPECT_IS_FREE) : NULL;
    if (!bucket_value) {
        // next bigger one links to the smallest entry to make the sorted linked list circular
        void *smallest_entry = allocator->bucket_values[0];
        if (smallest_entry)
            next_meta = get_meta(allocator, smallest_entry, EXPECT_IS_FREE);
        else
            next_meta = meta;
        // find the next smaller one
        ssize_t non_empty_bucket_idx = -1;
        const size_t starting_bucket_index = bucket_idx--;
        while (bucket_idx != starting_bucket_index) {
            void *entry = allocator->bucket_values[bucket_idx];
            if (entry) {
                MemorySlotMeta *ent_meta = get_meta(allocator, entry, EXPECT_IS_FREE);
                while (ent_meta->next_bigger_free != meta->next_bigger_free)
                    ent_meta = get_meta(allocator, ent_meta->next_bigger_free, EXPECT_IS_FREE);
                prev_meta = get_meta(allocator, ent_meta->data, EXPECT_IS_FREE);
                non_empty_bucket_idx = bucket_idx;
                break;
            }
            bucket_idx = (bucket_idx - 1) % allocator->num_buckets;
        }
        bucket_idx = starting_bucket_index;
        if (non_empty_bucket_idx < 0) {
            // no entry in even a single bucket, will just write myself into them
            meta->next_bigger_free = meta->data;
            meta->next_smaller_free = meta->data;
            refresh_checksum_of(allocator, meta);
            for (size_t bi = 0; bi < allocator->num_buckets; bi++)
                if (allocator->bucket_sizes[bi] <= meta->size)
                    allocator->bucket_values[bi] = meta->data;
            return;
        }
    } else {
        next_meta = first_in_bucket;
        // find the smallest heap alloc that is bigger
        while (next_meta->size < meta->size) {
            MemorySlotMeta *next_bigger = get_meta(allocator, next_meta->next_bigger_free, EXPECT_IS_FREE);
            if (next_bigger->size <= next_meta->size)
                break;
            next_meta = next_bigger;
        }
        prev_meta = get_meta(allocator, next_meta->next_smaller_free, EXPECT_IS_FREE);
    }

    assert(next_meta && prev_meta && "unreachable");

    // insert into sorted linked list
    meta->next_bigger_free = next_meta->data;
    meta->next_smaller_free = prev_meta->data;
    next_meta->next_smaller_free = meta->data;
    prev_meta->next_bigger_free = meta->data;

    refresh_checksum_of(allocator, prev_meta);
    refresh_checksum_of(allocator, meta);
    refresh_checksum_of(allocator, next_meta);

    // fill any buckets this size matches that point to NULL
    if (!first_in_bucket) {
        int no_nulls = 1;
        for (size_t bi = 0; bi < allocator->num_buckets; bi++) {
            if (allocator->bucket_sizes[bi] <= meta->size && !allocator->bucket_values[bi]) {
                allocator->bucket_values[bi] = meta->data;
                no_nulls = 0;
            } else {
                assert(no_nulls && "unreachable");
            }
        }
    }
    // must check buckets with size smaller than meta->size if those refer to meta->next_bigger_free
    while (!first_in_bucket || first_in_bucket == next_meta) {
        allocator->bucket_values[bucket_idx] = meta->data;
        if (!bucket_idx--)
            break;
        void *ent = allocator->bucket_values[bucket_idx];
        first_in_bucket = ent ? get_meta(allocator, ent, EXPECT_IS_FREE) : NULL;
    }
}

void refresh_checksum_of(VirtualAllocator *allocator, MemorySlotMeta *meta) {
    if (allocator->has_checksum)
        meta->checksum = get_checksum(sizeof(*meta) - offsetof(MemorySlotMeta, checksum) - sizeof(meta->checksum),
                                      (void *) meta + offsetof(MemorySlotMeta, checksum) + sizeof(meta->checksum));
}

/// grow an allocated slot into a free slot to the right
void consume_next_slot(VirtualAllocator *allocator, MemorySlotMeta *meta, size_t moved_bytes) {
    assert(!meta->is_free && "only works for allocated slots trying to grow into their free neighbour to the right");
    MemorySlotMeta *next_meta = get_meta(allocator, meta->next, EXPECT_IS_FREE);
    const ssize_t remaining_size = (ssize_t) (next_meta->size + sizeof(MemorySlotMeta)) - (ssize_t) moved_bytes;
    assert(remaining_size >= 0 && "cannot join: block to join with too small");
    assert(
        next_meta->data - sizeof(*next_meta) == meta->data + meta->size &&
        "cannot coalesce with slot that is not a contiguous neighbour");

    if (remaining_size < sizeof(MemorySlotMeta) + MIN_ALLOCATION_SIZE) {
        // next slot would become too small, must be consumed completely
        unbind_from_sorted_free_list(allocator, next_meta);

        moved_bytes += remaining_size;

        // unbind references to consumed block in normal linked list
        meta->next = next_meta->next;
        MemorySlotMeta *next_next_meta = get_meta(allocator, next_meta->next, NO_EXPECTATION);
        next_next_meta->prev = meta->data;
        // adjust size
        meta->size += moved_bytes;

        refresh_checksum_of(allocator, meta);
        refresh_checksum_of(allocator, next_next_meta);
    } else {
        // reduce next slot size, increase own size
        unbind_from_sorted_free_list(allocator, next_meta);

        // move the metadata of the free slot to the right
        memmove((void *) next_meta + moved_bytes, next_meta, sizeof(*next_meta));
        next_meta = (MemorySlotMeta *) ((void *) next_meta + moved_bytes);
        // adjust sizes
        next_meta->size -= moved_bytes;
        next_meta->data += moved_bytes;
        refresh_checksum_of(allocator, next_meta);

        meta->next += moved_bytes;
        refresh_checksum_of(allocator, meta);

        MemorySlotMeta *next_next_meta = get_meta(allocator, next_meta->next, NO_EXPECTATION);
        next_next_meta->prev += moved_bytes;
        refresh_checksum_of(allocator, next_next_meta);

        // insert the free slot to the right back into the sorted free list, just at the now appropriate location
        insert_into_sorted_free_list(allocator, next_meta);
    }
}

/// grow a free slot into an allocated slot to the left (opposite of consume_next_slot)
void consume_prev_slot(VirtualAllocator *allocator, MemorySlotMeta *meta, size_t moved_bytes) {
    assert(meta->is_free && "only works for free slots trying to grow into their allocated neighbour to the right");
    MemorySlotMeta *prev_meta = get_meta(allocator, meta->prev, EXPECT_IS_ALLOCATED);
    const ssize_t remaining_size = (ssize_t) (prev_meta->size + sizeof(MemorySlotMeta)) - (ssize_t) moved_bytes;
    assert(remaining_size >= 0 && "cannot join: block to join with too small");
    assert(
        meta->data - sizeof(*meta) == prev_meta->data + prev_meta->size &&
        "cannot coalesce with slot that is not a contiguous neighbour");

    if (remaining_size < sizeof(MemorySlotMeta) + MIN_ALLOCATION_SIZE) {
        unbind_from_sorted_free_list(allocator, meta);

        // next slot too small, must be consumed completely
        moved_bytes += remaining_size;

        // move the metadata of the free slot to the left
        memmove((void *) meta - moved_bytes, meta, sizeof(*meta));
        meta = (MemorySlotMeta *) ((void *) meta - moved_bytes);
        // adjust size and data ptr
        meta->size += moved_bytes;
        meta->data -= moved_bytes;
        // unbind references to consumed block in normal linked list
        meta->prev = prev_meta->prev;
        MemorySlotMeta *prev_prev_meta = get_meta(allocator, prev_meta->prev, NO_EXPECTATION);
        prev_prev_meta->next = meta->data;

        refresh_checksum_of(allocator, meta);
        refresh_checksum_of(allocator, prev_prev_meta);
    } else {
        unbind_from_sorted_free_list(allocator, meta);

        // move the metadata of the free slot to the right
        memmove((void *) meta - moved_bytes, meta, sizeof(*meta));
        meta = (MemorySlotMeta *) ((void *) meta - moved_bytes);
        // adjust sizes
        meta->size += moved_bytes;
        meta->data -= moved_bytes;
        prev_meta->size -= moved_bytes;
        prev_meta->next -= moved_bytes;

        // refresh all checksums
        refresh_checksum_of(allocator, prev_meta);
        refresh_checksum_of(allocator, meta);

        // insert the free slot to the right back into the sorted free list, just at the now appropriate location
        insert_into_sorted_free_list(allocator, meta);
    }
}

size_t align_to(const size_t size, const size_t align) {
    return (size * align + align - 1) / align;
}
