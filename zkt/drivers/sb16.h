#ifndef ZKT_DRIVERS_SB16_H
#define ZKT_DRIVERS_SB16_H

/* Looks for a Sound Blaster 16 at 0x220 and, found, plays /dev/audio
 * through it. Needs the timer. */
void sb16_init(void);

#endif
