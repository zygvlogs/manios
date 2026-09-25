#include "kconsole.h"
#include "cpu.h"
#include "device.h"
#include "kstring.h"
#include "mutex.h"
#include "ring.h"
#include "sched.h"
#include "serial.h"
#include "vga_text.h"

static const char HEX_DIGITS[] = "0123456789abcdef";

static struct mutex output_lock = MUTEX_INIT;

static uint8_t input_storage[256];
static struct ring input = RING_INIT(input_storage);
static struct waitq input_ready = WAITQ_INIT;

void kconsole_init(void)
{
	serial_init();
	vga_clear();
}

/* Two ways to keep a message whole. A thread that may block takes a
 * mutex and lets the serial interrupt drain its output, so ticks keep
 * running meanwhile. Early boot, IRQ handlers, code that already turned
 * interrupts off, and panics can't block: they write synchronously with
 * interrupts off, which also flushes anything still buffered first. */
void kconsole_write_n(const char *s, size_t len)
{
	if (serial_buffered() && thread_current() && cpu_interrupts_enabled()) {
		mutex_lock(&output_lock);
		serial_write_buffered(s, len);
		vga_write(s, len);
		mutex_unlock(&output_lock);
	} else {
		uint32_t flags = cpu_irq_save();
		serial_write_polled(s, len);
		vga_write(s, len);
		cpu_irq_restore(flags);
	}
}

void kconsole_write(const char *s)
{
	kconsole_write_n(s, strlen(s));
}

void kconsole_putc(char c)
{
	kconsole_write_n(&c, 1);
}

void kconsole_write_hex32(uint32_t value)
{
	char buf[9];
	buf[8] = '\0';
	for (int i = 7; i >= 0; i--) {
		buf[i] = HEX_DIGITS[value & 0xF];
		value >>= 4;
	}
	kconsole_write(buf);
}

void kconsole_write_dec(uint32_t value)
{
	char buf[11];
	int i = 10;
	buf[i] = '\0';
	do {
		buf[--i] = (char)('0' + value % 10);
		value /= 10;
	} while (value);
	kconsole_write(&buf[i]);
}

void console_input(char c)
{
	if (!ring_full(&input)) {
		ring_push(&input, (uint8_t)c);
	}
	waitq_wake_all(&input_ready);
}

static long cons_read(struct device *dev, void *buf, size_t len)
{
	(void)dev;
	uint8_t *out = buf;
	size_t n = 0;

	uint32_t flags = cpu_irq_save();
	while (ring_empty(&input)) {
		waitq_sleep(&input_ready);
	}
	while (n < len && !ring_empty(&input)) {
		out[n++] = ring_pop(&input);
	}
	cpu_irq_restore(flags);
	return (long)n;
}

static long cons_write(struct device *dev, const void *buf, size_t len)
{
	(void)dev;
	kconsole_write_n(buf, len);
	return (long)len;
}

static const struct char_device_ops cons_ops = { .read = cons_read, .write = cons_write };
static struct device cons_device = { .name = "cons", .class = DEVICE_CHAR, .char_ops = &cons_ops };

void console_register(void)
{
	device_register(&cons_device);
}
