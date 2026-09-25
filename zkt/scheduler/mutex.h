#ifndef ZKT_SCHEDULER_MUTEX_H
#define ZKT_SCHEDULER_MUTEX_H

#include "sched.h"

/* A sleeping lock: waiters block instead of spinning, so it may be held
 * across slow operations (disk I/O, a UART draining). Not for IRQ
 * handlers or code running with interrupts disabled. */
struct mutex {
	struct thread *owner;
	struct waitq waiters;
};
#define MUTEX_INIT { 0, WAITQ_INIT }

/* Panics if the calling thread already holds it. */
void mutex_lock(struct mutex *m);

/* Panics unless the calling thread holds it. */
void mutex_unlock(struct mutex *m);

#endif
