#ifndef VIRTALLOC_H
#define VIRTALLOC_H

typedef struct _IO_FILE FILE;

typedef void *vap_t;

/// creates a new allocator inside the given memory (and potentially memory slots if size is sufficient) and transfers
/// ownership of the memory to the allocator
vap_t virtalloc_new_virtual_allocator_from(size_t size, char memory[static size], int flags);

vap_t virtalloc_new_virtual_allocator(size_t size, int flags);

void virtalloc_destroy_virtual_allocator(vap_t allocator);

void *virtalloc_realloc(vap_t allocator, void *p, size_t size);

void virtalloc_free(vap_t allocator, void *p);

void *virtalloc_malloc(vap_t allocator, size_t size);

/// transfers ownership of the given memory to the allocator
void virtalloc_add_new_memory(vap_t allocator, void *memory, size_t size);

void virtalloc_set_release_mechanism(vap_t allocator, void (*release_memory)(void *p));

void virtalloc_unset_release_mechanism(vap_t allocator);

void virtalloc_set_request_mechanism(vap_t allocator, void *(*request_new_memory)(size_t size));

void virtalloc_unset_request_mechanism(vap_t allocator);

void virtalloc_dump_allocator_to_file(FILE *file, vap_t allocator);

#define VIRTALLOC_FLAG_VA_HAS_CHECKSUM 0x1
#define VIRTALLOC_FLAG_VA_HAS_NON_CHECKSUM_SAFETY_CHECKS 0x2
#define VIRTALLOC_FLAG_VA_HAS_SAFETY_CHECKS (VIRTALLOC_FLAG_VA_HAS_CHECKSUM | VIRTALLOC_FLAG_VA_HAS_NON_CHECKSUM_SAFETY_CHECKS)
#define VIRTALLOC_FLAG_VA_FEW_BUCKETS 0x4
#define VIRTALLOC_FLAG_VA_MANY_BUCKETS 0x0  // not a real setting, just uses default num buckets
#define VIRTALLOC_FLAG_VA_DEFAULT_SETTINGS VIRTALLOC_FLAG_VA_HAS_SAFETY_CHECKS

#endif
