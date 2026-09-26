/* What CPU this is, for /dev/sysstat (cpuinfo.h). */
#include "cpuinfo.h"
#include "kprintf.h"
#include "kstring.h"

#define EFLAGS_AC (1u << 18) /* settable from the 486 on */
#define EFLAGS_ID (1u << 21) /* settable where CPUID exists */

/* Whether `bit` of EFLAGS can be flipped: CPUs that lack the feature
 * keep it fixed. EFLAGS is restored either way. */
static bool eflags_bit_settable(uint32_t bit)
{
	uint32_t before, after;
	__asm__ volatile ("pushfl\n\t"
	                  "popl %0\n\t"
	                  "movl %0, %1\n\t"
	                  "xorl %2, %1\n\t"
	                  "pushl %1\n\t"
	                  "popfl\n\t"
	                  "pushfl\n\t"
	                  "popl %1\n\t"
	                  "pushl %0\n\t"
	                  "popfl"
	                  : "=&r"(before), "=&r"(after)
	                  : "ri"(bit)
	                  : "cc");
	return ((before ^ after) & bit) != 0;
}

/* Spelt as bytes: the assembler, told the target is a 386, refuses the
 * mnemonic. Only called once eflags_bit_settable(EFLAGS_ID) says so. */
static void cpuid(uint32_t leaf, uint32_t r[4])
{
	__asm__ volatile (".byte 0x0f, 0xa2" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(0));
}

void cpu_describe(char *buf, size_t size)
{
	if (!eflags_bit_settable(EFLAGS_ID)) {
		strlcpy(buf, eflags_bit_settable(EFLAGS_AC) ? "486" : "386", size);
		return;
	}
	uint32_t r[4];
	cpuid(0x80000000u, r);
	if (r[0] >= 0x80000004u) {
		char brand[49];
		for (uint32_t i = 0; i < 3; i++) {
			cpuid(0x80000002u + i, (uint32_t *)(brand + 16 * i));
		}
		brand[48] = '\0';
		const char *b = brand;
		while (*b == ' ') {
			b++; /* Intel right-aligns it */
		}
		if (*b) {
			strlcpy(buf, b, size);
			return;
		}
	}
	char vendor[13];
	cpuid(0, r);
	memcpy(vendor, &r[1], 4);
	memcpy(vendor + 4, &r[3], 4);
	memcpy(vendor + 8, &r[2], 4);
	vendor[12] = '\0';
	cpuid(1, r);
	uint32_t family = (r[0] >> 8) & 0xf, model = (r[0] >> 4) & 0xf;
	if (family == 0xf) {
		family += (r[0] >> 20) & 0xff;
	}
	if (family == 0x6 || family >= 0xf) {
		model |= ((r[0] >> 16) & 0xf) << 4;
	}
	ksnprintf(buf, size, "%s family %lu model %lu", vendor, family, model);
}
