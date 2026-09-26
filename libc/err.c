/* err(), warn() and friends: "program: message: error" on standard
 * error, as the BSDs print them. */
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void say(const char *fmt, va_list ap, bool with_error, int error)
{
	fprintf(stderr, "%s: ", getprogname());
	if (fmt) {
		vfprintf(stderr, fmt, ap);
		if (with_error) {
			fputs(": ", stderr);
		}
	}
	if (with_error) {
		fputs(strerror(error), stderr);
	}
	fputc('\n', stderr);
}

void vwarn(const char *fmt, va_list ap)
{
	say(fmt, ap, true, errno);
}

void vwarnx(const char *fmt, va_list ap)
{
	say(fmt, ap, false, 0);
}

void warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarn(fmt, ap);
	va_end(ap);
}

void warnx(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
}

void verr(int status, const char *fmt, va_list ap)
{
	vwarn(fmt, ap);
	exit(status);
}

void verrx(int status, const char *fmt, va_list ap)
{
	vwarnx(fmt, ap);
	exit(status);
}

void err(int status, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verr(status, fmt, ap);
}

void errx(int status, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verrx(status, fmt, ap);
}

void vwarnc(int code, const char *fmt, va_list ap)
{
	say(fmt, ap, true, code);
}

void warnc(int code, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vwarnc(code, fmt, ap);
	va_end(ap);
}

void verrc(int status, int code, const char *fmt, va_list ap)
{
	vwarnc(code, fmt, ap);
	exit(status);
}

void errc(int status, int code, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	verrc(status, code, fmt, ap);
}
