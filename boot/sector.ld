/* A boot sector (mbr.S, cdboot.S) as a flat binary loaded at 0x7C00. */
OUTPUT_FORMAT(binary)
SECTIONS
{
	. = 0x7C00;
	.text : { *(.text) }
	/DISCARD/ : { *(.comment) *(.note*) *(.data) *(.bss) }
}
