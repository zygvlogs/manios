#include "pic.h"
#include "io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

#define PIC1_VECTOR_OFFSET 0x20 /* IRQ0-7  -> vectors 0x20-0x27 */
#define PIC2_VECTOR_OFFSET 0x28 /* IRQ8-15 -> vectors 0x28-0x2F */

void pic_remap_and_mask(void)
{
	outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
	io_wait();
	outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
	io_wait();

	outb(PIC1_DATA, PIC1_VECTOR_OFFSET);
	io_wait();
	outb(PIC2_DATA, PIC2_VECTOR_OFFSET);
	io_wait();

	outb(PIC1_DATA, 0x04); /* tell PIC1 there is a slave PIC at IRQ2 */
	io_wait();
	outb(PIC2_DATA, 0x02); /* tell PIC2 its cascade identity */
	io_wait();

	outb(PIC1_DATA, ICW4_8086);
	io_wait();
	outb(PIC2_DATA, ICW4_8086);
	io_wait();

	/* Mask every line: no IRQ handlers exist yet (M3/M5). */
	outb(PIC1_DATA, 0xFF);
	outb(PIC2_DATA, 0xFF);
}
