#ifndef VIRTALLOC_H
#define VIRTALLOC_H

typedef struct _IO_FILE FILE;

typedef void *vap_t;

vap_t virtalloc_new_allocator_in(size_t size, char memory[static size], int flags);

vap_t virtalloc_new_allocator(size_t size, int flags);

void virtalloc_destroy_allocator(vap_t allocator);

void *virtalloc_malloc(vap_t allocator, size_t size);

void virtalloc_free(vap_t allocator, void *p);

void *virtalloc_realloc(vap_t allocator, void *p, size_t size);

void virtalloc_set_release_mechanism(vap_t allocator, void (*release_memory)(void *p));

void virtalloc_unset_release_mechanism(vap_t allocator);

void virtalloc_set_request_mechanism(vap_t allocator, void *(*request_new_memory)(size_t size));

void virtalloc_unset_request_mechanism(vap_t allocator);

void virtalloc_set_max_gpa_slot_checks_before_oom(vap_t allocator, size_t max_slot_checks);

void virtalloc_set_max_sma_slot_checks_before_oom(vap_t allocator, size_t max_slot_checks);

void virtalloc_dump_allocator_to_file(FILE *file, vap_t allocator);

void virtalloc_enable_heavy_debug_allocator_corruption_checks(vap_t allocator);

void virtalloc_disable_heavy_debug_allocator_corruption_checks(vap_t allocator);

#define VIRTALLOC_FLAG_VA_HAS_CHECKSUM 0x1
#define VIRTALLOC_FLAG_VA_HAS_NON_CHECKSUM_SAFETY_CHECKS 0x2
#define VIRTALLOC_FLAG_VA_HAS_SAFETY_CHECKS (VIRTALLOC_FLAG_VA_HAS_CHECKSUM | VIRTALLOC_FLAG_VA_HAS_NON_CHECKSUM_SAFETY_CHECKS)
#define VIRTALLOC_FLAG_VA_KEEP_SIZE_TINY 0x4
#define VIRTALLOC_FLAG_VA_KEEP_SIZE_SMALL 0x8
#define VIRTALLOC_FLAG_VA_KEEP_SIZE_NORMAL 0x0  // not a real setting because it's the default
#define VIRTALLOC_FLAG_VA_KEEP_SIZE_LARGE 0x10
#define VIRTALLOC_FLAG_VA_NO_RR_ALLOCATOR 0x20
#define VIRTALLOC_FLAG_VA_SMA_REQUEST_MEM_FROM_GPA 0x40
#define VIRTALLOC_FLAG_VA_HAS_SAFETY_PADDING_LINE 0x80
#define VIRTALLOC_FLAG_VA_DENSE_CHECKSUM_CHECKS 0x100
#define VIRTALLOC_FLAG_VA_DISABLE_BUCKETS 0x200
#define VIRTALLOC_FLAG_VA_BUCKET_TREE 0x400
#define VIRTALLOC_FLAG_VA_BUCKET_ARENAS 0x800
#define VIRTALLOC_FLAG_VA_ASSUME_THREAD_SAFE_USAGE 0x1000  // may be used in single threaded contexts for example
#define VIRTALLOC_FLAG_VA_HEAVY_DEBUG_CORRUPTION_CHECKS 0x2000

#define VIRTALLOC_FLAG_VA_DEFAULT_SETTINGS (VIRTALLOC_FLAG_VA_HAS_SAFETY_CHECKS | VIRTALLOC_FLAG_VA_SMA_REQUEST_MEM_FROM_GPA | VIRTALLOC_FLAG_VA_HAS_SAFETY_PADDING_LINE | VIRTALLOC_FLAG_VA_BUCKET_ARENAS)

#endif
