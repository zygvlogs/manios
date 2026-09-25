/*
 * The ManiOS CD boot image (M14, ADR-0006): El Torito "no emulation".
 * The BIOS loads these 2048 bytes at 0x7C00 in real mode with DL = the
 * CD drive, which it reads in 2048-byte sectors through INT 13h AH=42h.
 *
 * mkiso patches in where the boot area (MANIOS.BIN) is: its first
 * sector and its length. This reads the header, checks it, loads
 * stage 2 to STAGE2_ADDR and jumps there with
 *   DL = the drive, EBX = the boot area's first sector, CX = 2048.
 */
#include "bootarea.h"

	.code16
	.section .text
	.globl _start
_start:
	jmp start
	.org 8
area_lba:    .long 0                /* patched by tools/mkiso.py */
area_len:    .long 0

start:
	ljmp $0, $1f
1:	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %es
	cli
	movw %ax, %ss
	movw $0x7C00, %sp
	sti
	movb %dl, drive

	movl area_lba, %eax
	movw $HEADER_ADDR, %bx
	call read_sector
	movw $HEADER_ADDR, %si
	movw $magic, %di
	movw $8, %cx
	repe cmpsb
	jne bad_area

	movl HEADER_ADDR + BA_STAGE2_LEN, %ecx
	addl $2047, %ecx
	shrl $11, %ecx
	movl HEADER_ADDR + BA_STAGE2_OFF, %eax
	shrl $11, %eax
	addl area_lba, %eax
	movw $(STAGE2_ADDR >> 4), %dx
2:	movw %dx, %es
	xorw %bx, %bx
	call read_sector
	incl %eax
	addw $(2048 >> 4), %dx
	loop 2b

	xorw %ax, %ax
	movw %ax, %es
	movb drive, %dl
	movl area_lba, %ebx
	movw $2048, %cx
	ljmp $0, $STAGE2_ADDR

/* Reads CD sector EAX to ES:BX; stops the boot on failure. */
read_sector:
	pushal
	movl %eax, dap_lba
	movw %bx, dap_offset
	movw %es, dap_segment
	movw $dap, %si
	movb $0x42, %ah
	movb drive, %dl
	int $0x13
	jc disk_error
	popal
	ret

disk_error:
	movw $msg_disk, %si
	jmp fail
bad_area:
	movw $msg_bad, %si
fail:
	pushw %si
	movw $msg_prefix, %si
	call print
	popw %si
	call print
1:	hlt
	jmp 1b

print:
	lodsb
	testb %al, %al
	jz 2f
	movb $0x0E, %ah
	xorw %bx, %bx
	int $0x10
	movw $0x3F8, %dx
	outb %al, %dx
	jmp print
2:	ret

magic:       .ascii BOOTAREA_MAGIC
msg_prefix:  .asciz "ManiOS: "
msg_disk:    .asciz "CD read error\r\n"
msg_bad:     .asciz "bad boot area on the CD\r\n"

	.balign 4
dap:         .byte 16, 0
             .word 1
dap_offset:  .word 0
dap_segment: .word 0
dap_lba:     .long 0, 0
drive:       .byte 0

	.org 2048
