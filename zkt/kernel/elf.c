/* ELF32 loading per the System V ABI (gABI) and its i386 supplement. */
#include "elf.h"
#include <stdbool.h>
#include "kerrno.h"
#include "kstring.h"
#include "memlayout.h"
#include "pmm.h"
#include "vmm.h"

#define EI_NIDENT 16
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define EM_386 3
#define PT_LOAD 1
#define PF_W 2

struct elf_header {
	uint8_t ident[EI_NIDENT];
	uint16_t type, machine;
	uint32_t version, entry, phoff, shoff, flags;
	uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));

struct elf_phdr {
	uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
} __attribute__((packed));

/* Everything below the stack, less the guard page under it. */
#define USER_IMAGE_TOP (USER_STACK_TOP - (USER_STACK_PAGES + 1) * PAGE_SIZE)

static bool valid_header(const struct elf_header *h, size_t size)
{
	return size >= sizeof(*h) && memcmp(h->ident, "\x7f" "ELF", 4) == 0
	       && h->ident[4] == ELFCLASS32 && h->ident[5] == ELFDATA2LSB
	       && h->ident[6] == EV_CURRENT && h->type == ET_EXEC && h->machine == EM_386
	       && h->phentsize == sizeof(struct elf_phdr) && h->phnum > 0
	       && h->phoff < size && h->phnum <= (size - h->phoff) / sizeof(struct elf_phdr);
}

static bool valid_segment(const struct elf_phdr *p, size_t size)
{
	return p->memsz >= p->filesz && p->offset <= size && p->filesz <= size - p->offset
	       && p->vaddr >= USER_IMAGE_MIN && p->vaddr < USER_IMAGE_TOP
	       && p->memsz <= USER_IMAGE_TOP - p->vaddr;
}

/* Maps zeroed, writable user pages over [start, end); pages already
 * mapped (two segments sharing a page) are left as they are. */
static int map_range(uintptr_t start, uintptr_t end)
{
	uintptr_t phys;
	for (uintptr_t page = start & ~(uintptr_t)(PAGE_SIZE - 1); page < end; page += PAGE_SIZE) {
		if (vmm_translate(page, &phys) == 0) {
			continue;
		}
		uintptr_t frame = pmm_alloc_frame();
		if (!frame) {
			return -ENOMEM;
		}
		if (vmm_map_page(page, frame, VMM_WRITABLE | VMM_USER) != 0) {
			pmm_free_frame(frame);
			return -ENOMEM;
		}
		memset((void *)page, 0, PAGE_SIZE);
	}
	return 0;
}

static bool writable_segment_covers(const struct elf_header *h, const struct elf_phdr *ph,
                                    uintptr_t page)
{
	for (uint16_t i = 0; i < h->phnum; i++) {
		if (ph[i].type == PT_LOAD && ph[i].memsz && (ph[i].flags & PF_W)
		    && page < ph[i].vaddr + ph[i].memsz && ph[i].vaddr < page + PAGE_SIZE) {
			return true;
		}
	}
	return false;
}

int elf_load(const uint8_t *image, size_t size, uintptr_t *entry)
{
	const struct elf_header *h = (const void *)image;
	if (!valid_header(h, size)) {
		return -ENOEXEC;
	}
	const struct elf_phdr *ph = (const void *)(image + h->phoff);

	bool entry_loaded = false;
	for (uint16_t i = 0; i < h->phnum; i++) {
		if (ph[i].type != PT_LOAD || ph[i].memsz == 0) {
			continue;
		}
		if (!valid_segment(&ph[i], size)) {
			return -ENOEXEC;
		}
		int rc = map_range(ph[i].vaddr, ph[i].vaddr + ph[i].memsz);
		if (rc) {
			return rc;
		}
		memcpy((void *)ph[i].vaddr, image + ph[i].offset, ph[i].filesz);
		if (h->entry >= ph[i].vaddr && h->entry - ph[i].vaddr < ph[i].memsz) {
			entry_loaded = true;
		}
	}
	if (!entry_loaded) {
		return -ENOEXEC;
	}

	/* Segments without PF_W (code, constants) become read-only once
	 * filled, except pages a writable segment shares. */
	for (uint16_t i = 0; i < h->phnum; i++) {
		if (ph[i].type != PT_LOAD || ph[i].memsz == 0 || (ph[i].flags & PF_W)) {
			continue;
		}
		for (uintptr_t page = ph[i].vaddr & ~(uintptr_t)(PAGE_SIZE - 1);
		     page < ph[i].vaddr + ph[i].memsz; page += PAGE_SIZE) {
			if (!writable_segment_covers(h, ph, page)) {
				vmm_set_writable(page, false);
			}
		}
	}
	*entry = h->entry;
	return 0;
}
