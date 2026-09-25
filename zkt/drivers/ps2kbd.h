#ifndef ZKT_DRIVERS_PS2KBD_H
#define ZKT_DRIVERS_PS2KBD_H

/* PS/2 keyboard on IRQ1 (US layout). Decoded characters go to
 * console_input(); its only consumer is the console, so it registers no
 * device of its own. */
void ps2kbd_init(void);

#endif
