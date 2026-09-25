/* Round-robin kernel thread scheduler (FOUNDING-PROPOSAL §2.6). Single
 * CPU, so every scheduler structure is protected by disabling
 * interrupts. Design notes: docs/milestones/M4-multitasking.md. */
#include "sched.h"
#include <stdbool.h>
#include "context.h"
#include "cpu.h"
#include "heap.h"
#include "kstack.h"
#include "namespace.h"
#include "panic.h"
#include "timer.h"
#include "vmm.h"

#define TIME_SLICE_MS 20
#define TIME_SLICE_TICKS (TIME_SLICE_MS * TIMER_HZ / 1000)
_Static_assert(TIME_SLICE_TICKS > 0, "time slice shorter than one tick");

enum thread_state {
	THREAD_RUNNABLE,
	THREAD_RUNNING,
	THREAD_SLEEPING,
	THREAD_BLOCKED,
	THREAD_DEAD,
};

static const char *const STATE_NAMES[] = {
	[THREAD_RUNNABLE] = "ready",
	[THREAD_RUNNING] = "running",
	[THREAD_SLEEPING] = "sleeping",
	[THREAD_BLOCKED] = "blocked",
	[THREAD_DEAD] = "dead",
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
	struct namespace *ns;    /* inherited from the creating thread (ADR-0003) */
	struct address_space *as; /* NULL: the kernel's own */
	void *process;           /* owning user process, or NULL */
	struct thread *next;       /* run queue or wait queue link */
	struct thread *sleep_next; /* sleep list link: a timed wait is on both */
	struct waitq *waiting_on;  /* the wait queue of a timed wait */
	bool timed_out;
	struct thread *all_next;   /* every thread not yet reclaimed */
};

static struct thread *current;
static struct thread *idle;
static struct thread *run_head, *run_tail; /* FIFO of runnable threads */
static struct thread *sleepers;            /* sorted by wake_tick */
static struct thread *switched_from;       /* for finish_switch() */
static struct thread *all_threads;
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

static void all_threads_add(struct thread *t)
{
	t->all_next = all_threads;
	all_threads = t;
}

static void all_threads_remove(struct thread *t)
{
	struct thread **link = &all_threads;
	while (*link != t) {
		link = &(*link)->all_next;
	}
	*link = t->all_next;
}

/* Equal wake ticks keep sleep order, so wakeups are deterministic. */
static void sleep_insert(struct thread *t)
{
	struct thread **link = &sleepers;
	while (*link && (*link)->wake_tick <= t->wake_tick) {
		link = &(*link)->sleep_next;
	}
	t->sleep_next = *link;
	*link = t;
}

static void sleep_remove(struct thread *t)
{
	struct thread **link = &sleepers;
	while (*link && *link != t) {
		link = &(*link)->sleep_next;
	}
	if (*link) {
		*link = t->sleep_next;
	}
}

static void waitq_remove(struct waitq *wq, struct thread *t)
{
	struct thread *prev = NULL;
	for (struct thread *c = wq->head; c; prev = c, c = c->next) {
		if (c == t) {
			if (prev) {
				prev->next = c->next;
			} else {
				wq->head = c->next;
			}
			if (wq->tail == c) {
				wq->tail = prev;
			}
			return;
		}
	}
}

/* Runs on the new thread right after every switch. A thread that exited
 * can only be freed from another thread's stack, i.e. here. */
static void finish_switch(void)
{
	struct thread *prev = switched_from;
	if (prev->state == THREAD_DEAD) {
		all_threads_remove(prev);
		ns_unref(prev->ns);
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
	vmm_as_activate(next->as);
	if (next->stack_top) {
		arch_set_kernel_stack(next->stack_top);
	}
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
	t->sleep_next = NULL;
	t->waiting_on = NULL;
	t->timed_out = false;
	t->ns = current ? current->ns : NULL;
	ns_ref(t->ns);
	t->as = NULL;
	t->process = NULL;

	uint32_t flags = cpu_irq_save();
	t->id = next_id++;
	thread_count++;
	all_threads_add(t);
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
	main_thread->sleep_next = NULL;
	main_thread->waiting_on = NULL;
	main_thread->timed_out = false;
	main_thread->ns = NULL;
	main_thread->as = NULL;
	main_thread->process = NULL;
	thread_count++;
	all_threads_add(main_thread);

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
	return thread_create_in(name, entry, arg, NULL, NULL);
}

struct thread *thread_create_in(const char *name, void (*entry)(void *), void *arg,
                                struct address_space *as, void *process)
{
	struct thread *t = thread_alloc(name, entry, arg);
	if (t) {
		t->as = as;
		t->process = process;
		uint32_t flags = cpu_irq_save();
		run_enqueue(t);
		cpu_irq_restore(flags);
	}
	return t;
}

struct address_space *thread_address_space(void)
{
	return current ? current->as : NULL;
}

void *thread_process(void)
{
	return current ? current->process : NULL;
}

void thread_set_address_space(struct address_space *as)
{
	uint32_t flags = cpu_irq_save();
	current->as = as;
	vmm_as_activate(as);
	cpu_irq_restore(flags);
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

struct thread *thread_current(void)
{
	return current;
}

struct namespace *thread_namespace(void)
{
	return current ? current->ns : NULL;
}

void thread_set_namespace(struct namespace *ns)
{
	struct namespace *old = current->ns;
	ns_ref(ns);
	current->ns = ns;
	ns_unref(old);
}

const char *thread_current_name(void)
{
	return current ? current->name : NULL;
}

size_t sched_snapshot(struct thread_info *out, size_t max)
{
	size_t n = 0;
	uint32_t flags = cpu_irq_save();
	for (struct thread *t = all_threads; t && n < max; t = t->all_next, n++) {
		out[n].id = t->id;
		out[n].name = t->name;
		out[n].state = STATE_NAMES[t->state];
	}
	cpu_irq_restore(flags);
	return n;
}

/* A thread woken from an IRQ that interrupted idle should not wait for
 * the next tick to run. Switching here, inside the IRQ, is the same
 * thing the timer does for preemption. */
static void resched_if_idle(void)
{
	if (current && current == idle && run_head) {
		schedule();
	}
}

void waitq_sleep(struct waitq *wq)
{
	if (cpu_interrupts_enabled()) {
		panic("waitq_sleep: interrupts must be disabled around the condition check");
	}
	current->state = THREAD_BLOCKED;
	current->next = NULL;
	if (wq->tail) {
		wq->tail->next = current;
	} else {
		wq->head = current;
	}
	wq->tail = current;
	schedule();
}

bool waitq_sleep_until(struct waitq *wq, uint64_t tick)
{
	if (cpu_interrupts_enabled()) {
		panic("waitq_sleep_until: interrupts must be disabled around the condition check");
	}
	current->waiting_on = wq;
	current->timed_out = false;
	current->wake_tick = tick;
	sleep_insert(current);
	waitq_sleep(wq);
	return !current->timed_out;
}

static struct thread *waitq_pop(struct waitq *wq)
{
	struct thread *t = wq->head;
	if (t) {
		wq->head = t->next;
		if (!wq->head) {
			wq->tail = NULL;
		}
		if (t->waiting_on) { /* a timed wait, woken in time */
			sleep_remove(t);
			t->waiting_on = NULL;
		}
		t->state = THREAD_RUNNABLE;
		run_enqueue(t);
	}
	return t;
}

void waitq_wake_one(struct waitq *wq)
{
	uint32_t flags = cpu_irq_save();
	waitq_pop(wq);
	resched_if_idle();
	cpu_irq_restore(flags);
}

void waitq_wake_all(struct waitq *wq)
{
	uint32_t flags = cpu_irq_save();
	while (waitq_pop(wq)) {
	}
	resched_if_idle();
	cpu_irq_restore(flags);
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
		sleepers = t->sleep_next;
		if (t->waiting_on) { /* a timed wait ran out */
			waitq_remove(t->waiting_on, t);
			t->waiting_on = NULL;
			t->timed_out = true;
		}
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
