#pragma once

#include <stddef.h>
#include <stdint.h>
#include "posix_types.h"
#include "posix_errno.h"

#define FD_SETSIZE 1024

typedef struct {
    uint64_t bits[FD_SETSIZE / 64];
} fd_set;

static inline void FD_ZERO(fd_set *s)
{
    if (!s) return;
    for (int i = 0; i < (int)(FD_SETSIZE / 64); i++) s->bits[i] = 0;
}

static inline void FD_SET(int fd, fd_set *s)
{
    if (!s || fd < 0 || fd >= FD_SETSIZE) return;
    s->bits[fd / 64] |= (1ULL << (fd % 64));
}

static inline void FD_CLR(int fd, fd_set *s)
{
    if (!s || fd < 0 || fd >= FD_SETSIZE) return;
    s->bits[fd / 64] &= ~(1ULL << (fd % 64));
}

static inline int FD_ISSET(int fd, const fd_set *s)
{
    if (!s || fd < 0 || fd >= FD_SETSIZE) return 0;
    return (s->bits[fd / 64] >> (fd % 64)) & 1;
}

#define POLLIN   0x0001
#define POLLPRI  0x0002
#define POLLOUT  0x0004
#define POLLERR  0x0008
#define POLLHUP  0x0010
#define POLLNVAL 0x0020

#define FIONBIO  0x5421
#define FIONREAD 0x541B

int posix_select(int nfds, fd_set *readfds, fd_set *writefds,
                 fd_set *exceptfds, struct timeval *timeout);

int posix_poll(struct pollfd *fds, nfds_t nfds, int timeout_ms);

int posix_ioctl(int fd, unsigned long request, ...);

int posix_fcntl(int fd, int cmd, ...);
