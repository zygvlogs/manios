/* The kernel command line: space-separated words, each KEY=VALUE or a
 * bare KEY, e.g. "ip=10.0.0.2/24 gw=10.0.0.1 export=/n/ata0p1". Given
 * by the boot loader (QEMU: -append). */
#ifndef ZKT_KERNEL_CMDLINE_H
#define ZKT_KERNEL_CMDLINE_H

#include <stdbool.h>
#include <stddef.h>

#define CMDLINE_MAX 256

void cmdline_set(const char *line);
const char *cmdline(void);

/* Copies the value of the first KEY=VALUE word into out and returns
 * true; false if there is none (or it doesn't fit). */
bool cmdline_get(const char *key, char *out, size_t size);

#endif
