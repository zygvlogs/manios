#ifndef ZKT_DRIVERS_SERIAL_H
#define ZKT_DRIVERS_SERIAL_H

#include <stdbool.h>
#include <stddef.h>

/* COM1 at 38400 8N1. Output translates '\n' to "\r\n" (the line is a
 * terminal); received bytes go to console_input(). */

/* Programs the UART for polled output. Safe before anything else. */
void serial_init(void);

/* Starts interrupt-driven operation (RX, and buffered TX) and registers
 * device "com1". Needs IRQs and the scheduler. */
void serial_start(void);

bool serial_buffered(void);

/* Busy-waits on the UART. Interrupts must be disabled; anything still
 * buffered goes out first, so output stays in order. */
void serial_write_polled(const char *buf, size_t len);

/* Queues output for the TX interrupt, blocking while the buffer is
 * full. Thread context with interrupts enabled, after serial_start(). */
void serial_write_buffered(const char *buf, size_t len);

#endif
