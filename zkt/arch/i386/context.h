/* Kernel thread context switching. Generic code includes this by name. */
#ifndef ZKT_ARCH_I386_CONTEXT_H
#define ZKT_ARCH_I386_CONTEXT_H

#include <stdint.h>

/* Saves the current thread's callee-saved registers on its stack,
 * stores its stack pointer in *old_sp, then resumes the thread whose
 * stack pointer is new_sp. Returns when something later switches back.
 * Call with interrupts disabled. */
void arch_context_switch(uintptr_t *old_sp, uintptr_t new_sp);

/* Lays out a fresh stack so that the first arch_context_switch() to the
 * returned stack pointer starts `entry`, which must never return. */
uintptr_t arch_context_init(uintptr_t stack_top, void (*entry)(void));

/* The kernel stack the CPU switches to when a user-mode thread traps
 * into the kernel: the running thread's own (TSS esp0 on i386). */
void arch_set_kernel_stack(uintptr_t top);

/* Drops to user mode at `entry` with stack pointer `user_sp`, interrupts
 * enabled and every general register cleared. Never returns: the thread
 * comes back into the kernel only through interrupts and system calls. */
__attribute__((noreturn)) void arch_enter_user(uintptr_t entry, uintptr_t user_sp);

#endif
