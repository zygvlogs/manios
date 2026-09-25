#ifndef ZKT_DRIVERS_RTC_H
#define ZKT_DRIVERS_RTC_H

/* Reads the CMOS clock and registers device "time" (rtc.c). Needs the
 * timer. */
void rtc_init(void);

#endif
