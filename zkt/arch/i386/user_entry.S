/* void arch_enter_user(uintptr_t entry, uintptr_t user_sp);
 * Builds the frame an inter-privilege iret expects (SS, ESP, EFLAGS,
 * CS, EIP) and irets into ring 3. Registers are cleared so no kernel
 * value leaks into the new program. Selectors: see gdt.h. */
.set USER_CODE_SEL, 0x2B
.set USER_DATA_SEL, 0x33
.set EFLAGS_IF, 0x200
.set EFLAGS_RESERVED_1, 0x2

.section .text
.global arch_enter_user
.type arch_enter_user, @function
arch_enter_user:
	mov 4(%esp), %ecx        /* entry */
	mov 8(%esp), %edx        /* user stack pointer */

	mov $USER_DATA_SEL, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs

	push $USER_DATA_SEL      /* SS */
	push %edx                /* ESP */
	push $(EFLAGS_IF | EFLAGS_RESERVED_1)
	push $USER_CODE_SEL      /* CS */
	push %ecx                /* EIP */

	xor %eax, %eax
	xor %ebx, %ebx
	xor %ecx, %ecx
	xor %edx, %edx
	xor %esi, %esi
	xor %edi, %edi
	xor %ebp, %ebp
	iret
.size arch_enter_user, . - arch_enter_user
