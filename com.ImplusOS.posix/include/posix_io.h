#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "posix_types.h"
#include "posix_errno.h"

/* Maximum fd value supported; matches the dynamic fd table maximum */
#define FD_SETSIZE 65536

/* Dynamically sized fd_set: allocates enough 64-bit words to cover FD_SETSIZE */
typedef struct {
    uint64_t *bits;
    int capacity;
} fd_set;

/* Initialize an fd_set to zero. Must be called before first use. */
static inline void fd_set_init(fd_set *s)
{
    if (!s) return;
    s->capacity = (FD_SETSIZE + 63) / 64;
    s->bits = (uint64_t *)calloc(s->capacity, sizeof(uint64_t));
}

/* Free resources held by an fd_set */
static inline void fd_set_free(fd_set *s)
{
    if (!s) return;
    free(s->bits);
    s->bits = NULL;
    s->capacity = 0;
}

/* Ensure the fd_set can hold the given fd */
static inline int fd_set_ensure(fd_set *s, int fd)
{
    if (!s || fd < 0) return -1;
    int needed = (fd + 64) / 64;
    if (needed <= s->capacity) return 0;
    int new_cap = s->capacity ? s->capacity * 2 : 32;
    while (new_cap < needed) new_cap *= 2;
    uint64_t *new_bits = (uint64_t *)realloc(s->bits, (size_t)new_cap * sizeof(uint64_t));
    if (!new_bits) return -1;
    if (new_cap > s->capacity) {
        memset(&new_bits[s->capacity], 0, (size_t)(new_cap - s->capacity) * sizeof(uint64_t));
    }
    s->bits = new_bits;
    s->capacity = new_cap;
    return 0;
}

static inline void FD_ZERO(fd_set *s)
{
    if (!s || !s->bits) return;
    for (int i = 0; i < s->capacity; i++) s->bits[i] = 0;
}

static inline void FD_SET(int fd, fd_set *s)
{
    if (!s || fd < 0) return;
    if (fd_set_ensure(s, fd) != 0) return;
    s->bits[fd / 64] |= (1ULL << (fd % 64));
}

static inline void FD_CLR(int fd, fd_set *s)
{
    if (!s || fd < 0 || fd >= s->capacity * 64) return;
    s->bits[fd / 64] &= ~(1ULL << (fd % 64));
}

static inline int FD_ISSET(int fd, const fd_set *s)
{
    if (!s || fd < 0 || fd >= s->capacity * 64) return 0;
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
