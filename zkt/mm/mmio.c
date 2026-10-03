#include "mmio.h"
#include "cpu.h"
#include "memlayout.h"
#include "vmm.h"

static uintptr_t next = KERNEL_MMIO_START;

volatile void *mmio_map(uintptr_t phys, size_t len)
{
	uintptr_t first = phys & ~(uintptr_t)(PAGE_SIZE - 1);
	size_t pages = (phys - first + len + PAGE_SIZE - 1) / PAGE_SIZE;
	uint32_t flags = cpu_irq_save();
	uintptr_t base = next;
	/* The window must not reach the kernel's own page tables, which
	 * live in the directory entry below the recursive one: mapping
	 * over them would overwrite a page table with device memory. */
	if (pages > (KERNEL_PT_AREA_START - base) / PAGE_SIZE) {
		cpu_irq_restore(flags);
		return 0;
	}
	next += pages * PAGE_SIZE;
	cpu_irq_restore(flags);
	for (size_t i = 0; i < pages; i++) {
		if (vmm_map_page(base + i * PAGE_SIZE, first + i * PAGE_SIZE, VMM_WRITABLE | VMM_UNCACHED) != 0) {
			return 0;
		}
	}
	return (volatile void *)(base + (phys - first));
}
