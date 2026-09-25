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

/* Sleeps at least `ms` milliseconds, at tick granularity, halting the
 * CPU between ticks. Panics if interrupts are disabled, since no tick
 * could ever end the sleep. */
void timer_sleep_ms(uint32_t ms);

#endif
