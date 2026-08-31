#pragma once

#include <stddef.h>
#include <stdint.h>
#include "posix_types.h"
#include "posix_errno.h"

#define PROT_NONE  0x00
#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define PROT_EXEC  0x04

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *)-1)

#define MAP_ANON_FD   (-1)
#define MS_ASYNC      0x01
#define MS_SYNC       0x04
#define MS_INVALIDATE 0x02

void *posix_mmap  (void *addr, size_t length, int prot, int flags,
                   int fd, off_t offset);
int   posix_munmap(void *addr, size_t length);
int   posix_mprotect(void *addr, size_t length, int prot);
int   posix_msync(void *addr, size_t length, int flags);
