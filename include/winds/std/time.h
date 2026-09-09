#ifndef winds_time_h
#define winds_time_h

typedef long time_t;

struct timespec { time_t tv_sec; long tv_nsec; };

struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};

extern time_t time(time_t *timer);
extern struct tm *localtime(const time_t *timer);
extern struct tm *localtime_r(const time_t *timer, struct tm *result);
extern int nanosleep(const struct timespec *duration, struct timespec *remaining);

#endif
