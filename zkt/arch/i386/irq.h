/* Hardware interrupt registration. Generic code includes this by name;
 * on i386 the lines are the 16 legacy ISA IRQs behind the 8259 PICs. */
#ifndef ZKT_ARCH_I386_IRQ_H
#define ZKT_ARCH_I386_IRQ_H

typedef void (*irq_handler_t)(void);

/* Installs a handler for IRQ line `irq` and unmasks the line. A line
 * can have a few handlers (PCI cards share lines); all of them run on
 * each interrupt, and each must check whether its device asked. Panics
 * if the line doesn't exist or has too many handlers. Handlers run with
 * interrupts disabled, after the PIC has been acknowledged, and must not
 * sleep, yield or exit: that would suspend whichever thread happened to
 * be interrupted. */
void irq_install_handler(unsigned irq, irq_handler_t handler);

/* Called from isr_handler for vectors IRQ_BASE_VECTOR..+15. */
void irq_dispatch(unsigned irq);

#endif
