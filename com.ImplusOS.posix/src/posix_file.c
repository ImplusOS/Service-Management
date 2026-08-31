 

#include "../include/posix_file.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

 

extern int32_t file_open   (const char *path, uint64_t flags);
extern int32_t file_creat  (const char *path);
extern int64_t file_read   (int32_t fd, void *buffer, uint64_t len);
extern int64_t file_write  (int32_t fd, const void *buffer, uint64_t len);
extern int64_t file_seek   (int32_t fd, int64_t offset, int32_t whence);
extern int32_t file_close  (int32_t fd);
extern int32_t file_mkdir  (const char *path);
extern int32_t file_unlink (const char *path);
extern int32_t file_pipe   (int32_t fds[2]);
extern int32_t file_dup    (int32_t oldfd);
extern int32_t file_dup2   (int32_t oldfd, int32_t newfd);

typedef struct {
    uint32_t size;
    uint8_t  is_dir;
    uint8_t  exists;
} os_file_stat_t;

typedef struct {
    char     name[260];
    uint32_t size;
    uint32_t first_cluster;
    uint8_t  attributes;
} os_file_dirent_t;

extern int32_t file_stat    (const char *path, os_file_stat_t *stat_out);
extern int32_t file_opendir (const char *path);
extern int32_t file_readdir (int32_t dir_handle, os_file_dirent_t *out_entry);
extern int32_t file_closedir(int32_t dir_handle);

 

static uint64_t kernel_open_flags(int posix_flags)
{
    int rw = posix_flags & (O_RDONLY | O_WRONLY | O_RDWR);
    if (rw == O_WRONLY || rw == O_RDWR) {
        return 1ULL;    
    }
    return 0ULL;        
}

 

int posix_open(const char *path, int flags, mode_t mode)
{
    (void)mode;

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    int32_t fd;

    if (flags & O_CREAT) {
        fd = file_creat(path);
        if (fd < 0) {
            posix_set_errno_from_status((int64_t)fd);
            return -1;
        }
         
    } else {
        fd = file_open(path, kernel_open_flags(flags));
        if (fd < 0) {
            posix_set_errno_from_status((int64_t)fd);
            return -1;
        }
         
        if ((flags & O_TRUNC) && (flags & (O_WRONLY | O_RDWR))) {
            file_close(fd);
            file_unlink(path);
            fd = file_creat(path);
            if (fd < 0) {
                posix_set_errno_from_status((int64_t)fd);
                return -1;
            }
        }
    }

     
    if (flags & O_APPEND) {
        if (file_seek(fd, 0, SEEK_END) < 0) {
            file_close(fd);
            errno = EIO;
            return -1;
        }
    }

     
    int type = POSIX_FD_TYPE_FILE;
    int sfl  = flags & (O_APPEND | O_NONBLOCK | O_RDONLY | O_WRONLY | O_RDWR);
    posix_fd_open((int)fd, type, sfl);

    os_errno = 0;
    return (int)fd;
}

 

int posix_creat(const char *path, mode_t mode)
{
    return posix_open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

 

ssize_t posix_read(int fd, void *buf, size_t count)
{
    if (!buf) {
        errno = EINVAL;
        return -1;
    }
    int64_t r = file_read((int32_t)fd, buf, (uint64_t)count);
    if (r < 0) {
        posix_set_errno_from_status(r);
        return -1;
    }
    os_errno = 0;
    return (ssize_t)r;
}

 

ssize_t posix_write(int fd, const void *buf, size_t count)
{
    if (!buf && count > 0) {
        errno = EINVAL;
        return -1;
    }
    int64_t r = file_write((int32_t)fd, buf, (uint64_t)count);
    if (r < 0) {
        posix_set_errno_from_status(r);
        return -1;
    }
    os_errno = 0;
    return (ssize_t)r;
}

 

int posix_close(int fd)
{
    int32_t r = file_close((int32_t)fd);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    posix_fd_close(fd);
    os_errno = 0;
    return 0;
}

 

off_t posix_lseek(int fd, off_t offset, int whence)
{
    int64_t r = file_seek((int32_t)fd, (int64_t)offset, (int32_t)whence);
    if (r < 0) {
        posix_set_errno_from_status(r);
        return (off_t)-1;
    }
    os_errno = 0;
    return (off_t)r;
}

 

int posix_pipe(int pipefd[2])
{
    if (!pipefd) {
        errno = EINVAL;
        return -1;
    }
    int32_t r = file_pipe((int32_t *)pipefd);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    posix_fd_open(pipefd[0], POSIX_FD_TYPE_PIPE, O_RDONLY);
    posix_fd_open(pipefd[1], POSIX_FD_TYPE_PIPE, O_WRONLY);
    os_errno = 0;
    return 0;
}

 

int posix_dup(int oldfd)
{
    int32_t newfd = file_dup((int32_t)oldfd);
    if (newfd < 0) {
        posix_set_errno_from_status((int64_t)newfd);
        return -1;
    }
    posix_fd_dup(oldfd, (int)newfd);
    os_errno = 0;
    return (int)newfd;
}

 

int posix_dup2(int oldfd, int newfd)
{
    if (oldfd == newfd) {
        if (!posix_fd_is_valid(oldfd)) {
            errno = EBADF;
            return -1;
        }
        return newfd;
    }
    int32_t r = file_dup2((int32_t)oldfd, (int32_t)newfd);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    posix_fd_dup(oldfd, newfd);
    os_errno = 0;
    return newfd;
}

 

int posix_stat(const char *path, struct stat *st)
{
    if (!path || !st) {
        errno = EINVAL;
        return -1;
    }
    os_file_stat_t info;
    int32_t r = file_stat(path, &info);
    if (r < 0 || !info.exists) {
        if (r < 0) {
            posix_set_errno_from_status((int64_t)r);
        } else {
            errno = ENOENT;
        }
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_size = (off_t)info.size;
    st->st_mode = info.is_dir ? S_IFDIR : S_IFREG;
    os_errno = 0;
    return 0;
}

 

int posix_fstat(int fd, struct stat *st)
{
    if (!st) {
        errno = EINVAL;
        return -1;
    }
     
    off_t cur = posix_lseek(fd, 0, SEEK_CUR);
    if (cur < 0) {
        return -1;
    }
    off_t end = posix_lseek(fd, 0, SEEK_END);
    if (end < 0) {
        return -1;
    }
    if (posix_lseek(fd, cur, SEEK_SET) < 0) {
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG;
    st->st_size = end;
    os_errno = 0;
    return 0;
}

 

int posix_mkdir(const char *path, mode_t mode)
{
    (void)mode;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    int32_t r = file_mkdir(path);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    os_errno = 0;
    return 0;
}

 

int posix_unlink(const char *path)
{
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    int32_t r = file_unlink(path);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    os_errno = 0;
    return 0;
}

 

POSIX_DIR *posix_opendir(const char *path)
{
    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    int32_t handle = file_opendir(path);
    if (handle < 0) {
        posix_set_errno_from_status((int64_t)handle);
        return NULL;
    }
    POSIX_DIR *dir = (POSIX_DIR *)malloc(sizeof(POSIX_DIR));
    if (!dir) {
        file_closedir(handle);
        errno = ENOMEM;
        return NULL;
    }
    memset(dir, 0, sizeof(*dir));
    dir->handle = (int)handle;
    os_errno = 0;
    return dir;
}

struct dirent *posix_readdir(POSIX_DIR *dirp)
{
    if (!dirp) {
        errno = EINVAL;
        return NULL;
    }
    os_file_dirent_t entry;
    int32_t r = file_readdir((int32_t)dirp->handle, &entry);
    if (r < 0) {
         
        return NULL;
    }
    memset(&dirp->entry, 0, sizeof(dirp->entry));
     
    size_t n = sizeof(dirp->entry.d_name) - 1;
    size_t i;
    for (i = 0; i < n && entry.name[i]; i++) {
        dirp->entry.d_name[i] = entry.name[i];
    }
    dirp->entry.d_name[i] = '\0';
    dirp->entry.d_type = (entry.attributes & 0x10u) ? DT_DIR : DT_REG;
    os_errno = 0;
    return &dirp->entry;
}

int posix_closedir(POSIX_DIR *dirp)
{
    if (!dirp) {
        errno = EINVAL;
        return -1;
    }
    int32_t r = file_closedir((int32_t)dirp->handle);
    free(dirp);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    os_errno = 0;
    return 0;
}
