#ifndef VIRTUAL_ALLOCATOR_H
#define VIRTUAL_ALLOCATOR_H

#include <stddef.h>
#include "virtalloc/cross_platform_lock.h"
#include "virtalloc/alloc_settings.h"

// TODO support lazy memory claiming: allow a parameter that tells the VA that size of buffer is currently X bytes and
// to just realloc when more data is needed for further allocations.
// TODO if a specific bucket size pattern can be assumed (eg a specific growth factor per bucket), then you may actually
// be able to do better than linear search through the buckets (e.g. instead of linear search, binary search more or
// less by repeated squaring exponentiation that then tells you how much to advance exponentially).
// TODO find a good minimum allocation size (eg 16 bytes of usable space)
// TODO add an alignment mechanism for size categories. Eg 16-64 bytes -> align 16 byte, >= 64 byte -> align 64 byte
// TODO small alloc optimization: the metadata struct is way too big (64 bytes) for small allocations, so I have to
// make it way smaller, more like 8 bytes

/// the internal per-allocator data stored in the first sizeof(VA) bytes of the heap
typedef struct VirtualAllocator {
    /// lock for multi-threaded allocators
    ThreadLock lock;
    /// size of the underlying memory region including the bytes taken up by this struct right here.
    size_t memory_size;
    /// array of [length] bytes. In the first sizeof(VirtualAllocator) bytes of memory, this struct itself is stored,
    /// meaning this type is self-referential.
    char *memory;
    /// a linked list connecting one slot to the previous and next one
    void *first_slot;
    /// number of buckets in bucket_sizes/bucket_pointers
    size_t num_buckets;
    /// buckets that slice into the linked list of free slots sorted from smallest to biggest by slot size
    size_t *bucket_sizes;
    /// the smallest free slot that falls into a given bucket category. size is num_buckets.
    void **bucket_values;

    /// allocation function
    void *(*malloc)(struct VirtualAllocator *allocator, size_t size, size_t max_backward_exploration_steps);

    /// reallocation function
    void *(*realloc)(struct VirtualAllocator *allocator, void *p, size_t size, size_t max_backward_exploration_steps);

    /// free function
    void (*free)(struct VirtualAllocator *allocator, void *p);

    /// a callback invoked *before* an allocator operation (one of the callbacks defined below)
    void (*pre_alloc_op_callback)(struct VirtualAllocator *allocator);

    /// a callback invoked *after* an allocator operation (one of the callbacks defined below)
    void (*post_alloc_op_callback)(struct VirtualAllocator *allocator);

    /// the number of times the allocator has been locked. The global lock will be released when this count reaches 0.
    /// Note that this counter is only used within a thread to keep track of the lock/unlock counts and is thread safe
    /// despite not being atomic because it is only modified in code passages where the allocator is locked already.
    int intra_thread_lock_count;
    /// whether the allocator should compute the checksum for the metadata
    unsigned has_checksum: 1;
    /// whether to enable some basic safety checks or not
    unsigned enable_safety_checks: 1;
    /// set only if the VA's underlying memory is owned (used in free_virtual_allocator for user foot gun protection)
    unsigned memory_is_owned: 1;
    /// may be set if the user guarantees thread safe usage of the allocator to remove the global allocator lock
    unsigned assume_thread_safe_usage: 1;
} VirtualAllocator __attribute__((aligned(ALLOCATION_ALIGN)));

void lock_virtual_allocator(VirtualAllocator *allocator);

void unlock_virtual_allocator(VirtualAllocator *allocator);

#endif
