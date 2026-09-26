# zkt/arch/i386/

The first ManiOS target architecture. Code generation is pinned to the
80386 (`-march=i386`); see
[M2 notes](../../../docs/milestones/M2-memory-management.md).

- `boot.S` — Multiboot header, `_start` (runs with paging off), boot
  page table, jump to the higher half, boot stack and its guard page
- `linker.ld` — higher-half link layout (VMA `0xC0000000` + physical)
- `arch.c` / `arch.h` — `arch_early_init()`: GDT, TSS, IDT, PIC
- `memlayout.h` — virtual memory layout constants, `P2V` / `V2P`
- `cpu.h` — interrupt enable/save/restore, halt, CR2, TLB flush
- `io.h` — port I/O
- `gdt.c` / `gdt_flush.S` — flat GDT plus the two TSS descriptors
- `tss.c` — kernel TSS and the double-fault task (own TSS and stack)
- `idt.c` / `idt_flush.S` — IDT (vector 8 is a task gate), `isr_handler` routing
- `context_switch.S` / `context.c` / `context.h` — kernel thread context switch
- `isr.S` / `isr.h` — exception and IRQ entry stubs, saved register layout
- `exception.c` — exception reporting, page-fault diagnostics
- `irq.c` / `irq.h` — IRQ handler registration and dispatch, spurious IRQs
- `pic.c` — 8259 PIC remap, mask/unmask, EOI
- `pit.c` / `clock.h` — PIT as the periodic clock behind `zkt/kernel/timer.c`
- `paging.c` — two-level paging behind `zkt/mm/vmm.h`
- `cpuinfo.c` — what CPU this is (CPUID's brand string, or vendor,
  family and model; 386 or 486 without CPUID), for `/dev/sysstat`
