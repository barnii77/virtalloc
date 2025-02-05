#ifndef ALLOCATOR_UTILS_H
#define ALLOCATOR_UTILS_H

#include <stddef.h>
#include "virtalloc/allocator_impl.h"
#include "virtalloc/gp_memory_slot_meta.h"
#include "virtalloc/small_rr_memory_slot_meta.h"

#define NO_EXPECTATION (-1)
#define EXPECT_IS_ALLOCATED 0
#define EXPECT_IS_FREE 1

void dump_gp_slot_meta_to_file(FILE *file, GPMemorySlotMeta *meta, size_t slot_num);

void dump_sm_slot_meta_to_file(FILE *file, SmallRRMemorySlotMeta *meta, size_t slot_num);

size_t get_bucket_index(const Allocator *allocator, size_t size);

void *get_bucket_entry(const Allocator *allocator, const size_t bucket_idx);

void validate_checksum_of(const Allocator *allocator, void *meta, int force_validate);

GPMemorySlotMeta *get_meta(const Allocator *allocator, void *p, int should_be_free);

GPEarlyReleaseMeta *get_early_rel_meta(const Allocator *allocator, void *p);

void *get_next_rr_slot(const Allocator *allocator, void *rr_slot);

void coalesce_memory_slots(Allocator *allocator, GPMemorySlotMeta *meta, int meta_requires_unbind_from_free_list);

void unbind_from_sorted_free_list(Allocator *allocator, GPMemorySlotMeta *meta);

void insert_into_sorted_free_list(Allocator *allocator, GPMemorySlotMeta *meta);

void refresh_checksum_of(const Allocator *allocator, void *meta);

void consume_next_slot(Allocator *allocator, GPMemorySlotMeta *meta, size_t moved_bytes);

void consume_prev_slot(Allocator *allocator, GPMemorySlotMeta *meta, size_t moved_bytes);

#endif
