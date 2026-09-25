# M2 — Memory Management

**Status:** Achieved (2026-09-25). Implements
[FOUNDING-PROPOSAL.md §2.3–§2.5](../FOUNDING-PROPOSAL.md#23-physical-memory-management-pmm).

## What M2 delivers

| Piece | Files | Summary |
|---|---|---|
| Higher-half kernel | `zkt/arch/i386/boot.S`, `linker.ld`, `memlayout.h` | Loaded at 1 MiB physical, runs at `0xC0100000` |
| Physical memory manager | `zkt/mm/pmm.c` | Bitmap, one bit per 4 KiB frame |
| Virtual memory manager | `zkt/mm/vmm.h` (API), `zkt/arch/i386/paging.c` (i386) | Map / unmap / translate single pages |
| Kernel heap | `zkt/mm/heap.c` | `kmalloc` / `kfree`, first-fit free list |
| Boot memory map | `zkt/kernel/multiboot.c` | Multiboot → generic `struct mem_region` list |
| Self-test | `zkt/mm/mm_selftest.c` | Runs every boot; `make test` gates on it |

## Virtual memory layout (i386)

```
0x00000000 - 0xBFFFFFFF   unmapped (future user space, M8)
0xC0000000 - 0xC03FFFFF   physical [0, 4 MiB): kernel image, VGA text
                          buffer, PMM bitmap, Multiboot data
0xD0000000 - 0xDFFFFFFF   kernel heap, mapped on demand (256 MiB max)
0xE0000000                one page used by the boot-time VMM self-test
0xFFC00000 - 0xFFFFFFFF   recursive mapping: page tables, then the
                          page directory at 0xFFFFF000
```

## Boot sequence

1. The Multiboot loader enters `_start` with paging off. `_start` and
   the Multiboot header live in `.multiboot.*`, linked at their physical
   addresses; everything else is linked at `KERNEL_VIRT_BASE +
   physical`, via `AT()` in `linker.ld`.
2. `_start` fills one page table mapping physical `[0, 4 MiB)` and
   installs it at page directory entries 0 (identity) and 768
   (`0xC0000000`). It then enables paging and jumps to the higher half.
3. `higher_half_entry` clears entry 0, so nothing is identity-mapped
   anymore and NULL-pointer dereferences fault. It then calls
   `kernel_main(magic, mbi_phys)`.
4. `kernel_main` turns the Multiboot memory map into a
   `struct mem_region` list, then calls `pmm_init`, then `vmm_init`
   (which installs the recursive entry), then `heap_init`, then
   `mm_selftest`.

`linker.ld` asserts that the kernel image fits in the 4 MiB boot
mapping. `pmm_init` panics if the bitmap placed right after the kernel
doesn't fit either. At most the bitmap is 128 KiB, for 4 GiB of RAM.

## Design decisions

- **The PMM takes a generic region list, not Multiboot structures.**
  ADR-0001 plans a native ManiOS bootloader, so `zkt/mm/` stays
  independent of the boot protocol. The list carries reserved regions
  as well as usable ones. Usable regions are rounded inward and reserved
  regions outward, so a frame covered by both (buggy BIOS maps
  overlap) ends up reserved.
- **Pre-E820 fallback.** When the loader has no memory map, only
  `mem_upper` (some pre-1995 BIOSes), the PMM treats `[1 MiB, 1 MiB +
  mem_upper)` as usable. This matters for the old-hardware goal.
- **Frame 0 is never allocated.** Everything below the end of the PMM
  bitmap is reserved: the real-mode IVT, the BIOS data area, Multiboot
  structures, the kernel and the bitmap. That lets `pmm_alloc_frame()`
  return 0 to mean out of memory.
- **RAM at or above 4 GiB is ignored.** Without PAE (a Pentium Pro
  feature) the CPU can't address it.
- **Recursive page directory mapping.** Page tables can live in any
  frame the PMM returns, not just the 4 MiB the boot mapping covers.
  This also avoids needing a direct map of all RAM, which wouldn't fit
  in 1 GiB of kernel address space on large machines anyway.
- **TLB flushes reload CR3.** `INVLPG` is a 486 instruction. A full
  flush costs more, but mapping changes are rare at this stage.
  Per-page `INVLPG`, gated on CPU detection, is a possible later
  optimization.
- **The heap is a first-fit, address-ordered free list with a magic
  number in each block header.** Its blocks exactly tile the mapped
  heap, and `kfree` coalesces, so no two free blocks are ever adjacent.
  `heap_check()` checks all of these properties. The magic numbers turn
  a double free or a stray pointer into a named panic instead of silent
  corruption. The heap never shrinks. A slab allocator waits until real
  allocation patterns exist (§2.5).
- **Generic code includes arch headers by name** (`"memlayout.h"`,
  `"cpu.h"`) through the per-architecture include path, never by
  `../arch/i386/` paths. That way another architecture can provide the
  same headers. `cpu.h` also removed the inline assembly that M1 had
  left in `zkt/kernel/`, restoring the §1.3 rule.

## Found and fixed along the way

- **Code generation wasn't i386.** The `i686-elf` compiler defaults to
  `-march=pentiumpro`. M1 happened to contain no i686-only
  instructions, but M2 compiled to 12 `CMOV`s, and on an emulated 486
  the kernel died with *Invalid opcode* right after the M1 banner.
  The Makefile now pins `-march=i386`, plus `-Wa,-march=i386` so the
  assembler rejects post-386 instructions in `.S` files and in inline
  assembly. The smoke test boots on QEMU's `486` model, its oldest,
  which faults on `CMOV`. A deliberate compiler-default build confirms
  the test fails as it should.
- **`-lgcc` came before the objects on the link line.** That meant any
  libgcc reference would have been left unresolved. It now comes last.
  The link map (`build/manios-zkt.map`) shows no libgcc members are
  currently pulled in. That is worth keeping true, because the libgcc
  built with the toolchain uses i686 code generation.

## Verification

- `make test` boots three configurations and requires the M1 and M2
  markers, the `ZKT> ` prompt, and no panic:
  8 MiB on a 486, 256 MiB on a 486, and 4 GiB on `qemu32`. In the
  4 GiB case QEMU puts 1 GiB above 4 GiB, which the PMM must ignore
  (it reports ~3 GiB usable). A 486 has no PAE, so QEMU can't give it
  4 GiB at all.
- The self-test covers PMM allocation, free, counts and reuse. It
  covers VMM map, double-map rejection, write-through, translate,
  zeroing of a fresh page table, and unmap. It covers heap alignment,
  non-overlap, reuse, coalescing, growth across pages, and
  `heap_check()` after each phase.
- Failure paths were checked by hand, each as a temporary edit that was
  reverted:
  - a read of `0x1000` gives *page fault at 0x00001000: page not
    present, read, kernel mode*, which confirms the identity mapping is
    gone;
  - a write past the mapped heap gives *…: page not present, write…*;
  - `kfree` twice gives *kfree: double free*;
  - `pmm_free_frame` twice gives *pmm_free_frame: … (double free)*.
- The kernel also boots under real GRUB 2 (from a `grub-mkrescue` ISO,
  on a 486 model), not just QEMU's built-in Multiboot loader.

## Known limits (deliberately deferred)

- Kernel text and rodata are mapped writable (no W^X yet), and the
  boot mapping covers all of `[0, 4 MiB)` rather than exactly the
  kernel's sections.
- Empty page tables are never freed, and the heap never returns pages.
- There is one global page directory. Per-process address spaces (M8)
  will need the kernel's entries 768–1022 shared by every directory;
  pre-allocating those page tables is the simple way to get that.
- No locking. *Resolved in M4:* every PMM, VMM and heap operation now
  runs with interrupts off, which is a correct lock on one CPU (see
  the [M4 notes](M4-multitasking.md)).
