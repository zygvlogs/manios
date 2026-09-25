#ifndef ZKT_ARCH_I386_IDT_H
#define ZKT_ARCH_I386_IDT_H

/* Builds the IDT: vectors 0-31 go to the CPU exception stubs and
 * IRQ_BASE_VECTOR..+15 to the PIC IRQ stubs, all in isr.S. */
void idt_init(void);

#endif
