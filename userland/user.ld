/* Static ZKT user programs: code and read-only data from 0x08048000,
 * then data and bss on their own pages. The kernel's loader
 * (zkt/kernel/elf.c) accepts anything in [0x1000, the stack) -- page 0
 * stays unmapped so NULL dereferences fault -- and requires the ABI note
 * (libc/crt0.S) in a PT_NOTE segment. */
ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS(5);  /* read, execute */
	data PT_LOAD FLAGS(6);  /* read, write */
	note PT_NOTE FLAGS(4);
}

SECTIONS
{
	. = 0x08048000;

	.text : { *(.text .text.*) } :text
	.rodata : { *(.rodata .rodata.*) } :text
	.note.zkt.abi : { KEEP(*(.note.zkt.abi)) } :text :note

	. = ALIGN(4096);
	.data : { *(.data .data.*) } :data
	.bss : { *(COMMON) *(.bss .bss.*) } :data

	/DISCARD/ : { *(.comment) *(.eh_frame) *(.note.GNU-stack) }
}
