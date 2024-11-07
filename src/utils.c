#include "utils.h"
#include "err.h"

#include <stdlib.h>
#include <string.h>

void* safe_malloc(size_t n) {
    void *ptr = malloc(n);
    if (!ptr)
        fatal("Malloc failed.");
    return ptr;
}

void safe_mutex_init(pthread_mutex_t *mutex) {
    int err;
    if ((err = pthread_mutex_init(mutex, 0)) != 0)
        syserr (err, "Mutex init failed.");
}

void safe_mutex_destroy(pthread_mutex_t *mutex) {
    int err;
    if ((err = pthread_mutex_destroy(mutex)) != 0)
        syserr (err, "Mutex destroy failed.");
}

void safe_cond_init(pthread_cond_t *cond) {
    int err;
    if ((err = pthread_cond_init(cond, 0)) != 0)
        syserr (err, "Cond init failed.");
}

void safe_cond_destroy(pthread_cond_t *cond) {
    int err;
    if ((err = pthread_cond_destroy(cond)) != 0)
        syserr (err, "Cond destroy failed.");
}

void safe_lock(pthread_mutex_t *mutex) {
    int err;
    if ((err = pthread_mutex_lock(mutex)) != 0)
        syserr(err, "Lock failed.");
}

void safe_unlock(pthread_mutex_t *mutex) {
    int err;
    if ((err = pthread_mutex_unlock(mutex)) != 0)
        syserr(err, "Unlock failed.");
}

void safe_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    int err;
    if ((err = pthread_cond_wait(cond, mutex)) != 0)
        syserr (err, "Cond wait failed.");
}

void safe_signal(pthread_cond_t *cond) {
    int err;
    if ((err = pthread_cond_signal(cond)) != 0)
        syserr (err, "Cond signal failed.");
}

bool is_root(const char *path) {
    return strcmp(path, "/") == 0;
}
