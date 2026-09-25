/* void arch_context_switch(uintptr_t *old_sp, uintptr_t new_sp);
 * Only the cdecl callee-saved registers need saving: the caller already
 * treats EAX/ECX/EDX as clobbered, and EFLAGS (including IF) is the
 * caller's to save around the call (sched.c holds interrupts off). */
.section .text
.global arch_context_switch
.type arch_context_switch, @function
arch_context_switch:
	mov 4(%esp), %eax
	mov 8(%esp), %edx

	push %ebp
	push %ebx
	push %esi
	push %edi
	mov %esp, (%eax)

	mov %edx, %esp
	pop %edi
	pop %esi
	pop %ebx
	pop %ebp
	ret
.size arch_context_switch, . - arch_context_switch
