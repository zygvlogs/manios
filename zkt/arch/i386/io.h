/* i386 port I/O primitives. The only place these belong (see
 * docs/FOUNDING-PROPOSAL.md §1.3 on the arch/ HAL boundary). */
#ifndef ZKT_ARCH_I386_IO_H
#define ZKT_ARCH_I386_IO_H

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val)
{
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
	uint8_t ret;
	__asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

static inline void outw(uint16_t port, uint16_t val)
{
	__asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
	uint16_t ret;
	__asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
	return ret;
}

/* Write to the unused POST-diagnostic port 0x80 as a ~1us delay, so a
 * slow device has time to react to the previous out/in. Standard
 * technique on real PC hardware. */
static inline void io_wait(void)
{
	outb(0x80, 0);
}

#endif
