#include <string.h>
#include <stdint.h>

void *memset(void *dst, int c, size_t n)
{
	unsigned char *d = dst;
	while (n--) {
		*d++ = (unsigned char)c;
	}
	return dst;
}

void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;
	while (n--) {
		*d++ = *s++;
	}
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	unsigned char *d = dst;
	const unsigned char *s = src;
	if ((uintptr_t)d < (uintptr_t)s) {
		while (n--) {
			*d++ = *s++;
		}
	} else {
		while (n--) {
			d[n] = s[n];
		}
	}
	return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *x = a, *y = b;
	for (; n; n--, x++, y++) {
		if (*x != *y) {
			return *x - *y;
		}
	}
	return 0;
}

size_t strlen(const char *s)
{
	size_t n = 0;
	while (s[n]) {
		n++;
	}
	return n;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b || !*a) {
			return (unsigned char)*a - (unsigned char)*b;
		}
	}
	return 0;
}

char *strcpy(char *restrict dst, const char *restrict src)
{
	char *d = dst;
	while ((*d++ = *src++)) {
	}
	return dst;
}

char *strcat(char *restrict dst, const char *restrict src)
{
	strcpy(dst + strlen(dst), src);
	return dst;
}

char *strchr(const char *s, int c)
{
	for (;; s++) {
		if (*s == (char)c) {
			return (char *)s;
		}
		if (!*s) {
			return NULL;
		}
	}
}

char *strrchr(const char *s, int c)
{
	const char *found = NULL;
	for (;; s++) {
		if (*s == (char)c) {
			found = s;
		}
		if (!*s) {
			return (char *)found;
		}
	}
}

size_t strlcpy(char *restrict dst, const char *restrict src, size_t size)
{
	size_t len = strlen(src);
	if (size) {
		size_t n = len < size - 1 ? len : size - 1;
		memcpy(dst, src, n);
		dst[n] = '\0';
	}
	return len;
}
