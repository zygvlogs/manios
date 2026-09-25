#ifndef ZKT_SCHEDULER_SCHED_SELFTEST_H
#define ZKT_SCHEDULER_SCHED_SELFTEST_H

/* Run from thread "main" with interrupts enabled. The cooperative test
 * must run before sched_enable_preemption(), the preemptive one after;
 * each panics naming the first failed check. */
void sched_selftest_cooperative(void);
void sched_selftest_preemptive(void);

/* Mutex mutual exclusion under forced interleaving. */
void sched_selftest_sync(void);

#endif
