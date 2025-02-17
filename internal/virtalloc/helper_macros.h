#ifndef HELPER_MACROS_H
#define HELPER_MACROS_H

#include <assert.h>

#ifdef VIRTALLOC_LOGGING
#define debug_print_enter_fn(is_blocked, name) \
    if (!(is_blocked)) fprintf(stderr, "########### <%s> ###########\n", (name))

#define debug_print_leave_fn(is_blocked, name) \
    if (!(is_blocked)) fprintf(stderr, "########### <// %s> ###########\n", (name))
#else
#define debug_print_enter_fn(is_blocked, name)

#define debug_print_leave_fn(is_blocked, name)
#endif

#ifdef NDEBUG
void __virtalloc_release_assert_fail(const char *expr, const char *file, int line, const char *func);

#define assert_external(expr) \
    ((expr) ? ((void) 0) : __virtalloc_release_assert_fail(#expr, __FILE__, __LINE__, __func__))
#else
#define assert_external(expr) assert(expr)
#endif

#ifdef VIRTALLOC_EXTERNAL_ASSERTS_ONLY
#define assert_internal(expr)
#else
#define assert_internal(expr) assert_external(expr)
#endif

#endif
