#ifndef ZKT_DRIVERS_PS2KBD_H
#define ZKT_DRIVERS_PS2KBD_H

/* PS/2 keyboard on IRQ1 (US layout). Decoded keys go to input_key()
 * (input.c): to /dev/kbd while it is open, else to the console. */
void ps2kbd_init(void);

#endif
