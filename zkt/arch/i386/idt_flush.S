/* void idt_flush(uint32_t idt_ptr_addr); -- loads IDTR. */
.section .text
.global idt_flush
.type idt_flush, @function
idt_flush:
	mov 4(%esp), %eax
	lidt (%eax)
	ret
.size idt_flush, . - idt_flush
