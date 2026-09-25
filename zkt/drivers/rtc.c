/* The CMOS real-time clock (the MC146818 of the PC/AT), read once at
 * boot; afterwards the time is that reading plus the uptime. Device
 * "time" reads as the seconds since 1970-01-01 00:00 UTC, as text
 * ("1758800000\n"), like Plan 9's /dev/time. The clock is taken to hold
 * UTC. */
#include "rtc.h"
#include <stdbool.h>
#include "cpu.h"
#include "device.h"
#include "io.h"
#include "kprintf.h"
#include "kstring.h"
#include "timer.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71
#define REG_SECONDS 0x00
#define REG_MINUTES 0x02
#define REG_HOURS   0x04
#define REG_DAY     0x07
#define REG_MONTH   0x08
#define REG_YEAR    0x09
#define REG_STATUS_A 0x0A
#define REG_STATUS_B 0x0B
#define UPDATE_IN_PROGRESS 0x80
#define BINARY_MODE 0x04
#define HOURS_24 0x02
#define PM_BIT 0x80

static uint32_t boot_time; /* seconds since the epoch, at uptime 0 */

static uint8_t cmos(uint8_t reg)
{
	outb(CMOS_INDEX, reg);
	return inb(CMOS_DATA);
}

struct reading {
	uint8_t sec, min, hour, day, month, year;
};

static void read_raw(struct reading *r)
{
	for (int i = 0; i < 100000 && (cmos(REG_STATUS_A) & UPDATE_IN_PROGRESS); i++) {
	}
	r->sec = cmos(REG_SECONDS);
	r->min = cmos(REG_MINUTES);
	r->hour = cmos(REG_HOURS);
	r->day = cmos(REG_DAY);
	r->month = cmos(REG_MONTH);
	r->year = cmos(REG_YEAR);
}

static uint8_t from_bcd(uint8_t v)
{
	return (uint8_t)((v >> 4) * 10 + (v & 0x0F));
}

/* Days from 1970-01-01 to the given date (proleptic Gregorian): the
 * well-known "days from civil" computation, in integers. */
static int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d)
{
	y -= m <= 2;
	int32_t era = (y >= 0 ? y : y - 399) / 400;
	uint32_t yoe = (uint32_t)(y - era * 400);
	uint32_t doy = (153 * (m + (m > 2 ? (uint32_t)-3 : 9)) + 2) / 5 + d - 1;
	uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (int32_t)doe - 719468;
}

static long time_read(struct device *dev, uint32_t offset, void *buf, size_t len)
{
	(void)dev;
	char text[16];
	int n = ksnprintf(text, sizeof(text), "%lu\n",
	                  (unsigned long)(boot_time + (uint32_t)(timer_uptime_ms() / 1000)));
	if (offset >= (uint32_t)n) {
		return 0;
	}
	size_t k = (size_t)n - offset < len ? (size_t)n - offset : len;
	memcpy(buf, text + offset, k);
	return (long)k;
}

uint32_t rtc_time(void)
{
	return boot_time ? boot_time + (uint32_t)(timer_uptime_ms() / 1000) : 0;
}

static const struct char_device_ops time_ops = { .pread = time_read };
static struct device time_device = { .name = "time", .class = DEVICE_CHAR, .char_ops = &time_ops };

void rtc_init(void)
{
	/* Two identical readings in a row: not torn by an update. */
	struct reading a, b;
	uint32_t flags = cpu_irq_save();
	read_raw(&b);
	do {
		a = b;
		read_raw(&b);
	} while (memcmp(&a, &b, sizeof(a)) != 0);
	uint8_t status = cmos(REG_STATUS_B);
	cpu_irq_restore(flags);

	bool pm = !(status & HOURS_24) && (a.hour & PM_BIT);
	a.hour &= (uint8_t)~PM_BIT;
	if (!(status & BINARY_MODE)) {
		a.sec = from_bcd(a.sec);
		a.min = from_bcd(a.min);
		a.hour = from_bcd(a.hour);
		a.day = from_bcd(a.day);
		a.month = from_bcd(a.month);
		a.year = from_bcd(a.year);
	}
	if (!(status & HOURS_24)) {
		a.hour = (uint8_t)(a.hour % 12 + (pm ? 12 : 0));
	}
	if (a.month < 1 || a.month > 12 || a.day < 1 || a.day > 31 || a.hour > 23 || a.min > 59
	    || a.sec > 59) {
		kprintf("rtc: implausible clock reading, using 1970\n");
		return;
	}
	int32_t year = a.year < 70 ? 2000 + a.year : 1900 + a.year;
	int32_t days = days_from_civil(year, a.month, a.day);
	boot_time = (uint32_t)(days * 86400 + a.hour * 3600 + a.min * 60 + a.sec)
	            - (uint32_t)(timer_uptime_ms() / 1000);
	kprintf("rtc: %04ld-%02u-%02u %02u:%02u:%02u UTC\n", (long)year, a.month, a.day, a.hour,
	        a.min, a.sec);
	device_register(&time_device);
}
