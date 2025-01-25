#include <stdint.h>
#include "testing.h"
#include "virtalloc.h"
#include "virtalloc/memory_slot_meta.h"

// TODO add benchmarking against libc

#define SMALL_HEAP_FLAGS (VIRTALLOC_FLAG_VA_DEFAULT_SETTINGS | VIRTALLOC_FLAG_VA_FEW_BUCKETS)

#define MAKE_AUTO_INIT_INT_ALLOC(out, size) \
    int *out = virtalloc_malloc(alloc, size * sizeof(int)); \
    if (!out) \
        goto fail; \
    for (int i = 0; i < size; i++) \
        out[i] = size + i;

#define ASSERT_CORRECT_CONTENT(mem, size) \
    for (int i = 0; i < size; i++) \
        if (mem[i] != size + i) \
            goto fail;

#define MAKE_AUTO_INIT_DOUBLE_ALLOC(out, size) \
    double *out = virtalloc_malloc(alloc, size * sizeof(double)); \
    if (!out) \
        goto fail; \
    for (int i = 0; i < size; i++) \
        out[i] = size * 1.5 + i;

#define ASSERT_DOUBLE_CONTENT(mem, size) \
    for (int i = 0; i < size; i++) \
        if (mem[i] != size * 1.5 + i) \
            goto fail;

#define ASSERT_DOUBLE_CONTENT_STARTING_AT(mem, size, start) \
    for (int i = 0; i < size; i++) \
        if (mem[i] != start * 1.5 + i) \
            goto fail;

#define TEST_ASSERT_MSG(expr, msg) \
    if (!(expr)) { \
        printf("ASSERTION FAILED: %s\n", msg); \
        goto fail; \
    }

void release_memory_wrapper(void *p) {
    free(p);
}

int monolithic_test_1() {
    vap_t alloc = virtalloc_new_virtual_allocator(512 * sizeof(int), SMALL_HEAP_FLAGS);
    virtalloc_set_release_mechanism(alloc, release_memory_wrapper);

    MAKE_AUTO_INIT_INT_ALLOC(x, 2);
    MAKE_AUTO_INIT_INT_ALLOC(y, 64);

    // check if x gets clipped up to minimum allocation size
    if ((void *) y - (void *) x - sizeof(MemorySlotMeta) != 64)
        goto fail;

    MAKE_AUTO_INIT_INT_ALLOC(z, 32);

    // this should move y
    int *y_realloc = virtalloc_realloc(alloc, y, 66 * sizeof(int));

    // this should now allocate where y used to be
    MAKE_AUTO_INIT_INT_ALLOC(w, 32);

    if (y_realloc <= w)
        goto fail;

    virtalloc_free(alloc, z);

    // assert the contents of the still valid allocations are correct
    ASSERT_CORRECT_CONTENT(x, 2);
    ASSERT_CORRECT_CONTENT(y_realloc, 64);

    int *y_realloc_2 = virtalloc_realloc(alloc, y_realloc, 96 * sizeof(int));
    TEST_ASSERT_MSG(y_realloc == y_realloc_2, "realloc with free space to the right still moved");
    ASSERT_CORRECT_CONTENT(y_realloc, 64);

    virtalloc_destroy_virtual_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_virtual_allocator(alloc);
    return 1;
}

int monolithic_test_2() {
    vap_t alloc = virtalloc_new_virtual_allocator(1024 * sizeof(double), SMALL_HEAP_FLAGS);
    virtalloc_set_release_mechanism(alloc, release_memory_wrapper);

    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Verify alignment constraints
    TEST_ASSERT_MSG((uintptr_t) a % 16 == 0, "a is not aligned to 16 bytes");
    TEST_ASSERT_MSG((uintptr_t) b % 16 == 0, "b is not aligned to 16 bytes");

    // Perform realloc on b
    double *b_realloc = virtalloc_realloc(alloc, b, 256 * sizeof(double));
    TEST_ASSERT_MSG(b_realloc, "b realloc failed");
    TEST_ASSERT_MSG(b_realloc == b, "realloc moved unnecessarily")
    ASSERT_DOUBLE_CONTENT(b_realloc, 128);

    // Allocate smaller memory to force fragmentation
    MAKE_AUTO_INIT_DOUBLE_ALLOC(c, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(d, 16);

    // Free smaller allocation to create gaps in memory
    virtalloc_free(alloc, c);

    // Allocate memory to see if the allocator can handle fragmented gaps
    MAKE_AUTO_INIT_DOUBLE_ALLOC(e, 8);

    TEST_ASSERT_MSG(e == c, "e should reuse c's memory slot");

    // Check realloc growth that fits in existing slots
    double *d_realloc = virtalloc_realloc(alloc, d, 32 * sizeof(double));
    TEST_ASSERT_MSG(d_realloc != NULL, "d realloc failed");

    // Check realloc shrink
    double *b_shrink = virtalloc_realloc(alloc, b_realloc, 64 * sizeof(double));
    TEST_ASSERT_MSG(b_shrink != NULL, "b shrink failed");
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);

    // Test allocator's ability to handle edge-case large allocations
    double *f = virtalloc_malloc(alloc, 512 * sizeof(double));
    TEST_ASSERT_MSG(f != NULL, "large allocation failed");

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    virtalloc_destroy_virtual_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_virtual_allocator(alloc);
    return 1;
}

int monolithic_test_3_inner_should_fail() {
    vap_t alloc = virtalloc_new_virtual_allocator(256 * sizeof(int), SMALL_HEAP_FLAGS);
    virtalloc_set_release_mechanism(alloc, release_memory_wrapper);

    MAKE_AUTO_INIT_INT_ALLOC(x, 2);
    MAKE_AUTO_INIT_INT_ALLOC(y, 64);

    // check if x gets clipped up to minimum allocation size
    if ((void *) y - (void *) x - sizeof(MemorySlotMeta) != 64)
        goto fail;

    MAKE_AUTO_INIT_INT_ALLOC(z, 32);

    // this should move y
    int *y_realloc = virtalloc_realloc(alloc, y, 66 * sizeof(int));

    // this should now allocate where y used to be
    MAKE_AUTO_INIT_INT_ALLOC(w, 32);

    if (y_realloc <= w)
        goto fail;

    virtalloc_free(alloc, z);

    // assert the contents of the still valid allocations are correct
    ASSERT_CORRECT_CONTENT(x, 2);
    ASSERT_CORRECT_CONTENT(y_realloc, 64);

    int *y_realloc_2 = virtalloc_realloc(alloc, y_realloc, 96 * sizeof(int));
    TEST_ASSERT_MSG(y_realloc == y_realloc_2, "realloc with free space to the right still moved");
    ASSERT_CORRECT_CONTENT(y_realloc, 64);

    virtalloc_destroy_virtual_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_virtual_allocator(alloc);
    return 1;
}

MAKE_INVERTED_TEST(monolithic_test_3, monolithic_test_3_inner_should_fail)

int monolithic_test_4() {
    vap_t alloc = virtalloc_new_virtual_allocator(384 * sizeof(double), SMALL_HEAP_FLAGS);
    virtalloc_set_release_mechanism(alloc, release_memory_wrapper);

    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Verify alignment constraints
    TEST_ASSERT_MSG((uintptr_t) a % 16 == 0, "a is not aligned to 16 bytes");
    TEST_ASSERT_MSG((uintptr_t) b % 16 == 0, "b is not aligned to 16 bytes");

    // Perform realloc on b
    double *b_realloc = virtalloc_realloc(alloc, b, 256 * sizeof(double));
    TEST_ASSERT_MSG(b_realloc, "b realloc failed");
    TEST_ASSERT_MSG(b_realloc == b, "realloc moved unnecessarily")
    ASSERT_DOUBLE_CONTENT(b_realloc, 128);

    // Allocate smaller memory to force fragmentation
    MAKE_AUTO_INIT_DOUBLE_ALLOC(c, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(d, 16);

    // Free smaller allocation to create gaps in memory
    virtalloc_free(alloc, c);

    // Allocate memory to see if the allocator can handle fragmented gaps
    MAKE_AUTO_INIT_DOUBLE_ALLOC(e, 8);

    TEST_ASSERT_MSG(e == c, "e should reuse c's memory slot");

    // Check realloc growth that fits in existing slots
    int *new_mem = malloc(512 * sizeof(double));
    virtalloc_add_new_memory(alloc, new_mem, 512 * sizeof(double));
    double *d_realloc = virtalloc_realloc(alloc, d, 32 * sizeof(double));
    TEST_ASSERT_MSG(d_realloc != NULL, "d realloc failed");

    // Check realloc shrink
    double *b_shrink = virtalloc_realloc(alloc, b_realloc, 64 * sizeof(double));
    TEST_ASSERT_MSG(b_shrink != NULL, "b shrink failed");
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);

    // Test allocator's ability to handle edge-case large allocations
    new_mem = malloc(784 * sizeof(double));
    virtalloc_add_new_memory(alloc, new_mem, 784 * sizeof(double));
    double *f = virtalloc_malloc(alloc, 512 * sizeof(double));
    TEST_ASSERT_MSG(f != NULL, "large allocation failed");

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    virtalloc_destroy_virtual_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_virtual_allocator(alloc);
    return 1;
}

int monolithic_test_5_inner_should_fail() {
    vap_t alloc = virtalloc_new_virtual_allocator(384 * sizeof(double), SMALL_HEAP_FLAGS);
    virtalloc_set_release_mechanism(alloc, release_memory_wrapper);

    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Verify alignment constraints
    TEST_ASSERT_MSG((uintptr_t) a % 16 == 0, "a is not aligned to 16 bytes");
    TEST_ASSERT_MSG((uintptr_t) b % 16 == 0, "b is not aligned to 16 bytes");

    // Perform realloc on b
    double *b_realloc = virtalloc_realloc(alloc, b, 256 * sizeof(double));
    TEST_ASSERT_MSG(b_realloc, "b realloc failed");
    TEST_ASSERT_MSG(b_realloc == b, "realloc moved unnecessarily")
    ASSERT_DOUBLE_CONTENT(b_realloc, 128);

    // Allocate smaller memory to force fragmentation
    MAKE_AUTO_INIT_DOUBLE_ALLOC(c, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(d, 16);

    // Free smaller allocation to create gaps in memory
    virtalloc_free(alloc, c);

    // Allocate memory to see if the allocator can handle fragmented gaps
    MAKE_AUTO_INIT_DOUBLE_ALLOC(e, 8);

    TEST_ASSERT_MSG(e == c, "e should reuse c's memory slot");

    // Check realloc growth that fits in existing slots
    int *new_mem = malloc(512 * sizeof(double));
    virtalloc_add_new_memory(alloc, new_mem, 512 * sizeof(double));
    double *d_realloc = virtalloc_realloc(alloc, d, 32 * sizeof(double));
    TEST_ASSERT_MSG(d_realloc != NULL, "d realloc failed");

    // Check realloc shrink
    double *b_shrink = virtalloc_realloc(alloc, b_realloc, 64 * sizeof(double));
    TEST_ASSERT_MSG(b_shrink != NULL, "b shrink failed");
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);

    // Test allocator's ability to handle edge-case large allocations
    double *f = virtalloc_malloc(alloc, 512 * sizeof(double));
    TEST_ASSERT_MSG(f, "large allocation failed");

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    virtalloc_destroy_virtual_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_virtual_allocator(alloc);
    return 1;
}

MAKE_INVERTED_TEST(monolithic_test_5, monolithic_test_5_inner_should_fail)

BEGIN_RUNNER_SETTINGS()
    print_on_pass = 1;
    print_pre_run_msg = 0;
    run_all_tests = 1;
    n_test_reps = 1;
    selected_test = "";
END_RUNNER_SETTINGS()

BEGIN_TEST_LIST()
    REGISTER_TEST_CASE(monolithic_test_1)
    REGISTER_TEST_CASE(monolithic_test_2)
    REGISTER_TEST_CASE(monolithic_test_3)
    REGISTER_TEST_CASE(monolithic_test_4)
    REGISTER_TEST_CASE(monolithic_test_5)
END_TEST_LIST()

MAKE_TEST_SUITE_RUNNABLE()
