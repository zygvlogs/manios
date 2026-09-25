#ifndef ZKT_DRIVERS_VGA_TEXT_H
#define ZKT_DRIVERS_VGA_TEXT_H

#include <stddef.h>

/* 80x25 VGA text-mode console at 0xB8000, with scrolling and the
 * hardware cursor. Handles '\n', '\r', '\b' and '\t'. Each call is
 * atomic with respect to interrupts. */
void vga_clear(void);
void vga_write(const char *buf, size_t len);

/* Registers device "vga" (write-only character device). */
void vga_register(void);

#endif
