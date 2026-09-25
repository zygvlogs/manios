# zkt/mm/

Memory management: physical memory manager (bitmap frame allocator over
the boot-time memory map), virtual memory manager (i386 two-level
paging, higher-half kernel mapping), and the kernel heap
(`kmalloc`/`kfree` free-list allocator).

See [`docs/FOUNDING-PROPOSAL.md` §2.3–2.5](../../docs/FOUNDING-PROPOSAL.md#23-physical-memory-management-pmm).
Targeted at milestone M2.
