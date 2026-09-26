/*
 * Stage 2 of the ManiOS boot loader, protected-mode part (M14,
 * ADR-0006). The real-mode part (stage2_entry.S) has put the whole boot
 * area where its header asked (just past the kernel's memory), the memory map at MMAP_ADDR and the command line
 * at CMDLINE_ADDR. This checks the boot area, loads the kernel's ELF
 * segments where they ask to be, builds the Multiboot 1 information --
 * with the boot area as a module, "bootarea", for the installer -- and
 * starts the kernel as a Multiboot loader would: EAX = 0x2BADB002, EBX =
 * the information, flat segments, paging off, interrupts off.
 *
 * No BIOS from here on: messages go to the VGA text screen and COM1.
 */
#include <stdint.h>
#include "bootarea.h"

struct params { /* filled in by stage2_entry.S */
	uint32_t mmap_length, mem_lower, mem_upper, area_size, area_lba, area_addr;
};

/* Multiboot 1, section 3.3. */
#define MB_MEMORY  (1u << 0)
#define MB_CMDLINE (1u << 2)
#define MB_MODS    (1u << 3)
#define MB_MMAP    (1u << 6)
#define MB_LOADER  (1u << 9)
#define MB_BOOTLOADER_MAGIC 0x2BADB002
#define MB_HEADER_MAGIC 0x1BADB002

struct mb_info {
	uint32_t flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr;
	uint32_t syms[4];
	uint32_t mmap_length, mmap_addr;
	uint32_t drives_length, drives_addr, config_table, boot_loader_name;
};

struct mb_module {
	uint32_t start, end, string, reserved;
};

/* ELF32 (the System V ABI). */
struct elf_header {
	uint8_t ident[16];
	uint16_t type, machine;
	uint32_t version, entry, phoff, shoff, flags;
	uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct elf_phdr {
	uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
};

#define PT_LOAD 1

/* GCC may emit calls to these. */
void *memcpy(void *dst, const void *src, uint32_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;
	while (n--) {
		*d++ = *s++;
	}
	return dst;
}

void *memset(void *dst, int c, uint32_t n)
{
	uint8_t *d = dst;
	while (n--) {
		*d++ = (uint8_t)c;
	}
	return dst;
}

static void outb(uint16_t port, uint8_t v)
{
	__asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static uint8_t inb(uint16_t port)
{
	uint8_t v;
	__asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}

/* --- output ---------------------------------------------------------- */

static int row = 24, col;

static void putc(char c)
{
	for (int spin = 0; spin < 100000 && !(inb(0x3FD) & 0x20); spin++) {
	}
	outb(0x3F8, (uint8_t)c);
	volatile uint16_t *screen = (volatile uint16_t *)0xB8000;
	if (c == '\n') {
		col = 0;
		if (++row == 25) {
			for (int i = 0; i < 24 * 80; i++) {
				screen[i] = screen[i + 80];
			}
			for (int i = 24 * 80; i < 25 * 80; i++) {
				screen[i] = 0x0720;
			}
			row = 24;
		}
	} else if (c != '\r') {
		screen[row * 80 + col] = (uint16_t)(0x0700 | (uint8_t)c);
		if (++col == 80) {
			putc('\n');
		}
	}
}

static void puts(const char *s)
{
	while (*s) {
		if (*s == '\n') {
			putc('\r');
		}
		putc(*s++);
	}
}

static void fail(const char *why)
{
	puts("\nManiOS boot loader: ");
	puts(why);
	puts("\n");
	for (;;) {
		__asm__ volatile("cli; hlt");
	}
}

/* --- loading ------------------------------------------------------------ */

/* Loads the kernel's segments, which must end below the boot area at
 * area_addr; returns its entry point. */
static uint32_t load_kernel(const uint8_t *file, uint32_t size, uint32_t area_addr)
{
	const struct elf_header *h = (const void *)file;
	if (size < sizeof(*h) || h->ident[0] != 0x7F || h->ident[1] != 'E' || h->ident[2] != 'L'
	    || h->ident[3] != 'F' || h->ident[4] != 1 || h->machine != 3 || h->type != 2) {
		fail("the kernel is not an i386 ELF executable");
	}
	/* A Multiboot kernel says so in its first 8 KiB. */
	int multiboot = 0;
	for (uint32_t i = 0; i + 12 <= size && i < 8192; i += 4) {
		const uint32_t *w = (const void *)(file + i);
		multiboot |= w[0] == MB_HEADER_MAGIC && w[0] + w[1] + w[2] == 0;
	}
	if (!multiboot) {
		fail("the kernel has no Multiboot header");
	}
	if (h->phoff > size || (uint32_t)h->phnum * sizeof(struct elf_phdr) > size - h->phoff) {
		fail("the kernel's program headers are damaged");
	}
	const struct elf_phdr *ph = (const void *)(file + h->phoff);
	for (int i = 0; i < h->phnum; i++, ph++) {
		if (ph->type != PT_LOAD || ph->memsz == 0) {
			continue;
		}
		if (ph->filesz > ph->memsz || ph->offset > size || ph->filesz > size - ph->offset
		    || ph->paddr < 0x100000 || ph->paddr > area_addr
		    || ph->memsz > area_addr - ph->paddr) {
			fail("a kernel segment is damaged, or would overlap the loader");
		}
		memcpy((void *)ph->paddr, file + ph->offset, ph->filesz);
		memset((void *)(ph->paddr + ph->filesz), 0, ph->memsz - ph->filesz);
	}
	return h->entry;
}

void stage2_main(const struct params *p)
{
	const uint8_t *area = (const uint8_t *)p->area_addr;
	uint32_t total = bootarea_u32(area, BA_TOTAL);
	if (total != p->area_size || total < BOOTAREA_ALIGN
	    || bootarea_u32(area, BA_LOAD_ADDR) != p->area_addr) {
		fail("the boot area's header is damaged");
	}
	if (bootarea_crc32(0, area + BOOTAREA_ALIGN, total - BOOTAREA_ALIGN)
	    != bootarea_u32(area, BA_CRC32)) {
		fail("the boot area is damaged (its checksum is wrong)");
	}
	uint32_t koff = bootarea_u32(area, BA_KERNEL_OFF), klen = bootarea_u32(area, BA_KERNEL_LEN);
	if (koff > total || klen > total - koff) {
		fail("the boot area's header is damaged");
	}
	uint32_t entry = load_kernel(area + koff, klen, p->area_addr);

	/* The Multiboot information, the module and their strings. */
	struct mb_info *mbi = (struct mb_info *)MBI_ADDR;
	struct mb_module *mod = (struct mb_module *)(MBI_ADDR + 0x100);
	char *mod_name = (char *)(MBI_ADDR + 0x120);
	char *loader = (char *)(MBI_ADDR + 0x140);
	memset(mbi, 0, 0x200);
	memcpy(mod_name, "bootarea", 9);
	const char *name = "ManiOS boot loader " MANIOS_VERSION;
	uint32_t n = 0;
	while (name[n]) {
		n++;
	}
	memcpy(loader, name, n + 1);
	mod->start = p->area_addr;
	mod->end = p->area_addr + total;
	mod->string = (uint32_t)mod_name;
	mbi->flags = MB_MEMORY | MB_CMDLINE | MB_MODS | MB_LOADER;
	mbi->mem_lower = p->mem_lower;
	mbi->mem_upper = p->mem_upper;
	mbi->cmdline = CMDLINE_ADDR;
	mbi->mods_count = 1;
	mbi->mods_addr = (uint32_t)mod;
	mbi->boot_loader_name = (uint32_t)loader;
	if (p->mmap_length) {
		mbi->flags |= MB_MMAP;
		mbi->mmap_addr = MMAP_ADDR;
		mbi->mmap_length = p->mmap_length;
	}

	__asm__ volatile("cli\n\tjmp *%%ecx" : : "c"(entry), "a"(MB_BOOTLOADER_MAGIC), "b"(mbi) : "memory");
	for (;;) {
	}
}
