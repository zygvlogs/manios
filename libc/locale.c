/* The "C" locale, ManiOS's only one: a character is a byte, printable if
 * it is printable ASCII. */
#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

char *setlocale(int category, const char *locale)
{
	(void)category;
	if (!locale || !*locale || !strcmp(locale, "C") || !strcmp(locale, "POSIX")) {
		return (char *)"C";
	}
	return NULL;
}

int mbtowc(wchar_t *wc, const char *s, size_t n)
{
	if (!s) {
		return 0; /* no shift states */
	}
	if (!n) {
		return -1;
	}
	if (wc) {
		*wc = (unsigned char)*s;
	}
	return *s ? 1 : 0;
}

int mblen(const char *s, size_t n)
{
	return mbtowc(NULL, s, n);
}

int wctomb(char *s, wchar_t wc)
{
	if (!s) {
		return 0;
	}
	if (wc < 0 || wc > 0xFF) {
		errno = EINVAL;
		return -1;
	}
	*s = (char)wc;
	return 1;
}

int wcwidth(wchar_t wc)
{
	if (!wc) {
		return 0;
	}
	return wc >= 0x20 && wc < 0x7F ? 1 : -1;
}

int iswblank(wint_t wc) { return wc == ' ' || wc == '\t'; }
int iswspace(wint_t wc) { return wc == ' ' || (wc >= '\t' && wc <= '\r'); }
int iswprint(wint_t wc) { return wc >= 0x20 && wc < 0x7F; }
wint_t towlower(wint_t wc) { return wc >= 'A' && wc <= 'Z' ? wc + ('a' - 'A') : wc; }
wint_t towupper(wint_t wc) { return wc >= 'a' && wc <= 'z' ? wc - ('a' - 'A') : wc; }
