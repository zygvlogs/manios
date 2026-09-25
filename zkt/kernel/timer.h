#ifndef ZKT_KERNEL_TIMER_H
#define ZKT_KERNEL_TIMER_H

#include <stdint.h>

/* The system tick rate: the scheduler's time slice unit from M4 on. */
#define TIMER_HZ 100

/* Starts the periodic hardware clock. Ticks only arrive once interrupts
 * are enabled. */
void timer_init(void);

uint64_t timer_ticks(void);
uint64_t timer_uptime_ms(void);

/* Blocks the calling thread for at least `ms` milliseconds, at tick
 * granularity. Not for IRQ handlers (see thread_sleep_until()). */
void timer_sleep_ms(uint32_t ms);

#endif
