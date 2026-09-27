#ifndef ZKT_DRIVERS_UART_H
#define ZKT_DRIVERS_UART_H

/* Looks for serial ports COM2, COM3 and COM4 and registers each found
 * as "comN" and "comNctl". Needs interrupts. */
void uart_init(void);

#endif
