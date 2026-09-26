#ifndef ZKT_DRIVERS_INPUT_H
#define ZKT_DRIVERS_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/* From IRQ handlers: a key (ASCII or ZKT_KEY_*) and whether Alt was
 * held, a mouse report. */
void input_key(uint8_t key, bool alt);
void input_mouse(int dx, int dy, uint8_t buttons);

/* Registers /dev/kbd, and /dev/mouse if a mouse was found (input.c). */
void input_register(bool have_mouse);

#endif
