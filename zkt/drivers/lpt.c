/* The PC's parallel printer port (LPT1 at 0x378), in its original,
 * "standard" mode, from the IBM PC's technical reference and the IEEE
 * 1284 compatibility mode. A byte goes out by putting it on the data
 * lines once the printer isn't busy and pulsing STROBE; the printer
 * acknowledges and goes busy while it takes it. Device "lpt1", written
 * to; a printer that stays busy for 10 seconds, or is out of paper or
 * off line, makes the write fail. The port is found by its data
 * register reading back what was written. Polled: printers are slow and
 * the IRQ (7) is shared and unreliable on many machines. */
#include "lpt.h"
#include <stdbool.h>
#include <stdint.h>
#include "device.h"
#include "io.h"
#include "kerrno.h"
#include "kprintf.h"
#include "mutex.h"
#include "sched.h"
#include "timer.h"

#define BASE 0x378
#define DATA    (BASE + 0)
#define STATUS  (BASE + 1)
#define CONTROL (BASE + 2)
#define STATUS_ERROR   0x08 /* low: an error */
#define STATUS_SELECT  0x10 /* high: on line */
#define STATUS_PAPER   0x20 /* high: out of paper */
#define STATUS_NBUSY   0x80 /* high: not busy */
#define CONTROL_STROBE 0x01
#define CONTROL_NINIT  0x04 /* low: reset the printer */
#define CONTROL_SELECT 0x08
#define BUSY_TIMEOUT_MS 10000

static struct mutex lock = MUTEX_INIT;
static uint32_t sent;

static long lpt_write(struct device *dev, const void *buf, size_t len)
{
	(void)dev;
	const uint8_t *p = buf;
	size_t i = 0;
	mutex_lock(&lock);
	for (; i < len; i++) {
		uint64_t deadline = timer_uptime_ms() + BUSY_TIMEOUT_MS;
		uint8_t status;
		for (int spins = 0; !((status = inb(STATUS)) & STATUS_NBUSY); spins++) {
			if (timer_uptime_ms() > deadline) {
				break;
			}
			if (spins > 1000) {
				thread_yield();
			}
		}
		if (!(status & STATUS_NBUSY) || (status & STATUS_PAPER) || !(status & STATUS_SELECT)
		    || !(status & STATUS_ERROR)) {
			break;
		}
		outb(DATA, p[i]);
		/* Data settles, STROBE low for at least 0.5 us, then high. */
		(void)inb(STATUS);
		outb(CONTROL, CONTROL_SELECT | CONTROL_NINIT | CONTROL_STROBE);
		(void)inb(STATUS);
		(void)inb(STATUS);
		outb(CONTROL, CONTROL_SELECT | CONTROL_NINIT);
		sent++;
	}
	mutex_unlock(&lock);
	return i ? (long)i : -EIO;
}

static const struct char_device_ops lpt_ops = { .write = lpt_write };
static struct device lpt_device = { .name = "lpt1", .class = DEVICE_CHAR, .char_ops = &lpt_ops };

void lpt_init(void)
{
	outb(DATA, 0xA5);
	if (inb(DATA) != 0xA5) {
		return;
	}
	outb(DATA, 0x5A);
	if (inb(DATA) != 0x5A) {
		return;
	}
	/* Reset the printer: INIT low for 50 us or more. */
	outb(CONTROL, CONTROL_SELECT);
	timer_sleep_ms(1);
	outb(CONTROL, CONTROL_SELECT | CONTROL_NINIT);
	if (device_register(&lpt_device) == 0) {
		uint8_t status = inb(STATUS);
		kprintf("lpt1: parallel port at 0x%x, printer %s\n", BASE,
		        (status & STATUS_SELECT) && !(status & STATUS_PAPER) ? "ready" : "not ready");
	}
}
