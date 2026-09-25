/* libc-internal: the printf engine. */
#ifndef MANIOS_LIBC_FORMAT_H
#define MANIOS_LIBC_FORMAT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef void (*format_emit)(void *ctx, const char *s, size_t n);

/* Formats as printf does, passing the output to `emit` in pieces;
 * returns the number of characters produced. */
int format(format_emit emit, void *ctx, const char *fmt, va_list ap);

#endif
