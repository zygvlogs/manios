/* The AdLib card's Yamaha YM3812 (OPL2) FM synthesizer, at ports
 * 0x388-0x389 -- on AdLib cards, and on every Sound Blaster -- written
 * from Yamaha's YM3812 application manual and AdLib's programming
 * guide. The chip has 244 registers, written through an address port
 * and a data port, with short waits after each (3.3 us after the
 * address, 23 us after the data; the chip's status port is read for
 * them, as AdLib's guide says). It is found by its timers: started, the
 * status register's flags come up.
 *
 * Device "opl" takes pairs of bytes, register then value, and writes
 * them to the chip; reading it gives the status register. Music is for
 * programs to make (/bin/fm plays notes). */
#include "adlib.h"
#include <stdbool.h>
#include "cpu.h"
#include "device.h"
#include "io.h"
#include "kerrno.h"
#include "kprintf.h"
#include "mutex.h"
#include "timer.h"

#define OPL_ADDRESS 0x388 /* read: the status */
#define OPL_DATA    0x389
#define REG_TIMER1  0x02
#define REG_TIMER_CONTROL 0x04
#define TIMER_RESET_FLAGS 0x80
#define TIMER_MASK_BOTH   0x60
#define TIMER1_START      0x21 /* start timer 1, mask timer 2 */
#define STATUS_FLAGS 0xE0     /* IRQ, timer 1, timer 2 */

static struct mutex lock = MUTEX_INIT;

static void opl_write(uint8_t reg, uint8_t value)
{
	uint32_t flags = cpu_irq_save();
	outb(OPL_ADDRESS, reg);
	for (int i = 0; i < 6; i++) {
		(void)inb(OPL_ADDRESS);
	}
	outb(OPL_DATA, value);
	for (int i = 0; i < 35; i++) {
		(void)inb(OPL_ADDRESS);
	}
	cpu_irq_restore(flags);
}

static long opl_dev_write(struct device *dev, const void *buf, size_t len)
{
	(void)dev;
	const uint8_t *p = buf;
	if (len % 2) {
		return -EINVAL; /* register and value, always in pairs */
	}
	mutex_lock(&lock);
	for (size_t i = 0; i < len; i += 2) {
		opl_write(p[i], p[i + 1]);
	}
	mutex_unlock(&lock);
	return (long)len;
}

static long opl_dev_read(struct device *dev, void *buf, size_t len)
{
	(void)dev;
	if (!len) {
		return 0;
	}
	*(uint8_t *)buf = inb(OPL_ADDRESS);
	return 1;
}

static const struct char_device_ops opl_ops = { .read = opl_dev_read, .write = opl_dev_write };
static struct device opl_device = { .name = "opl", .class = DEVICE_CHAR, .char_ops = &opl_ops };

void adlib_init(void)
{
	/* AdLib's detection: timers reset, status clear; timer 1 run for
	 * 80 us at its fastest; then its flag and the IRQ flag are up. */
	opl_write(REG_TIMER_CONTROL, TIMER_MASK_BOTH);
	opl_write(REG_TIMER_CONTROL, TIMER_RESET_FLAGS);
	uint8_t before = inb(OPL_ADDRESS);
	opl_write(REG_TIMER1, 0xFF);
	opl_write(REG_TIMER_CONTROL, TIMER1_START);
	timer_sleep_ms(1);
	uint8_t after = inb(OPL_ADDRESS);
	opl_write(REG_TIMER_CONTROL, TIMER_MASK_BOTH);
	opl_write(REG_TIMER_CONTROL, TIMER_RESET_FLAGS);
	if ((before & STATUS_FLAGS) != 0 || (after & STATUS_FLAGS) != 0xC0) {
		return;
	}
	for (int reg = 0x01; reg <= 0xF5; reg++) {
		opl_write((uint8_t)reg, 0); /* silence, and a known state */
	}
	opl_write(0x01, 0x20); /* waveform select on (OPL2) */
	if (device_register(&opl_device) == 0) {
		kprintf("opl: Yamaha OPL2 (AdLib) FM synthesizer at 0x%x: /dev/opl\n", OPL_ADDRESS);
	}
}
