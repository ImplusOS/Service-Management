#pragma once

#include <stdint.h>
#include "posix_types.h"
#include "posix_errno.h"
 
#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3

#define POSIX_EPOCH_YEAR 1970

int     posix_clock_gettime (clockid_t clk_id, struct timespec *tp);
int     posix_clock_settime (clockid_t clk_id, const struct timespec *tp);
int     posix_clock_getres  (clockid_t clk_id, struct timespec *res);

int     posix_nanosleep     (const struct timespec *req, struct timespec *rem);
int     posix_gettimeofday  (struct timeval *tv, struct timezone *tz);
time_t  posix_time          (time_t *out);

struct tm *posix_gmtime_r   (const time_t *timep, struct tm *result);
struct tm *posix_localtime_r(const time_t *timep, struct tm *result);

time_t  posix_mktime        (struct tm *tm);
double  posix_difftime      (time_t t1, time_t t0);