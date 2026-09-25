/*
 * The ManiOS master boot record (M14, ADR-0006): the first sector of a
 * disk ManiOS is installed on (or of the ISO, written to a USB stick).
 * The BIOS loads it at 0x7C00 in real mode with DL = the drive.
 *
 * It finds the ManiOS boot partition (type 0xDA), reads that
 * partition's first sector -- the boot area header -- checks its magic,
 * loads stage 2 to STAGE2_ADDR and jumps there with
 *   DL = the drive, EBX = the partition's first sector, CX = 512.
 *
 * Disk reads use the BIOS's LBA extensions (INT 13h AH=42h), or CHS
 * (AH=02h) on BIOSes from before them, for the 386s and 486s ManiOS
 * wants to run on. Only 446 bytes: the partition table follows.
 */
#include "bootarea.h"

	.code16
	.section .text
	.globl _start
_start:
	ljmp $0, $start                 /* CS:IP = 0000:7Cxx, whatever the BIOS used */
start:
	xorw %ax, %ax
	movw %ax, %ds
	movw %ax, %es
	cli
	movw %ax, %ss
	movw $0x7C00, %sp
	sti
	movb %dl, drive

	/* LBA extensions? Otherwise the geometry, for CHS. (FORCE_CHS: a
	 * test build that pretends there are none, tests/install_test.py.) */
#ifdef FORCE_CHS
	jmp 1f
#endif
	movb $0x41, %ah
	movw $0x55AA, %bx
	int $0x13
	jc 1f
	cmpw $0xAA55, %bx
	jne 1f
	testb $1, %cl                   /* the "packet" interface */
	jz 1f
	movb $1, use_lba
	jmp 2f
1:	movb $0x08, %ah
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
	/* The first partition of type 0xDA. */
	movw $0x7DBE, %si
	movw $4, %cx
3:	cmpb $BOOTAREA_PARTITION_TYPE, 4(%si)
	je 4f
	addw $16, %si
	loop 3b
	movw $msg_nopart, %si
	jmp fail
4:	movl 8(%si), %eax
	movl %eax, base

	/* The header, and its magic. */
	movw $HEADER_ADDR, %bx
	call read_sector
	movw $HEADER_ADDR, %si
	movw $magic, %di
	movw $8, %cx
	repe cmpsb
	jne bad_area

	/* Stage 2: header[BA_STAGE2_OFF] bytes in, header[BA_STAGE2_LEN] long. */
	movl HEADER_ADDR + BA_STAGE2_LEN, %ecx
	addl $511, %ecx
	shrl $9, %ecx
	movl HEADER_ADDR + BA_STAGE2_OFF, %eax
	shrl $9, %eax
	addl base, %eax
	movw $(STAGE2_ADDR >> 4), %dx
5:	movw %dx, %es
	xorw %bx, %bx
	call read_sector
	incl %eax
	addw $(512 >> 4), %dx           /* the next 512 bytes */
	loop 5b

	xorw %ax, %ax
	movw %ax, %es
	movb drive, %dl
	movl base, %ebx
	movw $512, %cx
	ljmp $0, $STAGE2_ADDR

/* Reads sector EAX of the drive to ES:BX; stops the boot on failure. */
read_sector:
	pushal
	cmpb $0, use_lba
	je 1f
	movl %eax, dap_lba
	movw %bx, dap_offset
	movw %es, dap_segment
	movw $dap, %si
	movb $0x42, %ah
	jmp 2f
1:	xorl %edx, %edx                 /* CHS: sector, head, cylinder */
	movzwl sectors, %ecx
	divl %ecx
	incw %dx
	movb %dl, %cl                   /* sector, from 1 */
	xorl %edx, %edx
	movzwl heads, %esi
	divl %esi
	movb %dl, %dh                   /* head */
	movb %al, %ch                   /* cylinder, low 8 bits */
	shlb $6, %ah
	orb %ah, %cl                    /* cylinder bits 8-9 */
	movw $0x0201, %ax               /* read one sector */
2:	movb drive, %dl
	int $0x13
	jc disk_error
	popal
	ret

disk_error:
	movw $msg_disk, %si
	jmp fail
bad_area:
	movw $msg_bad, %si
/* Prints "ManiOS: " and the message at SI, on the screen and COM1, and
 * stops. */
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
msg_nopart:  .asciz "no boot partition\r\n"
msg_disk:    .asciz "disk error\r\n"
msg_bad:     .asciz "bad boot area\r\n"

	.balign 4
dap:         .byte 16, 0
             .word 1
dap_offset:  .word 0
dap_segment: .word 0
dap_lba:     .long 0, 0
base:        .long 0
sectors:     .word 0
heads:       .word 0
drive:       .byte 0
use_lba:     .byte 0

	/* The partition table and the signature are the installer's
	 * (or mkiso's) to fill in. */
	.org 446
