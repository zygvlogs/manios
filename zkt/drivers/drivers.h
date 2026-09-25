#ifndef ZKT_DRIVERS_DRIVERS_H
#define ZKT_DRIVERS_DRIVERS_H

/* Starts the interrupt-driven drivers and registers every device. Needs
 * the scheduler, the timer and interrupts enabled. */
void drivers_init(void);

#endif
