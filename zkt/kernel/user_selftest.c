#include "user_selftest.h"
#include <stdbool.h>
#include "heap.h"
#include "kerrno.h"
#include "kprintf.h"
#include "memlayout.h"
#include "panic.h"
#include "pmm.h"
#include "process.h"
#include "sched.h"
#include "timer.h"
#include "vfs.h"
#include "zkt_abi.h"

/* Runs `path` as a process of the kernel and returns its wait status. */
static int run(const char *path)
{
	char *argv[] = { (char *)path, 0 };
	int pid = process_spawn(path, 1, argv, 0);
	if (pid < 0) {
		kprintf("selftest: user: cannot start %s: %s\n", path, kstrerror(pid));
		panic("selftest: user: spawn failed");
	}
	int status;
	if (process_wait(0, pid, &status) != pid) {
		panic("selftest: user: wait failed");
	}
	return status;
}

/* Process threads are reclaimed on a later switch; their kernel stacks
 * are only free once the thread count is back down. */
static void wait_for_threads(size_t baseline)
{
	uint64_t deadline = timer_uptime_ms() + 2000;
	while (sched_thread_count() > baseline) {
		if (timer_uptime_ms() > deadline) {
			panic("selftest: user: process threads were not reclaimed within 2 s");
		}
		thread_yield();
	}
}

/* Frames in use, not counting the heap's growth (which is permanent). */
static size_t frames_in_use(void)
{
	return pmm_usable_frames() - pmm_free_frames() - heap_mapped_bytes() / PAGE_SIZE;
}

static bool resolves(const char *path)
{
	struct file *f;
	if (vfs_open(path, OREAD, &f) != 0) {
		return false;
	}
	vfs_close(f);
	return true;
}

/* Runs a test program, which must exit 0 and leave no frames or heap
 * behind once its threads are reclaimed. */
static void run_checked(const char *path)
{
	size_t baseline = sched_thread_count();
	size_t frames = frames_in_use();
	size_t heap_bytes = heap_used();

	int status = run(path);
	wait_for_threads(baseline);
	if (status != 0) {
		kprintf("selftest: user: %s status 0x%x\n", path, status);
		panic("selftest: user: a conformance test failed");
	}
	if (frames_in_use() != frames) {
		kprintf("selftest: user: %lu frames before %s, %lu after\n", frames, path, frames_in_use());
		panic("selftest: user: processes leaked physical memory");
	}
	if (heap_used() != heap_bytes || heap_check() != 0) {
		panic("selftest: user: processes leaked heap memory");
	}
}

void user_selftest(void)
{
	/* The first process also creates kernel tables that stay. */
	size_t baseline = sched_thread_count();
	if (run("/bin/hello") != 0) {
		panic("selftest: user: /bin/hello did not exit 0");
	}
	wait_for_threads(baseline);

	run_checked("/boot/test/utest");
	/* utest forked its namespace before binding anything. */
	if (resolves("/n/bin") || !resolves("/bin/hello")) {
		panic("selftest: user: a process's private binds leaked into the kernel's namespace");
	}
}

void libc_selftest(void)
{
	run_checked("/boot/test/ctest");
}

void gfx_selftest(void)
{
	run_checked("/boot/test/gtest");
}

void channel_selftest(void)
{
	run_checked("/boot/test/ztest");
}

void cluster_selftest(void)
{
	run_checked("/boot/test/cltest");
}
