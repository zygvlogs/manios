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

/* Copies the kernel command line (empty if the loader gave none),
 * truncated to fit. Call before pmm_init(): the string lies in memory
 * the PMM may hand out. */
void multiboot_cmdline(uint32_t mbi_phys, char *out, size_t size);

/* A module the boot loader loaded with the kernel (M14: the ManiOS boot
 * loader passes its boot area, which the installer copies to disk).
 * `name` is made from the module's string: the last element of its
 * first word, without an extension, in lowercase letters and digits. */
#define BOOT_MODULES_MAX 4
struct boot_module {
	uint32_t start, end; /* physical, end exclusive */
	char name[16];
};
size_t multiboot_modules(uint32_t mbi_phys, struct boot_module *out, size_t max);

#endif
