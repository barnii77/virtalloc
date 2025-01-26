#ifndef ALLOC_SETTINGS_H
#define ALLOC_SETTINGS_H

// TODO maybe make the min alloc size and align a per allocator var instead of global?
#ifndef MIN_ALLOCATION_SIZE  // this ifndef is to allow the user to define these in the build system
#define MIN_ALLOCATION_SIZE 64
#endif

#ifndef ALLOCATION_ALIGN  // this ifndef is to allow the user to define these in the build system
#define ALLOCATION_ALIGN ((MIN_ALLOCATION_SIZE) < 64 ? (MIN_ALLOCATION_SIZE) : 64)  // 64 is cache line size
#endif

#define BUCKET_SIZE_GROWTH_FACTOR_DEFAULT (1.5)
#define BUCKET_SIZE_GROWTH_FACTOR_FEW_BUCKET_MODE (1.5*1.5)
#define NUM_BUCKETS_DEFAULT 256
#define NUM_BUCKETS_FEW_BUCKET_MODE 16

#endif
