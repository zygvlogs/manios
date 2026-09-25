#ifndef ZKT_ARCH_I386_ISR_H
#define ZKT_ARCH_I386_ISR_H

#include <stdint.h>

/* Saved register state at the point an exception, IRQ or system call
 * was taken. Field order matches what isr_common_stub (isr.S) leaves on
 * the stack: ds, then the pusha block in reverse push order, then the
 * int_no/err_code the stub pushed itself, then what the CPU pushed
 * automatically (eip, cs, eflags). Coming from ring 3 the CPU also
 * pushes the user ESP and SS above eflags; nothing reads them, so they
 * are left out. */
typedef struct {
	uint32_t ds;
	uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	uint32_t int_no, err_code;
	uint32_t eip, cs, eflags;
} __attribute__((packed)) registers_t;

/* Called from isr.S for every vector: CPU exceptions and PIC IRQs. */
void isr_handler(registers_t *regs);

/* CPU exceptions (vectors 0-31): fatal in the kernel; in user mode they
 * end the process. */
__attribute__((noreturn)) void exception_handle(registers_t *regs);

#endif
