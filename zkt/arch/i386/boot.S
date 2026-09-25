/*
 * Multiboot 1 entry point (see docs/adr/0001-bootloader-strategy.md).
 * GRUB or QEMU's built-in Multiboot loader enters _start in 32-bit
 * protected mode, paging off, with EAX=magic and EBX=physical address
 * of the multiboot_info structure.
 *
 * ZKT is linked in the higher half (KERNEL_VIRT_BASE) but loaded at
 * 1 MiB physical. _start therefore lives in .multiboot.text, linked at
 * its physical address, and builds a boot page table mapping physical
 * [0, 4 MiB) both at 0 (so the instruction after enabling paging still
 * resolves) and at KERNEL_VIRT_BASE. Once running in the higher half,
 * the identity mapping is dropped so NULL dereferences fault.
 * See docs/milestones/M2-memory-management.md.
 */
#include "memlayout.h"

.set MB_MAGIC,    0x1BADB002
.set MB_ALIGN,    1 << 0        /* align loaded modules on page boundaries */
.set MB_MEMINFO,  1 << 1        /* provide a memory map */
.set MB_FLAGS,    (MB_ALIGN | MB_MEMINFO)
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.set PTE_PRESENT_WRITABLE, 0x003
.set CR0_PG, 0x80000000
.set KERNEL_PD_INDEX, (KERNEL_VIRT_BASE >> 22)

/* "a" (alloc) is required explicitly: recent binutils gives a
 * non-standard section name no flags by default, which would leave
 * this section out of any loadable segment -- and then out of the
 * first 8 KiB of the file where a Multiboot loader looks for it. */
.section .multiboot.data, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM

.section .bss
.balign PAGE_SIZE
.global boot_page_directory
boot_page_directory:
.skip PAGE_SIZE
boot_page_table0:
.skip PAGE_SIZE
.balign 16
stack_bottom:
.skip 16384                     /* 16 KiB kernel entry stack */
stack_top:

.section .multiboot.text, "ax"
.global _start
.type _start, @function
_start:
	/* EAX/EBX carry the Multiboot handoff; use only ECX/ESI/EDI here.
	 * .bss is zeroed by the loader, so every other PDE is not-present. */
	movl $(boot_page_table0 - KERNEL_VIRT_BASE), %edi
	movl $PTE_PRESENT_WRITABLE, %esi     /* physical 0, present, writable */
	movl $1024, %ecx
1:
	movl %esi, (%edi)
	addl $PAGE_SIZE, %esi
	addl $4, %edi
	loop 1b

	movl $(boot_page_table0 - KERNEL_VIRT_BASE + PTE_PRESENT_WRITABLE), %ecx
	movl %ecx, boot_page_directory - KERNEL_VIRT_BASE
	movl %ecx, boot_page_directory - KERNEL_VIRT_BASE + KERNEL_PD_INDEX * 4

	movl $(boot_page_directory - KERNEL_VIRT_BASE), %ecx
	movl %ecx, %cr3

	movl %cr0, %ecx
	orl $CR0_PG, %ecx
	movl %ecx, %cr0

	movl $higher_half_entry, %ecx
	jmp *%ecx
.size _start, . - _start

.section .text
.type higher_half_entry, @function
higher_half_entry:
	movl $0, boot_page_directory        /* drop the identity mapping */
	movl %cr3, %ecx
	movl %ecx, %cr3                     /* flush it from the TLB */

	mov $stack_top, %esp
	xorl %ebp, %ebp          /* mark the bottom of the call-stack frame chain */

	/* cdecl pushes args right-to-left: info (2nd arg) first, then magic */
	push %ebx                /* multiboot info (physical) -> kernel_main(magic, info) */
	push %eax                /* multiboot magic */
	call kernel_main

	/* kernel_main() does not return; halt defensively if it ever does */
	cli
.hang:
	hlt
	jmp .hang
.size higher_half_entry, . - higher_half_entry
