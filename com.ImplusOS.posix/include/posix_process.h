 
#pragma once

#include <stdint.h>
#include "posix_types.h"
#include "posix_errno.h"

 
#ifndef WNOHANG
#define WNOHANG   1
#define WUNTRACED 2
#endif

 
#ifndef WIFEXITED
#define WIFEXITED(s)   (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFSIGNALED(s) (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)    ((s) & 0x7f)
#endif

 
#ifndef SIGKILL
#define SIGKILL  9
#endif

 

pid_t   posix_getpid (void);
pid_t   posix_getppid(void);
void    posix_exit   (int status) __attribute__((noreturn));

 
pid_t   posix_fork   (void);

 
int     posix_execve (const char *path, char *const argv[], char *const envp[]);
int     posix_execv  (const char *path, char *const argv[]);
int     posix_execvp (const char *file, char *const argv[]);

 
pid_t   posix_waitpid(pid_t pid, int *status, int options);
pid_t   posix_wait   (int *status);

 
int     posix_kill   (pid_t pid, int sig);

 
void    posix_process_init(const char *argv0);
