#ifndef ZKT_KERNEL_KCONSOLE_H
#define ZKT_KERNEL_KCONSOLE_H

#include <stdint.h>

/* The M1 kernel console: every message goes to both the serial port
 * and the VGA text display, since either one may be the only thing
 * visible depending on how ZKT was booted (see
 * docs/FOUNDING-PROPOSAL.md §4.3 on serial as a first-class surface). */
void kconsole_init(void);
void kconsole_putc(char c);
void kconsole_write(const char *s);
void kconsole_write_hex32(uint32_t value);

#endif
