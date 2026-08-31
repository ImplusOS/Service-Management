#pragma once

#include <stdint.h>

#define EPERM           1
#define ENOENT          2
#define ESRCH           3
#define EINTR           4
#define EIO             5
#define ENXIO           6
#define E2BIG           7
#define ENOEXEC         8
#define EBADF           9
#define ECHILD          10
#define EAGAIN          11
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define EBUSY           16
#define EEXIST          17
#define EXDEV           18
#define ENODEV          19
#define ENOTDIR         20
#define EISDIR          21
#define EINVAL          22
#define ENFILE          23
#define EMFILE          24
#define ENOTTY          25
#define EFBIG           27
#define ENOSPC          28
#define ESPIPE          29
#define EROFS           30
#define EPIPE           32
#define ERANGE          34
#define ENAMETOOLONG    36
#define ENOSYS          38
#define ENOTEMPTY       39
#define ENOTSUP         95
#define EAFNOSUPPORT    97
#define EADDRINUSE      98
#define EADDRNOTAVAIL   99
#define ENETDOWN        100
#define ENETUNREACH     101
#define ENETRESET       102
#define ECONNABORTED    103
#define ECONNRESET      104
#define ENOBUFS         105
#define EISCONN         106
#define ENOTCONN        107
#define ETIMEDOUT       110
#define ECONNREFUSED    111
#define EALREADY        114
#define EINPROGRESS     115
#define EDESTADDRREQ    89
#define EPROTONOSUPPORT 93

#define OS_STATUS_OK              0LL
#define OS_STATUS_NOT_FOUND      -2LL
#define OS_STATUS_IO_ERROR       -5LL
#define OS_STATUS_ACCESS_DENIED  -13LL
#define OS_STATUS_FAULT          -14LL
#define OS_STATUS_INVALID_ARG    -22LL
#define OS_STATUS_LIMIT_REACHED  -24LL
#define OS_STATUS_NOT_SUPPORTED  -95LL
#define OS_STATUS_INTERNAL       -255LL

extern int os_errno;

#ifndef errno
#define errno os_errno
#endif

static inline int posix_translate_status(int64_t status)
{
    if (status >= 0) {
        return 0;
    }
    switch (status) {
        case OS_STATUS_NOT_FOUND:      return ENOENT;
        case OS_STATUS_IO_ERROR:       return EIO;
        case OS_STATUS_ACCESS_DENIED:  return EACCES;
        case OS_STATUS_FAULT:          return EFAULT;
        case OS_STATUS_INVALID_ARG:    return EINVAL;
        case OS_STATUS_LIMIT_REACHED:  return EMFILE;
        case OS_STATUS_NOT_SUPPORTED:  return ENOTSUP;
        case OS_STATUS_INTERNAL:       return EIO;
        default:
            if (status <= -4096LL) {
                return EIO;
            }
            return (int)(-status);
    }
}

static inline void posix_set_errno_from_status(int64_t status)
{
    if (status >= 0) {
        os_errno = 0;
    } else {
        os_errno = posix_translate_status(status);
    }
}

static inline int posix_rc32(int32_t raw)
{
    if (raw < 0) {
        posix_set_errno_from_status((int64_t)raw);
        return -1;
    }
    os_errno = 0;
    return (int)raw;
}

static inline long posix_rc64(int64_t raw)
{
    if (raw < 0) {
        posix_set_errno_from_status(raw);
        return -1;
    }
    os_errno = 0;
    return (long)raw;
}
