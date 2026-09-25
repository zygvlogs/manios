/* Periodic hardware clock. Generic code includes this by name; the i386
 * implementation is the 8253/8254 PIT (pit.c). */
#ifndef ZKT_ARCH_I386_CLOCK_H
#define ZKT_ARCH_I386_CLOCK_H

#include <stdint.h>
#include "irq.h"

/* Calls `tick` from interrupt context `hz` times per second. Panics if
 * the hardware cannot produce that rate. */
void clock_start_periodic(uint32_t hz, irq_handler_t tick);

#endif
