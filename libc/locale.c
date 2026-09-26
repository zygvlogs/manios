/* The "C" locale, ManiOS's only one: a character is a byte, printable if
 * it is printable ASCII. */
#include <ctype.h>
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

int iswalnum(wint_t wc) { return wc >= 0 && wc < 0x80 && isalnum(wc); }
int iswalpha(wint_t wc) { return wc >= 0 && wc < 0x80 && isalpha(wc); }
int iswdigit(wint_t wc) { return wc >= '0' && wc <= '9'; }
int iswlower(wint_t wc) { return wc >= 'a' && wc <= 'z'; }
int iswupper(wint_t wc) { return wc >= 'A' && wc <= 'Z'; }
int iswpunct(wint_t wc) { return wc >= 0 && wc < 0x80 && ispunct(wc); }
int iswgraph(wint_t wc) { return wc > 0x20 && wc < 0x7F; }
int iswcntrl(wint_t wc) { return (wc >= 0 && wc < 0x20) || wc == 0x7F; }
int iswblank(wint_t wc) { return wc == ' ' || wc == '\t'; }
int iswspace(wint_t wc) { return wc == ' ' || (wc >= '\t' && wc <= '\r'); }
int iswprint(wint_t wc) { return wc >= 0x20 && wc < 0x7F; }
wint_t towlower(wint_t wc) { return wc >= 'A' && wc <= 'Z' ? wc + ('a' - 'A') : wc; }
wint_t towupper(wint_t wc) { return wc >= 'a' && wc <= 'z' ? wc - ('a' - 'A') : wc; }

size_t wcslen(const wchar_t *s)
{
	size_t n = 0;
	while (s[n]) {
		n++;
	}
	return n;
}

wint_t btowc(int c)
{
	return c == EOF ? WEOF : (wint_t)(unsigned char)c;
}

int wctob(wint_t wc)
{
	return wc >= 0 && wc <= 0xFF ? wc : EOF;
}

wint_t fputwc(wchar_t wc, FILE *f)
{
	if (wc < 0 || wc > 0xFF) {
		errno = EILSEQ;
		return WEOF;
	}
	return fputc(wc, f) == EOF ? WEOF : (wint_t)wc;
}

wint_t putwchar(wchar_t wc)
{
	return fputwc(wc, stdout);
}

wint_t fgetwc(FILE *f)
{
	int c = fgetc(f);
	return c == EOF ? WEOF : (wint_t)c;
}

wint_t getwc(FILE *f)
{
	return fgetwc(f);
}

size_t mbrtowc(wchar_t *wc, const char *s, size_t n, mbstate_t *ps)
{
	(void)ps;
	if (!s) {
		return 0;
	}
	if (!n) {
		return (size_t)-2; /* incomplete: nothing to read */
	}
	if (wc) {
		*wc = (unsigned char)*s;
	}
	return *s ? 1 : 0;
}

size_t wcrtomb(char *s, wchar_t wc, mbstate_t *ps)
{
	(void)ps;
	if (!s) {
		return 1;
	}
	if (wc < 0 || wc > 0xFF) {
		errno = EILSEQ;
		return (size_t)-1;
	}
	*s = (char)wc;
	return 1;
}

int mbsinit(const mbstate_t *ps)
{
	(void)ps;
	return 1; /* no shift states */
}

wchar_t *wcschr(const wchar_t *s, wchar_t wc)
{
	for (;; s++) {
		if (*s == wc) {
			return (wchar_t *)s;
		}
		if (!*s) {
			return NULL;
		}
	}
}

size_t mbstowcs(wchar_t *dst, const char *src, size_t n)
{
	size_t i = 0;
	for (; !dst || i < n; i++) {
		if (dst) {
			dst[i] = (unsigned char)src[i];
		}
		if (!src[i]) {
			break;
		}
	}
	return i;
}

size_t wcstombs(char *dst, const wchar_t *src, size_t n)
{
	size_t i = 0;
	for (; !dst || i < n; i++) {
		if (src[i] < 0 || src[i] > 0xFF) {
			errno = EILSEQ;
			return (size_t)-1;
		}
		if (dst) {
			dst[i] = (char)src[i];
		}
		if (!src[i]) {
			break;
		}
	}
	return i;
}

size_t mbrlen(const char *s, size_t n, mbstate_t *ps)
{
	return mbrtowc(NULL, s, n, ps);
}
