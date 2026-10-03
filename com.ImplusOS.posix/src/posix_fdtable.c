#include "../include/posix_fdtable.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static posix_fd_entry_t *g_fd_table = NULL;
static int g_fd_capacity = 0;

static posix_fd_entry_t *entry_get(int fd)
{
    if (fd < 0) {
        return NULL;
    }
    if (fd >= g_fd_capacity) {
        return NULL;
    }
    return &g_fd_table[fd];
}

static posix_fd_entry_t *entry_grow(int fd)
{
    if (fd < 0 || fd >= POSIX_FD_TABLE_MAX) {
        return NULL;
    }
    int new_capacity = g_fd_capacity ? g_fd_capacity * 2 : POSIX_FD_TABLE_INITIAL;
    while (new_capacity <= fd && new_capacity < POSIX_FD_TABLE_MAX) {
        new_capacity *= 2;
    }
    if (new_capacity > POSIX_FD_TABLE_MAX) {
        new_capacity = POSIX_FD_TABLE_MAX;
    }

    posix_fd_entry_t *new_entries = (posix_fd_entry_t *)realloc(g_fd_table,
        (size_t)new_capacity * sizeof(posix_fd_entry_t));
    if (!new_entries) {
        return NULL;
    }
    /* Zero the newly allocated entries */
    if (new_capacity > g_fd_capacity) {
        memset(&new_entries[g_fd_capacity], 0,
               (size_t)(new_capacity - g_fd_capacity) * sizeof(posix_fd_entry_t));
    }
    g_fd_table = new_entries;
    g_fd_capacity = new_capacity;
    return &g_fd_table[fd];
}

posix_fd_entry_t *posix_fd_entry(int fd)
{
    posix_fd_entry_t *e = NULL;
    if (fd < 0) {
        return NULL;
    }
    if (fd < g_fd_capacity) {
        e = &g_fd_table[fd];
    } else {
        e = entry_grow(fd);
    }
    return e;
}

posix_fd_entry_t *posix_fd_entry_grow(int fd)
{
    return entry_grow(fd);
}

void posix_fd_open(int fd, int type, int status_flags)
{
    posix_fd_entry_t *e = posix_fd_entry(fd);
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
    posix_fd_entry_t *de = posix_fd_entry(dst);
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