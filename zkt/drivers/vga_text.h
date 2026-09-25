#ifndef ZKT_DRIVERS_VGA_TEXT_H
#define ZKT_DRIVERS_VGA_TEXT_H

#include <stddef.h>

/* 80x25 VGA text-mode console at 0xB8000, with scrolling and the
 * hardware cursor. Handles '\n', '\r', '\b' and '\t'. Each call is
 * atomic with respect to interrupts. */
void vga_clear(void);
void vga_write(const char *buf, size_t len);

/* While a graphics mode owns the display (fb.c): suspend keeps writing
 * into a copy of the screen, resume puts that copy back on the (text
 * mode) display. */
void vga_text_suspend(void);
void vga_text_resume(void);

/* Registers device "vga" (write-only character device). */
void vga_register(void);

#endif
