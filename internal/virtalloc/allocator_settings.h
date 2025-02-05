#ifndef ALLOCATOR_SETTINGS_H
#define ALLOCATOR_SETTINGS_H

#ifndef MIN_LARGE_ALLOCATION_SIZE  // this ifndef is to allow the user to define these in the build system
#define MIN_LARGE_ALLOCATION_SIZE 64
#endif

#ifndef MAX_TINY_ALLOCATION_SIZE  // this ifndef is to allow the user to define these in the build system
#define MAX_TINY_ALLOCATION_SIZE 64
#endif

#ifndef LARGE_ALLOCATION_ALIGN  // this ifndef is to allow the user to define these in the build system
#define LARGE_ALLOCATION_ALIGN ((MIN_LARGE_ALLOCATION_SIZE) < 64 ? (MIN_LARGE_ALLOCATION_SIZE) : 64)  // 64 is cache line size
#endif

#ifndef MIN_NEW_MEM_REQUEST_SIZE  // this ifndef is to allow the user to define these in the build system
#define MIN_NEW_MEM_REQUEST_SIZE (64 * 1024)
#endif

#define EARLY_RELEASE_SIZE_TINY   (   4 * 1024)
#define EARLY_RELEASE_SIZE_SMALL  (  32 * 1024)
#define EARLY_RELEASE_SIZE_NORMAL ( 128 * 1024)
#define EARLY_RELEASE_SIZE_LARGE  (1024 * 1024)

#define GP_META_TYPE_SLOT 1
#define GP_META_TYPE_EARLY_RELEASE_SLOT 2
#define RR_META_TYPE_SLOT 3
#define RR_META_TYPE_LINK 4

#define MIN_SIZE_FOR_SAFETY_PADDING 512

#define STEPS_PER_CHECKSUM_CHECK 16

#define DEFAULT_EXPLORATION_STEPS_BEFORE_RR_OOM 64

#endif
