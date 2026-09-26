/*
 * Stage 2 of the ManiOS boot loader, real-mode part (M14, ADR-0006).
 * Entered at STAGE2_ADDR from mbr.S or cdboot.S with the boot area's
 * header at HEADER_ADDR and
 *   DL = the BIOS drive, EBX = the boot area's first sector,
 *   CX = the sector size (512 on a disk, 2048 on a CD).
 *
 * With the BIOS still at hand it: asks for the memory map; offers to
 * edit the kernel command line; loads the whole boot area to
 * STAGE_ADDR (reading into a buffer below 1 MiB, then copying up with
 * INT 15h AH=87h); enables A20; and enters 32-bit protected mode to run
 * stage2_main() (stage2.c), which loads the kernel. Messages go to the
 * screen and to COM1.
 */
#include "bootarea.h"

#define TIMEOUT_TICKS 55              /* the BIOS clock ticks 18.2 times a second: 3 s */
#define BIOS_TICKS    0x046C
#define COM1          0x3F8

	.code16
	.section .entry, "ax"
	.globl _start
_start:
	ljmp $0, $1f
1:	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %es
	cli
	movw %ax, %ss
	movw $STACK_TOP, %sp
	sti

	/* .bss isn't in the file: clear it -- before keeping anything there. */
	pushw %cx
	pushw %dx
	movw $__bss_start, %di
	movw $__bss_end, %cx
	subw %di, %cx
	xorb %al, %al
	cld
	rep stosb
	popw %dx
	popw %cx
	movb %dl, drive
	movl %ebx, area_lba
	movw %cx, sector_size

	call serial_init
	movw $msg_banner, %si
	call print

	/* LBA extensions: a CD was just read with them (some BIOSes don't
	 * answer AH=41h for one). A disk: ask; else CHS geometry. */
	cmpw $512, sector_size
	je 3f
	movb $1, use_lba
	jmp 2f
3:
#ifdef FORCE_CHS
	jmp 1f
#endif
	movb $0x41, %ah
	movw $0x55AA, %bx
	movb drive, %dl
	int $0x13
	jc 1f
	cmpw $0xAA55, %bx
	jne 1f
	testb $1, %cl
	jz 1f
	movb $1, use_lba
	jmp 2f
1:	cmpw $512, sector_size
	jne disk_error
	movb $0x08, %ah
	movb drive, %dl
	pushw %es
	int $0x13
	popw %es
	jc disk_error
	andw $0x3F, %cx
	movw %cx, sectors
	movzbw %dh, %dx
	incw %dx
	movw %dx, heads
2:
	call memory_map
	call boot_prompt
	call load_area
	call enable_a20

	movw $msg_starting, %si
	call print
	cli
	lgdt gdt_pointer
	movl %cr0, %eax
	orb $1, %al
	movl %eax, %cr0
	ljmp $0x08, $protected

/* --- output: the screen (INT 10h) and COM1 ----------------------------- */

serial_init:
	movw $COM1 + 1, %dx
	xorb %al, %al
	outb %al, %dx                   /* no interrupts */
	movw $COM1 + 3, %dx
	movb $0x80, %al
	outb %al, %dx                   /* divisor latch */
	movw $COM1, %dx
	movb $1, %al
	outb %al, %dx                   /* 115200 baud */
	incw %dx
	xorb %al, %al
	outb %al, %dx
	movw $COM1 + 3, %dx
	movb $0x03, %al
	outb %al, %dx                   /* 8N1 */
	movw $COM1 + 2, %dx
	movb $0xC7, %al
	outb %al, %dx                   /* FIFOs on */
	movw $COM1 + 4, %dx
	movb $0x03, %al
	outb %al, %dx                   /* DTR, RTS */
	ret

/* Prints the character in AL. */
putc:
	pushaw
	movb $0x0E, %ah
	xorw %bx, %bx
	int $0x10
	movb %al, %cl
	movw $COM1 + 5, %dx
	movw $0x8000, %si
1:	inb %dx, %al
	testb $0x20, %al
	jnz 2f
	decw %si
	jnz 1b
2:	movw $COM1, %dx
	movb %cl, %al
	outb %al, %dx
	popaw
	ret

/* Prints the NUL-terminated string at SI. */
print:
	pushw %ax
	pushw %si
1:	lodsb
	testb %al, %al
	jz 2f
	call putc
	jmp 1b
2:	popw %si
	popw %ax
	ret

/* --- input: the keyboard (INT 16h) or COM1 ------------------------------ */

/* ZF clear if a key is waiting. */
key_waiting:
	pushw %ax
	pushw %dx
	movb $0x01, %ah
	int $0x16
	jnz 1f
	movw $COM1 + 5, %dx
	inb %dx, %al
	testb $0x01, %al
1:	popw %dx
	popw %ax
	ret

/* The next key's character in AL (0 for keys without one). */
get_key:
	pushw %dx
1:	movb $0x01, %ah
	int $0x16
	jz 2f
	xorb %ah, %ah
	int $0x16
	jmp 3f
2:	movw $COM1 + 5, %dx
	inb %dx, %al
	testb $0x01, %al
	jz 1b
	movw $COM1, %dx
	inb %dx, %al
3:	popw %dx
	ret

/* --- memory ------------------------------------------------------------ */

/* The BIOS memory map (INT 15h EAX=E820h) into MMAP_ADDR as Multiboot
 * entries (size 20, base 8, length 8, type 4); mem_lower (INT 12h) and
 * mem_upper (E801h, or AH=88h on older BIOSes). */
memory_map:
	int $0x12
	movzwl %ax, %eax
	movl %eax, mem_lower

	movw $MMAP_ADDR, %di
	xorl %ebx, %ebx
1:	movl $0xE820, %eax
	movl $20, %ecx
	movl $0x534D4150, %edx          /* "SMAP" */
	pushw %di
	addw $4, %di
	int $0x15
	popw %di
	jc 3f
	cmpl $0x534D4150, %eax
	jne 3f
	movl $20, (%di)
	addw $24, %di
	addl $24, mmap_length
	testl %ebx, %ebx
	jz 3f
	cmpw $MMAP_ADDR + MMAP_MAX - 24, %di
	jb 1b
3:
	movw $0xE801, %ax
	int $0x15
	jc 4f
	testw %ax, %ax                  /* some BIOSes answer in CX/DX */
	jnz 5f
	movw %cx, %ax
	movw %dx, %bx
5:	movzwl %ax, %eax                /* KiB from 1 MiB to 16 MiB */
	movzwl %bx, %ebx                /* 64 KiB blocks above 16 MiB */
	shll $6, %ebx
	addl %ebx, %eax
	movl %eax, mem_upper
	ret
4:	movb $0x88, %ah
	int $0x15
	movzwl %ax, %eax
	movl %eax, mem_upper
	ret

/* --- the command line --------------------------------------------------- */

/* Copies the header's command line to CMDLINE_ADDR, shows it, and gives
 * the user TIMEOUT_TICKS to press a key and edit it. */
boot_prompt:
	movw $HEADER_ADDR + BA_CMDLINE, %si
	movw $CMDLINE_ADDR, %di
	movw $BA_CMDLINE_MAX - 1, %cx
1:	lodsb
	testb %al, %al
	jz 2f
	stosb
	loop 1b
2:	movb $0, (%di)
	movw %di, cmdline_end

	movw $msg_cmdline, %si
	call print
	movw $CMDLINE_ADDR, %si
	cmpb $0, (%si)
	jne 3f
	movw $msg_none, %si
3:	call print
	movw $msg_prompt, %si
	call print

	movl BIOS_TICKS, %ebx
3:	call key_waiting
	jnz edit
	movl BIOS_TICKS, %eax
	subl %ebx, %eax
	cmpl $TIMEOUT_TICKS, %eax
	jb 3b
	movw $msg_newline, %si
	call print
	ret

/* A line editor: printable characters are added, Backspace takes the
 * last one back, Enter boots with the line, Escape clears it. */
edit:
	call get_key                    /* the key that stopped the clock */
	movw $msg_edit, %si
	call print
	movw $CMDLINE_ADDR, %si
	call print
	movw cmdline_end, %di
1:	call get_key
	cmpb $0x0D, %al
	je 4f
	cmpb $0x0A, %al
	je 4f
	cmpb $0x08, %al
	je 2f
	cmpb $0x7F, %al
	je 2f
	cmpb $0x1B, %al
	je 3f
	cmpb $0x20, %al
	jb 1b
	cmpb $0x7E, %al
	ja 1b
	cmpw $CMDLINE_ADDR + BA_CMDLINE_MAX - 1, %di
	jae 1b
	stosb
	call putc
	jmp 1b
2:	cmpw $CMDLINE_ADDR, %di         /* Backspace */
	je 1b
	decw %di
	movw $msg_rubout, %si
	call print
	jmp 1b
3:	cmpw $CMDLINE_ADDR, %di         /* Escape: rub out the whole line */
	je 1b
	decw %di
	movw $msg_rubout, %si
	call print
	jmp 3b
4:	movb $0, (%di)
	movw $msg_newline, %si
	call print
	ret

/* --- loading ------------------------------------------------------------- */

/* Reads the whole boot area, header[BA_TOTAL] bytes, to header[BA_LOAD_ADDR]:
 * just past the kernel's memory, however big the kernel is (bootarea.h). */
load_area:
	movl HEADER_ADDR + BA_LOAD_ADDR, %eax
	cmpl $AREA_MIN_ADDR, %eax
	jb 9f
	testl $0xFFF, %eax              /* page-aligned: a Multiboot module */
	jnz 9f
	movl %eax, area_addr
	movl HEADER_ADDR + BA_TOTAL, %ecx
	cmpl $AREA_MAX_SIZE, %ecx
	ja 9f
	/* Enough memory? Up to where the area ends, and room to run;
	 * mem_upper counts KiB above 1 MiB. The message rounds up to MiB. */
	addl %ecx, %eax
	addl $RUN_MEMORY, %eax
	movl %eax, %edx
	addl $0xFFFFF, %edx
	shrl $20, %edx
	movl %edx, need_mib
	subl $0x100000 - 1023, %eax
	shrl $10, %eax
	cmpl mem_upper, %eax
	jbe 1f
	movw $msg_memory, %si
	call print_error
	movl need_mib, %eax
	call print_decimal
	movw $msg_mib, %si
	call print
	jmp halt
9:	movw $msg_header, %si
	jmp fail
1:	movw $msg_loading, %si
	call print
	movl HEADER_ADDR + BA_TOTAL, %eax
	movl %eax, area_size
	movzwl sector_size, %ecx
	decl %ecx
	addl %ecx, %eax
	incl %ecx
	xorl %edx, %edx
	divl %ecx
	movl %eax, left                 /* sectors to read */
	movl area_lba, %eax
	movl %eax, next_lba
	movl area_addr, %eax
	movl %eax, dest

1:	cmpl $0, left
	je 3f
	movl $BOUNCE_SIZE, %eax         /* sectors in a buffer-full */
	movzwl sector_size, %ecx
	xorl %edx, %edx
	divl %ecx
	cmpl left, %eax
	jbe 2f
	movl left, %eax
2:	movw %ax, chunk
	call read_chunk
	/* Copy chunk * sector_size bytes up; INT 15h AH=87h counts words. */
	movzwl chunk, %eax
	movzwl sector_size, %ecx
	mull %ecx
	movl %eax, %ecx
	shrl $1, %ecx
	movl dest, %edx
	call copy_up
	movzwl chunk, %eax
	addl %eax, next_lba
	subl %eax, left
	movzwl chunk, %eax
	movzwl sector_size, %ecx
	mull %ecx
	addl %eax, dest
	movb $'.', %al
	call putc
	jmp 1b
3:	movw $msg_newline, %si
	call print
	ret

/* Reads `chunk` sectors from next_lba into the bounce buffer. */
read_chunk:
	pushal
	cmpb $0, use_lba
	je 2f
	movw chunk, %ax
	movw %ax, dap_count
	movl next_lba, %eax
	movl %eax, dap_lba
	movw $0, dap_offset
	movw $BOUNCE_SEG, dap_segment
	movw $dap, %si
	movb $0x42, %ah
	movb drive, %dl
	int $0x13
	jc disk_error
	popal
	ret
2:	movl next_lba, %eax             /* CHS: a sector at a time */
	movw chunk, %di
	movw $BOUNCE_SEG, %bx
	movw %bx, %es
	xorw %bx, %bx
3:	pushl %eax
	xorl %edx, %edx
	movzwl sectors, %ecx
	divl %ecx
	incw %dx
	movb %dl, %cl
	xorl %edx, %edx
	movzwl heads, %esi
	divl %esi
	movb %dl, %dh
	movb %al, %ch
	shlb $6, %ah
	orb %ah, %cl
	movw $0x0201, %ax
	movb drive, %dl
	int $0x13
	jc disk_error
	popl %eax
	incl %eax
	addw $512, %bx
	decw %di
	jnz 3b
	xorw %ax, %ax
	movw %ax, %es
	popal
	ret

/* Copies ECX words from the bounce buffer to physical address EDX
 * (below 16 MiB) with INT 15h AH=87h. */
copy_up:
	pushal
	movw $BOUNCE_SEG << 4 & 0xFFFF, move_gdt + 16 + 2
	movb $BOUNCE_SEG >> 12, move_gdt + 16 + 4
	movw %dx, move_gdt + 24 + 2
	shrl $16, %edx
	movb %dl, move_gdt + 24 + 4
	movb %dh, move_gdt + 24 + 7
	movw $move_gdt, %si
	movb $0x87, %ah
	int $0x15
	jc copy_error
	popal
	ret

/* --- A20 ------------------------------------------------------------------ */

/* ZF clear if A20 is on: 0000:0500 and FFFF:0510 are different bytes. */
a20_on:
	pushw %ds
	pushw %es
	pushw %ax
	xorw %ax, %ax
	movw %ax, %ds
	notw %ax
	movw %ax, %es
	movb 0x0500, %al
	movb %es:0x0510, %ah
	movb $0x00, 0x0500
	movb $0xFF, %es:0x0510
	cmpb $0xFF, 0x0500
	movb %al, 0x0500
	movb %ah, %es:0x0510
	popw %ax
	popw %es
	popw %ds
	ret

enable_a20:
	call a20_on
	jnz 9f
	movw $0x2401, %ax               /* the BIOS */
	int $0x15
	call a20_on
	jnz 9f
	inb $0x92, %al                  /* "fast A20" */
	orb $0x02, %al
	andb $0xFE, %al
	outb %al, $0x92
	call a20_on
	jnz 9f
	call kbc_wait                   /* the keyboard controller */
	movb $0xD1, %al
	outb %al, $0x64
	call kbc_wait
	movb $0xDF, %al
	outb %al, $0x60
	call kbc_wait
	call a20_on
	jnz 9f
	movw $msg_a20, %si
	jmp fail
9:	ret

kbc_wait:
	inb $0x64, %al
	testb $0x02, %al
	jnz kbc_wait
	ret

/* --- failures --------------------------------------------------------- */

disk_error:
	movw $msg_disk, %si
	jmp fail
copy_error:
	movw $msg_copy, %si
/* Prints "ManiOS boot loader: " and the message at SI. */
print_error:
	pushw %si
	movw $msg_error, %si
	call print
	popw %si
	jmp print                       /* which returns to our caller */

fail:
	call print_error
halt:
1:	hlt
	jmp 1b

/* Prints EAX in decimal. */
print_decimal:
	pushal
	movl $10, %ecx
	xorw %bx, %bx                   /* digits on the stack */
1:	xorl %edx, %edx
	divl %ecx
	pushw %dx
	incw %bx
	testl %eax, %eax
	jnz 1b
2:	popw %ax
	addb $'0', %al
	call putc
	decw %bx
	jnz 2b
	popal
	ret

/* --- protected mode --------------------------------------------------- */

	.code32
protected:
	movw $0x10, %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs
	movw %ax, %ss
	movl $STACK_TOP, %esp
	pushl $params
	call stage2_main
1:	hlt
	jmp 1b

	.section .rodata
msg_banner:   .ascii "\r\nManiOS boot loader "
              .ascii MANIOS_VERSION
              .asciz "\r\n"
msg_cmdline:  .asciz "Boot options: "
msg_none:     .asciz "(none)"
msg_prompt:   .asciz "\r\nPress any key within 3 seconds to change them.\r\n"
msg_edit:     .ascii "\r\nType the boot options, then press Enter to start ManiOS.\r\n"
              .asciz "Backspace deletes a character; Escape clears the line.\r\nboot: "
msg_rubout:   .asciz "\b \b"
msg_loading:  .asciz "Loading ManiOS"
msg_starting: .asciz "Starting ManiOS...\r\n"
msg_newline:  .asciz "\r\n"
msg_error:    .asciz "\r\nManiOS boot loader: "
msg_disk:     .asciz "disk error\r\n"
msg_copy:     .asciz "cannot copy above 1 MiB (INT 15h AH=87h)\r\n"
msg_a20:      .asciz "cannot enable the A20 line\r\n"
msg_memory:   .asciz "not enough memory: ManiOS needs "
msg_mib:      .asciz " MiB\r\n"
msg_header:   .asciz "the boot area's header is damaged\r\n"

	.balign 8
gdt:
	.quad 0
	.quad 0x00CF9A000000FFFF        /* 0x08: code, flat 4 GiB, 32-bit */
	.quad 0x00CF92000000FFFF        /* 0x10: data, flat 4 GiB */
gdt_pointer:
	.word gdt_pointer - gdt - 1
	.long gdt

	.section .data
	.balign 4
/* For INT 15h AH=87h: six descriptors; the BIOS fills in all but the
 * source (2) and the destination (3). */
move_gdt:
	.quad 0, 0
	.word 0xFFFF, 0, 0x9300, 0      /* source */
	.word 0xFFFF, 0, 0x9300, 0      /* destination */
	.quad 0, 0

dap:         .byte 16, 0
dap_count:   .word 0
dap_offset:  .word 0
dap_segment: .word 0
dap_lba:     .long 0, 0

/* What stage2_main() gets (struct params in stage2.c). */
	.globl params
params:
mmap_length: .long 0
mem_lower:   .long 0
mem_upper:   .long 0
area_size:   .long 0
area_lba:    .long 0
area_addr:   .long 0

	.section .bss
next_lba:    .long 0
left:        .long 0
dest:        .long 0
need_mib:    .long 0
cmdline_end: .word 0
chunk:       .word 0
sector_size: .word 0
sectors:     .word 0
heads:       .word 0
drive:       .byte 0
use_lba:     .byte 0
