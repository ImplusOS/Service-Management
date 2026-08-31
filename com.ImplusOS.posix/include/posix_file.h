#pragma once

#include <stddef.h>
#include <stdint.h>
#include "posix_types.h"
#include "posix_errno.h"
#include "posix_fdtable.h"

#ifndef O_RDONLY
#define O_RDONLY   0x0000
#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0040
#define O_TRUNC    0x0200
#define O_APPEND   0x0400
#define O_NONBLOCK 0x0800
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#ifndef S_IFMT
#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#define DT_REG     8
#define DT_DIR     4
#endif

int     posix_open  (const char *path, int flags, mode_t mode);
int     posix_creat (const char *path, mode_t mode);
ssize_t posix_read  (int fd, void *buf, size_t count);
ssize_t posix_write (int fd, const void *buf, size_t count);
int     posix_close (int fd);
off_t   posix_lseek (int fd, off_t offset, int whence);
int     posix_pipe  (int pipefd[2]);
int     posix_dup   (int oldfd);
int     posix_dup2  (int oldfd, int newfd);
int     posix_stat  (const char *path, struct stat *st);
int     posix_fstat (int fd, struct stat *st);
int     posix_mkdir (const char *path, mode_t mode);
int     posix_unlink(const char *path);

POSIX_DIR      *posix_opendir (const char *path);
struct dirent  *posix_readdir (POSIX_DIR *dirp);
int             posix_closedir(POSIX_DIR *dirp);
