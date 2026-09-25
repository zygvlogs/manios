#ifndef ZKT_SCHEDULER_SCHED_H
#define ZKT_SCHEDULER_SCHED_H

#include <stddef.h>
#include <stdint.h>

struct thread;
struct namespace;

struct thread_info {
	uint32_t id;
	const char *name;
	const char *state;
};

/* Threads blocked until an event. Use it like a condition variable:
 *
 *     flags = cpu_irq_save();
 *     while (!condition)
 *         waitq_sleep(&wq);
 *     ...consume...
 *     cpu_irq_restore(flags);
 *
 * and have the producer (a thread or an IRQ handler) make the condition
 * true, then call waitq_wake_one()/waitq_wake_all(). Checking with
 * interrupts off is what makes the wakeup impossible to miss. */
struct waitq {
	struct thread *head, *tail;
};
#define WAITQ_INIT { 0, 0 }

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

/* The running thread, or NULL before sched_init(). */
struct thread *thread_current(void);

/* The calling thread's namespace. New threads share their creator's;
 * thread_set_namespace() switches to another (taking a reference). */
struct namespace *thread_namespace(void);
void thread_set_namespace(struct namespace *ns);

/* The running thread's name, or NULL before sched_init(). */
const char *thread_current_name(void);

/* Copies up to `max` entries describing live threads; returns how many. */
size_t sched_snapshot(struct thread_info *out, size_t max);

/* Blocks the calling thread on wq. Interrupts must be disabled (panics
 * otherwise); they are disabled again when it returns. */
void waitq_sleep(struct waitq *wq);

/* Safe from IRQ handlers. */
void waitq_wake_one(struct waitq *wq);
void waitq_wake_all(struct waitq *wq);

/* Threads not yet reclaimed, including main (until it exits) and idle. */
size_t sched_thread_count(void);

/* Called by the timer interrupt on every tick. */
void sched_tick(uint64_t now);

#endif
