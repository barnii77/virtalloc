/*
 * This file is a reference implementation of the virtalloc tests using the normal malloc and free functions.
 * It will be used for benchmarking virtalloc
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "testing.h"
#define LARGE_ALLOC_REQUIRED_ALIGN 1
#include "test_utils.h"

// Define macros to replace virtalloc-specific allocation and initialization
#define MAKE_AUTO_INIT_INT_ALLOC(ptr, size)                 \
    int *ptr = (int *)malloc((size) * sizeof(int));         \
    if (!ptr) {                                            \
        goto fail;                                         \
    }                                                      \
    memset(ptr, 0, (size) * sizeof(int));                  \
    initialize_int_alloc(ptr, size);

#define MAKE_AUTO_INIT_DOUBLE_ALLOC(ptr, size)              \
    double *ptr = (double *)malloc((size) * sizeof(double));\
    if (!ptr) {                                            \
        goto fail;                                         \
    }                                                      \
    memset(ptr, 0, (size) * sizeof(double));               \
    initialize_double_alloc(ptr, size);

#define MAKE_AUTO_INIT_INT_ALLOC_INTO(target, size)        \
    do {                                                   \
        target = (int *)malloc((size) * sizeof(int));     \
        if (!target) {                                     \
            goto fail;                                     \
        }                                                  \
        memset(target, 0, (size) * sizeof(int));           \
        initialize_int_alloc(target, size);               \
    } while(0)

// Placeholder functions to initialize allocations
void initialize_int_alloc(int *ptr, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        ptr[i] = size + (int) i;
    }
}

void initialize_double_alloc(double *ptr, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        ptr[i] = (double) size * 1.5 + (double) i;
    }
}

// Test 1
int monolithic_test_1() {
    MAKE_AUTO_INIT_INT_ALLOC(x, 2);
    MAKE_AUTO_INIT_INT_ALLOC(y, 64);

    // Note: Pointer arithmetic checks are not meaningful with malloc,
    // so this check is omitted.

    MAKE_AUTO_INIT_INT_ALLOC(z, 32);

    // Reallocate y to a larger size
    int *y_realloc = (int *) realloc(y, 66 * sizeof(int));
    if (!y_realloc) {
        goto fail;
    }
    // Initialize new memory if realloc moved the block
    if (y_realloc != y) {
        initialize_int_alloc(y_realloc + 64, 2); // Initialize the additional elements
    }

    // Allocate w
    MAKE_AUTO_INIT_INT_ALLOC(w, 32);

    // The comparison below is not meaningful with malloc,
    // so it is omitted. Instead, ensure y_realloc is not NULL
    if (!y_realloc) {
        goto fail;
    }

    free(z);

    // Assert the contents of the still valid allocations are correct
    ASSERT_CORRECT_CONTENT(x, 2);
    ASSERT_CORRECT_CONTENT(y_realloc, 64);

    int *y_realloc_2 = realloc(y_realloc, 96 * sizeof(int));
    ASSERT_CORRECT_CONTENT(y_realloc_2, 64);

    free(x);
    free(w);
    free(y_realloc_2);
    return 0;
fail:
    free(x);
    free(y);
    free(z);
    free(w);
    return 1;
}

// Test 2
int monolithic_test_2() {
    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Verify alignment constraints using posix_memalign if needed
    // However, malloc does not guarantee 64-byte alignment
    // To enforce alignment, use posix_memalign instead of malloc
    // For simplicity, we omit alignment checks here

    // Reallocate b to a larger size
    double *b_realloc = realloc(b, 256 * sizeof(double));
    if (!b_realloc) {
        goto fail;
    }

    // Allocate smaller memory to force fragmentation
    MAKE_AUTO_INIT_DOUBLE_ALLOC(c, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(d, 16);

    // Free smaller allocation to create gaps in memory
    free(c);

    // Allocate memory to see if the allocator can handle fragmented gaps
    double *e = malloc(8 * sizeof(double));
    if (!e) {
        goto fail;
    }
    initialize_double_alloc(e, 8);

    // In malloc, e is not guaranteed to reuse c's memory slot,
    // so we omit this check
    // TEST_ASSERT_MSG(e == c, "e should reuse c's memory slot");

    // Reallocate d to a larger size
    double *d_realloc = realloc(d, 32 * sizeof(double));
    if (!d_realloc) {
        goto fail;
    }

    // Reallocate shrink b_realloc
    double *b_shrink = realloc(b_realloc, 64 * sizeof(double));
    if (!b_shrink) {
        goto fail;
    }
    // Optionally, reinitialize the shrunk memory if needed
    // Here we assume the first 64 elements remain unchanged

    // Test large allocation
    double *f = malloc(512 * sizeof(double));
    if (!f) {
        goto fail;
    }
    initialize_double_alloc(f, 512);

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    free(a);
    free(b_shrink);
    free(d_realloc);
    free(e);
    free(f);
    return 0;
fail:
    free(a);
    free(b);
    free(c);
    free(d);
    free(e);
    free(f);
    return 1;
}

// Test 3 (Inner should fail)
int monolithic_test_3() {
    MAKE_AUTO_INIT_INT_ALLOC(x, 2);
    MAKE_AUTO_INIT_INT_ALLOC(y, 64);

    // Pointer arithmetic check is omitted as with malloc

    MAKE_AUTO_INIT_INT_ALLOC(z, 32);

    // Reallocate y to a larger size
    int *y_realloc = (int *) realloc(y, 66 * sizeof(int));
    if (!y_realloc) {
        goto fail;
    }
    if (y_realloc != y) {
        initialize_int_alloc(y_realloc + 64, 2);
    }

    MAKE_AUTO_INIT_INT_ALLOC(w, 32);

    // The pointer comparison is not meaningful with malloc
    // Omit the check or adjust accordingly
    if (!y_realloc) {
        goto fail;
    }

    free(z);

    // Assert the contents of the still valid allocations are correct
    ASSERT_CORRECT_CONTENT(x, 2);
    ASSERT_CORRECT_CONTENT(y_realloc, 64);

    int *y_realloc_2 = (int *) realloc(y_realloc, 96 * sizeof(int));
    // TEST_ASSERT_MSG(y_realloc == y_realloc_2, "This was supposed to fail");
    ASSERT_CORRECT_CONTENT(y_realloc_2, 64);

    free(x);
    free(w);
    free(y_realloc_2);
    return 0;
fail:
    free(x);
    free(y);
    free(z);
    free(w);
    return 1;
}

// Test 4
int monolithic_test_4() {
    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Alignment checks are omitted as with malloc
    // If alignment is necessary, use posix_memalign

    // Reallocate b to a larger size
    double *b_realloc = (double *) realloc(b, 256 * sizeof(double));
    if (!b_realloc) {
        goto fail;
    }
    if (b_realloc != b) {
        initialize_double_alloc(b_realloc + 128, 128);
    }

    // Allocate smaller memory to force fragmentation
    MAKE_AUTO_INIT_DOUBLE_ALLOC(c, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(d, 16);

    // Free smaller allocation to create gaps in memory
    free(c);

    // Allocate memory to see if the allocator can handle fragmented gaps
    double *e = malloc(8 * sizeof(double));
    if (!e) {
        goto fail;
    }
    initialize_double_alloc(e, 8);

    // Allocation reuse check is not applicable with malloc

    // Reallocate d to a larger size
    double *d_realloc = (double *) realloc(d, 32 * sizeof(double));
    if (!d_realloc) {
        goto fail;
    }

    // Reallocate shrink b_realloc
    double *b_shrink = (double *) realloc(b_realloc, 64 * sizeof(double));
    if (!b_shrink) {
        goto fail;
    }

    // Test large allocation
    double *f = malloc(512 * sizeof(double));
    if (!f) {
        goto fail;
    }
    initialize_double_alloc(f, 512);

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    free(a);
    free(b_shrink);
    free(d_realloc);
    free(e);
    free(f);
    return 0;
fail:
    free(a);
    free(b);
    free(c);
    free(d);
    free(e);
    free(f);
    return 1;
}

// Test 5 (Inner should fail)
int monolithic_test_5() {
    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Alignment checks are omitted as with malloc

    // Reallocate b to a larger size
    double *b_realloc = realloc(b, 256 * sizeof(double));
    if (!b_realloc) {
        goto fail;
    }

    // Allocate smaller memory to force fragmentation
    MAKE_AUTO_INIT_DOUBLE_ALLOC(c, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(d, 16);

    // Free smaller allocation to create gaps in memory
    free(c);

    // Allocate memory to see if the allocator can handle fragmented gaps
    double *e = (double *) malloc(8 * sizeof(double));
    if (!e) {
        goto fail;
    }
    initialize_double_alloc(e, 8);

    // Allocation reuse check is not applicable with malloc

    // Reallocate d to a larger size
    double *d_realloc = (double *) realloc(d, 32 * sizeof(double));
    if (!d_realloc) {
        goto fail;
    }

    // Reallocate shrink b_realloc
    double *b_shrink = realloc(b_realloc, 64 * sizeof(double));
    if (!b_shrink) {
        goto fail;
    }

    // Attempt to allocate a large block which should fail
    double *f = malloc(512 * sizeof(double));
    if (!f) {
        goto fail;
    }

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    free(a);
    free(b_shrink);
    free(d_realloc);
    free(e);
    return 0;
fail:
    free(a);
    free(b);
    free(c);
    free(d);
    free(e);
    free(f);
    return 1;
}

// Test 6
int monolithic_test_6() {
    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Alignment checks are omitted as with malloc

    // Reallocate b to a larger size
    double *b_realloc = (double *) realloc(b, 256 * sizeof(double));
    if (!b_realloc) {
        goto fail;
    }
    if (b_realloc != b) {
        initialize_double_alloc(b_realloc + 128, 128);
    }

    // Allocate smaller memory to force fragmentation
    MAKE_AUTO_INIT_DOUBLE_ALLOC(c, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(d, 16);

    // Free smaller allocation to create gaps in memory
    free(c);

    // Allocate memory to see if the allocator can handle fragmented gaps
    double *e = (double *) malloc(8 * sizeof(double));
    if (!e) {
        goto fail;
    }
    initialize_double_alloc(e, 8);

    // Reallocate d to a larger size
    double *d_realloc = (double *) realloc(d, 32 * sizeof(double));
    if (!d_realloc) {
        goto fail;
    }
    if (d_realloc != d) {
        initialize_double_alloc(d_realloc + 16, 16);
    }

    // Reallocate shrink b_realloc
    double *b_shrink = (double *) realloc(b_realloc, 64 * sizeof(double));
    if (!b_shrink) {
        goto fail;
    }

    // Test large allocation
    double *f = (double *) malloc(512 * sizeof(double));
    if (!f) {
        goto fail;
    }
    initialize_double_alloc(f, 512);

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    free(a);
    free(b_shrink);
    free(d_realloc);
    free(e);
    free(f);
    return 0;
fail:
    free(a);
    free(b);
    free(c);
    free(d);
    free(e);
    free(f);
    return 1;
}

// Test 7: Coalescing
int test_coalescing_7() {
    // Allocate multiple blocks
    const int n_allocs = 5;
    int *allocs[n_allocs];
    for (int j = 0; j < n_allocs; j++) {
        MAKE_AUTO_INIT_INT_ALLOC_INTO(allocs[j], 32);
    }
    for (int j = 0; j < n_allocs; j++) {
        ASSERT_CORRECT_CONTENT(allocs[j], 32);
    }
    for (int j = 0; j < n_allocs; j++) {
        free(allocs[j]);
    }
    // Allocate a large block after freeing
    int *final = (int *) malloc(224 * sizeof(int));
    if (!final) {
        goto fail;
    }
    initialize_int_alloc(final, 224);
    ASSERT_CORRECT_CONTENT(final, 224);

    free(final);
    return 0;
fail:
    for (int j = 0; j < n_allocs; j++) {
        free(allocs[j]);
    }
    free(final);
    return 1;
}

// Test 8
int monolithic_test_rr_8() {
    MAKE_AUTO_INIT_INT_ALLOC(x, 2);
    MAKE_AUTO_INIT_INT_ALLOC(y, 15);

    // Pointer arithmetic check is omitted as with malloc

    MAKE_AUTO_INIT_INT_ALLOC(z, 32);

    // Reallocate y to a larger size
    int *y_realloc = (int *) realloc(y, 66 * sizeof(int));
    if (!y_realloc) {
        goto fail;
    }
    if (y_realloc != y) {
        initialize_int_alloc(y_realloc + 15, 51); // Initialize the additional elements
    }

    // Allocate w
    MAKE_AUTO_INIT_INT_ALLOC(w, 32);

    // Pointer comparison is not meaningful with malloc

    free(z);

    // Assert the contents of the still valid allocations are correct
    ASSERT_CORRECT_CONTENT(x, 2);
    ASSERT_CORRECT_CONTENT(y_realloc, 15);

    int *y_realloc_2 = (int *) realloc(y_realloc, 96 * sizeof(int));
    // TEST_ASSERT_MSG(y_realloc != y_realloc_2, "realloc without free space didn't move");
    ASSERT_CORRECT_CONTENT(y_realloc_2, 15);

    free(x);
    free(w);
    free(y_realloc_2);
    return 0;
fail:
    free(x);
    free(y);
    free(z);
    free(w);
    return 1;
}

// Test 9
int monolithic_test_rr_9() {
    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Alignment checks are omitted as with malloc

    // Reallocate b to a larger size
    double *b_realloc = (double *) realloc(b, 256 * sizeof(double));
    if (!b_realloc) {
        goto fail;
    }
    if (b_realloc != b) {
        initialize_double_alloc(b_realloc + 128, 128);
    }

    // Allocate smaller memory to force fragmentation
    MAKE_AUTO_INIT_DOUBLE_ALLOC(c, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(d, 16);

    // Free smaller allocation to create gaps in memory
    free(c);

    // Allocate memory to see if the allocator can handle fragmented gaps
    double *e = (double *) malloc(8 * sizeof(double));
    if (!e) {
        goto fail;
    }
    initialize_double_alloc(e, 8);

    // Reallocate d to a larger size
    double *d_realloc = (double *) realloc(d, 32 * sizeof(double));
    if (!d_realloc) {
        goto fail;
    }
    if (d_realloc != d) {
        initialize_double_alloc(d_realloc + 16, 16);
    }

    // Reallocate shrink b_realloc
    double *b_shrink = (double *) realloc(b_realloc, 64 * sizeof(double));
    if (!b_shrink) {
        goto fail;
    }

    // Test large allocation
    double *f = (double *) malloc(512 * sizeof(double));
    if (!f) {
        goto fail;
    }
    initialize_double_alloc(f, 512);

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    free(a);
    free(b_shrink);
    free(d_realloc);
    free(e);
    free(f);
    return 0;
fail:
    free(a);
    free(b);
    free(c);
    free(d);
    free(e);
    free(f);
    return 1;
}

// Test 10
int monolithic_test_rr_10() {
    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Alignment checks are omitted as with malloc

    // Reallocate b to a larger size
    double *b_realloc = (double *) realloc(b, 256 * sizeof(double));
    if (!b_realloc) {
        goto fail;
    }
    if (b_realloc != b) {
        initialize_double_alloc(b_realloc + 128, 128);
    }

    // Allocate smaller memory to force fragmentation
    MAKE_AUTO_INIT_DOUBLE_ALLOC(c, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(d, 16);

    // Free smaller allocation to create gaps in memory
    free(c);

    // Allocate memory to see if the allocator can handle fragmented gaps
    double *e = (double *) malloc(8 * sizeof(double));
    if (!e) {
        goto fail;
    }
    initialize_double_alloc(e, 8);

    // Reallocate d to a larger size
    double *d_realloc = (double *) realloc(d, 32 * sizeof(double));
    if (!d_realloc) {
        goto fail;
    }
    if (d_realloc != d) {
        initialize_double_alloc(d_realloc + 16, 16);
    }

    // Reallocate shrink b_realloc
    double *b_shrink = (double *) realloc(b_realloc, 64 * sizeof(double));
    if (!b_shrink) {
        goto fail;
    }

    // Test large allocation
    double *f = (double *) malloc(512 * sizeof(double));
    if (!f) {
        goto fail;
    }
    initialize_double_alloc(f, 512);

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    free(a);
    free(b_shrink);
    free(d_realloc);
    free(e);
    free(f);
    return 0;
fail:
    free(a);
    free(b);
    free(c);
    free(d);
    free(e);
    free(f);
    return 1;
}

// Test 11
int monolithic_test_rr_11() {
    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 128);

    // Alignment checks are omitted as with malloc

    // Reallocate b to a larger size
    double *b_realloc = (double *) realloc(b, 256 * sizeof(double));
    if (!b_realloc) {
        goto fail;
    }
    if (b_realloc != b) {
        initialize_double_alloc(b_realloc + 128, 128);
    }

    // Allocate smaller memory to force fragmentation
    MAKE_AUTO_INIT_DOUBLE_ALLOC(c, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(d, 16);

    // Free smaller allocation to create gaps in memory
    free(c);

    // Allocate memory to see if the allocator can handle fragmented gaps
    double *e = (double *) malloc(8 * sizeof(double));
    if (!e) {
        goto fail;
    }
    initialize_double_alloc(e, 8);

    // Reallocate d to a larger size
    double *d_realloc = (double *) realloc(d, 32 * sizeof(double));
    if (!d_realloc) {
        goto fail;
    }
    if (d_realloc != d) {
        initialize_double_alloc(d_realloc + 16, 16);
    }

    // Reallocate shrink b_realloc
    double *b_shrink = (double *) realloc(b_realloc, 64 * sizeof(double));
    if (!b_shrink) {
        goto fail;
    }

    // Test large allocation
    double *f = (double *) malloc(512 * sizeof(double));
    if (!f) {
        goto fail;
    }
    initialize_double_alloc(f, 512);

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(b_shrink, 64, 128);
    ASSERT_DOUBLE_CONTENT(d_realloc, 16); // Only first 16 should remain valid

    free(a);
    free(b_shrink);
    free(d_realloc);
    free(e);
    free(f);
    return 0;
fail:
    free(a);
    free(b);
    free(c);
    free(d);
    free(e);
    free(f);
    return 1;
}

// Test 12: Fragmentation and Operations
int test_fragmentation_and_operations_12() {
    // Fragment the allocator by allocating and freeing memory in a pattern
    MAKE_AUTO_INIT_DOUBLE_ALLOC(a, 4);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(b, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(c, 16);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(d, 32);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(e, 64);

    print_msg_with_line("freeing b");
    free(b);
    print_msg_with_line("freeing d");
    free(d);

    // Allocate smaller memory to force fragmentation
    MAKE_AUTO_INIT_DOUBLE_ALLOC(f, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(g, 16);

    // Reallocate e to a larger size
    print_msg_with_line("re-allocing e");
    double *e_realloc = (double *) realloc(e, 128 * sizeof(double));
    if (!e_realloc) {
        goto fail;
    }
    if (e_realloc != e) {
        initialize_double_alloc(e_realloc + 64, 64);
    }
    ASSERT_DOUBLE_CONTENT(e_realloc, 64);

    // Free smaller allocations to create gaps in memory
    print_msg_with_line("freeing f");
    free(f);
    print_msg_with_line("freeing g");
    free(g);

    // Allocate memory to see if the allocator can handle fragmented gaps
    MAKE_AUTO_INIT_DOUBLE_ALLOC(h, 8);
    MAKE_AUTO_INIT_DOUBLE_ALLOC(u, 16);

    // Allocation reuse check is not applicable with malloc

    // Reallocate c to a larger size
    print_msg_with_line("re-allocing c");
    double *c_realloc = (double *) realloc(c, 32 * sizeof(double));
    if (!c_realloc) {
        goto fail;
    }
    if (c_realloc != c) {
        initialize_double_alloc(c_realloc + 16, 16);
    }

    // Reallocate shrink e_realloc
    print_msg_with_line("shrinking e");
    double *e_shrink = (double *) realloc(e_realloc, 32 * sizeof(double));
    if (!e_shrink) {
        goto fail;
    }
    // Optionally, reinitialize the shrunk memory if needed
    ASSERT_DOUBLE_CONTENT_STARTING_AT(e_shrink, 32, 64);

    // Test large allocation
    double *j = (double *) malloc(512 * sizeof(double));
    if (!j) {
        goto fail;
    }
    initialize_double_alloc(j, 512);

    // Assert contents for valid allocations
    ASSERT_DOUBLE_CONTENT(a, 4);
    ASSERT_DOUBLE_CONTENT_STARTING_AT(e_shrink, 32, 64);
    ASSERT_DOUBLE_CONTENT(c_realloc, 16); // Only first 16 should remain valid

    free(a);
    free(e_shrink);
    free(c_realloc);
    free(h);
    free(u);
    free(j);
    return 0;
fail:
    free(a);
    free(b);
    free(c);
    free(d);
    free(e);
    free(f);
    free(g);
    free(h);
    free(u);
    free(j);
    return 1;
}

// Runner Settings
BEGIN_RUNNER_SETTINGS()
    suppress_test_status = 1;
    print_on_all_passed_this_iter = 0;
    print_on_pass = 0;
    print_pre_run_msg = 0;
    run_all_tests = 1;
    n_test_reps = 10000;
    selected_test = "";
END_RUNNER_SETTINGS()

// Test List
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
