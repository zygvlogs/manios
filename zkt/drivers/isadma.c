/* The ISA DMA controllers (isadma.h), written from Intel's 8237A data
 * sheet and the PC/AT's port assignments. */
#include "isadma.h"
#include "cpu.h"
#include "io.h"

#define MODE_SINGLE 0x40
#define MODE_AUTO   0x10
#define MODE_READ   0x08 /* memory to device: the 8237 "reads" memory */
#define MODE_WRITE  0x04

/* Per channel: its address, count and page registers. */
static const uint8_t ADDR[8] = { 0x00, 0x02, 0x04, 0x06, 0xC0, 0xC4, 0xC8, 0xCC };
static const uint8_t COUNT[8] = { 0x01, 0x03, 0x05, 0x07, 0xC2, 0xC6, 0xCA, 0xCE };
static const uint8_t PAGE[8] = { 0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A };

static uint16_t port_mask(int ch)
{
	return ch < 4 ? 0x0A : 0xD4;
}

static uint16_t port_mode(int ch)
{
	return ch < 4 ? 0x0B : 0xD6;
}

static uint16_t port_flipflop(int ch)
{
	return ch < 4 ? 0x0C : 0xD8;
}

void isa_dma_stop(int ch)
{
	outb(port_mask(ch), (uint8_t)(0x04 | (ch & 3)));
}

void isa_dma_start(int ch, uintptr_t phys, size_t len, bool to_device, bool repeat)
{
	bool wide = ch >= 4;
	uint32_t addr = wide ? (uint32_t)(phys >> 1) & 0xFFFF : (uint32_t)phys & 0xFFFF;
	uint32_t count = (uint32_t)(wide ? len / 2 : len) - 1;
	uint8_t page = (uint8_t)(wide ? (phys >> 16) & 0xFE : (phys >> 16) & 0xFF);
	uint32_t flags = cpu_irq_save();
	isa_dma_stop(ch);
	outb(port_flipflop(ch), 0);
	outb(port_mode(ch), (uint8_t)(MODE_SINGLE | (repeat ? MODE_AUTO : 0)
	                               | (to_device ? MODE_READ : MODE_WRITE) | (ch & 3)));
	outb(ADDR[ch], (uint8_t)addr);
	outb(ADDR[ch], (uint8_t)(addr >> 8));
	outb(PAGE[ch], page);
	outb(port_flipflop(ch), 0);
	outb(COUNT[ch], (uint8_t)count);
	outb(COUNT[ch], (uint8_t)(count >> 8));
	outb(port_mask(ch), (uint8_t)(ch & 3)); /* unmask */
	cpu_irq_restore(flags);
}

size_t isa_dma_remaining(int ch)
{
	uint32_t flags = cpu_irq_save();
	outb(port_flipflop(ch), 0);
	uint32_t lo = inb(COUNT[ch]), hi = inb(COUNT[ch]);
	cpu_irq_restore(flags);
	uint32_t n = ((lo | hi << 8) + 1) & 0xFFFF;
	return ch >= 4 ? n * 2 : n;
}
