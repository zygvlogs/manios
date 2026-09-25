/* Per-architecture bring-up. Generic code includes this by name. */
#ifndef ZKT_ARCH_I386_ARCH_H
#define ZKT_ARCH_I386_ARCH_H

/* CPU tables and the interrupt controller: everything needed before the
 * first exception can be reported. Leaves interrupts disabled. */
void arch_early_init(void);

#endif
