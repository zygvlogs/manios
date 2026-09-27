#ifndef ZKT_DRIVERS_ISADMA_H
#define ZKT_DRIVERS_ISADMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The PC's ISA DMA controllers (two Intel 8237s): channels 0-3 move
 * bytes, 5-7 16-bit words. isa_dma_start programs channel `ch` to move
 * `len` bytes at physical `phys` (below 16 MiB, within one 64 KiB block
 * -- 128 KiB for 16-bit channels: dma_alloc gives such memory), to the
 * device (`to_device`) or from it, once or over and over (`repeat`,
 * auto-initialisation, for sound), and unmasks it. */
void isa_dma_start(int ch, uintptr_t phys, size_t len, bool to_device, bool repeat);
void isa_dma_stop(int ch);
/* Bytes the channel has still to move in this round. */
size_t isa_dma_remaining(int ch);

#endif
