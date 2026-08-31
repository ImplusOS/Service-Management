 
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef long          ssize_t;
typedef long          off_t;
typedef int           pid_t;
typedef unsigned int  mode_t;
typedef unsigned long nfds_t;
typedef unsigned int  socklen_t;
typedef uint16_t      sa_family_t;
typedef uint16_t      in_port_t;
typedef uint32_t      in_addr_t;
typedef long          suseconds_t;
typedef int64_t       time_t;
typedef int           clockid_t;
typedef int           sig_atomic_t;

typedef uintptr_t pthread_t;

typedef struct {
    volatile int locked;
    int          type;
    pthread_t    owner;
} pthread_mutex_t;

typedef struct {
    volatile unsigned seq;
} pthread_cond_t;

typedef struct { int detached; }              pthread_attr_t;
typedef struct { int pshared; int type; }     pthread_mutexattr_t;
typedef struct { int pshared; }               pthread_condattr_t;
typedef struct { volatile int done; }         pthread_once_t;
typedef unsigned int                          pthread_key_t;

 

typedef uint64_t sigset_t;    
typedef void (*sighandler_t)(int);

 

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

struct timeval {
    time_t    tv_sec;
    suseconds_t tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

 

struct dirent {
    char          d_name[260];
    unsigned char d_type;
};

typedef struct _POSIX_DIR {
    int           handle;
    struct dirent entry;
} POSIX_DIR;

 

struct stat {
    mode_t st_mode;
    off_t  st_size;
};

 

struct sockaddr {
    sa_family_t sa_family;
    char        sa_data[14];
};

struct in_addr {
    in_addr_t s_addr;
};

struct sockaddr_in {
    sa_family_t    sin_family;
    in_port_t      sin_port;
    struct in_addr sin_addr;
    char           sin_zero[8];
};

 

struct pollfd {
    int   fd;
    short events;
    short revents;
};

 

struct sigaction {
    sighandler_t sa_handler;
    sigset_t     sa_mask;
    int          sa_flags;
};
