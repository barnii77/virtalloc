#ifndef CROSS_PLATFORM_LOCK_H
#define CROSS_PLATFORM_LOCK_H

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct ThreadLock {
    union {
#ifdef _WIN32
        CRITICAL_SECTION win_lock;
#else
        pthread_mutex_t pthread_lock;
#endif
        char padding[64];
    };
} ThreadLock;

void init_lock(ThreadLock *lock);

void destroy_lock(ThreadLock *lock);

void lock(ThreadLock *lock);

void unlock(ThreadLock *lock);

#endif
