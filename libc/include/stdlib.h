#ifndef MANIOS_STDLIB_H
#define MANIOS_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

/* The heap grows with sbrk(); blocks are 16-byte aligned. free() of a
 * pointer malloc() did not return aborts the program. */
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

/* exit() runs atexit handlers (last registered first, at most 16),
 * flushes every stream, then ends the process; abort() does not. */
__attribute__((noreturn)) void exit(int code);
__attribute__((noreturn)) void abort(void);
int atexit(void (*fn)(void));

int abs(int v);
long labs(long v);
int atoi(const char *s);
long atol(const char *s);
/* Base 0 (prefix-detected), or 2-36; out of range: LONG_MAX/LONG_MIN or
 * ULONG_MAX and errno ERANGE. */
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);

/* From OpenBSD (third_party/openbsd/lib/libc/stdlib). */
long long strtonum(const char *s, long long min, long long max, const char **errstr);
void *reallocarray(void *ptr, size_t count, size_t size);

/* This program's name: the last part of argv[0] (err() prints it). */
const char *getprogname(void);

/* Multibyte characters, in the "C" locale: one byte each (<locale.h>). */
#define MB_CUR_MAX 1
int mblen(const char *s, size_t n);
int mbtowc(wchar_t *wc, const char *s, size_t n);
int wctomb(char *s, wchar_t wc);

void qsort(void *base, size_t count, size_t size, int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*cmp)(const void *, const void *));

#endif
