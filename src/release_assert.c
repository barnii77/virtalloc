#include <stdio.h>
#include <stdlib.h>

void __virtalloc_release_assert_fail(const char *expr, const char *file, int line, const char *func) {
    fprintf(stderr, "Assertion failed: %s, file %s, line %d, function %s\n", expr, file, line, func);
    abort();
}
