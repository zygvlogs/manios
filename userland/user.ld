/* Static ZKT user programs: code and read-only data from 0x08048000,
 * then data and bss on their own pages. The kernel's loader
 * (zkt/kernel/elf.c) accepts anything in [0x1000, the stack) -- page 0
 * stays unmapped so NULL dereferences fault. */
ENTRY(_start)

SECTIONS
{
	. = 0x08048000;

	.text : { *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }

	. = ALIGN(4096);
	.data : { *(.data .data.*) }
	.bss : { *(COMMON) *(.bss .bss.*) }

	/DISCARD/ : { *(.comment) *(.eh_frame) *(.note.GNU-stack) }
}
