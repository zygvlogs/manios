# zkt/mm/

Architecture-neutral memory management. Design and verification:
[M2 notes](../../docs/milestones/M2-memory-management.md).

- `pmm.c` — physical frame allocator: one bit per 4 KiB frame, built
  from a boot-protocol-neutral `struct mem_region` list
- `vmm.h` — virtual memory interface (map / unmap / translate); the
  i386 implementation is `zkt/arch/i386/paging.c`
- `heap.c` — `kmalloc` / `kfree`, a first-fit free list that grows the
  heap region on demand
- `mm_selftest.c` — boot-time checks of all three, which
  `make test` gates on
