 
#pragma once

#include <stdint.h>
#include "posix_types.h"
#include "posix_errno.h"

 

#define PTHREAD_CREATE_JOINABLE  0
#define PTHREAD_CREATE_DETACHED  1

#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_RECURSIVE  1

#define PTHREAD_CANCEL_ENABLE    0
#define PTHREAD_CANCEL_DISABLE   1

 
#define PTHREAD_MUTEX_INITIALIZER  { 0, PTHREAD_MUTEX_NORMAL, 0 }
#define PTHREAD_COND_INITIALIZER   { 0 }
#define PTHREAD_ONCE_INIT          { 0 }

 

 
typedef struct posix_thread_desc {
    pthread_t  tid;
    void    *(*routine)(void *);   
    void     *arg;                 
    void     *retval;              
    volatile int done;             
    int       detached;            
    int       cancel_state;        
    int       creator_ready;
    volatile int cleanup_claimed;
} posix_thread_desc_t;

 

int         posix_pthread_create      (pthread_t *thread,
                                       const pthread_attr_t *attr,
                                       void *(*start_routine)(void *),
                                       void *arg);
int         posix_pthread_join        (pthread_t thread, void **retval);
int         posix_pthread_detach      (pthread_t thread);
pthread_t   posix_pthread_self        (void);
int         posix_pthread_equal       (pthread_t a, pthread_t b);
int         posix_pthread_cancel      (pthread_t thread);
int         posix_pthread_setcancelstate(int state, int *oldstate);
int         posix_pthread_once        (pthread_once_t *once_ctrl,
                                       void (*init_routine)(void));

 

int         posix_pthread_mutex_init    (pthread_mutex_t *mutex,
                                         const pthread_mutexattr_t *attr);
int         posix_pthread_mutex_destroy (pthread_mutex_t *mutex);
int         posix_pthread_mutex_lock    (pthread_mutex_t *mutex);
int         posix_pthread_mutex_trylock (pthread_mutex_t *mutex);
int         posix_pthread_mutex_unlock  (pthread_mutex_t *mutex);
int         posix_pthread_mutexattr_init   (pthread_mutexattr_t *attr);
int         posix_pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int         posix_pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);

 

int         posix_pthread_cond_init      (pthread_cond_t *cond,
                                           const pthread_condattr_t *attr);
int         posix_pthread_cond_destroy   (pthread_cond_t *cond);
int         posix_pthread_cond_wait      (pthread_cond_t *cond,
                                           pthread_mutex_t *mutex);
int         posix_pthread_cond_timedwait (pthread_cond_t *cond,
                                           pthread_mutex_t *mutex,
                                           const struct timespec *abstime);
int         posix_pthread_cond_signal    (pthread_cond_t *cond);
int         posix_pthread_cond_broadcast (pthread_cond_t *cond);
int         posix_pthread_condattr_init  (pthread_condattr_t *attr);
int         posix_pthread_condattr_destroy(pthread_condattr_t *attr);

 

int         posix_pthread_key_create (pthread_key_t *key,
                                       void (*destructor)(void *));
int         posix_pthread_key_delete (pthread_key_t key);
void       *posix_pthread_getspecific(pthread_key_t key);
int         posix_pthread_setspecific(pthread_key_t key, const void *value);

uint64_t   *posix_pthread_signal_mask_storage(void);
uint64_t   *posix_pthread_signal_pending_storage(void);
