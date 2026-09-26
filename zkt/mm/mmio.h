#ifndef ZKT_MM_MMIO_H
#define ZKT_MM_MMIO_H

#include <stddef.h>
#include <stdint.h>

/* Maps `len` bytes of device registers at physical `phys` into the
 * kernel, uncached, for good (drivers keep their devices). The kernel
 * address of `phys`, or NULL when the window is full. */
volatile void *mmio_map(uintptr_t phys, size_t len);

#endif
