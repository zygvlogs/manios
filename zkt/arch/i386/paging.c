/* i386 two-level paging behind the generic VMM interface (zkt/mm/vmm.h).
 *
 * Page tables are reached through a recursive mapping: page directory
 * entry 1023 points at the page directory itself, so page table N
 * appears at RECURSIVE_PT_BASE + N * PAGE_SIZE and the directory at
 * RECURSIVE_PD_ADDR. That lets page tables live in any physical frame
 * the PMM hands out, not just the 4 MiB the boot mapping covers.
 *
 * Every address space shares the kernel's page tables (directory entries
 * KERNEL_PD_FIRST..1022). A kernel page table created while any address
 * space is active is therefore written into every page directory: each
 * one is also mapped at a kernel address, so all are always reachable. */
#include "vmm.h"
#include "cpu.h"
#include "heap.h"
#include "kpage.h"
#include "kstring.h"
#include "memlayout.h"
#include "panic.h"
#include "pmm.h"

#define PTE_PRESENT  0x001u
#define PTE_WRITABLE 0x002u
#define PTE_USER     0x004u
#define PTE_ADDR_MASK 0xFFFFF000u

extern char kernel_stack_guard[]; /* boot.S */

struct address_space {
	uintptr_t pd_phys;
	uint32_t *pd; /* the page directory's kernel virtual address */
	struct address_space *next;
};

static struct address_space kernel_space;
static struct address_space *all_spaces = &kernel_space;
static struct address_space *active = &kernel_space;

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
	kernel_space.pd = boot_page_directory;
	kernel_space.pd_phys = V2P(boot_page_directory);
	boot_page_directory[RECURSIVE_PD_INDEX] =
	    kernel_space.pd_phys | PTE_PRESENT | PTE_WRITABLE;
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
	bool user = flags & VMM_USER;

	if ((virt | phys) & (PAGE_SIZE - 1) || pdi == RECURSIVE_PD_INDEX
	    || user != (virt < USER_SPACE_TOP)) {
		return -1;
	}

	if (!(page_directory[pdi] & PTE_PRESENT)) {
		uintptr_t table = pmm_alloc_frame();
		if (!table) {
			return -1;
		}
		uint32_t pde = table | PTE_PRESENT | PTE_WRITABLE | (user ? PTE_USER : 0);
		if (pdi >= KERNEL_PD_FIRST) {
			for (struct address_space *as = all_spaces; as; as = as->next) {
				as->pd[pdi] = pde;
			}
		} else {
			page_directory[pdi] = pde;
		}
		cpu_flush_tlb();
		memset(page_table(pdi), 0, PAGE_SIZE);
	}

	uint32_t *pt = page_table(pdi);
	if (pt[pti] & PTE_PRESENT) {
		return -1;
	}
	pt[pti] = phys | PTE_PRESENT | ((flags & VMM_WRITABLE) ? PTE_WRITABLE : 0)
	          | (user ? PTE_USER : 0);
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

int vmm_set_writable(uintptr_t virt, bool writable)
{
	int rc = -1;
	uint32_t flags = cpu_irq_save();
	uint32_t *pte = pte_of(virt);
	if (pte && pd_index_of(virt) != RECURSIVE_PD_INDEX) {
		*pte = writable ? *pte | PTE_WRITABLE : *pte & ~PTE_WRITABLE;
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

struct address_space *vmm_as_create(void)
{
	struct address_space *as = kmalloc(sizeof(*as));
	if (!as) {
		return 0;
	}
	as->pd = kpage_alloc(&as->pd_phys);
	if (!as->pd) {
		kfree(as);
		return 0;
	}
	/* Copying the kernel half and joining the list must be atomic with
	 * respect to map_page_locked(), which updates every listed directory. */
	uint32_t flags = cpu_irq_save();
	for (uint32_t i = KERNEL_PD_FIRST; i < RECURSIVE_PD_INDEX; i++) {
		as->pd[i] = kernel_space.pd[i];
	}
	as->pd[RECURSIVE_PD_INDEX] = as->pd_phys | PTE_PRESENT | PTE_WRITABLE;
	as->next = all_spaces;
	all_spaces = as;
	cpu_irq_restore(flags);
	return as;
}

void vmm_as_activate(struct address_space *as)
{
	struct address_space *target = as ? as : &kernel_space;
	uint32_t flags = cpu_irq_save();
	if (target != active) {
		active = target;
		cpu_load_cr3(target->pd_phys);
	}
	cpu_irq_restore(flags);
}

void vmm_as_clear_user(void)
{
	for (uint32_t pdi = 0; pdi < KERNEL_PD_FIRST; pdi++) {
		if (!(page_directory[pdi] & PTE_PRESENT)) {
			continue;
		}
		uint32_t *pt = page_table(pdi);
		for (uint32_t pti = 0; pti < 1024; pti++) {
			if (pt[pti] & PTE_PRESENT) {
				pmm_free_frame(pt[pti] & PTE_ADDR_MASK);
				pt[pti] = 0;
			}
		}
		uintptr_t table = page_directory[pdi] & PTE_ADDR_MASK;
		page_directory[pdi] = 0;
		cpu_flush_tlb();
		pmm_free_frame(table);
	}
}

void vmm_as_destroy(struct address_space *as)
{
	uint32_t flags = cpu_irq_save();
	if (as == active || as == &kernel_space) {
		panic("vmm_as_destroy: address space is in use");
	}
	struct address_space **link = &all_spaces;
	while (*link != as) {
		link = &(*link)->next;
	}
	*link = as->next;
	cpu_irq_restore(flags);

	kpage_free(as->pd);
	kfree(as);
}

bool vmm_user_range_ok(uintptr_t addr, size_t len, bool write)
{
	if (len == 0) {
		return addr <= USER_SPACE_TOP;
	}
	if (addr >= USER_SPACE_TOP || len > USER_SPACE_TOP - addr) {
		return false;
	}
	uint32_t need = PTE_PRESENT | PTE_USER | (write ? PTE_WRITABLE : 0);
	bool ok = true;
	uint32_t flags = cpu_irq_save();
	for (uintptr_t page = addr & ~(uintptr_t)(PAGE_SIZE - 1); page < addr + len;
	     page += PAGE_SIZE) {
		uint32_t pdi = pd_index_of(page);
		if ((page_directory[pdi] & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)
		    || (page_table(pdi)[pt_index_of(page)] & need) != need) {
			ok = false;
			break;
		}
	}
	cpu_irq_restore(flags);
	return ok;
}
