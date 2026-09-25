#include "pic.h"
#include <stdint.h>
#include "io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

#define OCW2_EOI      0x20
#define OCW3_READ_ISR 0x0B

#define CASCADE_IRQ 2 /* the slave PIC is wired to the master's IRQ2 */

void pic_remap_and_mask(void)
{
	outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
	io_wait();
	outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
	io_wait();

	outb(PIC1_DATA, IRQ_BASE_VECTOR);     /* IRQ0-7  -> 0x20-0x27 */
	io_wait();
	outb(PIC2_DATA, IRQ_BASE_VECTOR + 8); /* IRQ8-15 -> 0x28-0x2F */
	io_wait();

	outb(PIC1_DATA, 1u << CASCADE_IRQ); /* tell PIC1 where the slave is */
	io_wait();
	outb(PIC2_DATA, CASCADE_IRQ);       /* tell PIC2 its cascade identity */
	io_wait();

	outb(PIC1_DATA, ICW4_8086);
	io_wait();
	outb(PIC2_DATA, ICW4_8086);
	io_wait();

	outb(PIC1_DATA, 0xFF);
	outb(PIC2_DATA, 0xFF);
}

static uint16_t mask_port(unsigned irq)
{
	return irq < 8 ? PIC1_DATA : PIC2_DATA;
}

void pic_mask(unsigned irq)
{
	uint16_t port = mask_port(irq);
	outb(port, inb(port) | (uint8_t)(1u << (irq % 8)));
}

void pic_unmask(unsigned irq)
{
	uint16_t port = mask_port(irq);
	outb(port, inb(port) & (uint8_t)~(1u << (irq % 8)));
	if (irq >= 8) {
		pic_unmask(CASCADE_IRQ);
	}
}

void pic_send_eoi(unsigned irq)
{
	if (irq >= 8) {
		outb(PIC2_CMD, OCW2_EOI);
	}
	outb(PIC1_CMD, OCW2_EOI);
}

bool pic_is_spurious(unsigned irq)
{
	uint16_t cmd = irq < 8 ? PIC1_CMD : PIC2_CMD;
	outb(cmd, OCW3_READ_ISR);
	return !(inb(cmd) & (1u << (irq % 8)));
}
