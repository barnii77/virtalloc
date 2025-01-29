#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include "testing.h"
#include "virtalloc.h"

#define SMALL_HEAP_FLAGS_NO_RR (VIRTALLOC_FLAG_VA_DEFAULT_SETTINGS | VIRTALLOC_FLAG_VA_FEW_BUCKETS | VIRTALLOC_FLAG_VA_NO_RR_ALLOCATOR)
#define SMALL_HEAP_FLAGS (VIRTALLOC_FLAG_VA_DEFAULT_SETTINGS | VIRTALLOC_FLAG_VA_FEW_BUCKETS | VIRTALLOC_FLAG_VA_SMA_REQUEST_MEM_FROM_GPA)

#ifndef LARGE_ALLOC_REQUIRED_ALIGN
#error "must define LARGE_ALLOC_REQUIRED_ALIGN before including test_utils.h"
#endif

#define ASSERT_CORRECT_CONTENT(mem, size) \
    if ((size_t) mem % LARGE_ALLOC_REQUIRED_ALIGN != 0) \
        goto fail; \
    for (int i = 0; i < size; i++) \
        if (mem[i] != size + i) \
            goto fail;

#ifdef VIRTALLOC_LOGGING
#define print_msg_with_line(msg) \
    fprintf(stderr, "\n<<<<<<<<<<<<<<< %s on line %d\n", msg, __LINE__);
#else
#define print_msg_with_line(msg)
#endif

#define ASSERT_DOUBLE_CONTENT(mem, size) \
    if ((size_t) mem % LARGE_ALLOC_REQUIRED_ALIGN != 0) \
        goto fail; \
    for (int i = 0; i < size; i++) \
        if (mem[i] != size * 1.5 + i) \
            goto fail;

#define ASSERT_DOUBLE_CONTENT_STARTING_AT(mem, size, start) \
    if ((size_t) mem % LARGE_ALLOC_REQUIRED_ALIGN != 0) \
        goto fail; \
    for (int i = 0; i < size; i++) \
        if (mem[i] != start * 1.5 + i) \
            goto fail;

#define TEST_ASSERT_MSG(expr, msg) \
    if (!(expr)) { \
        if (!suppress_test_status) printf("ASSERTION FAILED: %s\n", msg); \
        goto fail; \
    }

// kind of unnecessary in its current form, but it's nice to have for placing breakpoints :)
void release_memory(void *p) {
    free(p);
}

void *request_new_memory(const size_t min_size) {
    void *mem = malloc(min_size);
    *(size_t *) mem = min_size;
    return mem;
}

#endif
