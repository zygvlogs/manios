#ifndef ZKT_DRIVERS_SERIAL_H
#define ZKT_DRIVERS_SERIAL_H

/* COM1 (0x3F8) polling driver, 38400 8N1. First-class debug surface
 * per docs/FOUNDING-PROPOSAL.md §4.3 -- often the only visible output
 * before a display driver exists, and the channel CI reads. */
void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);

#endif
