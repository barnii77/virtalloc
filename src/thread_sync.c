#include "virtalloc/allocator.h"
#include "virtalloc/cross_platform_lock.h"

void lock_virtual_allocator(Allocator *allocator) {
    if (!allocator->intra_thread_lock_count)
        lock(&allocator->lock);
    allocator->intra_thread_lock_count++;
}

void unlock_virtual_allocator(Allocator *allocator) {
    allocator->intra_thread_lock_count--;
    if (!allocator->intra_thread_lock_count)
        unlock(&allocator->lock);
}
