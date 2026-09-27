/* DMA memory (dma.h). */
#include "dma.h"
#include "kstring.h"
#include "memlayout.h"
#include "pmm.h"

#define DMA_LIMIT BOOT_MAPPED_PHYS_LIMIT /* 16 MiB: also ISA DMA's reach */
#define ISA_BOUNDARY 0x10000

void *dma_alloc(size_t bytes, bool isa, uintptr_t *phys)
{
	size_t frames = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
	if (!frames || (isa && bytes > ISA_BOUNDARY)) {
		return NULL;
	}
	uintptr_t p = pmm_alloc_contiguous(frames, DMA_LIMIT, isa ? ISA_BOUNDARY : 0);
	if (!p) {
		return NULL;
	}
	void *va = P2V(p);
	memset(va, 0, frames * PAGE_SIZE);
	*phys = p;
	return va;
}

void dma_free(void *p, size_t bytes)
{
	uintptr_t phys = V2P(p);
	for (size_t f = 0; f < (bytes + PAGE_SIZE - 1) / PAGE_SIZE; f++) {
		pmm_free_frame(phys + f * PAGE_SIZE);
	}
}
