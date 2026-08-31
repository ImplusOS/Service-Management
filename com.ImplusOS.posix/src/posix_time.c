 

#include "../include/posix_time.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

 

extern uint64_t get_uptime_ms(void);
extern void     sleep_ms(uint64_t ms);

typedef struct {
    uint8_t  second;
    uint8_t  minute;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
} rtc_time_t;

extern int32_t sys_get_rtc_time(rtc_time_t *time);

 

static const int DAYS_IN_MONTH[12] =
    { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static int is_leap(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int mon, int year)
{
    if (mon == 2 && is_leap(year)) return 29;
    return DAYS_IN_MONTH[mon - 1];
}

 
static int64_t date_to_epoch_days(int year, int month, int day)
{
    int64_t y = year - 1;    
    int64_t days = y * 365 + y / 4 - y / 100 + y / 400;

     
    for (int m = 1; m < month; m++) {
        days += days_in_month(m, year);
    }
    days += day;

     
    int64_t y69 = 1969;
    int64_t epoch_base = y69 * 365 + y69 / 4 - y69 / 100 + y69 / 400;
     
    for (int m = 1; m <= 12; m++) {
        epoch_base += days_in_month(m, 1969);
    }
     
    (void)epoch_base;

     
    int64_t base_y = 1969;
    int64_t base = base_y * 365 + base_y / 4 - base_y / 100 + base_y / 400;
    base += 365;  

    return days - base;
}

 

int posix_clock_gettime(clockid_t clk_id, struct timespec *tp)
{
    if (!tp) {
        errno = EINVAL;
        return -1;
    }

    if (clk_id == CLOCK_MONOTONIC || clk_id == CLOCK_PROCESS_CPUTIME_ID ||
        clk_id == CLOCK_THREAD_CPUTIME_ID)
    {
        uint64_t ms = get_uptime_ms();
        tp->tv_sec  = (time_t)(ms / 1000ULL);
        tp->tv_nsec = (long)((ms % 1000ULL) * 1000000L);
        os_errno = 0;
        return 0;
    }

    if (clk_id == CLOCK_REALTIME) {
        rtc_time_t rtc;
        if (sys_get_rtc_time(&rtc) < 0) {
            errno = EIO;
            return -1;
        }
        int64_t epoch_days = date_to_epoch_days(
            (int)rtc.year, (int)rtc.month, (int)rtc.day);
        time_t t = (time_t)(epoch_days * 86400LL +
                            (int64_t)rtc.hour   * 3600LL +
                            (int64_t)rtc.minute *   60LL +
                            (int64_t)rtc.second);
        tp->tv_sec  = t;
        tp->tv_nsec = 0;
        os_errno = 0;
        return 0;
    }

    errno = EINVAL;
    return -1;
}

 

int posix_clock_settime(clockid_t clk_id, const struct timespec *tp)
{
    (void)clk_id; (void)tp;
    errno = ENOTSUP;
    return -1;
}

 

int posix_clock_getres(clockid_t clk_id, struct timespec *res)
{
    (void)clk_id;
    if (!res) {
        errno = EINVAL;
        return -1;
    }
     
    res->tv_sec  = 0;
    res->tv_nsec = 1000000L;
    os_errno = 0;
    return 0;
}

 

int posix_nanosleep(const struct timespec *req, struct timespec *rem)
{
    (void)rem;
    if (!req) {
        errno = EINVAL;
        return -1;
    }
    uint64_t ms = 0;
    if (req->tv_sec > 0) {
        ms += (uint64_t)req->tv_sec * 1000ULL;
    }
    if (req->tv_nsec > 0) {
        ms += (uint64_t)(req->tv_nsec / 1000000L);
    }
    sleep_ms(ms);
    os_errno = 0;
    return 0;
}

 

int posix_gettimeofday(struct timeval *tv, struct timezone *tz)
{
    if (tz) {
        tz->tz_minuteswest = 0;
        tz->tz_dsttime     = 0;
    }
    if (!tv) {
        os_errno = 0;
        return 0;
    }
    struct timespec now;
    if (posix_clock_gettime(CLOCK_REALTIME, &now) < 0) return -1;
    tv->tv_sec = now.tv_sec;
    tv->tv_usec = (suseconds_t)(now.tv_nsec / 1000L);
    os_errno = 0;
    return 0;
}

 

time_t posix_time(time_t *out)
{
    struct timespec ts;
    posix_clock_gettime(CLOCK_REALTIME, &ts);
    if (out) {
        *out = ts.tv_sec;
    }
    return ts.tv_sec;
}

 

struct tm *posix_gmtime_r(const time_t *timep, struct tm *result)
{
    if (!timep || !result) {
        return NULL;
    }

    memset(result, 0, sizeof(*result));

    int64_t t   = (int64_t)*timep;
    int64_t rem = t % 86400LL;
    int64_t days = t / 86400LL;
    if (rem < 0) {
        rem  += 86400LL;
        days -= 1;
    }

    result->tm_sec  = (int)(rem % 60); rem /= 60;
    result->tm_min  = (int)(rem % 60);
    result->tm_hour = (int)(rem / 60);

     
    result->tm_wday = (int)((days + 4) % 7);
    if (result->tm_wday < 0) result->tm_wday += 7;

     
    int year = 1970;
    if (days >= 0) {
        while (1) {
            int dy = is_leap(year) ? 366 : 365;
            if (days < dy) break;
            days -= dy;
            year++;
        }
    } else {
        while (days < 0) {
            year--;
            int dy = is_leap(year) ? 366 : 365;
            days += dy;
        }
    }

    result->tm_year = year - 1900;
    result->tm_yday = (int)days;

    int mon = 1;
    while ((int64_t)days >= days_in_month(mon, year)) {
        days -= days_in_month(mon, year);
        mon++;
    }
    result->tm_mon  = mon - 1;
    result->tm_mday = (int)days + 1;
    result->tm_isdst = 0;

    return result;
}

 

struct tm *posix_localtime_r(const time_t *timep, struct tm *result)
{
     
    return posix_gmtime_r(timep, result);
}

 

time_t posix_mktime(struct tm *tm)
{
    if (!tm) return (time_t)-1;

     
    while (tm->tm_mon < 0)  { tm->tm_mon += 12; tm->tm_year--; }
    while (tm->tm_mon > 11) { tm->tm_mon -= 12; tm->tm_year++; }

    int year  = tm->tm_year + 1900;
    int month = tm->tm_mon + 1;
    int day   = tm->tm_mday;

     
    while (day <= 0) {
        month--;
        if (month < 1) { month = 12; year--; }
        day += days_in_month(month, year);
    }
    while (day > days_in_month(month, year)) {
        day -= days_in_month(month, year);
        month++;
        if (month > 12) { month = 1; year++; }
    }
    tm->tm_year  = year - 1900;
    tm->tm_mon   = month - 1;
    tm->tm_mday  = day;

    int64_t epoch_days = date_to_epoch_days(year, month, day);

    time_t t = (time_t)(epoch_days * 86400LL +
                        (int64_t)tm->tm_hour * 3600LL +
                        (int64_t)tm->tm_min  *   60LL +
                        (int64_t)tm->tm_sec);
                        
    struct tm check;
    posix_gmtime_r(&t, &check);
    tm->tm_wday  = check.tm_wday;
    tm->tm_yday  = check.tm_yday;
    tm->tm_isdst = 0;

    return t;
}

double posix_difftime(time_t t1, time_t t0)
{
    return (double)(t1 - t0);
}
