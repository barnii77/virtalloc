#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include "virtalloc/virtual_allocator.h"
#include <stddef.h>

void *virtalloc_malloc_impl(VirtualAllocator *allocator, size_t size);

void virtalloc_free_impl(VirtualAllocator *allocator, void *p);

void *virtalloc_realloc_impl(VirtualAllocator *allocator, void *p, size_t size);

void virtalloc_pre_op_callback_impl(VirtualAllocator *allocator);

void virtalloc_post_op_callback_impl(VirtualAllocator *allocator);

/// transfers ownership of the given memory to the allocator
void virtalloc_add_new_memory_impl(VirtualAllocator *allocator, void *p, size_t size);

#endif
