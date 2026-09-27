#ifndef ZKT_DRIVERS_FLOPPY_H
#define ZKT_DRIVERS_FLOPPY_H

/* Finds the floppy drives the CMOS lists, resets the controller, finds
 * each disk's format and registers block devices "fd0" and "fd1". Needs
 * interrupts and the scheduler. */
void floppy_init(void);

#endif
