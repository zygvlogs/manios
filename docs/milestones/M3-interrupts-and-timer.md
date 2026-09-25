# M3 — Interrupts and Timer

**Status:** Achieved (2026-09-25). Implements the IRQ half of
[FOUNDING-PROPOSAL.md §2.2](../FOUNDING-PROPOSAL.md#22-gdt--idt-and-interrupt-handling)
and the timer that §2.6's preemptive scheduler (M4) will run on.

## What M3 delivers

| Piece | Files | Summary |
|---|---|---|
| IRQ entry | `zkt/arch/i386/isr.S`, `idt.c` | Stubs and interrupt gates for vectors 32–47; `isr_handler` routes each vector to exception or IRQ handling |
| IRQ dispatch | `zkt/arch/i386/irq.c`, `irq.h` | `irq_install_handler()`, EOI, spurious IRQ7/15 filtering, masking of stray lines |
| PIC | `zkt/arch/i386/pic.c` | Per-line mask/unmask (the cascade line is unmasked automatically), EOI, in-service register read |
| Clock | `zkt/arch/i386/pit.c`, `clock.h` | PIT channel 0 in square-wave mode at any rate it can divide to |
| Timer | `zkt/kernel/timer.c` | 100 Hz tick, `timer_ticks()`, `timer_uptime_ms()`, `timer_sleep_ms()` |
| Critical sections | `zkt/arch/i386/cpu.h` | `cpu_irq_save()` / `cpu_irq_restore()`, `cpu_interrupts_enabled()` |
| Stack guard | `boot.S`, `paging.c` | Unmapped page below the boot kernel stack |

## Design decisions

- **Every vector enters through one stub path and one C function.**
  `isr_handler` (in `idt.c`) sends vectors below `IRQ_BASE_VECTOR` to
  `exception_handle()` and the rest to `irq_dispatch()`. Only vectors
  0–47 have present gates, so nothing else reaches it.
- **Interrupt gates alone manage IF.** They clear IF on entry, and
  `iret` restores the interrupted context's IF. M1's stubs also ran
  `cli` on entry and `sti` just before `iret`. The `sti` was actively
  wrong once IRQs were live, because it opened a one-instruction window
  in which a new IRQ could nest on top of a frame that was about to be
  popped. Both are gone.
- **The PIC is acknowledged before the handler runs, not after.** Once
  M4 lands, the timer handler may switch to another thread and not
  return to `irq_dispatch` for a long time. An EOI deferred until then
  would block every IRQ of equal or lower priority. IF stays clear until
  `iret`, so an early EOI can't cause nesting.
- **Spurious IRQs are expected, not errors.** A request withdrawn before
  the CPU acknowledges it makes the 8259 report its lowest-priority
  line (7 or 15) without setting that line's in-service bit. This
  happens on real old boards (noise, marginal ISA cards). Such an IRQ
  gets no EOI, except that a spurious IRQ15 still needs an EOI to the
  master, which did see a genuine request on its cascade line.
- **A stray IRQ gets masked rather than causing a panic.** Lines are
  unmasked only when a handler is installed, so an IRQ with no handler
  means odd hardware. Silencing the line keeps the machine up.
- **The arch layer has no dependency on the generic timer.**
  `timer_init()` passes its tick function to `clock_start_periodic()`.
  The PIT doesn't know `timer.c` exists, and another architecture only
  needs to provide `clock.h`.
- **Ticks are a 64-bit count read with interrupts off.** At 100 Hz a
  32-bit count wraps after 497 days. On i386 a 64-bit load is two loads,
  and a tick between them would tear the value. `TIMER_HZ` must divide
  1000, which keeps millisecond conversion a multiply; a 64-bit divide
  would pull in libgcc, which was built for i686 (see the
  [M2 notes](M2-memory-management.md)).
- **`timer_sleep_ms()` halts between ticks and refuses to run with
  interrupts disabled**, since no tick could ever end that sleep. It
  sleeps at least the requested time, at 10 ms granularity. If a tick
  lands between the check and the `hlt`, the sleep lasts one tick
  longer; it never hangs, because the clock is periodic.
  *Update (M4):* `timer_sleep_ms()` now blocks the calling thread through
  the scheduler, and the interrupts-disabled check is gone: the idle
  thread runs with interrupts on, so the sleep always ends.

## Found and fixed along the way

- **A kernel stack overflow would have silently corrupted the page
  tables.** The boot stack sat directly above `boot_page_table0` in
  `.bss`, and that table maps the kernel itself. A guard page now sits
  between them, unmapped by `vmm_init()`. An overflow now becomes page
  fault → double fault → triple fault: a loud reset instead of
  corruption. The fault can't yet be *reported*. That needs a
  double-fault handler running on its own stack through a TSS task
  gate, which fits M4, where per-thread stacks and the TSS arrive
  anyway.
- **The smoke test hid triple faults.** It passed `-no-shutdown`, which
  makes QEMU *pause* on a triple fault rather than exit. A crashing
  kernel therefore burned the full timeout and failed with no stated
  cause. QEMU now exits, and the test reports "QEMU exited on its own:
  triple fault…". A deliberately faulting kernel fails the suite in
  about 2 s instead of 45 s.

## Verification

- `make test` (8 MiB/486, 256 MiB/486, 4 GiB/qemu32) requires the
  M3 marker. The marker is printed only after a 50 ms sleep returns,
  which needs 6 or more ticks. The PIC delivers nothing further on a
  line until it gets an EOI, so the marker proves IRQ delivery *and*
  acknowledgment.
- **Negative control:** with the EOI removed, all three cases fail (the
  sleep never returns).
- **Rate:** a 2000 ms sleep measured 2005 ms against the host clock.
- **Spurious and stray IRQs:** QEMU never raises a real spurious IRQ,
  but a software `int` to the IRQ7 or IRQ15 vector looks identical to
  the PIC, since nothing is in service. `int $0x27`, `int $0x2F`, and
  `int $0x21` (IRQ1, which has no handler) all returned cleanly, and
  the timer kept running afterward.
- `timer_sleep_ms()` with interrupts disabled gives a named panic.
- A push into the guard page gives #PF → #DF → triple fault (checked
  with QEMU's `-d int,cpu_reset`), not a silent write.
- These were temporary edits, all reverted. There are still no post-386
  instructions and no libgcc members in the image.

## Known limits (deliberately deferred)

- No double-fault handler on a separate stack, so stack overflows reset
  rather than report (see above). *Resolved in M4:* vector 8 is now a
  task gate to its own TSS and stack, and overflows are reported.
- Legacy 8259 PICs only. APIC/IOAPIC support belongs with newer
  hardware and SMP, which aren't on the i386-first roadmap.
- One handler per IRQ line; no sharing, which PCI-era hardware will
  eventually need.
- Keyboard input arrives with the driver framework in M5, per the
  roadmap, rather than being built now and refactored then.
