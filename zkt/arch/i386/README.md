# zkt/arch/i386/

The first ManiOS target architecture. Code generation is pinned to the
80386 (`-march=i386`); see
[M2 notes](../../../docs/milestones/M2-memory-management.md).

- `boot.S` — Multiboot header, `_start` (runs with paging off), boot
  page table, jump to the higher half, boot stack and its guard page
- `linker.ld` — higher-half link layout (VMA `0xC0000000` + physical)
- `memlayout.h` — virtual memory layout constants, `P2V` / `V2P`
- `cpu.h` — interrupt enable/save/restore, halt, CR2, TLB flush
- `io.h` — port I/O
- `gdt.c` / `gdt_flush.S` — flat GDT
- `idt.c` / `idt_flush.S` — IDT
- `isr.S` / `isr.h` — exception and IRQ entry stubs, saved register layout
- `exception.c` — exception reporting, page-fault diagnostics
- `irq.c` / `irq.h` — IRQ handler registration and dispatch, spurious IRQs
- `pic.c` — 8259 PIC remap, mask/unmask, EOI
- `pit.c` / `clock.h` — PIT as the periodic clock behind `zkt/kernel/timer.c`
- `paging.c` — two-level paging behind `zkt/mm/vmm.h`
