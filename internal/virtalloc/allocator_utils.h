#ifndef ALLOCATOR_UTILS_H
#define ALLOCATOR_UTILS_H

#include <stddef.h>
#include "virtalloc/allocator.h"
#include "virtalloc/memory_slot_meta.h"

#define NO_EXPECTATION (-1)
#define EXPECT_IS_ALLOCATED 0
#define EXPECT_IS_FREE 1

size_t get_bucket_index(const VirtualAllocator *allocator, size_t size);

MemorySlotMeta *get_meta(const VirtualAllocator *allocator, void *p, int should_be_free);

void coalesce_memory_slots(VirtualAllocator *allocator, MemorySlotMeta *meta, int meta_requires_unbind);

void unbind_from_sorted_free_list(VirtualAllocator *allocator, MemorySlotMeta *meta);

void insert_into_sorted_free_list(VirtualAllocator *allocator, MemorySlotMeta *meta);

void refresh_checksum_of(VirtualAllocator *allocator, MemorySlotMeta *meta);

void consume_next_slot(VirtualAllocator *allocator, MemorySlotMeta *meta, size_t moved_bytes);

void consume_prev_slot(VirtualAllocator *allocator, MemorySlotMeta *meta, size_t moved_bytes);

size_t align_to(size_t size, size_t align);

#endif
