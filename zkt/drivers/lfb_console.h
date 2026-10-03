#ifndef ZKT_DRIVERS_LFB_CONSOLE_H
#define ZKT_DRIVERS_LFB_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

/* A software text console that draws into a 32-bit XRGB linear framebuffer.
 * Used when the boot loader has already put the display into a VESA graphics
 * mode, because the VGA text buffer is no longer visible. */
void lfb_console_init(uintptr_t base, uint32_t width, uint32_t height,
                      uint32_t pitch);
void lfb_console_write(const char *buf, size_t len);

#endif
