#ifndef ZKT_MM_DMA_H
#define ZKT_MM_DMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Memory for devices to read and write by DMA: physically contiguous,
 * zeroed, below 16 MiB -- where the kernel's boot mapping reaches it,
 * and where the PC's ISA DMA controller can -- and, for ISA DMA (`isa`),
 * within one 64 KiB block, which that controller can't cross. Returns
 * its kernel address and stores its physical one; NULL when there is no
 * such memory (or `isa` and more than 64 KiB is asked for). */
void *dma_alloc(size_t bytes, bool isa, uintptr_t *phys);
void dma_free(void *p, size_t bytes);

#endif
