/* Wide characters in the "C" locale: one per byte (<locale.h>). */
#ifndef MANIOS_WCHAR_H
#define MANIOS_WCHAR_H

#include <stddef.h>

typedef int wint_t;
typedef struct { int unused; } mbstate_t;
#define WEOF ((wint_t)-1)

/* Columns a character takes: 1 for printable ASCII, 0 for NUL, -1 for
 * anything else. */
int wcwidth(wchar_t wc);

#include <stdio.h>
size_t wcslen(const wchar_t *s);
wchar_t *wcschr(const wchar_t *s, wchar_t wc);
wint_t putwchar(wchar_t wc);
wint_t fputwc(wchar_t wc, FILE *f);
wint_t getwc(FILE *f);
wint_t fgetwc(FILE *f);
wint_t btowc(int c);
int wctob(wint_t wc);
size_t mbrtowc(wchar_t *wc, const char *s, size_t n, mbstate_t *ps);
size_t mbrlen(const char *s, size_t n, mbstate_t *ps);
size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps);
int mbsinit(const mbstate_t *ps);

#endif
