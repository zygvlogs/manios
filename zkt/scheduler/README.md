# zkt/scheduler/

Kernel threads and the scheduler. Design and verification:
[M4 notes](../../docs/milestones/M4-multitasking.md).

- `sched.c` — threads, round-robin run queue with 20 ms slices,
  cooperative or preemptive mode, sleep list, idle thread, exit and
  reaping
- `sched.c` also provides wait queues (condition-variable style,
  wakeable from IRQ handlers)
- `mutex.c` — sleeping mutex built on a wait queue
- Each thread carries a namespace (`zkt/fs/namespace.c`), inherited
  from its creator
- `sched_selftest.c` — boot-time checks of yield, preemption, sleep
  order, resource reclamation and mutual exclusion, which `make test`
  gates on

Processes (a thread plus its own address space) arrive with userspace
in M8, and priorities after that; see
[`docs/FOUNDING-PROPOSAL.md` §2.6](../../docs/FOUNDING-PROPOSAL.md#26-scheduler--processesthreads).
