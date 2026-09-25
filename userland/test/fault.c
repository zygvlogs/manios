/* Misbehaves on request, so tests can check that the kernel ends the
 * process -- and only the process. Usage: fault MODE. */
#include <assert.h>
#include <manios.h>
#include <stdlib.h>
#include <string.h>

#define KERNEL_ADDR 0xC0100000u /* inside the kernel image */

static volatile unsigned sink;

/* Runs into the guard page below the stack long before the limit. */
static unsigned recurse(unsigned depth)
{
	volatile unsigned char pad[256];
	pad[0] = (unsigned char)depth;
	if (depth > 0x100000) {
		return 0;
	}
	return recurse(depth + 1) + pad[0]; /* not a tail call */
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "";
	if (!strcmp(mode, "null")) {
		sink = *(volatile unsigned *)0;
	} else if (!strcmp(mode, "kread")) {
		sink = *(volatile unsigned *)KERNEL_ADDR;
	} else if (!strcmp(mode, "kwrite")) {
		*(volatile unsigned *)KERNEL_ADDR = 0;
	} else if (!strcmp(mode, "wtext")) {
		*(volatile unsigned char *)(uintptr_t)main = 0x90; /* code is read-only */
	} else if (!strcmp(mode, "jump")) {
		/* Above the stack, the last user page is never mapped: the
		 * instruction fetch itself faults. */
		((void (*)(void))0xBFFFFFF0u)();
	} else if (!strcmp(mode, "stack")) {
		sink = recurse(0);
	} else if (!strcmp(mode, "priv")) {
		__asm__ volatile("cli");
	} else if (!strcmp(mode, "io")) {
		__asm__ volatile("outb %%al, $0x80" : : "a"(0));
	} else if (!strcmp(mode, "int")) {
		/* Exception vectors are ring-0 gates: raising one is itself a
		 * protection fault, so a program cannot fake a page fault. */
		__asm__ volatile("int $14");
	} else if (!strcmp(mode, "divide")) {
		/* In asm: C division by zero is undefined, and GCC is free to
		 * compile 1 / x as x == 1. */
		unsigned quotient, remainder = 0, zero = 0;
		__asm__ volatile("divl %2" : "=a"(quotient), "+d"(remainder) : "rm"(zero), "0"(1u));
		sink = quotient;
	} else if (!strcmp(mode, "shrunk")) {
		char *p = sbrk(4096);
		p[0] = 1;
		sbrk(-4096);
		p[0] = 2; /* the page is gone */
	} else if (!strcmp(mode, "doublefree")) {
		char *p = malloc(32);
		free(p);
		free(p); /* malloc detects it and aborts: status 134 */
	} else if (!strcmp(mode, "assert")) {
		assert(argc == 99);
	} else if (!strcmp(mode, "exit42")) {
		return 42;
	} else if (!strcmp(mode, "spin")) {
		/* CPU-bound for 300 ms: only preemption lets anything else run. */
		uint32_t start = uptime_ms();
		while (uptime_ms() - start < 300) {
		}
		return 0;
	}
	return 1; /* unknown mode, or the fault did not happen */
}
