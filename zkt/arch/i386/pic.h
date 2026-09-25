#ifndef ZKT_ARCH_I386_PIC_H
#define ZKT_ARCH_I386_PIC_H

#include <stdbool.h>

/* IRQ n arrives on vector IRQ_BASE_VECTOR + n; the power-on default
 * (0x08) collides with CPU exception vectors. Must match isr.S. */
#define IRQ_BASE_VECTOR 0x20
#define IRQ_LINES 16

/* Remaps both 8259s to IRQ_BASE_VECTOR and masks every line; lines are
 * unmasked one at a time as handlers are installed (irq.c). */
void pic_remap_and_mask(void);

void pic_mask(unsigned irq);
void pic_unmask(unsigned irq);
void pic_send_eoi(unsigned irq);

/* IRQ 7 and 15 can be raised spuriously (line noise, or a request
 * withdrawn before the CPU acknowledged it); the PIC then reports the
 * lowest-priority line without marking it in service. */
bool pic_is_spurious(unsigned irq);

#endif
