# zkt/arch/i386/

The first ManiOS target architecture. Will hold, in the order they're
built (see [`docs/FOUNDING-PROPOSAL.md` §7](../../../docs/FOUNDING-PROPOSAL.md#7-first-bootable-prototype-plan-m1-in-detail)):

- `boot.S` — Multiboot header + kernel entry stub
- `linker.ld` — higher-half link layout
- `gdt.c` / `gdt_flush.S` — flat GDT setup
- `idt.c` / `isr.S` — IDT and CPU exception handlers
- `pic.c` — 8259 PIC remap

Empty until [M1 implementation tasks](../../../docs/FOUNDING-PROPOSAL.md#10-exact-first-implementation-tasks)
begin.
