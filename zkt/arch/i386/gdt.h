#ifndef ZKT_ARCH_I386_GDT_H
#define ZKT_ARCH_I386_GDT_H

/* Installs ZKT's own flat GDT (null, kernel code, kernel data) and
 * reloads the segment registers. User-mode segments and the TSS are
 * added when ring 3 processes exist (M8) -- see
 * docs/FOUNDING-PROPOSAL.md §2.2. */
void gdt_init(void);

#endif
