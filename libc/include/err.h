/* Messages to standard error, as "program: message[: error]" (BSD). */
#ifndef MANIOS_ERR_H
#define MANIOS_ERR_H

#include <stdarg.h>
#include <sys/cdefs.h>

__dead void err(int status, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
__dead void errx(int status, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
__dead void verr(int status, const char *fmt, va_list ap);
__dead void verrx(int status, const char *fmt, va_list ap);
void warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void warnx(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void vwarn(const char *fmt, va_list ap);
void vwarnx(const char *fmt, va_list ap);
/* With an error number given, instead of errno's. */
__dead void errc(int status, int code, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
__dead void verrc(int status, int code, const char *fmt, va_list ap);
void warnc(int code, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void vwarnc(int code, const char *fmt, va_list ap);

#endif
