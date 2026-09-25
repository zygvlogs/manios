#ifndef ZKT_KERNEL_KSTRING_H
#define ZKT_KERNEL_KSTRING_H

#include <stddef.h>

/* GCC may emit calls to these four even with -ffreestanding (struct
 * copies, loops it recognises), so the kernel must always provide them
 * under their standard names. */
void *memset(void *dst, int c, size_t n);
void *memcpy(void *restrict dst, const void *restrict src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

#endif
