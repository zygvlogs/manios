#ifndef ZKT_KERNEL_KPRINTF_H
#define ZKT_KERNEL_KPRINTF_H

#include <stdarg.h>
#include <stddef.h>

/* printf subset: %s %c %d %u %x %%, with optional '-' (left-justify),
 * '0' (zero-pad) and a width. Integers are 32-bit (%lu etc. accepted);
 * there is no 64-bit conversion, since dividing a 64-bit value would
 * pull in libgcc, which is built for i686. Output is truncated to fit
 * and always NUL-terminated; returns the untruncated length. */
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/* Formats up to 255 characters and writes them as one console message. */
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
