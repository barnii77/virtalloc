#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stddef.h>
#include "virtalloc/cross_platform_lock.h"
#include "virtalloc/allocator_settings.h"

/// Small allocation Round Robin Allocator. In practice, this is used for small allocations (size < 64 bytes)
typedef struct SmallRRAllocator {
    /// how many potential slots may at most be checked before an OOM (out of memory -> request more) is triggered
    size_t max_slot_checks_before_oom;
    /// the first slot (must point to a slot)
    void *first_slot;
    /// the last slot (must point to a link)
    void *last_slot;
    /// the last slot that was converted from free to allocated
    void *rr_slot;
} SmallRRAllocator;

/// General Purpose Allocator: the main allocator used by default. In practice, it is used for medium and large
/// allocations (size >= 64 bytes). It maintains a sorted free list with a bucket mechanism to massively reduce the
/// amount of searched slots. The allocator is thread safe.
typedef struct GeneralPurposeAllocator {
    /// how many potential slots may at most be checked before an OOM (out of memory -> request more) is triggered
    size_t max_slot_checks_before_oom;
    /// a linked list connecting one slot to the previous and next one
    void *first_slot;
    /// number of buckets in bucket_sizes/bucket_pointers
    size_t num_buckets;
    /// at this size or greater, a slot will be released early and not re-used to save resources
    size_t min_size_for_early_release;
    /// buckets that slice into the linked list of free slots sorted from smallest to biggest by slot size
    size_t *bucket_sizes;
    /// the smallest free slot that falls into a given bucket category. size is num_buckets.
    void **bucket_values;
} GeneralPurposeAllocator;

/// the internal per-allocator data stored in the first sizeof(VA) bytes of the heap
typedef struct Allocator {
    /// the main allocator that is used by default
    GeneralPurposeAllocator gpa;
    /// a special purpose allocator for small allocations (size < MIN_LARGE_ALLOCATION_SIZE)
    SmallRRAllocator sma;
    /// lock for multithreaded allocators
    ThreadLock lock;

    /// allocation function
    void *(*malloc)(struct Allocator *allocator, size_t size, int is_retry_run);

    /// free function
    void (*free)(struct Allocator *allocator, void *p);

    /// reallocation function
    void *(*realloc)(struct Allocator *allocator, void *p, size_t size);

    /// the function used to give the general purpose allocator new memory it can use (assumed to be free initially)
    void (*gpa_add_new_memory)(struct Allocator *allocator, void *p, size_t size);

    /// the function used to give the round-robin small allocator new memory it can use (assumed to be free initially)
    void (*sma_add_new_memory)(struct Allocator *allocator, void *p, size_t size, int must_free_later);

    /// callback used when the VA is released: it is called on each owned memory slot (can be `free` for example)
    void (*release_memory)(void *p);

    /// called when the allocator OOMs to request more memory. May return NULL. If it returns a non-null pointer, the
    /// first 8 bytes at that address must be set to the size of the memory granted to the allocator. This will always
    /// be legal as it is guaranteed that the minimum size that will ever be requested is 8. Those 8 bytes are only a
    /// temporary size storage for simple information transfer to the allocator and the allocator may wipe that
    /// information at any point.
    void *(*request_new_memory)(size_t min_size);

    /// a callback invoked *before* an allocator operation (one of the callbacks defined below)
    void (*pre_alloc_op)(struct Allocator *allocator);

    /// a callback invoked *after* an allocator operation (one of the callbacks defined below)
    void (*post_alloc_op)(struct Allocator *allocator);

    /// decides how many bytes of padding should be added after an allocated slot to make the allocator more robust
    /// against off-by-1 errors and similar user-made bugs that may interfere with heap metadata. The way this is
    /// implemented is really simple - you literally just add this number times ALIGN to the size of every allocation.
    size_t (*get_gpa_padding_lines)(size_t allocation_size);

    /// the number of times the allocator has been locked. The global lock will be released when this count reaches 0.
    /// Note that this counter is only used within a thread to keep track of the lock/unlock counts and is thread safe
    /// despite not being atomic because it is only modified in code passages where the allocator is locked already.
    int intra_thread_lock_count;
    /// how many get_meta calls to do before get_meta checks the checksum once
    int steps_per_checksum_check;
    /// how many bytes the data pointer has been right adjusted to match the alignment requirements
    unsigned char memory_pointer_right_adjustment;
    /// whether the allocator should compute the checksum for the metadata
    unsigned has_checksum: 1;
    /// whether to enable some basic safety checks or not
    unsigned enable_safety_checks: 1;
    /// set only if the VA's underlying memory is owned (used in free_virtual_allocator for user foot gun protection)
    unsigned memory_is_owned: 1;
    /// if set, when destroying a VA, the release will not traverse the entire heap and instead only call
    /// allocator->release_memory on the allocator itself
    unsigned release_only_allocator: 1;
    /// may be set if the user guarantees thread safe usage of the allocator to remove the global allocator lock
    unsigned assume_thread_safe_usage: 1;
    /// if set, disables the round-robin allocator for small allocations
    unsigned no_rr_allocator: 1;
    /// temporarily disables the most verbose of logging, e.g. if a function for logging makes a bunch of calls to
    /// another function that would do logging every time as well
    unsigned block_logging: 1;
    /// if set, the SMA requests memory using the GPA instead of making a malloc call itself directly
    unsigned sma_request_mem_from_gpa: 1;
    /// if set, disable bucket mechanism completely
    unsigned disable_bucket_mechanism: 1;
} __attribute__((aligned(LARGE_ALLOCATION_ALIGN))) Allocator;

void lock_virtual_allocator(Allocator *allocator);

void unlock_virtual_allocator(Allocator *allocator);

#endif
