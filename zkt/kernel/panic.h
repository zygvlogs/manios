#ifndef ZKT_KERNEL_PANIC_H
#define ZKT_KERNEL_PANIC_H

#include "isr.h"

/* Prints a message to the kernel console and halts the CPU. */
__attribute__((noreturn)) void panic(const char *msg);

/* As panic(), plus the register state captured by a CPU exception. */
__attribute__((noreturn)) void panic_dump(const char *msg, registers_t *regs);

#endif
