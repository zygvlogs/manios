#include "multiboot.h"
#include <stdbool.h>
#include "memlayout.h"
#include "panic.h"

#define MULTIBOOT_INFO_MEMORY  (1u << 0) /* mem_lower / mem_upper valid */
#define MULTIBOOT_INFO_CMDLINE (1u << 2) /* cmdline valid */
#define MULTIBOOT_INFO_MODS    (1u << 3) /* mods_count / mods_addr valid */
#define MULTIBOOT_INFO_MEM_MAP (1u << 6) /* mmap_addr / mmap_length valid */
#define MULTIBOOT_INFO_FRAMEBUFFER (1u << 12) /* framebuffer_* valid */
#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB 1

/* Multiboot 1 specification, section 3.3 -- only the fields ZKT reads.
 * (framebuffer_addr is 32-bit here, not the spec's 64: the ManiOS boot
 * loader's own VBE probe -- the only source of these fields -- never
 * has more than a 32-bit physical address to give.) */
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
	uint32_t drives_length, drives_addr, config_table, boot_loader_name;
	uint32_t framebuffer_addr, framebuffer_addr_hi, framebuffer_pitch, framebuffer_width,
	    framebuffer_height;
	uint8_t framebuffer_bpp, framebuffer_type;
	uint16_t framebuffer_reserved;
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

struct multiboot_module {
	uint32_t mod_start, mod_end, string, reserved;
} __attribute__((packed));

/* "boot/bootarea.bin args" -> "bootarea": the last element of the first
 * word, up to its first '.', letters (lowercased) and digits only. */
static void module_name(uint32_t string, size_t index, char *out, size_t size)
{
	size_t k = 0;
	bool in_extension = false;
	for (uint32_t addr = string; string && addr - string < 4096; addr++) {
		char c = *(const char *)boot_phys(addr, 1);
		if (!c || c == ' ') {
			break;
		}
		if (c == '/') {
			k = 0; /* a later element replaces this one */
			in_extension = false;
		} else if (c == '.') {
			in_extension = true;
		} else if (!in_extension && k + 1 < size) {
			char l = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
			if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) {
				out[k++] = l;
			}
		}
	}
	if (k == 0) {
		const char *prefix = "module";
		while (*prefix && k + 2 < size) {
			out[k++] = *prefix++;
		}
		out[k++] = (char)('0' + index % 10);
	}
	out[k] = '\0';
}

size_t multiboot_modules(uint32_t mbi_phys, struct boot_module *out, size_t max)
{
	const struct multiboot_info *mbi = boot_phys(mbi_phys, sizeof(*mbi));
	if (!(mbi->flags & MULTIBOOT_INFO_MODS)) {
		return 0;
	}
	size_t n = 0;
	for (uint32_t i = 0; i < mbi->mods_count && n < max; i++) {
		const struct multiboot_module *m =
		    boot_phys(mbi->mods_addr + i * sizeof(*m), sizeof(*m));
		if (m->mod_end <= m->mod_start) {
			continue;
		}
		out[n].start = m->mod_start;
		out[n].end = m->mod_end;
		module_name(m->string, n, out[n].name, sizeof(out[n].name));
		n++;
	}
	return n;
}

static struct boot_framebuffer saved_fb;
static bool saved_fb_present;

/* Called once, from kernel_main: the Multiboot information lies in
 * memory the PMM may later hand out, so it must be read before that
 * (the same reason cmdline_set() runs early); fb_init() (drivers_init(),
 * much later) reads the copy kept here. */
void multiboot_save_framebuffer(uint32_t mbi_phys)
{
	const struct multiboot_info *mbi = boot_phys(mbi_phys, sizeof(*mbi));
	if (!(mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER)
	    || mbi->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB || mbi->framebuffer_addr_hi
	    || !mbi->framebuffer_width || !mbi->framebuffer_height) {
		return;
	}
	saved_fb.addr = mbi->framebuffer_addr;
	saved_fb.pitch = mbi->framebuffer_pitch;
	saved_fb.width = mbi->framebuffer_width;
	saved_fb.height = mbi->framebuffer_height;
	saved_fb.bpp = mbi->framebuffer_bpp;
	saved_fb_present = true;
}

bool multiboot_boot_framebuffer(struct boot_framebuffer *out)
{
	if (saved_fb_present) {
		*out = saved_fb;
	}
	return saved_fb_present;
}
