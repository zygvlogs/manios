#ifndef ZKT_KERNEL_MULTIBOOT_H
#define ZKT_KERNEL_MULTIBOOT_H

#include <stddef.h>
#include <stdint.h>
#include "pmm.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Fills `out` with up to `max` regions from the Multiboot 1 info
 * structure at physical address mbi_phys and returns how many were
 * written. Panics if the loader supplied no memory information. */
size_t multiboot_memory_regions(uint32_t mbi_phys, struct mem_region *out, size_t max);

#endif
