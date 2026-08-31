 

#include "../include/posix_mman.h"
#include "../include/posix_fdtable.h"
#include "../include/posix_errno.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscalls.h>

 

extern void    *os_mmap(uint64_t length, uint64_t flags);
extern int64_t  file_read (int32_t fd, void *buffer, uint64_t len);
extern int64_t  file_write(int32_t fd, const void *buffer, uint64_t len);
extern int64_t  file_seek (int32_t fd, int64_t offset, int32_t whence);
extern int32_t  file_close(int32_t fd);
extern int32_t  file_dup(int32_t fd);
extern uint64_t syscall2(uint64_t number, uint64_t arg1, uint64_t arg2);
extern uint64_t syscall3(uint64_t number, uint64_t arg1,
                         uint64_t arg2, uint64_t arg3);

typedef struct {
    void *address;
    size_t length;
    int32_t backing_fd;
    off_t offset;
    int shared;
    int prot;
} posix_mapping_t;

#define POSIX_MAPPING_MAX 128
static posix_mapping_t g_mappings[POSIX_MAPPING_MAX];

static posix_mapping_t *mapping_find(void *address, size_t length)
{
    for (size_t i = 0; i < POSIX_MAPPING_MAX; ++i) {
        if (g_mappings[i].address == address &&
            g_mappings[i].length == length) {
            return &g_mappings[i];
        }
    }
    return NULL;
}

static posix_mapping_t *mapping_allocate(void)
{
    for (size_t i = 0; i < POSIX_MAPPING_MAX; ++i) {
        if (g_mappings[i].address == NULL) return &g_mappings[i];
    }
    return NULL;
}

static int mapping_flush(posix_mapping_t *mapping)
{
    if (!mapping || !mapping->shared || mapping->backing_fd < 0) return 0;
    int restore_protection = (mapping->prot & PROT_READ) == 0;
    if (restore_protection) {
        int64_t protect = (int64_t)syscall3(
            SYSCALL_MPROTECT, (uint64_t)(uintptr_t)mapping->address,
            (uint64_t)mapping->length, PROT_READ);
        if (protect < 0) {
            posix_set_errno_from_status(protect);
            return -1;
        }
    }
    int64_t saved = file_seek(mapping->backing_fd, 0, 1);
    if (saved < 0 ||
        file_seek(mapping->backing_fd, mapping->offset, 0) < 0) {
        if (restore_protection) {
            (void)syscall3(SYSCALL_MPROTECT,
                           (uint64_t)(uintptr_t)mapping->address,
                           (uint64_t)mapping->length,
                           (uint64_t)(unsigned int)mapping->prot);
        }
        errno = EIO;
        return -1;
    }
    size_t total = 0;
    while (total < mapping->length) {
        int64_t written = file_write(
            mapping->backing_fd, (const uint8_t *)mapping->address + total,
            (uint64_t)(mapping->length - total));
        if (written <= 0) {
            (void)file_seek(mapping->backing_fd, saved, 0);
            if (restore_protection) {
                (void)syscall3(SYSCALL_MPROTECT,
                               (uint64_t)(uintptr_t)mapping->address,
                               (uint64_t)mapping->length,
                               (uint64_t)(unsigned int)mapping->prot);
            }
            errno = EIO;
            return -1;
        }
        total += (size_t)written;
    }
    (void)file_seek(mapping->backing_fd, saved, 0);
    if (restore_protection) {
        (void)syscall3(SYSCALL_MPROTECT,
                       (uint64_t)(uintptr_t)mapping->address,
                       (uint64_t)mapping->length,
                       (uint64_t)(unsigned int)mapping->prot);
    }
    return 0;
}

static void mapping_discard_pages(void *address, size_t length)
{
    (void)syscall2(SYSCALL_MUNMAP,
                   (uint64_t)(uintptr_t)address, (uint64_t)length);
}

 

void *posix_mmap(void *addr, size_t length, int prot, int flags,
                 int fd, off_t offset)
{
    if (length == 0 || (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0 ||
        (flags & (MAP_SHARED | MAP_PRIVATE)) == 0 ||
        (flags & (MAP_SHARED | MAP_PRIVATE)) == (MAP_SHARED | MAP_PRIVATE) ||
        (flags & ~(MAP_SHARED | MAP_PRIVATE | MAP_ANONYMOUS)) != 0 ||
        offset < 0 || (offset & 4095) != 0) {
        errno = EINVAL;
        return MAP_FAILED;
    }
    (void)addr;
    int anonymous = (flags & MAP_ANONYMOUS) != 0;
    if ((!anonymous && fd < 0) || (anonymous && offset != 0)) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    posix_mapping_t *mapping = mapping_allocate();
    if (!mapping) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    void *ptr = os_mmap((uint64_t)length, 0);
    if (!ptr) {
        errno = ENOMEM;
        return MAP_FAILED;
    }

    memset(ptr, 0, length);
    int32_t backing_fd = -1;
    if (!anonymous) {
        int64_t saved = file_seek((int32_t)fd, 0, 1);
        if (saved < 0 || file_seek((int32_t)fd, (int64_t)offset, 0) < 0) {
            mapping_discard_pages(ptr, length);
            errno = EBADF;
            return MAP_FAILED;
        }
        size_t total = 0;
        while (total < length) {
            int64_t count = file_read(
                (int32_t)fd, (uint8_t *)ptr + total,
                (uint64_t)(length - total));
            if (count < 0) {
                (void)file_seek((int32_t)fd, saved, 0);
                mapping_discard_pages(ptr, length);
                posix_set_errno_from_status(count);
                return MAP_FAILED;
            }
            if (count == 0) break;
            total += (size_t)count;
        }
        (void)file_seek((int32_t)fd, saved, 0);
        if ((flags & MAP_SHARED) != 0) {
            backing_fd = file_dup((int32_t)fd);
            if (backing_fd < 0) {
                mapping_discard_pages(ptr, length);
                errno = EMFILE;
                return MAP_FAILED;
            }
        }
    }

    memset(mapping, 0, sizeof(*mapping));
    mapping->address = ptr;
    mapping->length = length;
    mapping->backing_fd = backing_fd;
    mapping->offset = offset;
    mapping->shared = (flags & MAP_SHARED) != 0;
    mapping->prot = PROT_READ | PROT_WRITE;

    if (posix_mprotect(ptr, length, prot) < 0) {
        if (backing_fd >= 0) (void)file_close(backing_fd);
        memset(mapping, 0, sizeof(*mapping));
        (void)syscall2(SYSCALL_MUNMAP,
                       (uint64_t)(uintptr_t)ptr, (uint64_t)length);
        return MAP_FAILED;
    }

    os_errno = 0;
    return ptr;
}

 

int posix_munmap(void *addr, size_t length)
{
    posix_mapping_t *mapping = mapping_find(addr, length);
    if (!mapping) {
        errno = EINVAL;
        return -1;
    }
    if (mapping_flush(mapping) < 0) return -1;
    int64_t result = (int64_t)syscall2(
        SYSCALL_MUNMAP, (uint64_t)(uintptr_t)addr, (uint64_t)length);
    if (result < 0) {
        posix_set_errno_from_status(result);
        return -1;
    }
    if (mapping->backing_fd >= 0) (void)file_close(mapping->backing_fd);
    memset(mapping, 0, sizeof(*mapping));
    os_errno = 0;
    return 0;
}

int posix_msync(void *addr, size_t length, int flags)
{
    if ((flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE)) != 0 ||
        ((flags & MS_ASYNC) != 0 && (flags & MS_SYNC) != 0)) {
        errno = EINVAL;
        return -1;
    }
    posix_mapping_t *mapping = mapping_find(addr, length);
    if (!mapping) {
        errno = ENOMEM;
        return -1;
    }
    return mapping_flush(mapping);
}

 

int posix_mprotect(void *addr, size_t length, int prot)
{
    int64_t result = (int64_t)syscall3(
        SYSCALL_MPROTECT, (uint64_t)(uintptr_t)addr,
        (uint64_t)length, (uint64_t)(unsigned int)prot);
    if (result < 0) {
        posix_set_errno_from_status(result);
        return -1;
    }
    posix_mapping_t *mapping = mapping_find(addr, length);
    if (mapping) mapping->prot = prot;
    os_errno = 0;
    return 0;
}
