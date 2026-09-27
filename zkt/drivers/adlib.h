#ifndef ZKT_DRIVERS_ADLIB_H
#define ZKT_DRIVERS_ADLIB_H

/* Looks for an OPL2 FM synthesizer (an AdLib card, or a Sound Blaster's)
 * at 0x388 and registers it as device "opl". Needs the timer. */
void adlib_init(void);

#endif
