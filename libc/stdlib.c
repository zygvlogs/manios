#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <manios.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define ATEXIT_MAX 16

static void (*atexit_fns[ATEXIT_MAX])(void);
static int atexit_count;

int atexit(void (*fn)(void))
{
	if (atexit_count == ATEXIT_MAX) {
		return -1;
	}
	atexit_fns[atexit_count++] = fn;
	return 0;
}

/* Defined by stdio.c. A weak reference doesn't pull stdio into a
 * program that never uses it, so small programs stay small. */
void __stdio_exit(void) __attribute__((weak));

__attribute__((noreturn)) void exit(int code)
{
	while (atexit_count > 0) {
		atexit_fns[--atexit_count]();
	}
	if (__stdio_exit) {
		__stdio_exit();
	}
	_exit(code);
}

/* No signals: report and end with the status a shell would show for
 * SIGABRT. Streams are not flushed. */
__attribute__((noreturn)) void abort(void)
{
	static const char msg[] = "abort\n";
	write(2, msg, sizeof(msg) - 1);
	_exit(134);
}

int abs(int v) { return v < 0 ? -v : v; }
long labs(long v) { return v < 0 ? -v : v; }
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

static int digit_value(int c)
{
	if (isdigit(c)) {
		return c - '0';
	}
	if (isalpha(c)) {
		return tolower(c) - 'a' + 10;
	}
	return 36;
}

/* The shared part of strtol/strtoul: magnitude, sign and overflow. */
static unsigned long parse(const char *s, char **end, int base, bool *negative, bool *overflow)
{
	const char *p = s;
	*negative = *overflow = false;
	while (isspace((unsigned char)*p)) {
		p++;
	}
	if (*p == '+' || *p == '-') {
		*negative = *p++ == '-';
	}
	if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')
	    && digit_value((unsigned char)p[2]) < 16) {
		p += 2;
		base = 16;
	} else if (base == 0) {
		base = p[0] == '0' ? 8 : 10;
	}
	if (base < 2 || base > 36) {
		errno = EINVAL;
		if (end) {
			*end = (char *)s;
		}
		return 0;
	}
	const char *digits = p;
	unsigned long v = 0;
	for (int d; (d = digit_value((unsigned char)*p)) < base; p++) {
		if (v > (ULONG_MAX - (unsigned long)d) / (unsigned long)base) {
			*overflow = true;
		}
		v = v * (unsigned long)base + (unsigned long)d;
	}
	if (end) {
		*end = (char *)(p == digits ? s : p);
	}
	return v;
}

long strtol(const char *s, char **end, int base)
{
	bool negative, overflow;
	unsigned long v = parse(s, end, base, &negative, &overflow);
	unsigned long limit = negative ? (unsigned long)LONG_MAX + 1 : LONG_MAX;
	if (overflow || v > limit) {
		errno = ERANGE;
		return negative ? LONG_MIN : LONG_MAX;
	}
	return negative ? (long)(0 - v) : (long)v;
}

unsigned long strtoul(const char *s, char **end, int base)
{
	bool negative, overflow;
	unsigned long v = parse(s, end, base, &negative, &overflow);
	if (overflow) {
		errno = ERANGE;
		return ULONG_MAX;
	}
	return negative ? 0 - v : v;
}

static void swap(char *a, char *b, size_t size)
{
	while (size--) {
		char t = *a;
		*a++ = *b;
		*b++ = t;
	}
}

/* Heapsort: O(n log n) in the worst case, no recursion, no extra memory. */
static void sift_down(char *base, size_t root, size_t count, size_t size,
                      int (*cmp)(const void *, const void *))
{
	for (;;) {
		size_t child = 2 * root + 1;
		if (child >= count) {
			return;
		}
		if (child + 1 < count && cmp(base + child * size, base + (child + 1) * size) < 0) {
			child++;
		}
		if (cmp(base + root * size, base + child * size) >= 0) {
			return;
		}
		swap(base + root * size, base + child * size, size);
		root = child;
	}
}

void qsort(void *base, size_t count, size_t size, int (*cmp)(const void *, const void *))
{
	char *b = base;
	if (count < 2 || !size) {
		return;
	}
	for (size_t i = count / 2; i-- > 0;) {
		sift_down(b, i, count, size, cmp);
	}
	for (size_t end = count - 1; end > 0; end--) {
		swap(b, b + end * size, size);
		sift_down(b, 0, end, size, cmp);
	}
}

void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*cmp)(const void *, const void *))
{
	const char *b = base;
	while (count) {
		size_t mid = count / 2;
		int r = cmp(key, b + mid * size);
		if (r == 0) {
			return (void *)(b + mid * size);
		}
		if (r > 0) {
			b += (mid + 1) * size;
			count -= mid + 1;
		} else {
			count = mid;
		}
	}
	return NULL;
}
