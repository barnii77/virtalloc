cmake_minimum_required(VERSION 3.20)
project(virtalloc C)

set(CMAKE_C_STANDARD 17)

set(VIRTALLOC_LIBRARY_SOURCES
        src/allocator_impl.c
        src/virtalloc.c
        src/checksum.c
        src/thread_sync.c
        src/allocator_utils.c
        src/cross_platform_lock.c
        src/math_utils.c
        src/release_assert.c

        internal/virtalloc/allocator.h
        internal/virtalloc/gp_memory_slot_meta.h
        internal/virtalloc/allocator_settings.h
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

option(VIRTALLOC_SSE4 "If set, passes -msse4 to the compiler" OFF)
if (VIRTALLOC_SSE4)
    include(CheckCCompilerFlag)
    CHECK_C_COMPILER_FLAG("-msse4" COMPILER_SUPPORTS_MSSE4)
    set(VIRTALLOC_CMAKE_C_FLAGS_NO_MSSE4 ${CMAKE_C_FLAGS})
    if (COMPILER_SUPPORTS_MSSE4)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -msse4")
    else()
        message(WARNING "Compiler does not seem to support -msse4, compiling with SSE 4 disabled...")
    endif ()
endif ()

add_library(virtalloc STATIC ${VIRTALLOC_LIBRARY_SOURCES})
target_include_directories(virtalloc AFTER PUBLIC include/)
target_include_directories(virtalloc AFTER PRIVATE internal/)

# register a test suite
function(add_test_suite name files)
    add_executable(${name}
            ${files}
            tests/testing.h
            tests/test_utils.h
    )
    target_include_directories(${name} AFTER PRIVATE include/ internal/)
    target_link_libraries(${name} PRIVATE virtalloc)
endfunction()

# define existing test suites
add_test_suite(tests tests/tests.c)
add_test_suite(reference_tests tests/reference_tests.c)

add_test_suite(json_parser_bench tests/json_parser/json_parser_virtalloc.c)
add_test_suite(json_parser_reference tests/json_parser/json_parser.c)
target_compile_definitions(json_parser_bench PRIVATE TEST_JSON_PARSER)
target_compile_definitions(json_parser_reference PRIVATE TEST_JSON_PARSER)

if (VIRTALLOC_SSE4)
    # avoid potential side effects
    set(CMAKE_C_FLAGS ${VIRTALLOC_CMAKE_C_FLAGS_NO_MSSE4})
endif ()

if (VIRTALLOC_NDEBUG)
    remove_definitions(-DNDEBUG)
endif()
