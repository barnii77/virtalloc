#include "virtalloc/cross_platform_lock.h"

void init_lock(ThreadLock *lock) {
#ifdef _WIN32
    InitializeCriticalSection(&lock->win_lock);
#else
    pthread_mutex_init(&lock->pthread_lock, NULL);
#endif
}

void destroy_lock(ThreadLock *lock) {
#ifdef _WIN32
    DeleteCriticalSection(&lock->win_lock);
#else
    pthread_mutex_destroy(&lock->pthread_lock);
#endif
}

void lock(ThreadLock *lock) {
#ifdef _WIN32
    EnterCriticalSection(&lock->win_lock);
#else
    pthread_mutex_lock(&lock->pthread_lock);
#endif
}

void unlock(ThreadLock *lock) {
#ifdef _WIN32
    LeaveCriticalSection(&lock->win_lock);
#else
    pthread_mutex_unlock(&lock->pthread_lock);
#endif
}
