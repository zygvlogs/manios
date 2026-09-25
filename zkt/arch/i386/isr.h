#ifndef ZKT_ARCH_I386_ISR_H
#define ZKT_ARCH_I386_ISR_H

#include <stdint.h>

/* Saved register state at the point an exception or IRQ was taken.
 * Field order matches what isr_common_stub (isr.S) leaves on the
 * stack: ds, then the pusha block in reverse push order, then the
 * int_no/err_code the stub pushed itself, then what the CPU pushed
 * automatically (eip, cs, eflags). No privilege-level change happens
 * before ring 3 exists (M8), so useresp/ss are not part of this yet. */
typedef struct {
	uint32_t ds;
	uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	uint32_t int_no, err_code;
	uint32_t eip, cs, eflags;
} __attribute__((packed)) registers_t;

/* Called from isr.S for every vector: CPU exceptions and PIC IRQs. */
void isr_handler(registers_t *regs);

/* CPU exceptions (vectors 0-31) are all fatal for now. */
__attribute__((noreturn)) void exception_handle(registers_t *regs);

#endif
