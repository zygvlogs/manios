/* The printf engine, shared by the stdio and string variants. Only
 * 32-bit arithmetic: 64-bit values (%ll) are divided 16 bits at a time,
 * so no libgcc helper is needed. */
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "format.h"

#define LEFT  1u
#define ZERO  2u
#define PLUS  4u
#define SPACE 8u
#define ALT   16u

struct out {
	format_emit emit;
	void *ctx;
	int count;
};

static void put(struct out *o, const char *s, size_t n)
{
	if (n) {
		o->emit(o->ctx, s, n);
		o->count += (int)n;
	}
}

static void pad(struct out *o, char c, int n)
{
	char block[16];
	memset(block, c, sizeof(block));
	while (n > 0) {
		int k = n < (int)sizeof(block) ? n : (int)sizeof(block);
		put(o, block, (size_t)k);
		n -= k;
	}
}

/* Divides *v by `base` (at most 36) in place and returns the remainder. */
static unsigned divmod64(uint64_t *v, unsigned base)
{
	uint32_t hi = (uint32_t)(*v >> 32), lo = (uint32_t)*v;
	if (!hi) {
		*v = lo / base;
		return lo % base;
	}
	uint32_t q_hi = hi / base, rem = hi % base;
	uint32_t cur = rem << 16 | lo >> 16;
	uint32_t q_mid = cur / base;
	rem = cur % base;
	cur = rem << 16 | (lo & 0xFFFF);
	uint32_t q_lo = cur / base;
	*v = (uint64_t)q_hi << 32 | q_mid << 16 | q_lo;
	return cur % base;
}

/* One integer conversion: digits, then precision zeros, prefix/sign and
 * width padding in the usual order. */
static void number(struct out *o, uint64_t v, bool negative, unsigned base, bool upper,
                   unsigned flags, int width, int precision)
{
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	char buf[24];
	int n = 0;
	while (v) {
		buf[sizeof(buf) - 1 - n++] = digits[divmod64(&v, base)];
	}
	/* %.0d of zero prints nothing; otherwise zero is one digit. */
	int zeros = precision > n ? precision - n : 0;
	if (n == 0 && precision != 0) {
		zeros = zeros ? zeros : 1;
	}
	const char *prefix = "";
	if (negative) {
		prefix = "-";
	} else if (flags & PLUS) {
		prefix = "+";
	} else if (flags & SPACE) {
		prefix = " ";
	} else if ((flags & ALT) && base == 16 && n) {
		prefix = upper ? "0X" : "0x";
	} else if ((flags & ALT) && base == 8 && !zeros) {
		zeros = 1;
	}
	int len = (int)strlen(prefix) + zeros + n;
	int fill = width > len ? width - len : 0;
	if ((flags & ZERO) && !(flags & LEFT) && precision < 0) {
		zeros += fill;
		fill = 0;
	}
	if (!(flags & LEFT)) {
		pad(o, ' ', fill);
	}
	put(o, prefix, strlen(prefix));
	pad(o, '0', zeros);
	put(o, buf + sizeof(buf) - n, (size_t)n);
	if (flags & LEFT) {
		pad(o, ' ', fill);
	}
}

static void text(struct out *o, const char *s, size_t n, unsigned flags, int width)
{
	int fill = width > (int)n ? width - (int)n : 0;
	if (!(flags & LEFT)) {
		pad(o, ' ', fill);
	}
	put(o, s, n);
	if (flags & LEFT) {
		pad(o, ' ', fill);
	}
}

int format(format_emit emit, void *ctx, const char *fmt, va_list ap)
{
	struct out o = { emit, ctx, 0 };
	while (*fmt) {
		if (*fmt != '%') {
			const char *start = fmt;
			while (*fmt && *fmt != '%') {
				fmt++;
			}
			put(&o, start, (size_t)(fmt - start));
			continue;
		}
		const char *spec = fmt++;

		unsigned flags = 0;
		for (;; fmt++) {
			if (*fmt == '-') flags |= LEFT;
			else if (*fmt == '0') flags |= ZERO;
			else if (*fmt == '+') flags |= PLUS;
			else if (*fmt == ' ') flags |= SPACE;
			else if (*fmt == '#') flags |= ALT;
			else break;
		}
		int width = 0;
		if (*fmt == '*') {
			width = va_arg(ap, int);
			if (width < 0) {
				flags |= LEFT;
				width = -width;
			}
			fmt++;
		} else {
			while (*fmt >= '0' && *fmt <= '9') {
				width = width * 10 + (*fmt++ - '0');
			}
		}
		int precision = -1;
		if (*fmt == '.') {
			fmt++;
			precision = 0;
			if (*fmt == '*') {
				precision = va_arg(ap, int);
				fmt++;
			} else {
				while (*fmt >= '0' && *fmt <= '9') {
					precision = precision * 10 + (*fmt++ - '0');
				}
			}
		}
		bool wide = false; /* 64-bit: ll, j */
		while (*fmt == 'h' || *fmt == 'l' || *fmt == 'z' || *fmt == 't' || *fmt == 'j') {
			if (*fmt == 'j' || (fmt[0] == 'l' && fmt[1] == 'l')) {
				wide = true;
			}
			fmt += (fmt[0] == 'l' && fmt[1] == 'l') || (fmt[0] == 'h' && fmt[1] == 'h') ? 2 : 1;
		}
		/* hh and h need no narrowing: printf's callers promoted to int,
		 * and the value is printed as given. */

		char c = *fmt ? *fmt++ : '\0';
		switch (c) {
		case 'd':
		case 'i': {
			int64_t v = wide ? va_arg(ap, int64_t) : va_arg(ap, int);
			uint64_t mag = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
			number(&o, mag, v < 0, 10, false, flags, width, precision);
			break;
		}
		case 'u':
		case 'x':
		case 'X':
		case 'o': {
			uint64_t v = wide ? va_arg(ap, uint64_t) : va_arg(ap, unsigned);
			unsigned base = c == 'u' ? 10 : c == 'o' ? 8 : 16;
			number(&o, v, false, base, c == 'X', flags & ~(PLUS | SPACE), width, precision);
			break;
		}
		case 'p':
			number(&o, (uintptr_t)va_arg(ap, void *), false, 16, false, ALT, width, -1);
			break;
		case 'c': {
			char ch = (char)va_arg(ap, int);
			text(&o, &ch, 1, flags, width);
			break;
		}
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s) {
				s = "(null)";
			}
			size_t n = 0;
			while ((precision < 0 || n < (size_t)precision) && s[n]) {
				n++;
			}
			text(&o, s, n, flags, width);
			break;
		}
		case '%':
			put(&o, "%", 1);
			break;
		default:
			/* Unknown or truncated: print the specification as it was. */
			put(&o, spec, (size_t)(fmt - spec));
			break;
		}
	}
	return o.count;
}
