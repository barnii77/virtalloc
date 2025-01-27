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

#define BUCKET_SIZE_GROWTH_FACTOR_DEFAULT (1.5)
#define BUCKET_SIZE_GROWTH_FACTOR_FEW_BUCKET_MODE (1.5*1.5)
#define NUM_BUCKETS_DEFAULT 256
#define NUM_BUCKETS_FEW_BUCKET_MODE 16

#define GP_META_TYPE_SLOT 1
#define RR_META_TYPE_SLOT 2
#define RR_META_TYPE_LINK 3

#endif
