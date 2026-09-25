#include "kconsole.h"
#include "cpu.h"
#include "serial.h"
#include "vga_text.h"

static const char HEX_DIGITS[] = "0123456789abcdef";

void kconsole_init(void)
{
	serial_init();
	vga_clear();
}

void kconsole_putc(char c)
{
	if (c == '\n') {
		serial_putc('\r'); /* raw serial terminals need CRLF, not just LF */
	}
	serial_putc(c);
	vga_putc(c);
}

/* Interrupts stay off for the whole string, so output from different
 * threads can't interleave mid-message. This stalls ticks while a long
 * line drains through the polled UART on real hardware; buffered,
 * interrupt-driven serial output belongs with the driver framework (M5). */
void kconsole_write(const char *s)
{
	uint32_t flags = cpu_irq_save();
	for (; *s; s++) {
		kconsole_putc(*s);
	}
	cpu_irq_restore(flags);
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
