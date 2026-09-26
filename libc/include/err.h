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

#endif
