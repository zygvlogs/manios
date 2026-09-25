/* Architecture-neutral virtual memory interface. The i386 implementation
 * lives in zkt/arch/i386/paging.c; generic code only uses these calls. */
#ifndef ZKT_MM_VMM_H
#define ZKT_MM_VMM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VMM_WRITABLE (1u << 0)
#define VMM_USER     (1u << 1) /* user-accessible; exactly for user-space addresses */

/* Takes over the boot page directory. Requires pmm_init() first, since
 * new page tables are allocated from the PMM. */
void vmm_init(void);

/* Maps one page. Returns 0, or -1 if the addresses are not page-aligned,
 * the page is already mapped, or no frame is left for a page table. */
int vmm_map_page(uintptr_t virt, uintptr_t phys, unsigned flags);

/* Returns 0 and stores the previously mapped frame, or -1 if unmapped.
 * Page tables are not freed when they become empty. */
int vmm_unmap_page(uintptr_t virt, uintptr_t *phys_out);

/* Makes the page containing virt writable or read-only. Returns 0, or
 * -1 if it is unmapped. The 80386 ignores read-only at ring 0 (it has
 * no CR0.WP), so this protects pages from user code only. */
int vmm_set_writable(uintptr_t virt, bool writable);

/* Returns 0 and stores the physical address backing virt (including its
 * offset within the page), or -1 if virt is unmapped. */
int vmm_translate(uintptr_t virt, uintptr_t *phys_out);

/* Address spaces. Each has its own user half; the kernel half is shared.
 * map/unmap/translate act on the active one. */
struct address_space;

/* NULL when out of memory. */
struct address_space *vmm_as_create(void);

/* Makes `as` the active address space; NULL means the kernel's own,
 * which has an empty user half. */
void vmm_as_activate(struct address_space *as);

/* Unmaps and frees every user page and user page table of the active
 * address space. */
void vmm_as_clear_user(void);

/* Frees an address space. It must not be active, and its user half
 * must already be cleared. */
void vmm_as_destroy(struct address_space *as);

/* Whether [addr, addr + len) is user space mapped user-accessible (and
 * writable, if `write`) in the active address space. */
bool vmm_user_range_ok(uintptr_t addr, size_t len, bool write);

#endif
