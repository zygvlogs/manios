#include <string.h>
#include <errno.h>
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

char *strncpy(char *restrict dst, const char *restrict src, size_t n)
{
	size_t i = 0;
	for (; i < n && src[i]; i++) {
		dst[i] = src[i];
	}
	for (; i < n; i++) {
		dst[i] = '\0';
	}
	return dst;
}

char *strncat(char *restrict dst, const char *restrict src, size_t n)
{
	char *d = dst + strlen(dst);
	while (n-- && *src) {
		*d++ = *src++;
	}
	*d = '\0';
	return dst;
}

char *strstr(const char *haystack, const char *needle)
{
	size_t n = strlen(needle);
	for (; *haystack; haystack++) {
		if (!strncmp(haystack, needle, n)) {
			return (char *)haystack;
		}
	}
	return n ? NULL : (char *)haystack;
}

size_t strspn(const char *s, const char *accept)
{
	size_t n = 0;
	while (s[n] && strchr(accept, s[n])) {
		n++;
	}
	return n;
}

size_t strcspn(const char *s, const char *reject)
{
	size_t n = 0;
	while (s[n] && !strchr(reject, s[n])) {
		n++;
	}
	return n;
}

char *strpbrk(const char *s, const char *accept)
{
	s += strcspn(s, accept);
	return *s ? (char *)s : NULL;
}

char *strtok_r(char *restrict s, const char *restrict delim, char **restrict save)
{
	if (!s) {
		s = *save;
	}
	s += strspn(s, delim);
	if (!*s) {
		*save = s;
		return NULL;
	}
	char *end = s + strcspn(s, delim);
	if (*end) {
		*end++ = '\0';
	}
	*save = end;
	return s;
}

char *strtok(char *restrict s, const char *restrict delim)
{
	static char *save;
	return strtok_r(s, delim, &save);
}

void *memchr(const void *s, int c, size_t n)
{
	const unsigned char *p = s;
	for (; n; n--, p++) {
		if (*p == (unsigned char)c) {
			return (void *)p;
		}
	}
	return NULL;
}

char *strerror(int err)
{
	switch (err) {
	case 0:            return "no error";
	case EPERM:        return "operation not permitted";
	case ENOENT:       return "no such file or directory";
	case EIO:          return "input/output error";
	case ENXIO:        return "no such device or address";
	case E2BIG:        return "argument list too long";
	case ENOEXEC:      return "not an executable";
	case EBADF:        return "bad file descriptor";
	case ECHILD:       return "no such child process";
	case ENOMEM:       return "out of memory";
	case EFAULT:       return "bad address";
	case EBUSY:        return "device or resource busy";
	case EEXIST:       return "file exists";
	case ENODEV:       return "no such device";
	case ENOTDIR:      return "not a directory";
	case EISDIR:       return "is a directory";
	case EINVAL:       return "invalid argument";
	case EMFILE:       return "too many open files";
	case EROFS:        return "read-only file system";
	case ERANGE:       return "result too large";
	case ENAMETOOLONG: return "file name too long";
	case ENOSYS:       return "function not implemented";
	case EPIPE:        return "broken pipe";
	case EPROTO:       return "protocol error";
	case EADDRINUSE:   return "address in use";
	case ENETUNREACH:  return "network unreachable";
	case ETIMEDOUT:    return "timed out";
	case EHOSTUNREACH: return "host unreachable";
	default:           return "unknown error";
	}
}
