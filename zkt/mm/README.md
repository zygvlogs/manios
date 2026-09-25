# zkt/mm/

Architecture-neutral memory management. Design and verification:
[M2 notes](../../docs/milestones/M2-memory-management.md).

- `pmm.c` — physical frame allocator: one bit per 4 KiB frame, built
  from a boot-protocol-neutral `struct mem_region` list
- `vmm.h` — virtual memory interface (map / unmap / translate); the
  i386 implementation is `zkt/arch/i386/paging.c`
- `heap.c` — `kmalloc` / `kfree`, a first-fit free list that grows the
  heap region on demand
- `kstack.c` — 8 KiB kernel thread stacks, each above an unmapped
  guard page ([M4 notes](../../docs/milestones/M4-multitasking.md))
- `mm_selftest.c` — boot-time checks of the PMM, VMM and heap, which
  `make test` gates on

All of these run each operation with interrupts off, which is their
lock on one CPU.
