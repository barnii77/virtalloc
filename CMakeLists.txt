cmake_minimum_required(VERSION 3.20)
project(virtalloc C)

set(CMAKE_C_STANDARD 17)

set(VIRTALLOC_LIBRARY_SOURCES
        src/allocator_impl.c
        src/virtalloc.c
        src/checksum.c
        src/thread_sync.c
        src/fast_hash.c
        src/allocator_utils.c
        src/cross_platform_lock.c
        src/math_utils.c
        src/release_assert.c

        internal/virtalloc/allocator.h
        internal/virtalloc/gp_memory_slot_meta.h
        internal/virtalloc/checksum.h
        internal/virtalloc/allocator_settings.h
        internal/virtalloc/fast_hash.h
        internal/virtalloc/allocator_impl.h
        internal/virtalloc/allocator_utils.h
        internal/virtalloc/cross_platform_lock.h
        internal/virtalloc/math_utils.h
        internal/virtalloc/small_rr_memory_slot_meta.h
        internal/virtalloc/helper_macros.h

        include/virtalloc.h
)

option(VIRTALLOC_LOGGING "enables logging in the library and tests which is really useful for debugging" OFF)
if (VIRTALLOC_LOGGING)
    add_compile_definitions(VIRTALLOC_LOGGING)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(VIRTALLOC_EXTERNAL_ASSERTS_ONLY_DEFAULT ON)
else ()
    set(VIRTALLOC_EXTERNAL_ASSERTS_ONLY_DEFAULT OFF)
endif ()

option(VIRTALLOC_EXTERNAL_ASSERTS_ONLY "disables internal asserts in the library, leaving asserts checking the user did not mess up but stripping away asserts checking that virtalloc didn't screw up" ${VIRTALLOC_EXTERNAL_ASSERTS_ONLY_DEFAULT})
if (VIRTALLOC_EXTERNAL_ASSERTS_ONLY)
    add_compile_definitions(VIRTALLOC_EXTERNAL_ASSERTS_ONLY)
endif ()

option(VIRTALLOC_NDEBUG "disables internal asserts in the library, leaving asserts checking the user did not mess up but stripping away asserts checking that virtalloc didn't screw up" OFF)
if (VIRTALLOC_NDEBUG)
    add_compile_definitions(NDEBUG)
endif ()

add_library(virtalloc STATIC ${VIRTALLOC_LIBRARY_SOURCES})
target_include_directories(virtalloc AFTER PUBLIC include/)
target_include_directories(virtalloc AFTER PRIVATE internal/)

# register a test suite
function(add_test_suite name file)
    add_executable(${name}
            ${file}
            tests/testing.h
    )
    target_include_directories(${name} AFTER PRIVATE include/ internal/)
    target_link_libraries(${name} PRIVATE virtalloc)
endfunction()

# define existing test suites
add_test_suite(tests tests/tests.c)
