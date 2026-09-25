#ifndef ZKT_MM_KPAGE_H
#define ZKT_MM_KPAGE_H

#include <stdint.h>

/* Allocates one zeroed page at a fixed kernel virtual address (so it is
 * reachable from every address space) and stores its physical address.
 * NULL when out of memory or address range. */
void *kpage_alloc(uintptr_t *phys_out);

void kpage_free(void *page);

#endif
