#ifndef ZKT_DRIVERS_RTC_H
#define ZKT_DRIVERS_RTC_H

#include <stdint.h>

/* Reads the CMOS clock and registers device "time" (rtc.c). Needs the
 * timer. */
void rtc_init(void);

/* Seconds since 1970-01-01 00:00 UTC (0 if the clock wasn't read). */
uint32_t rtc_time(void);

#endif
