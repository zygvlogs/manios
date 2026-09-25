/*
 * Entry stubs for the 32 CPU exception vectors (0-31) and the 16 PIC
 * IRQ vectors (32-47, see pic.h). Vectors without a CPU-pushed error
 * code get a dummy zero in its place, so isr_common_stub can treat every
 * vector uniformly. Standard i386 pattern (Intel SDM vol. 3, ch. 6) --
 * original code implementing a public spec.
 *
 * Exception and IRQ entries are interrupt gates, which clear IF on
 * entry; the system call entry is a trap gate, which leaves it on. iret
 * restores the interrupted context's IF. No stub touches IF itself.
 */

.macro ISR_NOERRCODE num
.global isr\num
.type isr\num, @function
isr\num:
	push $0
	push $\num
	jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
.global isr\num
.type isr\num, @function
isr\num:
	push $\num
	jmp isr_common_stub
.endm

/* Must match IRQ_BASE_VECTOR in pic.h. */
.macro IRQ num, vector
.global irq\num
.type irq\num, @function
irq\num:
	push $0
	push $\vector
	jmp isr_common_stub
.endm

.section .text

ISR_NOERRCODE 0   /* Divide-by-zero */
ISR_NOERRCODE 1   /* Debug */
ISR_NOERRCODE 2   /* Non-maskable interrupt */
ISR_NOERRCODE 3   /* Breakpoint */
ISR_NOERRCODE 4   /* Overflow */
ISR_NOERRCODE 5   /* Bound range exceeded */
ISR_NOERRCODE 6   /* Invalid opcode */
ISR_NOERRCODE 7   /* Device not available */
ISR_ERRCODE   8   /* Double fault */
ISR_NOERRCODE 9   /* Coprocessor segment overrun (legacy) */
ISR_ERRCODE   10  /* Invalid TSS */
ISR_ERRCODE   11  /* Segment not present */
ISR_ERRCODE   12  /* Stack-segment fault */
ISR_ERRCODE   13  /* General protection fault */
ISR_ERRCODE   14  /* Page fault */
ISR_NOERRCODE 15  /* Reserved */
ISR_NOERRCODE 16  /* x87 FPU error */
ISR_ERRCODE   17  /* Alignment check */
ISR_NOERRCODE 18  /* Machine check */
ISR_NOERRCODE 19  /* SIMD floating-point exception */
ISR_NOERRCODE 20  /* Virtualization exception */
ISR_ERRCODE   21  /* Control protection exception */
ISR_NOERRCODE 22  /* Reserved */
ISR_NOERRCODE 23  /* Reserved */
ISR_NOERRCODE 24  /* Reserved */
ISR_NOERRCODE 25  /* Reserved */
ISR_NOERRCODE 26  /* Reserved */
ISR_NOERRCODE 27  /* Reserved */
ISR_NOERRCODE 28  /* Hypervisor injection exception */
ISR_ERRCODE   29  /* VMM communication exception */
ISR_ERRCODE   30  /* Security exception */
ISR_NOERRCODE 31  /* Reserved */
ISR_NOERRCODE 128 /* System call (int $0x80) */

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

.extern isr_handler
.type isr_common_stub, @function
isr_common_stub:
	pusha

	mov %ds, %ax
	push %eax

	mov $0x10, %ax    /* kernel data selector, see gdt.c */
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs

	push %esp         /* registers_t * argument for isr_handler(regs) */
	call isr_handler
	add $4, %esp

	pop %eax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs

	popa
	add $8, %esp      /* discard int_no and err_code pushed by the stub */
	iret
.size isr_common_stub, . - isr_common_stub
