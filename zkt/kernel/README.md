# zkt/kernel/

The glue: boot handoff from `zkt/arch/<cpu>/`, `kernel_main()`,
panic/logging, and syscall dispatch (once the syscall ABI exists at
M8). This is where the M1 milestone's entry point
(`kernel_main()` wiring GDT → IDT → PIC → serial → VGA → banner → idle
loop) will live.

See [`docs/FOUNDING-PROPOSAL.md` §7](../../docs/FOUNDING-PROPOSAL.md#7-first-bootable-prototype-plan-m1-in-detail).
