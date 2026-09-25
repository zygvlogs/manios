#ifndef ZKT_ARCH_I386_GDT_H
#define ZKT_ARCH_I386_GDT_H

#include <stdint.h>

/* Selector = GDT index * 8. gdt_flush.S and isr.S hardcode the first two. */
#define GDT_KERNEL_CODE_SEL      0x08
#define GDT_KERNEL_DATA_SEL      0x10
#define GDT_KERNEL_TSS_SEL       0x18
#define GDT_DOUBLE_FAULT_TSS_SEL 0x20
#define GDT_USER_CODE_SEL        0x2B /* index 5, RPL 3 */
#define GDT_USER_DATA_SEL        0x33 /* index 6, RPL 3 */

/* Installs ZKT's flat GDT (kernel and ring 3 code/data) and reloads
 * the segment registers. The TSS slots stay not-present until
 * tss_init() fills them. */
void gdt_init(void);

/* Fills the TSS descriptor for `selector` (32-bit, available, ring 0). */
void gdt_set_tss(uint16_t selector, uint32_t base, uint32_t limit);

#endif
