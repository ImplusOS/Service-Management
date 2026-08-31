#pragma once

#include <stddef.h>
#include <stdint.h>
#include "posix_types.h"
#include "posix_errno.h"
 
#define AF_UNSPEC  0
#define AF_INET    2
#define AF_INET6   10

#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3
#define SOCK_NONBLOCK 0x0800
#define SOCK_CLOEXEC  0x80000

#define SOL_SOCKET   1
#define SO_REUSEADDR 2
#define SO_KEEPALIVE 9
#define SO_ERROR     4
#define SO_RCVBUF    8
#define SO_SNDBUF    7

#define IPPROTO_TCP  6
#define IPPROTO_UDP  17

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000

#define INADDR_ANY       ((in_addr_t)0x00000000)
#define INADDR_LOOPBACK  ((in_addr_t)0x7f000001)
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)

int     posix_socket    (int domain, int type, int protocol);
int     posix_bind      (int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int     posix_connect   (int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int     posix_listen    (int sockfd, int backlog);
int     posix_accept    (int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t posix_send      (int sockfd, const void *buf, size_t len, int flags);
ssize_t posix_recv      (int sockfd, void *buf, size_t len, int flags);
ssize_t posix_sendto    (int sockfd, const void *buf, size_t len, int flags,
                         const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t posix_recvfrom  (int sockfd, void *buf, size_t len, int flags,
                         struct sockaddr *src_addr, socklen_t *addrlen);
int     posix_shutdown  (int sockfd, int how);
int     posix_setsockopt(int sockfd, int level, int optname,
                         const void *optval, socklen_t optlen);
int     posix_getsockopt(int sockfd, int level, int optname,
                         void *optval, socklen_t *optlen);
int     posix_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int     posix_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
int     posix_closesocket(int sockfd);

 

uint16_t posix_htons(uint16_t hostshort);
uint16_t posix_ntohs(uint16_t netshort);
uint32_t posix_htonl(uint32_t hostlong);
uint32_t posix_ntohl(uint32_t netlong);

 

int      posix_inet_aton (const char *cp, struct in_addr *inp);
in_addr_t posix_inet_addr(const char *cp);
char     *posix_inet_ntoa(struct in_addr in);
