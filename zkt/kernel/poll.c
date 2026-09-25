#include "poll.h"
#include "cpu.h"
#include "kerrno.h"
#include "process.h"
#include "sched.h"
#include "timer.h"
#include "usercopy.h"
#include "vfs.h"
#include "zkt_abi.h"

static struct waitq pollers = WAITQ_INIT;

void poll_notify(void)
{
	waitq_wake_all(&pollers);
}

long poll_files(struct process *p, void *ufds, uint32_t count, int32_t timeout_ms)
{
	struct zkt_pollfd fds[ZKT_FD_MAX];
	if (count > ZKT_FD_MAX) {
		return -EINVAL;
	}
	if (copy_from_user(fds, ufds, count * sizeof(fds[0]))) {
		return -EFAULT;
	}
	struct file *files[ZKT_FD_MAX];
	for (uint32_t i = 0; i < count; i++) {
		files[i] = process_fd(p, fds[i].fd);
		if (!files[i]) {
			return -EBADF;
		}
	}
	uint64_t deadline = timeout_ms < 0 ? 0 : timer_ticks() + ((uint64_t)timeout_ms * TIMER_HZ + 999) / 1000;
	long ready;
	/* Checking and sleeping with interrupts off: a notify can't slip in
	 * between (the waitq pattern, sched.h). */
	uint32_t flags = cpu_irq_save();
	for (;;) {
		ready = 0;
		for (uint32_t i = 0; i < count; i++) {
			fds[i].ready = vfs_poll(files[i]) > 0;
			ready += fds[i].ready;
		}
		if (ready || timeout_ms == 0) {
			break;
		}
		if (timeout_ms < 0) {
			waitq_sleep(&pollers);
		} else if (!waitq_sleep_until(&pollers, deadline)) {
			break;
		}
	}
	cpu_irq_restore(flags);
	return copy_to_user(ufds, fds, count * sizeof(fds[0])) ? -EFAULT : ready;
}
