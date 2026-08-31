# ImplusOS POSIX Compatibility Layer

## Overview

`Userland/POSIX/` provides a complete POSIX compatibility layer for ImplusOS
user-space programs.  It maps the ImplusOS kernel syscall API onto the standard
POSIX C interface so that third-party C code can be ported with minimal or no
modifications.

---

## Directory Structure

```
Userland/POSIX/
├── include/               # POSIX headers (posix_*.h)
│   ├── posix.h            # Master include (include this one file)
│   ├── posix_types.h      # Canonical type definitions
│   ├── posix_errno.h      # errno codes + OS_STATUS translation helpers
│   ├── posix_fdtable.h    # File-descriptor table
│   ├── posix_file.h       # open/read/write/close/lseek/stat/dir
│   ├── posix_process.h    # fork/exec/wait/kill/getpid
│   ├── posix_signal.h     # signal/sigaction/sigprocmask/raise
│   ├── posix_thread.h     # pthread_create/join/mutex/cond/TLS
│   ├── posix_net.h        # socket/bind/connect/listen/accept/send/recv
│   ├── posix_time.h       # clock_gettime/nanosleep/gettimeofday/gmtime_r
│   ├── posix_mman.h       # mmap/munmap/mprotect
│   └── posix_io.h         # select/poll/ioctl/fcntl
└── src/                   # Implementation files
    ├── posix_fdtable.c
    ├── posix_file.c
    ├── posix_process.c
    ├── posix_signal.c
    ├── posix_thread.c
    ├── posix_net.c
    ├── posix_time.c
    ├── posix_mman.c
    └── posix_io.c
```

The traditional POSIX names (`open`, `read`, `write`, `socket`, …) are
implemented in `libc/src/posix.c` as thin wrappers that call these
`posix_*` functions.  Standard headers in `libc/include/` (e.g. `<unistd.h>`,
`<signal.h>`, `<time.h>`) expose the standard names.

---

## Supported API Surface

### File I/O (`posix_file.c`)

| Function | Notes |
|---|---|
| `open(path, flags, mode)` | O_CREAT, O_TRUNC, O_APPEND, O_RDWR, O_NONBLOCK |
| `creat(path, mode)` | Equivalent to open with O_CREAT\|O_WRONLY\|O_TRUNC |
| `read(fd, buf, count)` | |
| `write(fd, buf, count)` | |
| `close(fd)` | Updates fd table |
| `lseek(fd, offset, whence)` | SEEK_SET / SEEK_CUR / SEEK_END |
| `pipe(pipefd[2])` | |
| `dup(oldfd)` | fd-flags cleared per POSIX |
| `dup2(oldfd, newfd)` | |
| `stat(path, st)` | st_size, st_mode (S_IFREG/S_IFDIR) |
| `fstat(fd, st)` | via seek |
| `mkdir(path, mode)` | mode ignored |
| `unlink(path)` | |
| `opendir / readdir / closedir` | |

### Process Control (`posix_process.c`)

| Function | Notes |
|---|---|
| `getpid()` | |
| `getppid()` | |
| `fork()` | Spawns binary from `argv0`; child gets fresh ELF entry |
| `execve/execv/execvp()` | Spawns + exits; FD_CLOEXEC fds are closed |
| `waitpid(pid, status, opts)` | WNOHANG supported |
| `wait(status)` | = waitpid(-1, …, 0) |
| `kill(pid, sig)` | Self-signal only; cross-process returns ENOSYS |
| `_exit(status)` | |

### Signal Handling (`posix_signal.c` / `libc/src/posix.c`)

| Function | Notes |
|---|---|
| `signal(signum, handler)` | |
| `sigaction(signum, act, oldact)` | SA_RESETHAND, SA_NODEFER, sa_mask |
| `sigprocmask(how, set, oldset)` | SIG_BLOCK / SIG_UNBLOCK / SIG_SETMASK |
| `sigpending(set)` | |
| `raise(sig)` | |
| `sigemptyset / sigfillset / sigaddset / sigdelset / sigismember` | |

Full signal number table (SIGHUP … SIGSYS = 1…31).

### Threads (`posix_thread.c`)

| Function | Notes |
|---|---|
| `pthread_create` | Uses SYSCALL_THREAD_CREATE |
| `pthread_join` | Spin-wait on done flag |
| `pthread_detach` | |
| `pthread_self` | Returns thread descriptor ptr |
| `pthread_cancel` | Sets done flag |
| `pthread_setcancelstate` | |
| `pthread_once` | CAS-based |
| `pthread_mutex_init/lock/trylock/unlock/destroy` | NORMAL / ERRORCHECK / RECURSIVE |
| `pthread_mutexattr_*` | |
| `pthread_cond_init/wait/timedwait/signal/broadcast/destroy` | Sequence-number based |
| `pthread_condattr_*` | |
| `pthread_key_create/delete/getspecific/setspecific` | 64 keys, 128 threads |

### Networking (`posix_net.c`)

| Function | Notes |
|---|---|
| `socket(AF_INET, SOCK_STREAM/DGRAM, 0)` | |
| `bind(sockfd, addr, addrlen)` | IPv4 only |
| `connect(sockfd, addr, addrlen)` | |
| `listen(sockfd, backlog)` | backlog ignored |
| `accept(sockfd, addr, addrlen)` | peer addr zeroed |
| `send / recv` | |
| `sendto / recvfrom` | UDP: auto-connect before send |
| `shutdown(sockfd, how)` | Closes socket |
| `setsockopt / getsockopt` | SO_ERROR supported |
| `getsockname / getpeername` | Returns zeroed addr |
| `htons / ntohs / htonl / ntohl` | |
| `inet_aton / inet_addr / inet_ntoa` | |

### Time (`posix_time.c` / `libc/src/posix.c`)

| Function | Notes |
|---|---|
| `clock_gettime(CLOCK_MONOTONIC)` | get_uptime_ms() |
| `clock_gettime(CLOCK_REALTIME)` | sys_get_rtc_time() → Unix epoch |
| `clock_settime` | ENOTSUP |
| `clock_getres` | 1 ms |
| `nanosleep` | |
| `gettimeofday` | |
| `time` | |
| `gmtime_r / localtime_r` | Full Gregorian calendar |
| `mktime` | With field normalisation |
| `difftime` | |

### Memory Mapping (`posix_mman.c`)

| Function | Notes |
|---|---|
| `mmap(addr, length, prot, flags, fd, offset)` | |
| `munmap` | No-op |
| `mprotect` | No-op |

### I/O Multiplexing (`posix_io.c`)

| Function | Notes |
|---|---|
| `fcntl(fd, F_GETFL/F_SETFL/F_GETFD/F_SETFD/F_DUPFD_CLOEXEC)` | |
| `ioctl(fd, FIONBIO/FIONREAD, …)` | |
| `select(nfds, rfds, wfds, efds, timeout)` | Timeout-sleep + mark all valid fds ready |
| `poll(fds, nfds, timeout_ms)` | Same strategy |

---

## errno Mapping

Kernel `OS_STATUS_*` codes map to POSIX errno as follows:

| OS_STATUS | errno |
|---|---|
| OS_STATUS_NOT_FOUND (-2) | ENOENT (2) |
| OS_STATUS_IO_ERROR (-5) | EIO (5) |
| OS_STATUS_ACCESS_DENIED (-13) | EACCES (13) |
| OS_STATUS_FAULT (-14) | EFAULT (14) |
| OS_STATUS_INVALID_ARG (-22) | EINVAL (22) |
| OS_STATUS_LIMIT_REACHED (-24) | EMFILE (24) |
| OS_STATUS_NOT_SUPPORTED (-95) | ENOTSUP (95) |
| OS_STATUS_INTERNAL (-255) | EIO (5) |

---

## Usage

User programs should include the standard libc headers — no changes needed:

```c
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

int main(void) {
    int fd = open("/data/hello.txt", O_RDONLY);
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf));
    write(1, buf, (size_t)n);
    close(fd);
    return 0;
}
```

To use the `posix_*` prefixed functions directly (e.g. from drivers):

```c
#include "Userland/POSIX/include/posix.h"

int fd = posix_open("/data/log.txt", O_WRONLY | O_CREAT, 0644);
posix_write(fd, "hello\n", 6);
posix_close(fd);
```

---

## Implementation Notes

- **fork()**: Full process fork with address-space cloning. Parent and child have independent physical memory copies. Child's FPU state is cloned from parent.
- **execve()**: Full binary replacement. Closes FD_CLOEXEC fds, resets address space and signal handlers. Properly passes `argv`/`envp` to the new program on the standard ELF user stack.
- **mmap()**: Anonymous and file-backed (MAP_PRIVATE copies bytes, MAP_SHARED flushes to backing fd on munmap/msync). Page-granular prot control via mprotect.
- **munmap() / mprotect()**: Fully functional kernel calls with capability checks.
- **select() / poll()**: FD_POLL-based readiness checks with sleep-poll loop when no fds ready.
- **kill()**: Cross-process signal delivery via TKILL syscall.
- **Threads**: Full create/join/detach/cancel, mutex (normal/errorcheck/recursive), condition variables, pthread_once, TLS (256 keys × 256 threads).
- **pthread_cancel()**: Sends SIGTERM via TKILL and sets done flag for cleanup.

## Limitations

1. **Networking** — AF_INET/IPv4 only; no IPv6, no Unix domain sockets.

---

## Build Integration

The POSIX sources are compiled as part of the main userland build.  No
additional steps are required.  The root `Makefile` includes:

```makefile
USERLAND_CFLAGS += -IUserland/POSIX/include
USERLAND_C_SRCS += Userland/POSIX/src/posix_fdtable.c \
                   Userland/POSIX/src/posix_file.c \
                   ...
```
