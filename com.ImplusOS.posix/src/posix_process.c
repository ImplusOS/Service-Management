 

#include "../include/posix_process.h"
#include "../include/posix_fdtable.h"
#include "../include/posix_errno.h"
#include "../include/posix_signal.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

  

extern int32_t  process_get_current_pid(void);
extern int32_t  process_getppid(void);
extern void     process_exit(int32_t status)  __attribute__((noreturn));
extern int32_t  process_spawn(const char *path);
extern int32_t  process_waitpid(int32_t pid, int32_t *status_out,
                                int32_t options);
extern uint64_t syscall0(uint64_t num);
extern uint64_t syscall1(uint64_t num, uint64_t arg1);
extern uint64_t syscall3(uint64_t num, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3);
#define SYSCALL_FORK    190ULL
#define SYSCALL_EXECVE  191ULL

 

 
static char g_argv0[512];

 

void posix_process_init(const char *argv0)
{
    if (!argv0) {
        g_argv0[0] = '\0';
        return;
    }
    size_t i;
    for (i = 0; argv0[i] && i < sizeof(g_argv0) - 1; i++) {
        g_argv0[i] = argv0[i];
    }
    g_argv0[i] = '\0';
}

 

pid_t posix_getpid(void)
{
    return (pid_t)process_get_current_pid();
}

pid_t posix_getppid(void)
{
    return (pid_t)process_getppid();
}

 

void posix_exit(int status)
{
    process_exit((int32_t)status);
     
    while (1) {}
}

 

pid_t posix_fork(void)
{
    int64_t child_pid = (int64_t)syscall0(SYSCALL_FORK);
    if (child_pid < 0) {
        posix_set_errno_from_status(child_pid);
        return -1;
    }
    os_errno = 0;
    return (pid_t)child_pid;
}

 

 
static void close_cloexec_fds(void)
{
    extern int32_t file_close(int32_t fd);
    extern int posix_fd_get_fdflags(int fd);
    extern void posix_fd_close(int fd);

    for (int fd = 0; fd < POSIX_FD_TABLE_SIZE; fd++) {
        posix_fd_entry_t *e = posix_fd_entry(fd);
        if (e && e->valid && (e->fd_flags & POSIX_FDF_CLOEXEC)) {
            file_close((int32_t)fd);
            posix_fd_close(fd);
        }
    }
}

int posix_execve(const char *path, char *const argv[], char *const envp[])
{
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    close_cloexec_fds();

    int64_t r = (int64_t)syscall3(SYSCALL_EXECVE,
                                   (uint64_t)(uintptr_t)path,
                                   (uint64_t)(uintptr_t)argv,
                                   (uint64_t)(uintptr_t)envp);
    if (r < 0) {
        posix_set_errno_from_status(r);
        return -1;
    }
     
    process_exit(0);
    return -1;
}

int posix_execv(const char *path, char *const argv[])
{
    return posix_execve(path, argv, NULL);
}

int posix_execvp(const char *file, char *const argv[])
{
     
    return posix_execve(file, argv, NULL);
}

 

pid_t posix_waitpid(pid_t pid, int *status, int options)
{
    int32_t st = 0;
    int32_t r  = process_waitpid((int32_t)pid, &st, (int32_t)options);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    if (status) {
        *status = (int)st;
    }
    os_errno = 0;
    return (pid_t)r;
}

pid_t posix_wait(int *status)
{
    return posix_waitpid(-1, status, 0);
}

 

 
extern uint64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2);
#define SYSCALL_TKILL 186ULL

int posix_kill(pid_t pid, int sig)
{
    if (pid <= 0 || sig < 0 || sig >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    int64_t r = (int64_t)syscall2(SYSCALL_TKILL,
                                   (uint64_t)(int64_t)pid,
                                   (uint64_t)(uint32_t)sig);
    if (r < 0) {
        posix_set_errno_from_status(r);
        return -1;
    }
    os_errno = 0;
    return 0;
}
