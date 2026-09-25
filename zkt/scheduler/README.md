# zkt/scheduler/

Kernel threads and the scheduler. Design and verification:
[M4 notes](../../docs/milestones/M4-multitasking.md).

- `sched.c` — threads, round-robin run queue with 20 ms slices,
  cooperative or preemptive mode, sleep list, idle thread, exit and
  reaping
- `sched_selftest.c` — boot-time checks of yield, preemption, sleep
  order and resource reclamation, which `make test` gates on

Processes (a thread plus its own address space) arrive with userspace
in M8, and priorities after that; see
[`docs/FOUNDING-PROPOSAL.md` §2.6](../../docs/FOUNDING-PROPOSAL.md#26-scheduler--processesthreads).
