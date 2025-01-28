#ifndef ALLOCATOR_IMPL_H
#define ALLOCATOR_IMPL_H

#include "virtalloc/allocator.h"
#include <stddef.h>
#include <stdio.h>

/// pretty-prints slot metadata and allocator info to the given file
void virtalloc_dump_allocator_to_file_impl(FILE *file, Allocator *allocator);

void *virtalloc_malloc_impl(Allocator *allocator, size_t size, int is_retry_run);

void virtalloc_free_impl(Allocator *allocator, void *p);

void *virtalloc_realloc_impl(Allocator *allocator, void *p, size_t size);

/// gets called when the allocator enters a critical section (non-threadsafe section)
void virtalloc_pre_op_callback_impl(Allocator *allocator);

/// gets called when the allocator exits a critical section (non-threadsafe section)
void virtalloc_post_op_callback_impl(Allocator *allocator);

/// transfers ownership of the given memory to the allocator
void virtalloc_gpa_add_new_memory_impl(Allocator *allocator, void *p, size_t size);

/// transfers ownership of the given memory to the allocator
void virtalloc_sma_add_new_memory_impl(Allocator *allocator, void *p, size_t size, int must_free_later);

#endif
