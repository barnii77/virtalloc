#include <stdint.h>
#include "testing.h"
#include "virtalloc.h"
#include "virtalloc/gp_memory_slot_meta.h"
#define LARGE_ALLOC_REQUIRED_ALIGN 64
#include "test_utils.h"

// TODO I should consider adding a binary tree allocator for particularly large allocation sizes as well (because they don't fit into bins particularly well anymore)
// TODO then profile on those tests where I spend most of my time in virtalloc
// TODO then make that faster (on current tests, a 10x speedup is required perform like libc, but I will write others)

#ifdef VIRTALLOC_LOGGING
#define MAKE_AUTO_INIT_DOUBLE_ALLOC(out, size) \
    fprintf(stderr, "\n<<<<<<<<<<<<<<< allocation of %s on line %d\n", #out, __LINE__); \
    double *out = virtalloc_malloc(alloc, size * sizeof(double)); \
    if (!out) \
        goto fail; \
    for (int i = 0; i < size; i++) \
        out[i] = size * 1.5 + i;
#else
#define MAKE_AUTO_INIT_DOUBLE_ALLOC(out, size) \
    double *out = virtalloc_malloc(alloc, size * sizeof(double)); \
    if (!out) \
        goto fail; \
    for (int i = 0; i < size; i++) \
        out[i] = size * 1.5 + i;
#endif

#define MAKE_AUTO_INIT_INT_ALLOC(out, size) \
    int *out = virtalloc_malloc(alloc, size * sizeof(int)); \
    if (!out) \
        goto fail; \
    for (int i = 0; i < size; i++) \
        out[i] = size + i;

#define MAKE_AUTO_INIT_INT_ALLOC_INTO(out, size) \
    out = virtalloc_malloc(alloc, size * sizeof(int)); \
    if (!out) \
        goto fail; \
    for (int i = 0; i < size; i++) \
        out[i] = size + i;

int monolithic_test_1() {
    vap_t alloc = virtalloc_new_allocator(512 * sizeof(int), SMALL_HEAP_FLAGS_NO_RR);
    virtalloc_set_release_mechanism(alloc, release_memory);

    MAKE_AUTO_INIT_INT_ALLOC(x, 2);
    MAKE_AUTO_INIT_INT_ALLOC(y, 64);

    // check if x gets clipped up to minimum allocation size
    if ((void *) y - (void *) x - sizeof(GPMemorySlotMeta) != 64)
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

    virtalloc_destroy_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_allocator(alloc);
    return 1;
}

int monolithic_test_2() {
    vap_t alloc = virtalloc_new_allocator(1024 * sizeof(double), SMALL_HEAP_FLAGS_NO_RR);
    virtalloc_set_release_mechanism(alloc, release_memory);

    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Verify alignment constraints
    TEST_ASSERT_MSG((uintptr_t) a % 64 == 0, "a is not aligned to 64 bytes");
    TEST_ASSERT_MSG((uintptr_t) b % 64 == 0, "b is not aligned to 64 bytes");

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

    virtalloc_destroy_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_allocator(alloc);
    return 1;
}

int monolithic_test_3_inner_should_fail() {
    vap_t alloc = virtalloc_new_allocator(128 * sizeof(int), SMALL_HEAP_FLAGS_NO_RR);
    virtalloc_set_release_mechanism(alloc, release_memory);

    MAKE_AUTO_INIT_INT_ALLOC(x, 2);
    MAKE_AUTO_INIT_INT_ALLOC(y, 64);

    // check if x gets clipped up to minimum allocation size
    if ((void *) y - (void *) x - sizeof(GPMemorySlotMeta) != 64)
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
    TEST_ASSERT_MSG(y_realloc == y_realloc_2, "This was supposed to fail");
    ASSERT_CORRECT_CONTENT(y_realloc, 64);

    virtalloc_destroy_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_allocator(alloc);
    return 1;
}

MAKE_INVERTED_TEST(monolithic_test_3, monolithic_test_3_inner_should_fail)

int monolithic_test_4() {
    vap_t alloc = virtalloc_new_allocator(384 * sizeof(double), SMALL_HEAP_FLAGS_NO_RR);
    virtalloc_set_release_mechanism(alloc, release_memory);
    virtalloc_set_request_mechanism(alloc, request_new_memory);

    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Verify alignment constraints
    TEST_ASSERT_MSG((uintptr_t) a % 64 == 0, "a is not aligned to 64 bytes");
    TEST_ASSERT_MSG((uintptr_t) b % 64 == 0, "b is not aligned to 64 bytes");

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

    virtalloc_destroy_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_allocator(alloc);
    return 1;
}

int monolithic_test_5_inner_should_fail() {
    vap_t alloc = virtalloc_new_allocator(384 * sizeof(double), SMALL_HEAP_FLAGS_NO_RR);
    virtalloc_set_release_mechanism(alloc, release_memory);
    virtalloc_set_request_mechanism(alloc, request_new_memory);

    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Verify alignment constraints
    TEST_ASSERT_MSG((uintptr_t) a % 64 == 0, "a is not aligned to 64 bytes");
    TEST_ASSERT_MSG((uintptr_t) b % 64 == 0, "b is not aligned to 64 bytes");

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
    virtalloc_unset_request_mechanism(alloc);
    double *f = virtalloc_malloc(alloc, 512 * sizeof(double));
    TEST_ASSERT_MSG(f, "This was supposed to fail");

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    virtalloc_destroy_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_allocator(alloc);
    return 1;
}

MAKE_INVERTED_TEST(monolithic_test_5, monolithic_test_5_inner_should_fail)

int monolithic_test_6() {
    vap_t alloc = virtalloc_new_allocator(384 * sizeof(double), SMALL_HEAP_FLAGS_NO_RR);
    virtalloc_set_release_mechanism(alloc, release_memory);
    virtalloc_set_request_mechanism(alloc, request_new_memory);

    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Verify alignment constraints
    TEST_ASSERT_MSG((uintptr_t) a % 64 == 0, "a is not aligned to 64 bytes");
    TEST_ASSERT_MSG((uintptr_t) b % 64 == 0, "b is not aligned to 64 bytes");

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
    TEST_ASSERT_MSG(f, "large allocation failed");

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    virtalloc_destroy_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_allocator(alloc);
    return 1;
}

int test_coalescing_7() {
    vap_t alloc = virtalloc_new_allocator(256 * sizeof(int), SMALL_HEAP_FLAGS_NO_RR);
    virtalloc_set_release_mechanism(alloc, release_memory);

    const int n_allocs = 5;
    int *allocs[n_allocs];
    for (int j = 0; j < n_allocs; j++) {
        MAKE_AUTO_INIT_INT_ALLOC_INTO(allocs[j], 32)
    }
    for (int j = 0; j < n_allocs; j++) {
        ASSERT_CORRECT_CONTENT(allocs[j], 32)
    }
    for (int j = 0; j < n_allocs; j++) {
        virtalloc_free(alloc, allocs[j]);
    }
    MAKE_AUTO_INIT_INT_ALLOC(final, 224)
    ASSERT_CORRECT_CONTENT(final, 224)

    virtalloc_destroy_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_allocator(alloc);
    return 1;
}

int monolithic_test_rr_8() {
    vap_t alloc = virtalloc_new_allocator(512 * sizeof(int), SMALL_HEAP_FLAGS);
    virtalloc_set_release_mechanism(alloc, release_memory);
    virtalloc_set_request_mechanism(alloc, request_new_memory);

    MAKE_AUTO_INIT_INT_ALLOC(x, 2);
    MAKE_AUTO_INIT_INT_ALLOC(y, 15);

    // check if x gets clipped up to minimum allocation size
    if ((void *) y - (void *) x != 64)
        goto fail;

    MAKE_AUTO_INIT_INT_ALLOC(z, 32);

    // this should move y
    int *y_realloc = virtalloc_realloc(alloc, y, 66 * sizeof(int));

    // this should now allocate where y used to be
    MAKE_AUTO_INIT_INT_ALLOC(w, 32);

    if (w <= y_realloc)
        goto fail;

    virtalloc_free(alloc, z);

    // assert the contents of the still valid allocations are correct
    ASSERT_CORRECT_CONTENT(x, 2);
    ASSERT_CORRECT_CONTENT(y_realloc, 15);

    int *y_realloc_2 = virtalloc_realloc(alloc, y_realloc, 96 * sizeof(int));
    TEST_ASSERT_MSG(y_realloc != y_realloc_2, "realloc without free space didn't move");
    ASSERT_CORRECT_CONTENT(y_realloc, 15);

    virtalloc_destroy_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_allocator(alloc);
    return 1;
}

int monolithic_test_rr_9() {
    vap_t alloc = virtalloc_new_allocator(1024 * sizeof(double), SMALL_HEAP_FLAGS);
    virtalloc_set_release_mechanism(alloc, release_memory);
    virtalloc_set_request_mechanism(alloc, request_new_memory);

    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Verify alignment constraints
    TEST_ASSERT_MSG((uintptr_t) a % 64 == 0, "a is not aligned to 64 bytes");
    TEST_ASSERT_MSG((uintptr_t) b % 64 == 0, "b is not aligned to 64 bytes");

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

    virtalloc_destroy_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_allocator(alloc);
    return 1;
}

int monolithic_test_rr_10() {
    vap_t alloc = virtalloc_new_allocator(384 * sizeof(double), SMALL_HEAP_FLAGS);
    virtalloc_set_release_mechanism(alloc, release_memory);
    virtalloc_set_request_mechanism(alloc, request_new_memory);

    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Verify alignment constraints
    TEST_ASSERT_MSG((uintptr_t) a % 64 == 0, "a is not aligned to 64 bytes");
    TEST_ASSERT_MSG((uintptr_t) b % 64 == 0, "b is not aligned to 64 bytes");

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

    virtalloc_destroy_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_allocator(alloc);
    return 1;
}

int monolithic_test_rr_11() {
    vap_t alloc = virtalloc_new_allocator(384 * sizeof(double), SMALL_HEAP_FLAGS);
    virtalloc_set_release_mechanism(alloc, release_memory);
    virtalloc_set_request_mechanism(alloc, request_new_memory);

    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Verify alignment constraints
    TEST_ASSERT_MSG((uintptr_t) a % 64 == 0, "a is not aligned to 64 bytes");
    TEST_ASSERT_MSG((uintptr_t) b % 64 == 0, "b is not aligned to 64 bytes");

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
    TEST_ASSERT_MSG(f, "large allocation failed");

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    virtalloc_destroy_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_allocator(alloc);
    return 1;
}

int test_fragmentation_and_operations_12() {
    vap_t alloc = virtalloc_new_allocator(1024 * sizeof(double), SMALL_HEAP_FLAGS_NO_RR);
    virtalloc_set_max_gpa_slot_checks_before_oom(alloc, 0);
    virtalloc_set_release_mechanism(alloc, release_memory);

    // Fragment the allocator by allocating and freeing memory in a pattern
    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(c, 16);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(d, 32);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(e, 64);

    print_msg_with_line("freeing b");
    virtalloc_free(alloc, b);
    print_msg_with_line("freeing d");
    virtalloc_free(alloc, d);

    // Allocate smaller memory to force fragmentation
    MAKE_AUTO_INIT_DOUBLE_ALLOC(f, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(g, 16);

    // Perform realloc on e
    print_msg_with_line("re-allocing e");
    double *e_realloc = virtalloc_realloc(alloc, e, 128 * sizeof(double));
    TEST_ASSERT_MSG(e_realloc, "e realloc failed");
    TEST_ASSERT_MSG(e_realloc == e, "realloc moved unnecessarily");
    ASSERT_DOUBLE_CONTENT(e_realloc, 64);

    // Free smaller allocations to create gaps in memory
    print_msg_with_line("freeing f");
    virtalloc_free(alloc, f);
    print_msg_with_line("freeing g");
    virtalloc_free(alloc, g);

    // Allocate memory to see if the allocator can handle fragmented gaps
    MAKE_AUTO_INIT_DOUBLE_ALLOC(h, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(u, 16);

    TEST_ASSERT_MSG(h == f, "h should reuse f's memory slot");
    TEST_ASSERT_MSG(u == g, "u should reuse g's memory slot");

    // Check realloc growth that fits in existing slots
    print_msg_with_line("re-allocing c");
    double *c_realloc = virtalloc_realloc(alloc, c, 32 * sizeof(double));
    TEST_ASSERT_MSG(c_realloc != NULL, "c realloc failed");

    // Check realloc shrink
    print_msg_with_line("shrinking e");
    double *e_shrink = virtalloc_realloc(alloc, e_realloc, 32 * sizeof(double));
    TEST_ASSERT_MSG(e_shrink != NULL, "e shrink failed");
    ASSERT_DOUBLE_CONTENT_STARTING_AT(e_shrink, 32, 64);

    // Test allocator's ability to handle edge-case large allocations
    double *j = virtalloc_malloc(alloc, 512 * sizeof(double));
    TEST_ASSERT_MSG(j != NULL, "large allocation failed");

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(e_shrink, 32, 64);
    ASSERT_DOUBLE_CONTENT(c_realloc, 16); // Only first 16 should remain valid

    virtalloc_destroy_allocator(alloc);
    return 0;
fail:
    virtalloc_destroy_allocator(alloc);
    return 1;
}

BEGIN_RUNNER_SETTINGS()
    suppress_test_status = 1;
    print_on_all_passed_this_iter = 0;
    print_on_pass = 0;
    print_pre_run_msg = 0;
    run_all_tests = 1;
    n_test_reps = 10000;
    selected_test = "";
END_RUNNER_SETTINGS()

BEGIN_TEST_LIST()
    REGISTER_TEST_CASE(monolithic_test_1)
    REGISTER_TEST_CASE(monolithic_test_2)
    REGISTER_TEST_CASE(monolithic_test_3)
    REGISTER_TEST_CASE(monolithic_test_4)
    REGISTER_TEST_CASE(monolithic_test_5)
    REGISTER_TEST_CASE(monolithic_test_6)
    REGISTER_TEST_CASE(test_coalescing_7)
    REGISTER_TEST_CASE(monolithic_test_rr_8)
    REGISTER_TEST_CASE(monolithic_test_rr_9)
    REGISTER_TEST_CASE(monolithic_test_rr_10)
    REGISTER_TEST_CASE(monolithic_test_rr_11)
    REGISTER_TEST_CASE(test_fragmentation_and_operations_12)
END_TEST_LIST()

MAKE_TEST_SUITE_RUNNABLE()
