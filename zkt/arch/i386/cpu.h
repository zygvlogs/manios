/* CPU control primitives, so generic kernel code never needs inline
 * assembly (docs/FOUNDING-PROPOSAL.md §1.3). */
#ifndef ZKT_ARCH_I386_CPU_H
#define ZKT_ARCH_I386_CPU_H

#include <stdint.h>

static inline void cpu_enable_interrupts(void)
{
	__asm__ volatile ("sti");
}

static inline void cpu_wait_for_interrupt(void)
{
	__asm__ volatile ("hlt");
}

__attribute__((noreturn)) static inline void cpu_halt_forever(void)
{
	for (;;) {
		__asm__ volatile ("cli; hlt");
	}
}

static inline uint32_t cpu_read_cr2(void)
{
	uint32_t val;
	__asm__ volatile ("mov %%cr2, %0" : "=r"(val));
	return val;
}

/* Flushes the whole TLB. INVLPG would flush a single page but only
 * exists from the 486 on, and ZKT targets the 80386. */
static inline void cpu_flush_tlb(void)
{
	uint32_t cr3;
	__asm__ volatile ("mov %%cr3, %0\n\tmov %0, %%cr3" : "=r"(cr3) : : "memory");
}

#endif
