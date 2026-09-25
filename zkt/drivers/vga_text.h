#ifndef ZKT_DRIVERS_VGA_TEXT_H
#define ZKT_DRIVERS_VGA_TEXT_H

/* 80x25 VGA text-mode console at 0xB8000, with scrolling. */
void vga_clear(void);
void vga_putc(char c);
void vga_write(const char *s);

#endif
