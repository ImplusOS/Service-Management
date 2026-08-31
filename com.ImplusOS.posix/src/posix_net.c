#include "../include/posix_net.h"
#include "../include/posix_fdtable.h"
#include "../include/posix_errno.h"
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern int32_t socket_create (int32_t type);
extern int32_t socket_connect(int32_t sockfd, uint32_t ip, uint16_t port);
extern int32_t socket_bind   (int32_t sockfd, uint16_t port);
extern int32_t socket_listen (int32_t sockfd);
extern int32_t socket_listen_with_backlog(int32_t sockfd, int32_t backlog);
extern int32_t socket_accept (int32_t sockfd);
extern int32_t socket_send   (int32_t sockfd, const void *data, uint32_t len);
extern int32_t socket_recv   (int32_t sockfd, void *buf, uint32_t buf_len);
extern int32_t socket_close  (int32_t sockfd);
extern int32_t socket_set_option(int32_t sockfd, int32_t level,
                                 int32_t option, int32_t value);
extern int32_t socket_get_option(int32_t sockfd, int32_t level,
                                 int32_t option, int32_t *value_out);
extern int32_t socket_shutdown(int32_t sockfd, int32_t how);
extern void sleep_ms(uint64_t milliseconds);
extern uint64_t get_uptime_ms(void);

typedef struct {
    uint32_t local_ip;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t state;
    int32_t error;
} socket_info_t;

extern int32_t socket_get_info(int32_t sockfd, socket_info_t *info_out);

static int socket_address(int sockfd, int peer,
                          struct sockaddr *addr, socklen_t *addrlen)
{
    if (!addr || !addrlen || *addrlen < sizeof(struct sockaddr_in)) {
        errno = EINVAL;
        return -1;
    }
    socket_info_t info;
    int32_t result = socket_get_info((int32_t)sockfd, &info);
    if (result < 0) {
        posix_set_errno_from_status(result);
        return -1;
    }
    if (peer && info.remote_port == 0u) {
        errno = ENOTCONN;
        return -1;
    }
    struct sockaddr_in value;
    memset(&value, 0, sizeof(value));
    value.sin_family = AF_INET;
    value.sin_addr.s_addr = posix_htonl(peer ? info.remote_ip : info.local_ip);
    value.sin_port = posix_htons(peer ? info.remote_port : info.local_port);
    memcpy(addr, &value, sizeof(value));
    *addrlen = (socklen_t)sizeof(value);
    return 0;
}

uint16_t posix_htons(uint16_t h)
{
    return (uint16_t)((h << 8) | (h >> 8));
}

uint16_t posix_ntohs(uint16_t n)
{
    return posix_htons(n);
}

uint32_t posix_htonl(uint32_t h)
{
    return ((h & 0x000000FFu) << 24) |
           ((h & 0x0000FF00u) <<  8) |
           ((h & 0x00FF0000u) >>  8) |
           ((h & 0xFF000000u) >> 24);
}

uint32_t posix_ntohl(uint32_t n)
{
    return posix_htonl(n);
}

int posix_inet_aton(const char *cp, struct in_addr *inp)
{
    if (!cp || !inp) {
        errno = EINVAL;
        return 0;
    }

    uint32_t parts[4];
    int      npart = 0;
    const char *p  = cp;

    while (npart < 4) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
        uint32_t v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (uint32_t)(*p - '0');
            p++;
        }
        if (v > 255) {
            return 0;
        }
        parts[npart++] = v;
        if (npart < 4) {
            if (*p != '.') return 0;
            p++;
        }
    }

    if (*p != '\0') {
        return 0;
    }

    inp->s_addr = posix_htonl(
        (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]);
    return 1;
}

in_addr_t posix_inet_addr(const char *cp)
{
    struct in_addr addr;
    if (!posix_inet_aton(cp, &addr)) {
        return (in_addr_t)0xFFFFFFFFu;
    }
    return addr.s_addr;
}

char *posix_inet_ntoa(struct in_addr in)
{
    static char buf[16];
    uint32_t v = posix_ntohl(in.s_addr);
    uint8_t  a = (uint8_t)((v >> 24) & 0xFF);
    uint8_t  b = (uint8_t)((v >> 16) & 0xFF);
    uint8_t  c = (uint8_t)((v >>  8) & 0xFF);
    uint8_t  d = (uint8_t)( v        & 0xFF);
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             (unsigned)a, (unsigned)b, (unsigned)c, (unsigned)d);
    return buf;
}

 

int posix_socket(int domain, int type, int protocol)
{
    if (domain != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }

    int status_flags = 0;
    int fd_flags = 0;
    if ((type & SOCK_NONBLOCK) != 0) {
        status_flags |= POSIX_SFL_NONBLOCK;
        type &= ~SOCK_NONBLOCK;
    }
    if ((type & SOCK_CLOEXEC) != 0) {
        fd_flags |= POSIX_FDF_CLOEXEC;
        type &= ~SOCK_CLOEXEC;
    }

    if (type != SOCK_STREAM || (protocol != 0 && protocol != IPPROTO_TCP)) {
        errno = EPROTONOSUPPORT;
        return -1;
    }
    int32_t fd = socket_create((int32_t)type);
    if (fd < 0) {
        posix_set_errno_from_status((int64_t)fd);
        return -1;
    }
    posix_fd_open((int)fd, POSIX_FD_TYPE_SOCKET, status_flags);
    if (fd_flags != 0) {
        (void)posix_fd_set_fdflags((int)fd, fd_flags);
    }
    os_errno = 0;
    return (int)fd;
}

 

int posix_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (!addr || addrlen < sizeof(struct sockaddr_in)) {
        errno = EINVAL;
        return -1;
    }
    const struct sockaddr_in *in_addr = (const struct sockaddr_in *)addr;
    if (in_addr->sin_family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    uint16_t port = posix_ntohs(in_addr->sin_port);
    int32_t r = socket_bind((int32_t)sockfd, port);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    os_errno = 0;
    return 0;
}

 

int posix_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    if (!addr || addrlen < sizeof(struct sockaddr_in)) {
        errno = EINVAL;
        return -1;
    }
    const struct sockaddr_in *in_addr = (const struct sockaddr_in *)addr;
    if (in_addr->sin_family != AF_INET) {
        errno = EINVAL;
        return -1;
    }
    uint32_t ip   = posix_ntohl(in_addr->sin_addr.s_addr);
    uint16_t port = posix_ntohs(in_addr->sin_port);

    posix_fd_entry_t *entry = posix_fd_entry(sockfd);
    if (!entry || !entry->valid || entry->type != POSIX_FD_TYPE_SOCKET) {
        errno = EBADF;
        return -1;
    }
    bool nonblocking = (entry->status_flags & POSIX_SFL_NONBLOCK) != 0;

    int32_t r = socket_connect((int32_t)sockfd, ip, port);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    socket_info_t info;
    if (socket_get_info(sockfd, &info) < 0) {
        errno = EIO;
        return -1;
    }
    if (info.state == 4u) {
        os_errno = 0;
        return 0;
    }
    if (nonblocking) {
        errno = EINPROGRESS;
        return -1;
    }

    uint64_t deadline = get_uptime_ms() + 10000u;
    uint32_t sleep_time = 10u;
    for (;;) {
        if (socket_get_info(sockfd, &info) < 0) {
            errno = EIO;
            return -1;
        }
        if (info.state == 4u) break;
        if (info.state == 0u || get_uptime_ms() >= deadline) {
            errno = ETIMEDOUT;
            return -1;
        }
        uint64_t remaining = deadline - get_uptime_ms();
        uint32_t cur_sleep = (remaining < (uint64_t)sleep_time) ? (uint32_t)remaining : sleep_time;
        if (cur_sleep < 1u) { cur_sleep = 1u; }
        sleep_ms(cur_sleep);
        if (sleep_time < 100u) { sleep_time += 5u; }
    }
    os_errno = 0;
    return 0;
}

 

int posix_listen(int sockfd, int backlog)
{
    if (backlog < 0) {
        errno = EINVAL;
        return -1;
    }
    int32_t r = socket_listen_with_backlog((int32_t)sockfd, backlog);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    os_errno = 0;
    return 0;
}

 

int posix_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    int32_t fd = socket_accept((int32_t)sockfd);
    if (fd < 0) {
        posix_set_errno_from_status((int64_t)fd);
        return -1;
    }
    posix_fd_open((int)fd, POSIX_FD_TYPE_SOCKET, 0);
    if (addr != NULL || addrlen != NULL) {
        if (socket_address(fd, 1, addr, addrlen) < 0) {
            (void)socket_close(fd);
            posix_fd_close(fd);
            return -1;
        }
    }
    os_errno = 0;
    return (int)fd;
}

 

ssize_t posix_send(int sockfd, const void *buf, size_t len, int flags)
{
    if ((flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!buf) {
        errno = EINVAL;
        return -1;
    }

    posix_fd_entry_t *entry = posix_fd_entry(sockfd);
    if (!entry || !entry->valid || entry->type != POSIX_FD_TYPE_SOCKET) {
        errno = EBADF;
        return -1;
    }
    bool nonblocking =
        ((flags & MSG_DONTWAIT) != 0) ||
        ((entry->status_flags & POSIX_SFL_NONBLOCK) != 0);

    uint32_t send_sleep = 10u;
    for (;;) {
        int32_t r = socket_send((int32_t)sockfd, buf, (uint32_t)len);
        if (r > 0 || len == 0u) {
            os_errno = 0;
            return (ssize_t)r;
        }
        if (r < 0) {
            posix_set_errno_from_status((int64_t)r);
            return -1;
        }
        if (nonblocking) {
            errno = EAGAIN;
            return -1;
        }
        sleep_ms(send_sleep);
        if (send_sleep < 100u) { send_sleep += 5u; }
    }
}

 

ssize_t posix_recv(int sockfd, void *buf, size_t len, int flags)
{
    if ((flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!buf) {
        errno = EINVAL;
        return -1;
    }

    posix_fd_entry_t *entry = posix_fd_entry(sockfd);
    if (!entry || !entry->valid || entry->type != POSIX_FD_TYPE_SOCKET) {
        errno = EBADF;
        return -1;
    }
    bool nonblocking =
        ((flags & MSG_DONTWAIT) != 0) ||
        ((entry->status_flags & POSIX_SFL_NONBLOCK) != 0);

    uint32_t recv_sleep = 10u;
    for (;;) {
        int32_t r = socket_recv((int32_t)sockfd, buf, (uint32_t)len);
        if (r > 0 || len == 0u) {
            os_errno = 0;
            return (ssize_t)r;
        }
        if (r < 0) {
            posix_set_errno_from_status((int64_t)r);
            return -1;
        }
        socket_info_t info;
        if (socket_get_info(sockfd, &info) < 0 ||
            (info.state != 4u && info.state != 5u && info.state != 6u)) {
            os_errno = 0;
            return 0;
        }
        if (nonblocking) {
            errno = EAGAIN;
            return -1;
        }
        sleep_ms(recv_sleep);
        if (recv_sleep < 100u) { recv_sleep += 5u; }
    }
}

 

ssize_t posix_sendto(int sockfd, const void *buf, size_t len, int flags,
                     const struct sockaddr *dest_addr, socklen_t addrlen)
{
    if (dest_addr != NULL || addrlen != 0u) {
        errno = EISCONN;
        return -1;
    }
    return posix_send(sockfd, buf, len, flags);
}

 

ssize_t posix_recvfrom(int sockfd, void *buf, size_t len, int flags,
                       struct sockaddr *src_addr, socklen_t *addrlen)
{
    ssize_t received = posix_recv(sockfd, buf, len, flags);
    if (received >= 0 && (src_addr != NULL || addrlen != NULL) &&
        socket_address(sockfd, 1, src_addr, addrlen) < 0) {
        return -1;
    }
    return received;
}

 

int posix_shutdown(int sockfd, int how)
{
    if (how < SHUT_RD || how > SHUT_RDWR) {
        errno = EINVAL;
        return -1;
    }
    int32_t r = socket_shutdown((int32_t)sockfd, how);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    os_errno = 0;
    return 0;
}

 

int posix_closesocket(int sockfd)
{
    int32_t r = socket_close((int32_t)sockfd);
    if (r < 0) {
        posix_set_errno_from_status((int64_t)r);
        return -1;
    }
    posix_fd_close(sockfd);
    os_errno = 0;
    return 0;
}

 

int posix_setsockopt(int sockfd, int level, int optname,
                     const void *optval, socklen_t optlen)
{
    if (!optval || optlen < (socklen_t)sizeof(int)) {
        errno = EINVAL;
        return -1;
    }
    int32_t result = socket_set_option(sockfd, level, optname,
                                       *(const int *)optval);
    if (result < 0) {
        posix_set_errno_from_status(result);
        return -1;
    }
    os_errno = 0;
    return 0;
}

 

int posix_getsockopt(int sockfd, int level, int optname,
                     void *optval, socklen_t *optlen)
{
    if (!optval || !optlen || *optlen < (socklen_t)sizeof(int)) {
        errno = EINVAL;
        return -1;
    }
    int32_t result = socket_get_option(sockfd, level, optname,
                                       (int32_t *)optval);
    if (result < 0) {
        posix_set_errno_from_status(result);
        return -1;
    }
    *optlen = (socklen_t)sizeof(int);
    os_errno = 0;
    return 0;
}

 

int posix_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return socket_address(sockfd, 0, addr, addrlen);
}

 

int posix_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    return socket_address(sockfd, 1, addr, addrlen);
}
