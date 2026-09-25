#include "sched_selftest.h"
#include <stdint.h>
#include "heap.h"
#include "kstring.h"
#include "mutex.h"
#include "panic.h"
#include "pmm.h"
#include "sched.h"
#include "timer.h"

static void expect(int ok, const char *failure)
{
	if (!ok) {
		panic(failure);
	}
}

/* Exited threads are reclaimed on the next switch, so yielding until the
 * count drops also waits for their stacks to be freed. Ticks keep
 * arriving while main spins here, so a scheduler that never runs the
 * other threads fails with a message instead of hanging. */
static void wait_for_threads(size_t baseline)
{
	uint64_t deadline = timer_uptime_ms() + 2000;
	while (sched_thread_count() > baseline) {
		expect(timer_uptime_ms() < deadline, "selftest: threads did not finish within 2 s");
		thread_yield();
	}
}

static void create(const char *name, void (*entry)(void *), void *arg)
{
	expect(thread_create(name, entry, arg) != NULL, "selftest: thread_create failed");
}

static char coop_log[8];
static volatile int coop_len;

static void coop_worker(void *letter)
{
	for (int i = 0; i < 3; i++) {
		coop_log[coop_len++] = (char)(uintptr_t)letter;
		thread_yield();
	}
}

static void coop_round(void)
{
	size_t baseline = sched_thread_count();
	coop_len = 0;
	create("coop-a", coop_worker, (void *)'A');
	create("coop-b", coop_worker, (void *)'B');
	wait_for_threads(baseline);
	expect(coop_len == 6 && memcmp(coop_log, "ABABAB", 6) == 0,
	       "selftest: yield did not alternate between runnable threads");
}

void sched_selftest_cooperative(void)
{
	/* The first round may grow the heap and create the stack region's
	 * page table; a second round must then cost nothing. */
	coop_round();
	size_t frames = pmm_free_frames();
	size_t heap_bytes = heap_used();
	coop_round();
	expect(pmm_free_frames() == frames, "selftest: exited threads leaked stack memory");
	expect(heap_used() == heap_bytes, "selftest: exited threads leaked heap memory");
	expect(heap_check() == 0, "selftest: heap inconsistent after thread exit");
}

static volatile uint32_t spin_count[2];
static volatile int stop_spinning;

static void spinner(void *index)
{
	while (!stop_spinning) {
		spin_count[(uintptr_t)index]++;
	}
}

static char wake_log[2];
static volatile int wake_len;
static volatile int slept_too_little;

static void sleeper(void *ms_arg)
{
	uint32_t ms = (uint32_t)(uintptr_t)ms_arg;
	uint64_t start = timer_uptime_ms();
	timer_sleep_ms(ms);
	if (timer_uptime_ms() - start < ms) {
		slept_too_little = 1;
	}
	wake_log[wake_len++] = ms > 50 ? 'L' : 'S';
}

void sched_selftest_preemptive(void)
{
	size_t baseline = sched_thread_count();

	/* The spinners never yield: main's sleep can only end, and each
	 * spinner can only run at all, if the timer preempts them. */
	stop_spinning = 0;
	create("spin-0", spinner, (void *)0);
	create("spin-1", spinner, (void *)1);
	timer_sleep_ms(100);
	stop_spinning = 1;
	wait_for_threads(baseline);
	expect(spin_count[0] && spin_count[1], "selftest: a spinning thread never ran");

	wake_len = 0;
	create("sleep-long", sleeper, (void *)60);
	create("sleep-short", sleeper, (void *)20);
	wait_for_threads(baseline);
	expect(!slept_too_little, "selftest: timer_sleep_ms returned early");
	expect(wake_len == 2 && wake_log[0] == 'S' && wake_log[1] == 'L',
	       "selftest: sleepers did not wake in deadline order");
}

static struct mutex test_lock = MUTEX_INIT;
static volatile uint32_t shared_counter;

static void locked_incrementer(void *unused)
{
	(void)unused;
	for (int i = 0; i < 20; i++) {
		mutex_lock(&test_lock);
		uint32_t v = shared_counter;
		thread_yield(); /* invite the other thread in mid-update */
		shared_counter = v + 1;
		mutex_unlock(&test_lock);
	}
}

void sched_selftest_sync(void)
{
	size_t baseline = sched_thread_count();
	shared_counter = 0;
	create("lock-a", locked_incrementer, 0);
	create("lock-b", locked_incrementer, 0);
	wait_for_threads(baseline);
	expect(shared_counter == 40, "selftest: mutex did not serialize a read-modify-write");
}
