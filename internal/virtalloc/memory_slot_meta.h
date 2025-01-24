#ifndef MEMORY_SLOT_H
#define MEMORY_SLOT_H

#include "virtalloc/alloc_settings.h"

// TODO small meta optimization: create a second metadata struct that is as small as possible and has the same initial
// layout but a different meta_type
// idea for that: dynamically sized meta. The meta type determines how many bytes the meta takes up.

#define NORMAL_MEMORY_SLOT_META_TYPE 1
#define TINY_MEMORY_SLOT_META_TYPE 2

/// the metadata in front of a heap slot
typedef struct MemorySlotMeta {
    /// tells the allocator what type of metadata this is
    int meta_type;
    /// a checksum that can help detect double free's
    int checksum;
    /// size of this slot's data section
    size_t size;
    /// points to the slot start of this slot's data section
    void *data;
    /// points to the slot start of the next slot after this in memory
    void *next;
    /// points to the slot start of the previous slot before this in memory
    void *prev;
    /// points to the slot start of the next bigger free memory slot (may also be the same size)
    void *next_bigger_free;
    /// points to the slot start of the next smaller free memory slot (may also be the same size)
    void *next_smaller_free;
    /// whether it is a free slot or allocated
    unsigned is_free: 1;
} MemorySlotMeta __attribute__((aligned(ALLOCATION_ALIGN)));

#endif
