/* i386 two-level paging behind the generic VMM interface (zkt/mm/vmm.h).
 *
 * Page tables are reached through a recursive mapping: page directory
 * entry 1023 points at the page directory itself, so page table N
 * appears at RECURSIVE_PT_BASE + N * PAGE_SIZE and the directory at
 * RECURSIVE_PD_ADDR. That lets page tables live in any physical frame
 * the PMM hands out, not just the 4 MiB the boot mapping covers. */
#include "vmm.h"
#include "cpu.h"
#include "kstring.h"
#include "memlayout.h"
#include "pmm.h"

#define PTE_PRESENT  0x001u
#define PTE_WRITABLE 0x002u
#define PTE_ADDR_MASK 0xFFFFF000u

extern char kernel_stack_guard[]; /* boot.S */

static uint32_t *const page_directory = (uint32_t *)RECURSIVE_PD_ADDR;

static uint32_t *page_table(uint32_t pd_index)
{
	return (uint32_t *)(RECURSIVE_PT_BASE + pd_index * PAGE_SIZE);
}

static uint32_t pd_index_of(uintptr_t virt)
{
	return (uint32_t)(virt >> 22);
}

static uint32_t pt_index_of(uintptr_t virt)
{
	return (uint32_t)((virt >> 12) & 0x3FF);
}

void vmm_init(void)
{
	boot_page_directory[RECURSIVE_PD_INDEX] =
	    V2P(boot_page_directory) | PTE_PRESENT | PTE_WRITABLE;
	cpu_flush_tlb();

	/* The frame stays reserved in the PMM; only the mapping goes. */
	uintptr_t guard_phys;
	vmm_unmap_page((uintptr_t)kernel_stack_guard, &guard_phys);
}

/* Interrupts off is the page tables' lock (single CPU). */
static int map_page_locked(uintptr_t virt, uintptr_t phys, unsigned flags)
{
	uint32_t pdi = pd_index_of(virt);
	uint32_t pti = pt_index_of(virt);

	if ((virt | phys) & (PAGE_SIZE - 1) || pdi == RECURSIVE_PD_INDEX) {
		return -1;
	}

	if (!(page_directory[pdi] & PTE_PRESENT)) {
		uintptr_t table = pmm_alloc_frame();
		if (!table) {
			return -1;
		}
		page_directory[pdi] = table | PTE_PRESENT | PTE_WRITABLE;
		cpu_flush_tlb();
		memset(page_table(pdi), 0, PAGE_SIZE);
	}

	uint32_t *pt = page_table(pdi);
	if (pt[pti] & PTE_PRESENT) {
		return -1;
	}
	pt[pti] = phys | PTE_PRESENT | ((flags & VMM_WRITABLE) ? PTE_WRITABLE : 0);
	cpu_flush_tlb();
	return 0;
}

int vmm_map_page(uintptr_t virt, uintptr_t phys, unsigned flags)
{
	uint32_t irq_flags = cpu_irq_save();
	int rc = map_page_locked(virt, phys, flags);
	cpu_irq_restore(irq_flags);
	return rc;
}

static uint32_t *pte_of(uintptr_t virt)
{
	uint32_t pdi = pd_index_of(virt);
	if (!(page_directory[pdi] & PTE_PRESENT)) {
		return 0;
	}
	uint32_t *pte = &page_table(pdi)[pt_index_of(virt)];
	return (*pte & PTE_PRESENT) ? pte : 0;
}

int vmm_unmap_page(uintptr_t virt, uintptr_t *phys_out)
{
	int rc = -1;
	uint32_t flags = cpu_irq_save();
	uint32_t *pte = pte_of(virt);
	if (pte && pd_index_of(virt) != RECURSIVE_PD_INDEX) {
		*phys_out = *pte & PTE_ADDR_MASK;
		*pte = 0;
		cpu_flush_tlb();
		rc = 0;
	}
	cpu_irq_restore(flags);
	return rc;
}

int vmm_translate(uintptr_t virt, uintptr_t *phys_out)
{
	int rc = -1;
	uint32_t flags = cpu_irq_save();
	uint32_t *pte = pte_of(virt);
	if (pte) {
		*phys_out = (*pte & PTE_ADDR_MASK) | (virt & (PAGE_SIZE - 1));
		rc = 0;
	}
	cpu_irq_restore(flags);
	return rc;
}
