#ifndef ZKT_DRIVERS_PS2MOUSE_H
#define ZKT_DRIVERS_PS2MOUSE_H

#include <stdbool.h>

/* Enables a PS/2 mouse on the auxiliary port (IRQ 12); false if none
 * answers. Reports go to /dev/mouse (input.c). */
bool ps2mouse_init(void);

#endif
