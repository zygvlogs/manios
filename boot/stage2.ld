/* Stage 2 as a flat binary at STAGE2_ADDR (boot/bootarea.h): the
 * real-mode entry first. .bss is not in the file; the entry clears it. */
OUTPUT_FORMAT(binary)
ENTRY(_start)

SECTIONS
{
	. = 0x8000;
	.text : { *(.entry) *(.text) *(.text.*) }
	.rodata : { *(.rodata) *(.rodata.*) }
	.data : { *(.data) *(.data.*) }
	__bss_start = .;
	.bss : { *(.bss) *(.bss.*) *(COMMON) }
	__bss_end = .;
	/DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }

	ASSERT(__bss_end <= 0x20000, "stage 2 would overlap the bounce buffer (bootarea.h)")
}
