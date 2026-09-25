#ifndef ZKT_ARCH_I386_PIC_H
#define ZKT_ARCH_I386_PIC_H

/* Remaps the 8259 PIC's IRQ vectors from their power-on defaults
 * (0x08-0x0F, which collide with CPU exception vectors) to 0x20-0x2F,
 * then masks every IRQ line. No IRQ handlers are installed until M3/M5,
 * so every line stays masked after this call -- see
 * docs/FOUNDING-PROPOSAL.md §2.2 and §7. */
void pic_remap_and_mask(void);

#endif
