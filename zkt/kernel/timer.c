#include "timer.h"
#include "clock.h"
#include "cpu.h"
#include "panic.h"

/* Keeps millisecond conversion to a multiply: a 64-bit division would
 * pull in libgcc, which is built for i686
 * (docs/milestones/M2-memory-management.md). */
_Static_assert(1000 % TIMER_HZ == 0, "TIMER_HZ must divide 1000");
#define MS_PER_TICK (1000 / TIMER_HZ)

static volatile uint64_t ticks;

static void timer_tick(void)
{
	ticks++;
}

void timer_init(void)
{
	clock_start_periodic(TIMER_HZ, timer_tick);
}

/* A 64-bit load is two 32-bit loads on i386; a tick landing between them
 * would tear the value, so read it with interrupts off. */
uint64_t timer_ticks(void)
{
	uint32_t flags = cpu_irq_save();
	uint64_t t = ticks;
	cpu_irq_restore(flags);
	return t;
}

uint64_t timer_uptime_ms(void)
{
	return timer_ticks() * MS_PER_TICK;
}

void timer_sleep_ms(uint32_t ms)
{
	if (ms == 0) {
		return;
	}
	if (!cpu_interrupts_enabled()) {
		panic("timer_sleep_ms: called with interrupts disabled");
	}

	/* +1: the current tick is already partly over. */
	uint64_t until = timer_ticks() + (ms + MS_PER_TICK - 1) / MS_PER_TICK + 1;
	while (timer_ticks() < until) {
		cpu_wait_for_interrupt();
	}
}
