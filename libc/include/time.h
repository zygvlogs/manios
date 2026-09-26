/* Time. ManiOS's clock is /dev/time (seconds since 1970, UTC). */
#ifndef MANIOS_TIME_H
#define MANIOS_TIME_H

#include <sys/types.h>

struct timespec {
	time_t tv_sec;
	long tv_nsec;
};

/* Comparing, adding and subtracting timespecs (BSD). */
#define timespeccmp(a, b, cmp) ((a)->tv_sec == (b)->tv_sec ? \
	(a)->tv_nsec cmp (b)->tv_nsec : (a)->tv_sec cmp (b)->tv_sec)
#define timespecclear(t) ((t)->tv_sec = (t)->tv_nsec = 0)
#define timespecisset(t) ((t)->tv_sec || (t)->tv_nsec)

/* Seconds since 1970-01-01 00:00 UTC (from /dev/time); -1 if unknown. */
time_t time(time_t *t);

#endif
