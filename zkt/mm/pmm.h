#ifndef ZKT_MM_PMM_H
#define ZKT_MM_PMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A physical memory range reported by the boot loader. Kept independent
 * of Multiboot so a native ManiOS bootloader (ADR-0001) can feed the PMM
 * the same way. */
struct mem_region {
	uint64_t base;
	uint64_t length;
	bool usable;
};

/* Builds the frame bitmap from the boot memory map. Everything below the
 * end of the bitmap -- low memory, the kernel image, the bitmap itself --
 * is reserved, so physical frame 0 is never handed out. */
void pmm_init(const struct mem_region *regions, size_t count);

/* Returns the physical address of a free 4 KiB frame, or 0 when physical
 * memory is exhausted. */
uintptr_t pmm_alloc_frame(void);

/* Panics on a misaligned, out-of-range, or already-free frame. */
void pmm_free_frame(uintptr_t phys);

size_t pmm_free_frames(void);
size_t pmm_usable_frames(void);

#endif
