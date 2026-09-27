#ifndef ZKT_DRIVERS_RTC_H
#define ZKT_DRIVERS_RTC_H

#include <stdint.h>

/* Reads the CMOS clock and registers device "time" (rtc.c). Needs the
 * timer. */
void rtc_init(void);

/* Seconds since 1970-01-01 00:00 UTC (0 if the clock wasn't read). */
uint32_t rtc_time(void);

/* One byte of the CMOS memory (0-127; 0-13 are the clock's registers). */
uint8_t cmos_read(uint8_t reg);
void cmos_write(uint8_t reg, uint8_t value);

#endif
