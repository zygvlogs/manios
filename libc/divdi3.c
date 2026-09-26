/* 64-bit division, which GCC calls for on the 386 (a / b with 64-bit
 * operands). libgcc would provide it, but ManiOS's libgcc is built for
 * the i686 (no libgcc in ManiOS programs: M8, M9), so here it is for the
 * 386: one or two DIV instructions when the divisor fits in 32 bits,
 * shifts and subtractions when it doesn't. */
#include <stdint.h>

uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem);
uint64_t __udivdi3(uint64_t n, uint64_t d);
uint64_t __umoddi3(uint64_t n, uint64_t d);
int64_t __divdi3(int64_t n, int64_t d);
int64_t __moddi3(int64_t n, int64_t d);

uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem)
{
	uint64_t q;
	if (d >> 32 == 0) {
		/* (hi:lo) / d in two steps, each a 64-by-32-bit DIV whose
		 * quotient fits: the first remainder is below d. A zero d
		 * faults in the first, as a division by zero should. */
		uint32_t dl = (uint32_t)d, hi = (uint32_t)(n >> 32), lo = (uint32_t)n;
		uint32_t qh, ql, r;
		__asm__("divl %4" : "=a"(qh), "=d"(r) : "a"(hi), "d"(0u), "rm"(dl));
		__asm__("divl %4" : "=a"(ql), "=d"(r) : "a"(lo), "d"(r), "rm"(dl));
		q = (uint64_t)qh << 32 | ql;
		if (rem) {
			*rem = r;
		}
		return q;
	}
	/* d >= 2^32: the quotient fits in 32 bits; long division, one bit
	 * at a time from the highest the quotient can have. */
	int shift = 0;
	while (!(d >> 63) && (d << 1) <= n) {
		d <<= 1;
		shift++;
	}
	q = 0;
	for (;;) {
		q <<= 1;
		if (n >= d) {
			n -= d;
			q |= 1;
		}
		if (!shift--) {
			break;
		}
		d >>= 1;
	}
	if (rem) {
		*rem = n;
	}
	return q;
}

uint64_t __udivdi3(uint64_t n, uint64_t d)
{
	return __udivmoddi4(n, d, 0);
}

uint64_t __umoddi3(uint64_t n, uint64_t d)
{
	uint64_t r;
	__udivmoddi4(n, d, &r);
	return r;
}

/* Signed: truncating toward zero; the remainder takes the dividend's
 * sign. (INT64_MIN / -1 overflows, as in hardware; here it wraps.) */
int64_t __divdi3(int64_t n, int64_t d)
{
	uint64_t un = n < 0 ? 0 - (uint64_t)n : (uint64_t)n;
	uint64_t ud = d < 0 ? 0 - (uint64_t)d : (uint64_t)d;
	uint64_t q = __udivmoddi4(un, ud, 0);
	return (n < 0) != (d < 0) ? (int64_t)(0 - q) : (int64_t)q;
}

int64_t __moddi3(int64_t n, int64_t d)
{
	uint64_t un = n < 0 ? 0 - (uint64_t)n : (uint64_t)n;
	uint64_t ud = d < 0 ? 0 - (uint64_t)d : (uint64_t)d;
	uint64_t r;
	__udivmoddi4(un, ud, &r);
	return n < 0 ? (int64_t)(0 - r) : (int64_t)r;
}
