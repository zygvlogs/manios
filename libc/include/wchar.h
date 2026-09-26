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

#endif
