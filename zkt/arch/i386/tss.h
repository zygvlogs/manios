#ifndef ZKT_ARCH_I386_TSS_H
#define ZKT_ARCH_I386_TSS_H

/* Sets up two TSSes. The kernel TSS describes the running task; the CPU
 * saves into it on a task switch, and from M8 it supplies the ring 3 ->
 * ring 0 stack. The double-fault TSS is the target of the task gate on
 * vector 8 (idt.c), so a double fault runs on its own stack even when
 * the kernel stack is what overflowed. Loads TR. Requires gdt_init(). */
void tss_init(void);

#endif
