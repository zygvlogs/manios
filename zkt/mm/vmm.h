/* Architecture-neutral virtual memory interface. The i386 implementation
 * lives in zkt/arch/i386/paging.c; generic code only uses these calls. */
#ifndef ZKT_MM_VMM_H
#define ZKT_MM_VMM_H

#include <stdint.h>

#define VMM_WRITABLE (1u << 0)

/* Takes over the boot page directory. Requires pmm_init() first, since
 * new page tables are allocated from the PMM. */
void vmm_init(void);

/* Maps one page. Returns 0, or -1 if the addresses are not page-aligned,
 * the page is already mapped, or no frame is left for a page table. */
int vmm_map_page(uintptr_t virt, uintptr_t phys, unsigned flags);

/* Returns 0 and stores the previously mapped frame, or -1 if unmapped.
 * Page tables are not freed when they become empty. */
int vmm_unmap_page(uintptr_t virt, uintptr_t *phys_out);

/* Returns 0 and stores the physical address backing virt (including its
 * offset within the page), or -1 if virt is unmapped. */
int vmm_translate(uintptr_t virt, uintptr_t *phys_out);

#endif
