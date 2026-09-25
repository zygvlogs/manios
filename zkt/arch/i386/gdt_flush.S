/* void gdt_flush(uint32_t gdt_ptr_addr); -- loads GDTR then reloads
 * every segment register so the new flat kernel code/data selectors
 * take effect. Selector 0x08 = kernel code (GDT index 1), 0x10 =
 * kernel data (GDT index 2); see gdt.c. */
.section .text
.global gdt_flush
.type gdt_flush, @function
gdt_flush:
	mov 4(%esp), %eax
	lgdt (%eax)

	mov $0x10, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss

	ljmp $0x08, $.flush
.flush:
	ret
.size gdt_flush, . - gdt_flush
