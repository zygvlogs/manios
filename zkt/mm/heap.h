#ifndef ZKT_MM_HEAP_H
#define ZKT_MM_HEAP_H

#include <stddef.h>

/* The kernel heap occupies [KERNEL_HEAP_START, +KERNEL_HEAP_MAX_SIZE)
 * (memlayout.h) and maps frames from the PMM on demand. Requires
 * vmm_init(). */
void heap_init(void);

/* Returns 16-byte-aligned memory, or NULL for size 0 or when the heap
 * region or physical memory is exhausted. */
void *kmalloc(size_t size);

/* Panics on a double free or a pointer kmalloc() did not return. */
void kfree(void *ptr);

/* Payload bytes currently allocated (after rounding to 16). */
size_t heap_used(void);

/* Walks every block; returns 0 if the heap is consistent, -1 if not. */
int heap_check(void);

#endif
