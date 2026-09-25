# zkt/arch/i386/

The first ManiOS target architecture. Code generation is pinned to the
80386 (`-march=i386`); see
[M2 notes](../../../docs/milestones/M2-memory-management.md).

- `boot.S` — Multiboot header, `_start` (runs with paging off), boot
  page table, jump to the higher half
- `linker.ld` — higher-half link layout (VMA `0xC0000000` + physical)
- `memlayout.h` — virtual memory layout constants, `P2V` / `V2P`
- `cpu.h` — interrupt enable/halt, CR2, TLB flush
- `io.h` — port I/O
- `gdt.c` / `gdt_flush.S` — flat GDT
- `idt.c` / `idt_flush.S` — IDT
- `isr.S` / `isr.h` — CPU exception entry stubs, saved register layout
- `exception.c` — exception dispatch, page-fault diagnostics
- `pic.c` — 8259 PIC remap (all IRQs masked until M3)
- `paging.c` — two-level paging behind `zkt/mm/vmm.h`
