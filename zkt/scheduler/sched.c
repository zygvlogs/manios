/* Round-robin kernel thread scheduler (FOUNDING-PROPOSAL §2.6). Single
 * CPU, so every scheduler structure is protected by disabling
 * interrupts. Design notes: docs/milestones/M4-multitasking.md. */
#include "sched.h"
#include <stdbool.h>
#include "context.h"
#include "cpu.h"
#include "heap.h"
#include "kstack.h"
#include "panic.h"
#include "timer.h"

#define TIME_SLICE_MS 20
#define TIME_SLICE_TICKS (TIME_SLICE_MS * TIMER_HZ / 1000)
_Static_assert(TIME_SLICE_TICKS > 0, "time slice shorter than one tick");

enum thread_state {
	THREAD_RUNNABLE,
	THREAD_RUNNING,
	THREAD_SLEEPING,
	THREAD_DEAD,
};

struct thread {
	uint32_t id;
	const char *name;
	enum thread_state state;
	uintptr_t sp;        /* saved stack pointer while switched out */
	uintptr_t stack_top; /* 0 for main, which keeps the static boot stack */
	uint64_t wake_tick;
	void (*entry)(void *);
	void *arg;
	struct thread *next; /* run queue or sleep list link */
};

static struct thread *current;
static struct thread *idle;
static struct thread *run_head, *run_tail; /* FIFO of runnable threads */
static struct thread *sleepers;            /* sorted by wake_tick */
static struct thread *switched_from;       /* for finish_switch() */
static bool preemptive;
static unsigned slice_left;
static uint32_t next_id;
static size_t thread_count;

static void run_enqueue(struct thread *t)
{
	t->next = NULL;
	if (run_tail) {
		run_tail->next = t;
	} else {
		run_head = t;
	}
	run_tail = t;
}

static struct thread *run_dequeue(void)
{
	struct thread *t = run_head;
	if (t) {
		run_head = t->next;
		if (!run_head) {
			run_tail = NULL;
		}
	}
	return t;
}

/* Equal wake ticks keep sleep order, so wakeups are deterministic. */
static void sleep_insert(struct thread *t)
{
	struct thread **link = &sleepers;
	while (*link && (*link)->wake_tick <= t->wake_tick) {
		link = &(*link)->next;
	}
	t->next = *link;
	*link = t;
}

/* Runs on the new thread right after every switch. A thread that exited
 * can only be freed from another thread's stack, i.e. here. */
static void finish_switch(void)
{
	struct thread *prev = switched_from;
	if (prev->state == THREAD_DEAD) {
		if (prev->stack_top) {
			kstack_free(prev->stack_top);
		}
		kfree(prev);
		thread_count--;
	}
}

/* Interrupts must be disabled. The current thread stays runnable unless
 * the caller already marked it sleeping or dead. */
static void schedule(void)
{
	struct thread *prev = current;

	if (prev->state == THREAD_RUNNING) {
		prev->state = THREAD_RUNNABLE;
		if (prev != idle) {
			run_enqueue(prev);
		}
	}

	struct thread *next = run_dequeue();
	if (!next) {
		next = idle;
	}
	next->state = THREAD_RUNNING;
	slice_left = TIME_SLICE_TICKS;
	if (next == prev) {
		return;
	}

	current = next;
	switched_from = prev;
	arch_context_switch(&prev->sp, next->sp);
	finish_switch();
}

/* First code a new thread runs, arriving from schedule() with interrupts
 * disabled. */
static void thread_start(void)
{
	finish_switch();
	cpu_enable_interrupts();
	current->entry(current->arg);
	thread_exit();
}

static struct thread *thread_alloc(const char *name, void (*entry)(void *), void *arg)
{
	struct thread *t = kmalloc(sizeof(*t));
	if (!t) {
		return NULL;
	}
	uintptr_t top = kstack_alloc();
	if (!top) {
		kfree(t);
		return NULL;
	}

	t->name = name;
	t->state = THREAD_RUNNABLE;
	t->stack_top = top;
	t->sp = arch_context_init(top, thread_start);
	t->entry = entry;
	t->arg = arg;
	t->next = NULL;

	uint32_t flags = cpu_irq_save();
	t->id = next_id++;
	thread_count++;
	cpu_irq_restore(flags);
	return t;
}

static void idle_loop(void *unused)
{
	(void)unused;
	for (;;) {
		cpu_idle();
	}
}

void sched_init(void)
{
	struct thread *main_thread = kmalloc(sizeof(*main_thread));
	if (!main_thread) {
		panic("sched_init: out of memory");
	}
	main_thread->id = next_id++;
	main_thread->name = "main";
	main_thread->state = THREAD_RUNNING;
	main_thread->stack_top = 0;
	main_thread->next = NULL;
	thread_count++;

	idle = thread_alloc("idle", idle_loop, NULL);
	if (!idle) {
		panic("sched_init: out of memory");
	}

	slice_left = TIME_SLICE_TICKS;
	current = main_thread;
}

void sched_enable_preemption(void)
{
	preemptive = true;
}

struct thread *thread_create(const char *name, void (*entry)(void *), void *arg)
{
	struct thread *t = thread_alloc(name, entry, arg);
	if (t) {
		uint32_t flags = cpu_irq_save();
		run_enqueue(t);
		cpu_irq_restore(flags);
	}
	return t;
}

void thread_yield(void)
{
	uint32_t flags = cpu_irq_save();
	schedule();
	cpu_irq_restore(flags);
}

void thread_sleep_until(uint64_t tick)
{
	uint32_t flags = cpu_irq_save();
	current->state = THREAD_SLEEPING;
	current->wake_tick = tick;
	sleep_insert(current);
	schedule();
	cpu_irq_restore(flags);
}

__attribute__((noreturn)) void thread_exit(void)
{
	cpu_irq_save();
	current->state = THREAD_DEAD;
	schedule();
	panic("thread_exit: a dead thread was scheduled again");
}

const char *thread_current_name(void)
{
	return current ? current->name : NULL;
}

size_t sched_thread_count(void)
{
	return thread_count;
}

void sched_tick(uint64_t now)
{
	if (!current) {
		return;
	}

	while (sleepers && sleepers->wake_tick <= now) {
		struct thread *t = sleepers;
		sleepers = t->next;
		t->state = THREAD_RUNNABLE;
		run_enqueue(t);
	}

	/* The idle thread gives way as soon as anything is runnable, in both
	 * modes: that is a wakeup, not preemption. */
	if (current == idle) {
		if (run_head) {
			schedule();
		}
	} else if (preemptive && --slice_left == 0) {
		schedule();
	}
}
