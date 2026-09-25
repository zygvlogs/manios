#include "kprintf.h"
#include <stdbool.h>
#include <stdint.h>
#include "kconsole.h"

struct out {
	char *buf;
	size_t size;
	size_t len;
};

static void put(struct out *o, char c)
{
	if (o->len + 1 < o->size) {
		o->buf[o->len] = c;
	}
	o->len++;
}

static void put_padded(struct out *o, const char *s, size_t n, unsigned width,
                       bool left, char pad)
{
	size_t fill = width > n ? width - n : 0;
	if (!left) {
		while (fill--) {
			put(o, pad);
		}
	}
	for (size_t i = 0; i < n; i++) {
		put(o, s[i]);
	}
	if (left) {
		while (fill--) {
			put(o, ' ');
		}
	}
}

static size_t format_unsigned(char *tmp, uint32_t v, unsigned base)
{
	static const char digits[] = "0123456789abcdef";
	size_t n = 0;
	do {
		tmp[n++] = digits[v % base];
		v /= base;
	} while (v);
	for (size_t i = 0; i < n / 2; i++) {
		char c = tmp[i];
		tmp[i] = tmp[n - 1 - i];
		tmp[n - 1 - i] = c;
	}
	return n;
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	struct out o = { buf, size, 0 };

	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			put(&o, *fmt);
			continue;
		}
		fmt++;

		bool left = false;
		char pad = ' ';
		for (;; fmt++) {
			if (*fmt == '-') {
				left = true;
			} else if (*fmt == '0') {
				pad = '0';
			} else {
				break;
			}
		}
		unsigned width = 0;
		while (*fmt >= '0' && *fmt <= '9') {
			width = width * 10 + (unsigned)(*fmt++ - '0');
		}
		while (*fmt == 'l' || *fmt == 'z') {
			fmt++;
		}

		char tmp[12];
		switch (*fmt) {
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s) {
				s = "(null)";
			}
			size_t n = 0;
			while (s[n]) {
				n++;
			}
			put_padded(&o, s, n, width, left, ' ');
			break;
		}
		case 'c':
			tmp[0] = (char)va_arg(ap, int);
			put_padded(&o, tmp, 1, width, left, ' ');
			break;
		case 'd': {
			int32_t v = va_arg(ap, int32_t);
			uint32_t mag = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
			size_t n = 0;
			if (v < 0) {
				tmp[n++] = '-';
			}
			n += format_unsigned(tmp + n, mag, 10);
			put_padded(&o, tmp, n, width, left, pad);
			break;
		}
		case 'u':
			put_padded(&o, tmp, format_unsigned(tmp, va_arg(ap, uint32_t), 10),
			           width, left, pad);
			break;
		case 'x':
			put_padded(&o, tmp, format_unsigned(tmp, va_arg(ap, uint32_t), 16),
			           width, left, pad);
			break;
		case '%':
			put(&o, '%');
			break;
		case '\0':
			fmt--; /* a trailing lone '%': stop at the terminator */
			break;
		default:
			put(&o, '%');
			put(&o, *fmt);
			break;
		}
	}

	if (size) {
		buf[o.len < size ? o.len : size - 1] = '\0';
	}
	return (int)o.len;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = kvsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}

void kprintf(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	kvsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	kconsole_write(buf);
}
