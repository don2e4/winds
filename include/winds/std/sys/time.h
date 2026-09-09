#ifndef winds_sys_time_h
#define winds_sys_time_h

#include <time.h>

struct timeval { long tv_sec, tv_usec; };
struct timezone { int tz_minuteswest, tz_dsttime; };

extern int gettimeofday(struct timeval *time, void *timezone);
extern int utimes(const char *path, const struct timeval times[2]);

#endif
