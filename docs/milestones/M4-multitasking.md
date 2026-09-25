# M4 — Multitasking (Kernel Threads)

**Status:** Achieved (2026-09-25). Implements the kernel-thread part of
[FOUNDING-PROPOSAL.md §2.6](../FOUNDING-PROPOSAL.md#26-scheduler--processesthreads).
§2.6 sketched M4 as cooperative only, with preemption after it. The
roadmap table says "cooperative then preemptive", and both turned out
to be small once the M3 timer existed, so M4 delivers both.
Processes (a thread plus its own address space) arrive with userspace
in M8.

## What M4 delivers

| Piece | Files | Summary |
|---|---|---|
| Context switch | `zkt/arch/i386/context_switch.S`, `context.c`, `context.h` | Save callee-saved registers, swap stacks; lay out a new thread's first frame |
| Scheduler | `zkt/scheduler/sched.c`, `sched.h` | Round-robin FIFO, 20 ms slices, sleep list, idle thread, exit and reaping |
| Thread stacks | `zkt/mm/kstack.c` | 8 KiB stacks at `0xE1000000`, each above an unmapped guard page |
| Locking | `heap.c`, `pmm.c`, `paging.c`, `kconsole.c` | Each operation runs with interrupts off |
| Double-fault handler | `zkt/arch/i386/tss.c`, `gdt.c`, `idt.c` | Task gate on vector 8 → its own TSS and stack |
| Arch bring-up | `zkt/arch/i386/arch.c` | `arch_early_init()`: GDT, TSS, IDT, PIC |
| Self-test | `zkt/scheduler/sched_selftest.c` | Cooperative and preemptive checks at every boot |

## How it fits together

- `sched_init()` turns the boot context into thread **main**, which
  keeps the static boot stack, and creates the **idle** thread.
  `kernel_main` runs the tests, then calls `thread_exit()`, and idle
  (`sti; hlt` forever) keeps the CPU.
- `schedule()` always runs with interrupts off. A running thread goes to
  the back of the run queue unless it is sleeping or dead. Idle runs
  only when the queue is empty and is never queued itself.
- **Preemption** comes from the timer IRQ: `timer_tick()` →
  `sched_tick()` → `schedule()`, right inside the interrupt handler. The
  interrupted thread's IRQ frame stays on its own stack until it is
  resumed and returns through `iret`. This is why M3 acknowledges the
  PIC before running a handler.
- Nothing is lost across a switch, because each thread's interrupt
  state belongs to that thread: `thread_yield()` and friends
  save/restore flags around `schedule()`, a preempted thread gets its
  IF back from `iret`, and a new thread's first code (`thread_start`)
  enables interrupts itself.
- **Exit:** a thread can't free the stack it is running on. So
  `thread_exit()` only marks the thread dead and switches away, and
  `finish_switch()` frees it from the next thread's stack. At most one
  thread is ever waiting to be freed.
- **Sleeping** puts the thread on a list sorted by wake tick. Equal
  ticks keep insertion order, so wakeup order is deterministic.
  `timer_sleep_ms()` now blocks the calling thread instead of halting
  the CPU.

## Design decisions

- **Locking means interrupts off.** With one CPU, the only way another
  thread can run in the middle of an operation is through an interrupt,
  so `cpu_irq_save()` / `cpu_irq_restore()` around each heap, PMM, VMM
  and console operation is a correct lock. It nests, and it also makes
  the allocators safe to call from IRQ handlers. The rule in `irq.h`
  therefore changed from "don't allocate" to "don't sleep, yield or
  exit". SMP would need real spinlocks; it is not on the i386 roadmap.
- **Console writes are atomic per call**, so messages from different
  threads don't interleave. The cost: on real hardware, a long line
  drained through the polled UART holds interrupts off for milliseconds
  (about 20 ms for 80 characters at 38400 baud), stalling ticks. The
  proper fix is buffered, interrupt-driven serial, which belongs with
  the M5 driver framework.
- **Cooperative mode is a real scheduler mode, not just a test mode.**
  Scheduling starts cooperative and `sched_enable_preemption()` switches
  it on. That lets the cooperative test assert exact `ABABAB`
  alternation, which a stray preemption on a loaded CI host could
  otherwise perturb. In cooperative mode, ticks still wake sleepers and
  make idle give way. That's a wakeup, not preemption.
- **Stack guard pages are unmapped virtual pages, not reserved
  frames.** Each 12 KiB slot is a 4 KiB guard page (never mapped)
  followed by an 8 KiB stack, so a guard page costs no physical memory.
  The 16 MiB region holds 1365 slots.
- **Double faults use a hardware task switch.** By the time a double
  fault happens the kernel stack is usually unusable, often because it
  overflowed. That was what M3's guard page turned into an unreported
  triple fault. Vector 8 is now a task gate. The CPU saves the faulting
  context into the kernel TSS and switches to a separate TSS with its
  own 4 KiB stack, whose handler prints the saved `eip` and `esp` plus
  `cr2`. The kernel TSS loaded here is the same one ring 3 will use for
  its kernel stack in M8.
- **`panic()` disables interrupts permanently and names the thread**,
  so no other thread keeps running past a panic, and multithreaded
  failures say where they happened.
- **Generic `kernel_main` no longer calls i386 table setup directly.**
  It calls `arch_early_init()`, so the HAL rule (§1.3) holds for the
  boot sequence too.

## Verification

- `make test` requires the M4 marker, which is printed only after both
  scheduler self-tests pass, on all three machine configurations.
- **Cooperative:** two threads yielding in a loop must produce exactly
  `ABABAB`. A second round must then leave free frames and
  `heap_used()` exactly unchanged, and `heap_check()` must pass.
- **Preemptive:** two threads spin without ever yielding. Main's 100 ms
  sleep can only end, and each spinner can only run at all, if the
  timer preempts them. Both must make progress (typical counts come out
  about 1.2:1; with 20 ms slices in a 100 ms window, the thread that
  starts first gets 3 slices to the other's 2).
- **Sleep:** 60 ms and 20 ms sleepers must wake in deadline order, and
  neither may wake early.
- **Negative controls** (temporary sabotage, reverted afterward), each
  confirmed to be caught:
  - `thread_yield()` doesn't switch → *threads did not finish within 2 s*;
  - thread structs never freed → *exited threads leaked heap memory*
    (the first version of the test missed this; `heap_used()` was added
    to catch it);
  - stacks never freed → *exited threads leaked stack memory*;
  - sleep list unsorted → *sleepers did not wake in deadline order*;
  - preemption never enabled → boot stops after the M3 marker, and the
    smoke test fails.
- **Double faults:**
  - overflowing the boot stack → *double fault: … cr2 just below esp …
    in thread: main*;
  - a recursing thread → the same report, with `cr2` inside that
    thread's guard page and *in thread: overflower*.
- **Exhaustion:** creating threads until `thread_create()` returns NULL
  gives 862 threads at 8 MiB (memory runs out first) and 1364 at
  256 MiB (stack slots run out; idle holds the 1365th). After they all
  exit, heap bytes are back to baseline exactly. The first round keeps
  11–20 frames (heap growth and stack-region page tables, which are
  never freed by design), and a second full round keeps **0**, so the
  memory is reused, not leaked.
- There are still no post-386 instructions and no libgcc members.
  `-Wframe-larger-than=2048` is now on: a frame bigger than a guard page
  could step over it (the recursion test's `cr2` landed 0x460 bytes into
  its guard page), so frames are kept to half that size. The largest is
  `kernel_main` at 1312 bytes.

## Known limits (deliberately deferred)

- No priorities (the §2.6 step after round-robin), and no blocking
  primitives besides sleep: no mutexes, wait queues or join. These will
  be added when the first real consumer (a driver in M5) needs them.
- A single address space: all threads are kernel threads until M8.
- A kernel stack frame bigger than the 4 KiB guard page could still
  step over it; the compile-time frame limit mitigates this, but it
  isn't runtime stack probing.
- The double-fault handler can't return, and it reports only the
  faulting context the CPU saved (no stack trace).
