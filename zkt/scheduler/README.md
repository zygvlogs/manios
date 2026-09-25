# zkt/scheduler/

Kernel threads and the scheduler. Design and verification:
[M4 notes](../../docs/milestones/M4-multitasking.md).

- `sched.c` — threads, round-robin run queue with 20 ms slices,
  cooperative or preemptive mode, sleep list, idle thread, exit and
  reaping
- `sched.c` also provides wait queues (condition-variable style,
  wakeable from IRQ handlers), with optional deadlines
  (`waitq_sleep_until`, M10)
- `mutex.c` — sleeping mutex built on a wait queue
- Each thread carries a namespace (`zkt/fs/namespace.c`), inherited
  from its creator, and an address space
- `sched_selftest.c` — boot-time checks of yield, preemption, sleep
  order, resource reclamation and mutual exclusion, which `make test`
  gates on

A thread can run in a user address space and belong to a user process
(`thread_create_in`); the scheduler switches address space and the
TSS's kernel stack with it. Processes themselves live in
`zkt/kernel/process.c` (M8). Priorities come later; see
[`docs/FOUNDING-PROPOSAL.md` §2.6](../../docs/FOUNDING-PROPOSAL.md#26-scheduler--processesthreads).
