#ifndef MEMORY_SLOT_META_H
#define MEMORY_SLOT_META_H

#include "virtalloc/allocator_settings.h"

/// used for safe meta type computation (align-free)
typedef struct GenericMemorySlotMeta {
    unsigned char __bit_padding: 1;
    unsigned char meta_type: 7;
} __attribute__((aligned(1))) GenericMemorySlotMeta;

/// the metadata in front of a heap slot
typedef struct GPMemorySlotMeta {
    /// how many get_meta calls on this have to be done before get_meta checks the checksum again
    int time_to_checksum_check;
    /// a checksum that can help detect double frees and frees/reallocs with invalid pointer
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
    /// how many bytes the data pointer has been right adjusted to match the alignment requirements
    unsigned char memory_pointer_right_adjustment;
    /// whether it is a free slot or allocated
    unsigned char is_free: 1;
    /// whether to call the allocator->release_memory callback on this slot on allocator destruction or not
    unsigned char memory_is_owned: 1;
    /// bitfield-level padding for the bitfield above (so it doesn't become uninitialized memory)
    unsigned char __bit_padding1: 6;
    /// byte level padding
    char __padding[5];
    /// bitfield-level padding for the meta type
    unsigned char __bit_padding2: 1;
    /// a type identifier for a reflection-like mechanism in the allocator. Always 1 for this struct type.
    unsigned char meta_type: 7;
} __attribute__((aligned(LARGE_ALLOCATION_ALIGN))) GPMemorySlotMeta;

#endif
