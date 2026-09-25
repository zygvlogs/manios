#include "kconsole.h"
#include <stdbool.h>
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

static char raw_getc(void)
{
	uint32_t flags = cpu_irq_save();
	while (ring_empty(&input)) {
		waitq_sleep(&input_ready);
	}
	char c = (char)ring_pop(&input);
	cpu_irq_restore(flags);
	return c;
}

/* The line discipline: the console is "cooked", as Plan 9's /dev/cons
 * is. Input is echoed and edited a line at a time, and a read returns at
 * most one line. Serves the monitor and user programs alike. */
#define CTRL_D 0x04
#define CTRL_U 0x15

static struct mutex reader_lock = MUTEX_INIT;
static char line[CONSOLE_LINE_MAX];
static size_t line_len, line_pos; /* a finished line, handed out from line_pos */

/* Terminals send '\r' for Enter, and some follow it with '\n'. Either
 * ends the line, but a '\n' right after a '\r' is swallowed rather than
 * taken as an empty line. Returns false for end of file: ^D on an empty
 * line. ^D after some input ends the line without a newline. */
static bool edit_line(void)
{
	static bool after_cr;
	line_len = 0;
	for (;;) {
		char c = raw_getc();
		if (c == '\n' && after_cr) {
			after_cr = false;
			continue;
		}
		after_cr = c == '\r';

		if (c == '\r' || c == '\n') {
			kconsole_write("\n");
			line[line_len++] = '\n';
			return true;
		}
		if (c == CTRL_D) {
			return line_len > 0;
		}
		if (c == '\b' || c == 0x7F || c == CTRL_U) {
			do {
				if (line_len) {
					line_len--;
					kconsole_write("\b \b");
				}
			} while (c == CTRL_U && line_len);
			continue;
		}
		/* One byte stays free for the newline. */
		if (c >= 0x20 && c < 0x7F && line_len + 1 < CONSOLE_LINE_MAX) {
			line[line_len++] = c;
			kconsole_putc(c);
		}
	}
}

static long cons_read(struct device *dev, void *buf, size_t len)
{
	(void)dev;
	if (!len) {
		return 0;
	}
	mutex_lock(&reader_lock);
	if (line_pos == line_len) {
		line_pos = 0;
		if (!edit_line()) {
			line_len = 0;
			mutex_unlock(&reader_lock);
			return 0;
		}
	}
	size_t n = line_len - line_pos < len ? line_len - line_pos : len;
	memcpy(buf, line + line_pos, n);
	line_pos += n;
	mutex_unlock(&reader_lock);
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
