#ifndef ZKT_KERNEL_KCONSOLE_H
#define ZKT_KERNEL_KCONSOLE_H

#include <stddef.h>
#include <stdint.h>

/* The kernel console: output goes to both COM1 and the VGA display,
 * since either may be the only thing visible (FOUNDING-PROPOSAL §4.3);
 * input comes from the PS/2 keyboard and COM1. Each write is one
 * message: messages from different threads never interleave. */
void kconsole_init(void);
void kconsole_putc(char c);
void kconsole_write(const char *s);
void kconsole_write_n(const char *s, size_t len);
void kconsole_write_hex32(uint32_t value);
void kconsole_write_dec(uint32_t value);

/* Queues one input byte for readers of device "cons". Called by the
 * keyboard and serial IRQ handlers; the byte is dropped if the queue
 * is full. */
void console_input(char c);

/* Registers device "cons", the equivalent of Plan 9's /dev/cons. Writes
 * go to the console output. Reads are "cooked": input is echoed and can
 * be edited (backspace, ^U erases the line) until Enter, and a read
 * returns at most one line, ending in '\n'. ^D on an empty line reads
 * as end of file (0 bytes). A line holds up to CONSOLE_LINE_MAX - 1
 * characters, newline included. */
#define CONSOLE_LINE_MAX 256
void console_register(void);

#endif
