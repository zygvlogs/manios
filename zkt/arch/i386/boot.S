/*
 * Multiboot 1 entry point (see docs/adr/0001-bootloader-strategy.md).
 * GRUB or QEMU's built-in Multiboot loader enters _start in 32-bit
 * protected mode, paging off, with EAX=magic and EBX=physical address
 * of the multiboot_info structure.
 *
 * ZKT is linked in the higher half (KERNEL_VIRT_BASE) but loaded at
 * 1 MiB physical. _start therefore lives in .multiboot.text, linked at
 * its physical address, and builds boot page tables mapping physical
 * [0, BOOT_MAPPED_PHYS_LIMIT) -- 16 MiB, room for a big kernel image --
 * both at 0 (so the instruction after enabling paging still resolves)
 * and at KERNEL_VIRT_BASE. Once running in the higher half, the
 * identity mapping is dropped so NULL dereferences fault.
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
boot_page_tables:
.skip PAGE_SIZE * BOOT_PAGE_TABLES
/* Unmapped by vmm_init(), so overflowing the stack below faults instead
 * of silently overwriting the boot page tables. */
.global kernel_stack_guard
kernel_stack_guard:
.skip PAGE_SIZE
stack_bottom:
.skip 16384                     /* 16 KiB kernel entry stack */
stack_top:

.section .multiboot.text, "ax"
.global _start
.type _start, @function
_start:
	/* EAX/EBX carry the Multiboot handoff; use only ECX/ESI/EDI here.
	 * .bss is zeroed by the loader, so every other PDE is not-present. */
	movl $(boot_page_tables - KERNEL_VIRT_BASE), %edi
	movl $PTE_PRESENT_WRITABLE, %esi     /* physical 0, present, writable */
	movl $(1024 * BOOT_PAGE_TABLES), %ecx
1:
	movl %esi, (%edi)
	addl $PAGE_SIZE, %esi
	addl $4, %edi
	loop 1b

	/* Directory entries 0.. and KERNEL_PD_INDEX.. point at the tables. */
	movl $(boot_page_tables - KERNEL_VIRT_BASE + PTE_PRESENT_WRITABLE), %esi
	movl $(boot_page_directory - KERNEL_VIRT_BASE), %edi
	movl $BOOT_PAGE_TABLES, %ecx
2:
	movl %esi, (%edi)
	movl %esi, KERNEL_PD_INDEX * 4(%edi)
	addl $PAGE_SIZE, %esi
	addl $4, %edi
	loop 2b

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
	movl $boot_page_directory, %edi     /* drop the identity mapping */
	movl $BOOT_PAGE_TABLES, %ecx
3:
	movl $0, (%edi)
	addl $4, %edi
	loop 3b
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
