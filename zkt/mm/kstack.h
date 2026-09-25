#ifndef ZKT_MM_KSTACK_H
#define ZKT_MM_KSTACK_H

#include <stdint.h>
#include "memlayout.h"

#define KSTACK_PAGES 2
#define KSTACK_SIZE (KSTACK_PAGES * PAGE_SIZE)

/* Maps a KSTACK_SIZE kernel stack with an unmapped guard page below it
 * and returns its top (initial stack pointer), or 0 when the stack
 * region or physical memory is exhausted. */
uintptr_t kstack_alloc(void);

/* Unmaps and frees a stack by its top. Panics on anything else. */
void kstack_free(uintptr_t top);

#endif
