#pragma once

#include <stdint.h>
#include "posix_errno.h"

#define POSIX_FD_TABLE_SIZE  1024

#define POSIX_SFL_APPEND    0x0400
#define POSIX_SFL_NONBLOCK  0x0800
#define POSIX_SFL_RDONLY    0x0000
#define POSIX_SFL_WRONLY    0x0001
#define POSIX_SFL_RDWR      0x0002

#define POSIX_FDF_CLOEXEC   0x0001

#define POSIX_FD_TYPE_NONE     0
#define POSIX_FD_TYPE_FILE     1
#define POSIX_FD_TYPE_DIR      2
#define POSIX_FD_TYPE_PIPE     3
#define POSIX_FD_TYPE_SOCKET   4

typedef struct {
    int      valid;
    int      type;
    int      status_flags;
    int      fd_flags;
} posix_fd_entry_t;

posix_fd_entry_t *posix_fd_entry(int fd);
void posix_fd_open(int fd, int type, int status_flags);
void posix_fd_close(int fd);
int posix_fd_is_valid(int fd);
void posix_fd_dup(int src, int dst);
int posix_fd_get_flags(int fd);
int posix_fd_set_flags(int fd, int flags);
int posix_fd_get_fdflags(int fd);
int posix_fd_set_fdflags(int fd, int fdflags);
