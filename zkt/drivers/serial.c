#include "serial.h"
#include "../arch/i386/io.h"

#define COM1 0x3F8

void serial_init(void)
{
	outb(COM1 + 1, 0x00); /* disable UART interrupts */
	outb(COM1 + 3, 0x80); /* enable DLAB to set the baud rate divisor */
	outb(COM1 + 0, 0x03); /* divisor low byte:  115200 / 3 = 38400 baud */
	outb(COM1 + 1, 0x00); /* divisor high byte */
	outb(COM1 + 3, 0x03); /* 8 bits, no parity, one stop bit; clears DLAB */
	outb(COM1 + 2, 0xC7); /* enable FIFO, clear it, 14-byte threshold */
	outb(COM1 + 4, 0x0B); /* IRQs disabled, RTS/DSR set (polling mode) */
}

static int serial_tx_ready(void)
{
	return inb(COM1 + 5) & 0x20;
}

void serial_putc(char c)
{
	while (!serial_tx_ready()) {
	}
	outb(COM1, (uint8_t)c);
}

void serial_write(const char *s)
{
	for (; *s; s++) {
		if (*s == '\n') {
			serial_putc('\r');
		}
		serial_putc(*s);
	}
}
