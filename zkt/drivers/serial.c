#include "serial.h"
#include <stdint.h>
#include "kconsole.h"
#include "cpu.h"
#include "device.h"
#include "io.h"
#include "irq.h"
#include "ring.h"
#include "sched.h"

#define COM1 0x3F8
#define COM1_IRQ 4

#define REG_DATA 0
#define REG_IER  1
#define REG_IIR  2 /* read */
#define REG_FCR  2 /* write */
#define REG_LCR  3
#define REG_MCR  4
#define REG_LSR  5
#define REG_MSR  6

#define IER_RX_AVAILABLE 0x01
#define IER_TX_EMPTY     0x02
#define LSR_DATA_READY   0x01
#define LSR_TX_EMPTY     0x20
#define IIR_NONE_PENDING 0x01
#define IIR_ID_MASK      0x0E
#define IIR_TX_EMPTY     0x02
#define IIR_RX_DATA      0x04
#define IIR_LINE_STATUS  0x06
#define IIR_RX_TIMEOUT   0x0C
#define IIR_FIFO_16550A  0xC0

static uint8_t tx_storage[4096];
static struct ring tx = RING_INIT(tx_storage);
static struct waitq tx_space = WAITQ_INIT;
static uint8_t ier;
static unsigned tx_fifo_depth = 1;
static bool started;

void serial_init(void)
{
	outb(COM1 + REG_IER, 0x00); /* no UART interrupts yet */
	outb(COM1 + REG_LCR, 0x80); /* DLAB on, to set the divisor */
	outb(COM1 + REG_DATA, 0x03); /* 115200 / 3 = 38400 baud */
	outb(COM1 + REG_IER, 0x00);
	outb(COM1 + REG_LCR, 0x03); /* 8N1, DLAB off */
	outb(COM1 + REG_FCR, 0xC7); /* enable and clear FIFOs, RX trigger at 14 */
	outb(COM1 + REG_MCR, 0x0B); /* DTR, RTS, and OUT2, which gates the IRQ line */

	/* Only a 16550A has a usable 16-byte FIFO; an 8250/16450 (or the
	 * buggy original 16550) takes one byte per TX interrupt. */
	if ((inb(COM1 + REG_IIR) & IIR_FIFO_16550A) == IIR_FIFO_16550A) {
		tx_fifo_depth = 16;
	}
}

static void put_polled(uint8_t c)
{
	while (!(inb(COM1 + REG_LSR) & LSR_TX_EMPTY)) {
	}
	outb(COM1 + REG_DATA, c);
}

void serial_write_polled(const char *buf, size_t len)
{
	bool drained = !ring_empty(&tx);
	while (!ring_empty(&tx)) {
		put_polled(ring_pop(&tx));
	}
	for (size_t i = 0; i < len; i++) {
		if (buf[i] == '\n') {
			put_polled('\r');
		}
		put_polled((uint8_t)buf[i]);
	}
	if (drained) {
		waitq_wake_all(&tx_space);
	}
}

static void start_tx(void)
{
	/* A 16550 raises the TX-empty interrupt as soon as it is enabled if
	 * the transmitter is already idle, which starts the draining. */
	if (!(ier & IER_TX_EMPTY)) {
		ier |= IER_TX_EMPTY;
		outb(COM1 + REG_IER, ier);
	}
}

static void push_blocking(uint8_t c)
{
	while (ring_full(&tx)) {
		start_tx();
		waitq_sleep(&tx_space);
	}
	ring_push(&tx, c);
}

void serial_write_buffered(const char *buf, size_t len)
{
	uint32_t flags = cpu_irq_save();
	for (size_t i = 0; i < len; i++) {
		if (buf[i] == '\n') {
			push_blocking('\r');
		}
		push_blocking((uint8_t)buf[i]);
	}
	start_tx();
	cpu_irq_restore(flags);
}

static void serial_irq(void)
{
	/* Bounded, in case a broken UART never reports "none pending". */
	for (int round = 0; round < 16; round++) {
		uint8_t iir = inb(COM1 + REG_IIR);
		if (iir & IIR_NONE_PENDING) {
			return;
		}
		switch (iir & IIR_ID_MASK) {
		case IIR_RX_DATA:
		case IIR_RX_TIMEOUT:
			while (inb(COM1 + REG_LSR) & LSR_DATA_READY) {
				console_input((char)inb(COM1 + REG_DATA));
			}
			break;
		case IIR_TX_EMPTY:
			for (unsigned n = 0; n < tx_fifo_depth && !ring_empty(&tx); n++) {
				outb(COM1 + REG_DATA, ring_pop(&tx));
			}
			if (ring_empty(&tx)) {
				ier &= (uint8_t)~IER_TX_EMPTY;
				outb(COM1 + REG_IER, ier);
			}
			waitq_wake_all(&tx_space);
			break;
		case IIR_LINE_STATUS:
			inb(COM1 + REG_LSR);
			break;
		default:
			inb(COM1 + REG_MSR);
			break;
		}
	}
}

static long com1_write(struct device *dev, const void *buf, size_t len)
{
	(void)dev;
	if (thread_current() && cpu_interrupts_enabled()) {
		serial_write_buffered(buf, len);
	} else {
		uint32_t flags = cpu_irq_save();
		serial_write_polled(buf, len);
		cpu_irq_restore(flags);
	}
	return (long)len;
}

static const struct char_device_ops com1_ops = { .write = com1_write };
static struct device com1_device = { .name = "com1", .class = DEVICE_CHAR, .char_ops = &com1_ops };

void serial_start(void)
{
	ier = IER_RX_AVAILABLE;
	outb(COM1 + REG_IER, ier);
	irq_install_handler(COM1_IRQ, serial_irq);
	started = true;
	device_register(&com1_device);
}

bool serial_buffered(void)
{
	return started;
}
