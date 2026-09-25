#ifndef ZKT_SCHEDULER_SCHED_H
#define ZKT_SCHEDULER_SCHED_H

#include <stddef.h>
#include <stdint.h>

struct thread;

/* Turns the calling boot context into thread "main" and creates the idle
 * thread. Scheduling starts cooperative: threads switch only when they
 * yield, sleep or exit, until sched_enable_preemption(). Call once, with
 * interrupts still disabled, after heap_init(); timer ticks drive
 * wakeups once interrupts are enabled. */
void sched_init(void);

/* From now on the timer also switches threads every time slice. */
void sched_enable_preemption(void);

/* Creates a runnable kernel thread that calls entry(arg) and exits when
 * entry returns. Returns NULL when out of memory. `name` must outlive
 * the thread. */
struct thread *thread_create(const char *name, void (*entry)(void *), void *arg);

void thread_yield(void);

/* Blocks the calling thread until timer tick `tick`. Must not be called
 * from an IRQ handler: it would suspend whichever thread was interrupted. */
void thread_sleep_until(uint64_t tick);

__attribute__((noreturn)) void thread_exit(void);

/* The running thread's name, or NULL before sched_init(). */
const char *thread_current_name(void);

/* Threads not yet reclaimed, including main (until it exits) and idle. */
size_t sched_thread_count(void);

/* Called by the timer interrupt on every tick. */
void sched_tick(uint64_t now);

#endif
