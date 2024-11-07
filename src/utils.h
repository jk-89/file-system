#ifndef MIMUW_FORK_UTILS_H
#define MIMUW_FORK_UTILS_H

#include <stddef.h>
#include <pthread.h>
#include <stdbool.h>

// Calls malloc() and checks if alocation finished succesfully.
void* safe_malloc(size_t);

// Tries to init mutex, reports error if needed.
void safe_mutex_init(pthread_mutex_t*);

// Tries to destroy mutex, reports error if needed.
void safe_mutex_destroy(pthread_mutex_t*);

// Tries to init condition, reports error if needed.
void safe_cond_init(pthread_cond_t*);

// Tries to destroy condition, reports error if needed.
void safe_cond_destroy(pthread_cond_t*);

// Tries to lock mutex, reports error if needed.
void safe_lock(pthread_mutex_t*);

// Tries to unlock mutex, reports error if needed.
void safe_unlock(pthread_mutex_t*);

// Tries to wait on condition, reports error if needed.
void safe_wait(pthread_cond_t*, pthread_mutex_t*);

// Tries to make signal on condition, reports error if needed.
void safe_signal(pthread_cond_t*);

// Checks if path is equal to root path.
bool is_root(const char*);

#endif //MIMUW_FORK_UTILS_H
