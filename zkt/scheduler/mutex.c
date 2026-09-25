#include "mutex.h"
#include "cpu.h"
#include "panic.h"

void mutex_lock(struct mutex *m)
{
	uint32_t flags = cpu_irq_save();
	if (m->owner == thread_current()) {
		panic("mutex_lock: already held by this thread");
	}
	/* A woken waiter re-checks: another thread may have taken the lock
	 * between the unlock and this thread getting to run. */
	while (m->owner) {
		waitq_sleep(&m->waiters);
	}
	m->owner = thread_current();
	cpu_irq_restore(flags);
}

void mutex_unlock(struct mutex *m)
{
	uint32_t flags = cpu_irq_save();
	if (m->owner != thread_current()) {
		panic("mutex_unlock: not held by this thread");
	}
	m->owner = 0;
	waitq_wake_one(&m->waiters);
	cpu_irq_restore(flags);
}
