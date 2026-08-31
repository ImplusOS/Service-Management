 

#include "../include/posix_fdtable.h"
#include <stddef.h>
#include <stdint.h>

 

static posix_fd_entry_t g_fd_table[POSIX_FD_TABLE_SIZE];

 

static posix_fd_entry_t *entry_get(int fd)
{
    if (fd < 0 || fd >= POSIX_FD_TABLE_SIZE) {
        return NULL;
    }
    return &g_fd_table[fd];
}

 

posix_fd_entry_t *posix_fd_entry(int fd)
{
    return entry_get(fd);
}

void posix_fd_open(int fd, int type, int status_flags)
{
    posix_fd_entry_t *e = entry_get(fd);
    if (!e) {
        return;
    }
    e->valid        = 1;
    e->type         = type;
    e->status_flags = status_flags;
    e->fd_flags     = 0;
}

void posix_fd_close(int fd)
{
    posix_fd_entry_t *e = entry_get(fd);
    if (!e) {
        return;
    }
    e->valid        = 0;
    e->type         = POSIX_FD_TYPE_NONE;
    e->status_flags = 0;
    e->fd_flags     = 0;
}

int posix_fd_is_valid(int fd)
{
    posix_fd_entry_t *e = entry_get(fd);
    return (e != NULL) && (e->valid != 0);
}

void posix_fd_dup(int src, int dst)
{
    posix_fd_entry_t *se = entry_get(src);
    posix_fd_entry_t *de = entry_get(dst);
    if (!se || !de) {
        return;
    }
    de->valid        = se->valid;
    de->type         = se->type;
    de->status_flags = se->status_flags;
    de->fd_flags     = 0;
}

int posix_fd_get_flags(int fd)
{
    posix_fd_entry_t *e = entry_get(fd);
    if (!e || !e->valid) {
        errno = EBADF;
        return -1;
    }
    return e->status_flags;
}

int posix_fd_set_flags(int fd, int flags)
{
    posix_fd_entry_t *e = entry_get(fd);
    if (!e || !e->valid) {
        errno = EBADF;
        return -1;
    }
    e->status_flags = flags;
    return 0;
}

int posix_fd_get_fdflags(int fd)
{
    posix_fd_entry_t *e = entry_get(fd);
    if (!e || !e->valid) {
        errno = EBADF;
        return -1;
    }
    return e->fd_flags;
}

int posix_fd_set_fdflags(int fd, int fdflags)
{
    posix_fd_entry_t *e = entry_get(fd);
    if (!e || !e->valid) {
        errno = EBADF;
        return -1;
    }
    e->fd_flags = fdflags;
    return 0;
}
