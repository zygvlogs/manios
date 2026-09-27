/* The PC's other serial ports, COM2 to COM4 (COM1 is the console:
 * serial.c), on 8250/16450/16550 UARTs, written from National
 * Semiconductor's PC16550D data sheet. Each port found (its modem
 * control register's loopback mode echoes a byte) becomes device
 * "comN", read and written through interrupt-driven buffers, and
 * "comNctl", where "b9600" sets the speed (Plan 9's eia ctl style) and
 * reading gives the speed and the modem's lines. 8 data bits, no
 * parity, one stop bit. */
#include "uart.h"
#include <stdbool.h>
#include <stdint.h>
#include "cpu.h"
#include "device.h"
#include "io.h"
#include "irq.h"
#include "kerrno.h"
#include "kprintf.h"
#include "kstring.h"
#include "ring.h"
#include "sched.h"

#define REG_DATA 0
#define REG_IER  1
#define REG_IIR  2 /* read */
#define REG_FCR  2 /* write */
#define REG_LCR  3
#define REG_MCR  4
#define REG_LSR  5
#define REG_MSR  6
#define IER_RX 0x01
#define IER_TX 0x02
#define LCR_DLAB 0x80
#define LCR_8N1  0x03
#define MCR_DTR_RTS_OUT2 0x0B
#define MCR_LOOPBACK 0x10
#define LSR_DATA_READY 0x01
#define IIR_NONE 0x01
#define IIR_FIFO 0xC0
#define MSR_CTS 0x10
#define MSR_DSR 0x20
#define MSR_DCD 0x80
#define UART_HZ 115200
#define DEFAULT_BAUD 9600

struct port {
	struct device dev, ctl;
	uint16_t io;
	uint8_t irq, ier;
	bool present;
	unsigned fifo, baud;
	uint8_t rx_storage[1024], tx_storage[1024];
	struct ring rx, tx;
	struct waitq rx_wait, tx_wait;
	uint32_t overruns;
};

static struct port ports[3] = {
	{ .io = 0x2F8, .irq = 3 }, /* COM2 */
	{ .io = 0x3E8, .irq = 4 }, /* COM3 */
	{ .io = 0x2E8, .irq = 3 }, /* COM4 */
};

static void set_baud(struct port *p, unsigned baud)
{
	uint16_t divisor = (uint16_t)(UART_HZ / baud);
	uint32_t flags = cpu_irq_save();
	outb(p->io + REG_LCR, LCR_DLAB);
	outb(p->io + REG_DATA, (uint8_t)divisor);
	outb(p->io + REG_IER, (uint8_t)(divisor >> 8));
	outb(p->io + REG_LCR, LCR_8N1);
	outb(p->io + REG_IER, p->ier);
	p->baud = UART_HZ / divisor;
	cpu_irq_restore(flags);
}

static void service(struct port *p)
{
	for (int round = 0; round < 16; round++) {
		if (inb(p->io + REG_IIR) & IIR_NONE) {
			return;
		}
		while (inb(p->io + REG_LSR) & LSR_DATA_READY) {
			uint8_t c = inb(p->io + REG_DATA);
			if (ring_full(&p->rx)) {
				p->overruns++;
			} else {
				ring_push(&p->rx, c);
			}
			waitq_wake_all(&p->rx_wait);
		}
		if (inb(p->io + REG_LSR) & 0x20) { /* the transmitter wants more */
			for (unsigned n = 0; n < p->fifo && !ring_empty(&p->tx); n++) {
				outb(p->io + REG_DATA, ring_pop(&p->tx));
			}
			if (ring_empty(&p->tx) && (p->ier & IER_TX)) {
				p->ier &= (uint8_t)~IER_TX;
				outb(p->io + REG_IER, p->ier);
			}
			waitq_wake_all(&p->tx_wait);
		}
		(void)inb(p->io + REG_MSR);
	}
}

static void irq3(void)
{
	for (int i = 0; i < 3; i++) {
		if (ports[i].present && ports[i].irq == 3) {
			service(&ports[i]);
		}
	}
}

static void irq4(void)
{
	if (ports[1].present) {
		service(&ports[1]);
	}
}

static long com_read(struct device *dev, void *buf, size_t len)
{
	struct port *p = dev->driver_data;
	uint8_t *out = buf;
	size_t n = 0;
	uint32_t flags = cpu_irq_save();
	while (ring_empty(&p->rx)) {
		waitq_sleep(&p->rx_wait);
	}
	while (n < len && !ring_empty(&p->rx)) {
		out[n++] = ring_pop(&p->rx);
	}
	cpu_irq_restore(flags);
	return (long)n;
}

static long com_write(struct device *dev, const void *buf, size_t len)
{
	struct port *p = dev->driver_data;
	const uint8_t *in = buf;
	uint32_t flags = cpu_irq_save();
	for (size_t i = 0; i < len; i++) {
		while (ring_full(&p->tx)) {
			waitq_sleep(&p->tx_wait);
		}
		ring_push(&p->tx, in[i]);
		if (!(p->ier & IER_TX)) {
			p->ier |= IER_TX; /* an idle transmitter interrupts at once */
			outb(p->io + REG_IER, p->ier);
		}
	}
	cpu_irq_restore(flags);
	return (long)len;
}

static int com_poll(struct device *dev)
{
	struct port *p = dev->driver_data;
	return !ring_empty(&p->rx);
}

static long ctl_read(struct device *dev, uint32_t offset, void *buf, size_t len)
{
	struct port *p = dev->driver_data;
	uint8_t msr = inb(p->io + REG_MSR);
	char text[96];
	int n = ksnprintf(text, sizeof(text), "b%u l8 pn s1%s%s%s; %lu overruns\n", p->baud,
	                  (msr & MSR_CTS) ? " cts" : "", (msr & MSR_DSR) ? " dsr" : "",
	                  (msr & MSR_DCD) ? " dcd" : "", (unsigned long)p->overruns);
	if (offset >= (uint32_t)n) {
		return 0;
	}
	if (len > (size_t)n - offset) {
		len = (size_t)n - offset;
	}
	memcpy(buf, text + offset, len);
	return (long)len;
}

static long ctl_write(struct device *dev, uint32_t offset, const void *buf, size_t len)
{
	(void)offset;
	struct port *p = dev->driver_data;
	const char *s = buf;
	unsigned baud = 0;
	size_t i = 1;
	if (len < 2 || s[0] != 'b') {
		return -EINVAL;
	}
	for (; i < len && s[i] >= '0' && s[i] <= '9'; i++) {
		baud = baud * 10 + (unsigned)(s[i] - '0');
		if (baud > UART_HZ) {
			return -EINVAL;
		}
	}
	for (; i < len && (s[i] == '\n' || s[i] == ' '); i++) {
	}
	if (i != len || baud < 50) {
		return -EINVAL;
	}
	set_baud(p, baud);
	return (long)len;
}

static const struct char_device_ops com_ops = {
	.read = com_read, .write = com_write, .poll = com_poll,
};
static const struct char_device_ops ctl_ops = { .pread = ctl_read, .pwrite = ctl_write };

/* Loopback mode sends what is written straight back. */
static bool present(uint16_t io)
{
	if (inb(io + REG_LSR) == 0xFF) {
		return false; /* nothing on the bus */
	}
	outb(io + REG_MCR, MCR_LOOPBACK | 0x0F);
	outb(io + REG_DATA, 0xA5);
	for (int i = 0; i < 1000 && !(inb(io + REG_LSR) & LSR_DATA_READY); i++) {
	}
	bool ok = (inb(io + REG_LSR) & LSR_DATA_READY) && inb(io + REG_DATA) == 0xA5;
	outb(io + REG_MCR, MCR_DTR_RTS_OUT2);
	return ok;
}

void uart_init(void)
{
	bool irq3_used = false;
	for (int i = 0; i < 3; i++) {
		struct port *p = &ports[i];
		outb(p->io + REG_IER, 0);
		if (!present(p->io)) {
			continue;
		}
		p->present = true;
		p->rx = (struct ring)RING_INIT(p->rx_storage);
		p->tx = (struct ring)RING_INIT(p->tx_storage);
		p->rx_wait = (struct waitq)WAITQ_INIT;
		p->tx_wait = (struct waitq)WAITQ_INIT;
		outb(p->io + REG_FCR, 0xC7); /* FIFOs on and cleared, receive trigger at 14 */
		p->fifo = (inb(p->io + REG_IIR) & IIR_FIFO) == IIR_FIFO ? 16 : 1;
		(void)inb(p->io + REG_DATA);
		p->ier = IER_RX;
		set_baud(p, DEFAULT_BAUD);
		ksnprintf(p->dev.name, sizeof(p->dev.name), "com%d", i + 2);
		ksnprintf(p->ctl.name, sizeof(p->ctl.name), "com%dctl", i + 2);
		p->dev.class = p->ctl.class = DEVICE_CHAR;
		p->dev.char_ops = &com_ops;
		p->ctl.char_ops = &ctl_ops;
		p->dev.driver_data = p->ctl.driver_data = p;
		if (p->irq == 3 && !irq3_used) {
			irq_install_handler(3, irq3);
			irq3_used = true;
		} else if (p->irq == 4) {
			irq_install_handler(4, irq4); /* beside COM1's handler */
		}
		device_register(&p->dev);
		device_register(&p->ctl);
		kprintf("%s: %s UART at 0x%x, irq %d, %u baud\n", p->dev.name,
		        p->fifo == 16 ? "16550A" : "8250/16450", p->io, p->irq, p->baud);
	}
}
