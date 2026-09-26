/* asprintf(): printf into a string of the right size. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int vasprintf(char **s, const char *fmt, va_list ap)
{
	va_list again;
	va_copy(again, ap);
	int n = vsnprintf(NULL, 0, fmt, ap);
	*s = n < 0 ? NULL : malloc((size_t)n + 1);
	if (!*s) {
		va_end(again);
		errno = ENOMEM;
		return -1;
	}
	vsnprintf(*s, (size_t)n + 1, fmt, again);
	va_end(again);
	return n;
}

int asprintf(char **s, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vasprintf(s, fmt, ap);
	va_end(ap);
	return n;
}
