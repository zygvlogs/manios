#ifndef ZKT_DRIVERS_VGA_HW_H
#define ZKT_DRIVERS_VGA_HW_H

#include <stdbool.h>

/* Saves the text mode's registers, palette and font, before a graphics
 * mode overwrites them; restores them after. */
void vga_save_text(void);
void vga_restore_text(void);

/* Mode 13h: 320x200, one byte per pixel at physical 0xA0000, with the
 * palette set so each byte is an RGB 3-3-2 colour. */
void vga_set_mode13(void);

#endif
