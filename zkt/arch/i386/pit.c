#include "clock.h"
#include "cpu.h"
#include "io.h"
#include "panic.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_INPUT_HZ 1193182u /* the PC's 14.31818 MHz crystal / 12 */

/* Channel 0, low byte then high byte, mode 3 (square wave), binary. */
#define PIT_CMD_CH0_SQUARE_WAVE 0x36

#define PIT_IRQ 0

void clock_start_periodic(uint32_t hz, irq_handler_t tick)
{
	uint32_t divisor = hz ? PIT_INPUT_HZ / hz : 0;
	if (divisor < 2 || divisor > 0xFFFF) {
		panic("pit: requested rate is outside what the PIT can divide to");
	}

	outb(PIT_COMMAND, PIT_CMD_CH0_SQUARE_WAVE);
	outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
	outb(PIT_CHANNEL0, (uint8_t)(divisor >> 8));

	irq_install_handler(PIT_IRQ, tick);
}

uint32_t clock_fine(void)
{
	uint32_t flags = cpu_irq_save();
	outb(PIT_COMMAND, 0x00); /* latch channel 0's count */
	uint32_t lo = inb(PIT_CHANNEL0);
	uint32_t hi = inb(PIT_CHANNEL0);
	cpu_irq_restore(flags);
	return hi << 8 | lo;
}
