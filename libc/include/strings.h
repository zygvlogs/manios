#ifndef MANIOS_STRINGS_H
#define MANIOS_STRINGS_H

#include <stddef.h>

int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);
void bzero(void *p, size_t n); /* memset(p, 0, n) */

#endif
