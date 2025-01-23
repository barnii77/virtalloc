#include "virtalloc/virtual_allocator.h"
#include "virtalloc/cross_platform_lock.h"

void lock_virtual_allocator(VirtualAllocator *allocator) {
    if (!allocator->intra_thread_lock_count)
        lock(&allocator->lock);
    allocator->intra_thread_lock_count++;
}

void unlock_virtual_allocator(VirtualAllocator *allocator) {
    allocator->intra_thread_lock_count--;
    if (!allocator->intra_thread_lock_count)
        unlock(&allocator->lock);
}
