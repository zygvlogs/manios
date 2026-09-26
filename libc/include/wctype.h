#ifndef MANIOS_WCTYPE_H
#define MANIOS_WCTYPE_H

#include <wchar.h>

int iswblank(wint_t wc);
int iswspace(wint_t wc);
int iswprint(wint_t wc);
wint_t towlower(wint_t wc);
wint_t towupper(wint_t wc);

#endif
