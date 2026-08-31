 
#pragma once

#include <stdint.h>
#include "posix_types.h"
#include "posix_errno.h"

 

#define SIGHUP    1    
#define SIGINT    2    
#define SIGQUIT   3    
#define SIGILL    4    
#define SIGTRAP   5    
#define SIGABRT   6    
#define SIGBUS    7    
#define SIGFPE    8    
#define SIGKILL   9    
#define SIGUSR1   10   
#define SIGSEGV   11   
#define SIGUSR2   12   
#define SIGPIPE   13   
#define SIGALRM   14   
#define SIGTERM   15   
#define SIGCHLD   17   
#define SIGCONT   18   
#define SIGSTOP   19   
#define SIGTSTP   20   
#define SIGTTIN   21   
#define SIGTTOU   22   
#define SIGURG    23   
#define SIGXCPU   24   
#define SIGXFSZ   25   
#define SIGVTALRM 26   
#define SIGPROF   27   
#define SIGWINCH  28   
#define SIGIO     29   
#define SIGPWR    30   
#define SIGSYS    31   
#define NSIG      32   

 

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

 

#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO   0x00000004
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

 

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

 

static inline int sigemptyset(sigset_t *set)
{
    if (!set) { errno = EINVAL; return -1; }
    *set = 0ULL;
    return 0;
}

static inline int sigfillset(sigset_t *set)
{
    if (!set) { errno = EINVAL; return -1; }
    *set = ~0ULL;
    return 0;
}

static inline int sigaddset(sigset_t *set, int signum)
{
    if (!set || signum < 1 || signum >= NSIG) { errno = EINVAL; return -1; }
    *set |= (1ULL << (signum - 1));
    return 0;
}

static inline int sigdelset(sigset_t *set, int signum)
{
    if (!set || signum < 1 || signum >= NSIG) { errno = EINVAL; return -1; }
    *set &= ~(1ULL << (signum - 1));
    return 0;
}

static inline int sigismember(const sigset_t *set, int signum)
{
    if (!set || signum < 1 || signum >= NSIG) { errno = EINVAL; return -1; }
    return ((*set >> (signum - 1)) & 1ULL) ? 1 : 0;
}

 

 
sighandler_t posix_signal(int signum, sighandler_t handler);

 
int posix_sigaction(int signum, const struct sigaction *act,
                    struct sigaction *oldact);

 
int posix_sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

 
int posix_sigpending(sigset_t *set);

 
int posix_raise(int sig);
