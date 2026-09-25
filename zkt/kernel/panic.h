#ifndef ZKT_KERNEL_PANIC_H
#define ZKT_KERNEL_PANIC_H

#include "../arch/i386/isr.h"

/* Prints a fault report to the kernel console and halts the CPU.
 * Never returns. */
__attribute__((noreturn)) void panic_dump(const char *msg, registers_t *regs);

#endif
