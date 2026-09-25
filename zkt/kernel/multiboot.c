#include "multiboot.h"
#include "memlayout.h"
#include "panic.h"

#define MULTIBOOT_INFO_MEMORY  (1u << 0) /* mem_lower / mem_upper valid */
#define MULTIBOOT_INFO_CMDLINE (1u << 2) /* cmdline valid */
#define MULTIBOOT_INFO_MEM_MAP (1u << 6) /* mmap_addr / mmap_length valid */
#define MULTIBOOT_MEMORY_AVAILABLE 1

/* Multiboot 1 specification, section 3.3 -- only the fields ZKT reads. */
struct multiboot_info {
	uint32_t flags;
	uint32_t mem_lower; /* KiB below 1 MiB */
	uint32_t mem_upper; /* KiB above 1 MiB, up to the first hole */
	uint32_t boot_device;
	uint32_t cmdline;
	uint32_t mods_count;
	uint32_t mods_addr;
	uint32_t syms[4];
	uint32_t mmap_length;
	uint32_t mmap_addr;
} __attribute__((packed));

struct multiboot_mmap_entry {
	uint32_t size; /* size of the rest of this entry, excluding this field */
	uint64_t addr;
	uint64_t len;
	uint32_t type;
} __attribute__((packed));

/* Before the VMM exists, physical memory is only reachable through the
 * boot mapping of [0, BOOT_MAPPED_PHYS_LIMIT). */
static const void *boot_phys(uint32_t addr, uint32_t len)
{
	if (addr >= BOOT_MAPPED_PHYS_LIMIT || len > BOOT_MAPPED_PHYS_LIMIT - addr) {
		panic("multiboot: boot information lies outside the boot mapping");
	}
	return P2V(addr);
}

size_t multiboot_memory_regions(uint32_t mbi_phys, struct mem_region *out, size_t max)
{
	const struct multiboot_info *mbi = boot_phys(mbi_phys, sizeof(*mbi));
	size_t n = 0;

	if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
		uintptr_t entry = (uintptr_t)boot_phys(mbi->mmap_addr, mbi->mmap_length);
		uintptr_t end = entry + mbi->mmap_length;
		while (entry < end) {
			const struct multiboot_mmap_entry *e = (const void *)entry;
			if (n == max) {
				panic("multiboot: memory map has more entries than ZKT accepts");
			}
			out[n].base = e->addr;
			out[n].length = e->len;
			out[n].usable = e->type == MULTIBOOT_MEMORY_AVAILABLE;
			n++;
			entry += e->size + sizeof(e->size);
		}
		return n;
	}

	/* Pre-E820 BIOSes: the loader only knows the size of the contiguous
	 * block above 1 MiB. Low memory is reserved by the PMM regardless. */
	if (mbi->flags & MULTIBOOT_INFO_MEMORY && max > 0) {
		out[0].base = 0x100000;
		out[0].length = (uint64_t)mbi->mem_upper * 1024;
		out[0].usable = true;
		return 1;
	}

	panic("multiboot: boot loader provided no memory information");
}

void multiboot_cmdline(uint32_t mbi_phys, char *out, size_t size)
{
	const struct multiboot_info *mbi = boot_phys(mbi_phys, sizeof(*mbi));
	size_t n = 0;
	if (mbi->flags & MULTIBOOT_INFO_CMDLINE) {
		/* Checked a byte at a time: the string's length is unknown. */
		for (uint32_t addr = mbi->cmdline; n + 1 < size; addr++, n++) {
			char c = *(const char *)boot_phys(addr, 1);
			if (!c) {
				break;
			}
			out[n] = c;
		}
	}
	out[n] = '\0';
}
