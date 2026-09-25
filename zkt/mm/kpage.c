#include "kpage.h"
#include <stdbool.h>
#include <stddef.h>
#include "cpu.h"
#include "kstring.h"
#include "memlayout.h"
#include "panic.h"
#include "pmm.h"
#include "vmm.h"

#define SLOTS (KERNEL_PAGES_SIZE / PAGE_SIZE)

static bool used[SLOTS];

void *kpage_alloc(uintptr_t *phys_out)
{
	void *page = 0;
	uint32_t flags = cpu_irq_save();
	for (size_t i = 0; i < SLOTS; i++) {
		if (used[i]) {
			continue;
		}
		uintptr_t va = KERNEL_PAGES_START + i * PAGE_SIZE;
		uintptr_t frame = pmm_alloc_frame();
		if (!frame || vmm_map_page(va, frame, VMM_WRITABLE) != 0) {
			if (frame) {
				pmm_free_frame(frame);
			}
			break;
		}
		used[i] = true;
		memset((void *)va, 0, PAGE_SIZE);
		*phys_out = frame;
		page = (void *)va;
		break;
	}
	cpu_irq_restore(flags);
	return page;
}

void kpage_free(void *page)
{
	uintptr_t va = (uintptr_t)page;
	size_t i = (va - KERNEL_PAGES_START) / PAGE_SIZE;
	uintptr_t phys;
	if (va < KERNEL_PAGES_START || va % PAGE_SIZE || i >= SLOTS || !used[i]
	    || vmm_unmap_page(va, &phys) != 0) {
		panic("kpage_free: not a live kernel page");
	}
	pmm_free_frame(phys);
	uint32_t flags = cpu_irq_save();
	used[i] = false;
	cpu_irq_restore(flags);
}
