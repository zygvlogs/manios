#ifndef ZKT_DRIVERS_LPT_H
#define ZKT_DRIVERS_LPT_H

/* Looks for the parallel port at 0x378 and registers it as "lpt1".
 * Needs the timer. */
void lpt_init(void);

#endif
