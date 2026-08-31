 

#include "../include/posix_thread.h"
#include "../include/posix_process.h"
#include "../include/posix_time.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

 

extern uint64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2);
extern uint64_t syscall0(uint64_t num);
extern uint64_t syscall1(uint64_t num, uint64_t arg1);
extern uint64_t syscall4(uint64_t num, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4);

#define SYSCALL_THREAD_CREATE 9ULL
#define SYSCALL_THREAD_EXIT   10ULL
#define SYSCALL_THREAD_JOIN   11ULL
#define SYSCALL_THREAD_DETACH 12ULL
#define SYSCALL_PROCESS_YIELD 7ULL
#define SYSCALL_GETTID        158ULL
#define SYSCALL_FUTEX         180ULL

#define FUTEX_WAIT 0ULL
#define FUTEX_WAKE 1ULL

 

#define POSIX_TLS_MAX_KEYS   256
#define POSIX_MAX_THREADS   256

typedef struct {
    pthread_t  tid;
    void      *slots[POSIX_TLS_MAX_KEYS];
    uint64_t   signal_mask;
    uint64_t   signal_pending;
} posix_tls_record_t;

static posix_tls_record_t g_tls_table[POSIX_MAX_THREADS];
static pthread_key_t      g_next_key = 1;
static void (*g_key_destructor[POSIX_TLS_MAX_KEYS])(void *);
static posix_thread_desc_t *g_thread_table[POSIX_MAX_THREADS];
static volatile int g_thread_table_lock;

static void thread_table_lock(void)
{
    while (__sync_lock_test_and_set(&g_thread_table_lock, 1)) {
        (void)syscall0(SYSCALL_PROCESS_YIELD);
    }
}

static void thread_table_unlock(void)
{
    __sync_lock_release(&g_thread_table_lock);
}

static int futex_wait(volatile int *address, int expected,
                      uint64_t timeout_ns)
{
    return (int)(int64_t)syscall4(SYSCALL_FUTEX,
                                  (uint64_t)(uintptr_t)address,
                                  FUTEX_WAIT,
                                  (uint64_t)(uint32_t)expected,
                                  timeout_ns);
}

static void futex_wake(volatile int *address, int count)
{
    (void)syscall4(SYSCALL_FUTEX,
                   (uint64_t)(uintptr_t)address,
                   FUTEX_WAKE,
                   (uint64_t)(uint32_t)count,
                   0);
}

static int thread_table_add(posix_thread_desc_t *desc)
{
    int result = -1;
    thread_table_lock();
    for (int i = 0; i < POSIX_MAX_THREADS; ++i) {
        if (g_thread_table[i] == NULL) {
            g_thread_table[i] = desc;
            result = 0;
            break;
        }
    }
    thread_table_unlock();
    return result;
}

static posix_thread_desc_t *thread_table_find_locked(pthread_t tid)
{
    for (int i = 0; i < POSIX_MAX_THREADS; ++i) {
        if (g_thread_table[i] != NULL &&
            g_thread_table[i]->tid == tid) {
            return g_thread_table[i];
        }
    }
    return NULL;
}

static void thread_table_remove_locked(posix_thread_desc_t *desc)
{
    for (int i = 0; i < POSIX_MAX_THREADS; ++i) {
        if (g_thread_table[i] == desc) {
            g_thread_table[i] = NULL;
            return;
        }
    }
}

static void thread_desc_cleanup(posix_thread_desc_t *desc)
{
    if (desc != NULL &&
        __sync_bool_compare_and_swap(&desc->cleanup_claimed, 0, 1)) {
        free(desc);
    }
}

static posix_tls_record_t *tls_find(pthread_t tid)
{
    for (int i = 0; i < POSIX_MAX_THREADS; i++) {
        if (g_tls_table[i].tid == tid) {
            return &g_tls_table[i];
        }
    }
    return NULL;
}

static posix_tls_record_t *tls_alloc(pthread_t tid)
{
    posix_tls_record_t *r = tls_find(tid);
    if (r) return r;
    for (int i = 0; i < POSIX_MAX_THREADS; i++) {
        if (g_tls_table[i].tid == 0) {
            g_tls_table[i].tid = tid;
            return &g_tls_table[i];
        }
    }
    return NULL;
}

static void tls_cleanup(pthread_t tid)
{
    for (int iteration = 0; iteration < 4; ++iteration) {
        int called = 0;
        for (pthread_key_t key = 1; key < POSIX_TLS_MAX_KEYS; ++key) {
            void *value = NULL;
            void (*destructor)(void *) = NULL;
            thread_table_lock();
            posix_tls_record_t *record = tls_find(tid);
            if (record != NULL && record->slots[key] != NULL) {
                value = record->slots[key];
                record->slots[key] = NULL;
                destructor = g_key_destructor[key];
            }
            thread_table_unlock();
            if (value != NULL && destructor != NULL) {
                destructor(value);
                called = 1;
            }
        }
        if (!called) {
            break;
        }
    }

    thread_table_lock();
    posix_tls_record_t *record = tls_find(tid);
    if (record != NULL) {
        memset(record, 0, sizeof(*record));
    }
    thread_table_unlock();
}

 

 
static void thread_entry(posix_thread_desc_t *desc)
{
    if (!desc) {
        (void)syscall1(SYSCALL_THREAD_EXIT, 0);
        for (;;) {
            (void)syscall0(SYSCALL_PROCESS_YIELD);
        }
    }

    pthread_t self = (pthread_t)syscall0(SYSCALL_GETTID);
    thread_table_lock();
    (void)tls_alloc(self);
    thread_table_unlock();

    void *ret = desc->routine(desc->arg);
    tls_cleanup(self);
    int cleanup = 0;
    thread_table_lock();
    desc->retval = ret;
    __sync_synchronize();
    desc->done = 1;
    cleanup = desc->detached && desc->creator_ready;
    futex_wake(&desc->done, 0x7fffffff);
    thread_table_unlock();

    if (cleanup) {
        thread_desc_cleanup(desc);
    }
    (void)syscall1(SYSCALL_THREAD_EXIT, 0);
    for (;;) {
        (void)syscall0(SYSCALL_PROCESS_YIELD);
    }
}

 

int posix_pthread_create(pthread_t *thread,
                         const pthread_attr_t *attr,
                         void *(*start_routine)(void *),
                         void *arg)
{
    if (!thread || !start_routine) {
        return EINVAL;
    }

    posix_thread_desc_t *desc =
        (posix_thread_desc_t *)malloc(sizeof(posix_thread_desc_t));
    if (!desc) {
        return ENOMEM;
    }

    memset(desc, 0, sizeof(*desc));
    desc->routine      = start_routine;
    desc->arg          = arg;
    desc->done         = 0;
    desc->retval       = NULL;
    desc->detached     = (attr && attr->detached) ?
                          PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
    desc->cancel_state = PTHREAD_CANCEL_ENABLE;
    desc->creator_ready = desc->detached ? 0 : 1;
    desc->cleanup_claimed = 0;
    int detached = desc->detached;

    if (!detached && thread_table_add(desc) < 0) {
        free(desc);
        return EAGAIN;
    }

    uint64_t r = syscall2(SYSCALL_THREAD_CREATE,
                           (uint64_t)(uintptr_t)thread_entry,
                           (uint64_t)(uintptr_t)desc);
    if ((int64_t)r < 0) {
        if (!detached) {
            thread_table_lock();
            thread_table_remove_locked(desc);
            thread_table_unlock();
        }
        free(desc);
        return EAGAIN;
    }

    desc->tid = (pthread_t)r;
    *thread = desc->tid;
    if (detached) {
        (void)syscall1(SYSCALL_THREAD_DETACH, r);
        thread_table_lock();
        desc->creator_ready = 1;
        int done = desc->done;
        thread_table_unlock();
        if (done) {
            thread_desc_cleanup(desc);
        }
    }
    return 0;
}

 

int posix_pthread_join(pthread_t thread, void **retval)
{
    if (!thread || thread == posix_pthread_self()) {
        return EINVAL;
    }

    thread_table_lock();
    posix_thread_desc_t *desc = thread_table_find_locked(thread);
    if (desc == NULL || desc->detached) {
        thread_table_unlock();
        return ESRCH;
    }
    thread_table_unlock();

    while (!desc->done) {
        (void)futex_wait(&desc->done, 0, 0);
    }
    __sync_synchronize();

    int join_result;
    do {
        join_result = (int)(int64_t)syscall1(SYSCALL_THREAD_JOIN,
                                             (uint64_t)thread);
        if (join_result == -EAGAIN) {
            (void)syscall0(SYSCALL_PROCESS_YIELD);
        }
    } while (join_result == -EAGAIN);
    if (join_result < 0) {
        return -join_result;
    }

    if (retval) {
        *retval = desc->retval;
    }
    thread_table_lock();
    thread_table_remove_locked(desc);
    thread_table_unlock();
    thread_desc_cleanup(desc);
    return 0;
}

 

int posix_pthread_detach(pthread_t thread)
{
    if (!thread) {
        return EINVAL;
    }

    thread_table_lock();
    posix_thread_desc_t *desc = thread_table_find_locked(thread);
    if (desc == NULL || desc->detached) {
        thread_table_unlock();
        return ESRCH;
    }
    desc->detached = PTHREAD_CREATE_DETACHED;
    desc->creator_ready = 1;
    thread_table_remove_locked(desc);
    int done = desc->done;
    thread_table_unlock();

    int result = (int)(int64_t)syscall1(SYSCALL_THREAD_DETACH,
                                        (uint64_t)thread);
    if (done) {
        thread_desc_cleanup(desc);
    }
    if (result < 0) {
        return -result;
    }
    return 0;
}

 

pthread_t posix_pthread_self(void)
{
    return (pthread_t)syscall0(SYSCALL_GETTID);
}

static posix_tls_record_t *tls_current(void)
{
    pthread_t self = posix_pthread_self();
    thread_table_lock();
    posix_tls_record_t *record = tls_alloc(self);
    thread_table_unlock();
    return record;
}

uint64_t *posix_pthread_signal_mask_storage(void)
{
    posix_tls_record_t *record = tls_current();
    return record ? &record->signal_mask : NULL;
}

uint64_t *posix_pthread_signal_pending_storage(void)
{
    posix_tls_record_t *record = tls_current();
    return record ? &record->signal_pending : NULL;
}

 

int posix_pthread_equal(pthread_t a, pthread_t b)
{
    return a == b;
}

 

int posix_pthread_cancel(pthread_t thread)
{
    if (!thread) return EINVAL;
    thread_table_lock();
    posix_thread_desc_t *desc = thread_table_find_locked(thread);
    if (desc == NULL) {
        thread_table_unlock();
        return ESRCH;
    }
    desc->done = 1;
    __sync_synchronize();
    futex_wake(&desc->done, 0x7fffffff);
    int cleanup = desc->detached && desc->creator_ready;
    thread_table_unlock();
    if (cleanup) {
        thread_desc_cleanup(desc);
    }
    return 0;
}

 

int posix_pthread_setcancelstate(int state, int *oldstate)
{
    static int s_main_cancel_state = PTHREAD_CANCEL_ENABLE;
    int *cancel_state = &s_main_cancel_state;
    pthread_t self = posix_pthread_self();
    thread_table_lock();
    posix_thread_desc_t *desc = thread_table_find_locked(self);
    if (desc != NULL) {
        cancel_state = &desc->cancel_state;
    }
    if (oldstate) {
        *oldstate = *cancel_state;
    }
    if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE) {
        thread_table_unlock();
        return EINVAL;
    }
    *cancel_state = state;
    thread_table_unlock();
    return 0;
}

 

int posix_pthread_once(pthread_once_t *once_ctrl, void (*init_routine)(void))
{
    if (!once_ctrl || !init_routine) {
        return EINVAL;
    }
    if (__sync_bool_compare_and_swap(&once_ctrl->done, 0, 1)) {
        init_routine();
        __sync_synchronize();
        once_ctrl->done = 2;
        futex_wake(&once_ctrl->done, 0x7fffffff);
    } else {
        while (once_ctrl->done != 2) {
            (void)futex_wait(&once_ctrl->done, 1, 0);
        }
    }
    return 0;
}

 

int posix_pthread_mutex_init(pthread_mutex_t *mutex,
                              const pthread_mutexattr_t *attr)
{
    if (!mutex) return EINVAL;
    mutex->locked = 0;
    mutex->type   = attr ? attr->type : PTHREAD_MUTEX_NORMAL;
    mutex->owner  = 0;
    return 0;
}

int posix_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    if (!mutex) return EINVAL;
    if (mutex->locked != 0) return EBUSY;
    mutex->owner  = 0;
    return 0;
}

int posix_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (!mutex) return EINVAL;
    pthread_t self = posix_pthread_self();

    if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
        if (mutex->locked && mutex->owner == self) {
            mutex->locked++;
            return 0;
        }
    } else if (mutex->type == PTHREAD_MUTEX_ERRORCHECK) {
        if (mutex->locked && mutex->owner == self) {
            return EBUSY;
        }
    }

    for (;;) {
        if (__sync_bool_compare_and_swap(&mutex->locked, 0, 1)) {
            break;
        }
        int observed = mutex->locked;
        (void)futex_wait(&mutex->locked, observed, 0);
    }
    mutex->owner = self;
    return 0;
}

int posix_pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    if (!mutex) return EINVAL;
    pthread_t self = posix_pthread_self();

    if (mutex->type == PTHREAD_MUTEX_RECURSIVE &&
        mutex->locked != 0 && mutex->owner == self) {
        mutex->locked++;
        return 0;
    }
    if (__sync_lock_test_and_set(&mutex->locked, 1)) {
        return EBUSY;
    }
    mutex->owner = self;
    return 0;
}

int posix_pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    if (!mutex) return EINVAL;
    if (mutex->owner != posix_pthread_self()) {
        return EPERM;
    }
    if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->locked > 1) {
        mutex->locked--;
        return 0;
    }
    mutex->owner = 0;
    __sync_lock_release(&mutex->locked);
    futex_wake(&mutex->locked, 1);
    return 0;
}

int posix_pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    if (!attr) return EINVAL;
    attr->pshared = 0;
    attr->type    = PTHREAD_MUTEX_NORMAL;
    return 0;
}

int posix_pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    if (!attr) return EINVAL;
    attr->pshared = 0;
    attr->type    = PTHREAD_MUTEX_NORMAL;
    return 0;
}

int posix_pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    if (!attr ||
        (type != PTHREAD_MUTEX_NORMAL &&
         type != PTHREAD_MUTEX_RECURSIVE &&
         type != PTHREAD_MUTEX_ERRORCHECK)) {
        return EINVAL;
    }
    attr->type = type;
    return 0;
}

 

int posix_pthread_cond_init(pthread_cond_t *cond,
                             const pthread_condattr_t *attr)
{
    (void)attr;
    if (!cond) return EINVAL;
    cond->seq = 0;
    return 0;
}

int posix_pthread_cond_destroy(pthread_cond_t *cond)
{
    if (!cond) return EINVAL;
    cond->seq = 0;
    return 0;
}

int posix_pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    if (!cond || !mutex) return EINVAL;
    unsigned snap = cond->seq;
    posix_pthread_mutex_unlock(mutex);
    while (cond->seq == snap) {
        (void)futex_wait((volatile int *)&cond->seq, (int)snap, 0);
    }
    posix_pthread_mutex_lock(mutex);
    return 0;
}

int posix_pthread_cond_timedwait(pthread_cond_t *cond,
                                  pthread_mutex_t *mutex,
                                  const struct timespec *abstime)
{
    if (!cond || !mutex || !abstime) return EINVAL;
    unsigned snap = cond->seq;
    posix_pthread_mutex_unlock(mutex);
    while (cond->seq == snap) {
        struct timespec now;
        if (posix_clock_gettime(CLOCK_REALTIME, &now) < 0) {
            posix_pthread_mutex_lock(mutex);
            return EIO;
        }
        int64_t remaining_sec =
            (int64_t)abstime->tv_sec - (int64_t)now.tv_sec;
        int64_t remaining_ns =
            remaining_sec * 1000000000LL +
            ((int64_t)abstime->tv_nsec - (int64_t)now.tv_nsec);
        if (remaining_ns <= 0) {
            posix_pthread_mutex_lock(mutex);
            return ETIMEDOUT;
        }
        (void)futex_wait((volatile int *)&cond->seq, (int)snap,
                         (uint64_t)remaining_ns);
    }
    posix_pthread_mutex_lock(mutex);
    return 0;
}

int posix_pthread_cond_signal(pthread_cond_t *cond)
{
    if (!cond) return EINVAL;
    __sync_add_and_fetch(&cond->seq, 1);
    futex_wake((volatile int *)&cond->seq, 1);
    return 0;
}

int posix_pthread_cond_broadcast(pthread_cond_t *cond)
{
    if (!cond) return EINVAL;
    __sync_add_and_fetch(&cond->seq, 1);
    futex_wake((volatile int *)&cond->seq, 0x7fffffff);
    return 0;
}

int posix_pthread_condattr_init(pthread_condattr_t *attr)
{
    if (!attr) return EINVAL;
    attr->pshared = 0;
    return 0;
}

int posix_pthread_condattr_destroy(pthread_condattr_t *attr)
{
    if (!attr) return EINVAL;
    attr->pshared = 0;
    return 0;
}

 

int posix_pthread_key_create(pthread_key_t *key,
                              void (*destructor)(void *))
{
    if (!key) return EINVAL;
    thread_table_lock();
    if (g_next_key >= POSIX_TLS_MAX_KEYS) {
        thread_table_unlock();
        return EAGAIN;
    }
    pthread_key_t k       = g_next_key++;
    g_key_destructor[k]   = destructor;
    *key = k;
    thread_table_unlock();
    return 0;
}

int posix_pthread_key_delete(pthread_key_t key)
{
    if (key == 0 || key >= POSIX_TLS_MAX_KEYS) return EINVAL;
    thread_table_lock();
    g_key_destructor[key] = NULL;
    for (int i = 0; i < POSIX_MAX_THREADS; ++i) {
        g_tls_table[i].slots[key] = NULL;
    }
    thread_table_unlock();
    return 0;
}

void *posix_pthread_getspecific(pthread_key_t key)
{
    if (key == 0 || key >= POSIX_TLS_MAX_KEYS) return NULL;
    pthread_t self = posix_pthread_self();
    thread_table_lock();
    posix_tls_record_t *r = tls_find(self);
    void *value = r ? r->slots[key] : NULL;
    thread_table_unlock();
    return value;
}

int posix_pthread_setspecific(pthread_key_t key, const void *value)
{
    if (key == 0 || key >= POSIX_TLS_MAX_KEYS) return EINVAL;
    pthread_t self = posix_pthread_self();
    thread_table_lock();
    posix_tls_record_t *r = tls_alloc(self);
    if (!r) {
        thread_table_unlock();
        return ENOMEM;
    }
    r->slots[key] = (void *)value;
    thread_table_unlock();
    return 0;
}
