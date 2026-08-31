 

#include "../include/posix_io.h"
#include "../include/posix_fdtable.h"
#include "../include/posix_errno.h"
#include "../include/posix_file.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/syscalls.h>

 

extern void sleep_ms(uint64_t ms);
extern uint64_t get_uptime_ms(void);
extern uint64_t syscall2(uint64_t number, uint64_t arg1, uint64_t arg2);

 
#ifndef F_GETFD
#define F_GETFD    1
#define F_SETFD    2
#define F_GETFL    3
#define F_SETFL    4
#define F_DUPFD_CLOEXEC 1030
#endif

#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK 0x0800
#endif

 

int posix_fcntl(int fd, int cmd, ...)
{
    va_list ap;
    va_start(ap, cmd);

    posix_fd_entry_t *e = posix_fd_entry(fd);
    if (!e || !e->valid) {
        va_end(ap);
        errno = EBADF;
        return -1;
    }

    int ret = -1;

    switch (cmd) {
        case F_GETFL:
            ret = e->status_flags;
            break;

        case F_SETFL:
            e->status_flags = va_arg(ap, int);
            ret = 0;
            break;

        case F_GETFD:
            ret = e->fd_flags;
            break;

        case F_SETFD:
            e->fd_flags = va_arg(ap, int);
            ret = 0;
            break;

        case F_DUPFD_CLOEXEC: {
            (void)va_arg(ap, int);  
            int newfd = posix_dup(fd);
            if (newfd >= 0) {
                posix_fd_entry_t *ne = posix_fd_entry(newfd);
                if (ne) {
                    ne->fd_flags |= FD_CLOEXEC;
                }
            }
            ret = newfd;
            break;
        }

        default:
            errno = ENOTSUP;
            ret = -1;
            break;
    }

    va_end(ap);
    if (ret >= 0) os_errno = 0;
    return ret;
}

 

int posix_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    va_start(ap, request);

    if (request == FIONBIO) {
        int *param = va_arg(ap, int *);
        va_end(ap);
        if (!param) {
            errno = EINVAL;
            return -1;
        }

        int flags = posix_fcntl(fd, F_GETFL);
        if (flags < 0) {
            return -1;
        }
        if (*param) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        return posix_fcntl(fd, F_SETFL, flags);
    }

    if (request == FIONREAD) {
        int *out = va_arg(ap, int *);
        va_end(ap);
        if (!out) {
            errno = EINVAL;
            return -1;
        }
        uint32_t ready = (uint32_t)syscall2(
            SYSCALL_FD_POLL, (uint64_t)(int64_t)fd, POLLIN);
        *out = (ready & POLLIN) != 0u ? 1 : 0;
        return 0;
    }

    va_end(ap);
    errno = ENOTSUP;
    return -1;
}

 

int posix_select(int nfds, fd_set *readfds, fd_set *writefds,
                 fd_set *exceptfds, struct timeval *timeout)
{
    if (nfds < 0 || nfds > FD_SETSIZE ||
        (timeout && (timeout->tv_sec < 0 || timeout->tv_usec < 0 ||
                     timeout->tv_usec >= 1000000))) {
        errno = EINVAL;
        return -1;
    }

    fd_set requested_read;
    fd_set requested_write;
    fd_set requested_except;
    if (readfds) requested_read = *readfds;
    if (writefds) requested_write = *writefds;
    if (exceptfds) requested_except = *exceptfds;

    for (int fd = 0; fd < nfds && fd < FD_SETSIZE; fd++) {
        if ((readfds && FD_ISSET(fd, &requested_read)) ||
            (writefds && FD_ISSET(fd, &requested_write)) ||
            (exceptfds && FD_ISSET(fd, &requested_except))) {
            if (!posix_fd_is_valid(fd)) {
                errno = EBADF;
                return -1;
            }
        }
    }

    uint64_t timeout_ms = 0u;
    bool finite_timeout = timeout != NULL;
    if (timeout) {
        timeout_ms = (uint64_t)timeout->tv_sec * 1000ULL +
                     ((uint64_t)timeout->tv_usec + 999ULL) / 1000ULL;
    }
    uint64_t deadline = get_uptime_ms() + timeout_ms;
    uint32_t sleep_time = 1u;

    for (;;) {
        if (readfds) FD_ZERO(readfds);
        if (writefds) FD_ZERO(writefds);
        if (exceptfds) FD_ZERO(exceptfds);
        int ready_count = 0;

        for (int fd = 0; fd < nfds; ++fd) {
            uint32_t requested = 0u;
            if (readfds && FD_ISSET(fd, &requested_read)) requested |= POLLIN;
            if (writefds && FD_ISSET(fd, &requested_write)) requested |= POLLOUT;
            if (exceptfds && FD_ISSET(fd, &requested_except))
                requested |= POLLERR | POLLHUP;
            if (requested == 0u) continue;

            uint32_t ready = (uint32_t)syscall2(
                SYSCALL_FD_POLL, (uint64_t)(int64_t)fd, requested);
            bool fd_ready = false;
            if (readfds && FD_ISSET(fd, &requested_read) &&
                (ready & (POLLIN | POLLHUP)) != 0u) {
                FD_SET(fd, readfds);
                fd_ready = true;
            }
            if (writefds && FD_ISSET(fd, &requested_write) &&
                (ready & POLLOUT) != 0u) {
                FD_SET(fd, writefds);
                fd_ready = true;
            }
            if (exceptfds && FD_ISSET(fd, &requested_except) &&
                (ready & (POLLERR | POLLHUP)) != 0u) {
                FD_SET(fd, exceptfds);
                fd_ready = true;
            }
            if (fd_ready) ++ready_count;
        }

        if (ready_count != 0 || (finite_timeout && get_uptime_ms() >= deadline)) {
            os_errno = 0;
            return ready_count;
        }
        
        uint32_t cur_sleep = sleep_time;
        if (finite_timeout) {
            uint64_t now = get_uptime_ms();
            if (now < deadline) {
                uint64_t remaining = deadline - now;
                if ((uint64_t)cur_sleep > remaining) {
                    cur_sleep = (uint32_t)remaining;
                }
            } else {
                cur_sleep = 1u;
            }
        }
        if (cur_sleep < 1u) {
            cur_sleep = 1u;
        }
        sleep_ms(cur_sleep);
        /* Cap the idle back-off well below the old 100 ms so a newly-ready
         * fd is noticed within one short slice (this poll loop cannot be
         * truly event-driven - the scheduler can't resume a blocked syscall
         * mid-call; see the epoll_wait design note in Syscall_Epoll.c). */
        if (sleep_time < 16u) {
            sleep_time++;
        }
    }
}

 

int posix_poll(struct pollfd *fds, nfds_t nfds, int timeout_ms)
{
    if ((fds == NULL && nfds != 0u) || timeout_ms < -1) {
        errno = EINVAL;
        return -1;
    }

    bool finite_timeout = timeout_ms >= 0;
    uint64_t deadline = get_uptime_ms() +
        (finite_timeout ? (uint64_t)timeout_ms : 0u);
    uint32_t sleep_time = 1u;

    for (;;) {
        int ready_count = 0;
        for (nfds_t i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            if (fds[i].fd < 0) continue;
            if (!posix_fd_is_valid(fds[i].fd)) {
                fds[i].revents = POLLNVAL;
            } else {
                uint32_t requested =
                    (uint32_t)(uint16_t)fds[i].events |
                    POLLERR | POLLHUP;
                fds[i].revents = (short)(uint16_t)syscall2(
                    SYSCALL_FD_POLL,
                    (uint64_t)(int64_t)fds[i].fd,
                    requested);
            }
            if (fds[i].revents != 0) ++ready_count;
        }

        if (ready_count != 0 || (finite_timeout && get_uptime_ms() >= deadline)) {
            os_errno = 0;
            return ready_count;
        }

        uint32_t cur_sleep = sleep_time;
        if (finite_timeout) {
            uint64_t now = get_uptime_ms();
            if (now < deadline) {
                uint64_t remaining = deadline - now;
                if ((uint64_t)cur_sleep > remaining) {
                    cur_sleep = (uint32_t)remaining;
                }
            } else {
                cur_sleep = 1u;
            }
        }
        if (cur_sleep < 1u) {
            cur_sleep = 1u;
        }
        sleep_ms(cur_sleep);
        /* Cap the idle back-off well below the old 100 ms so a newly-ready
         * fd is noticed within one short slice (this poll loop cannot be
         * truly event-driven - the scheduler can't resume a blocked syscall
         * mid-call; see the epoll_wait design note in Syscall_Epoll.c). */
        if (sleep_time < 16u) {
            sleep_time++;
        }
    }
}
