/*
 * Multiboot 1 entry point (see docs/adr/0001-bootloader-strategy.md).
 * GRUB or QEMU's built-in Multiboot loader sets up 32-bit protected
 * mode and a flat GDT before jumping here with EAX=magic, EBX=pointer
 * to the multiboot_info structure. ZKT installs its own GDT/IDT once
 * kernel_main() starts (see gdt.c, idt.c).
 */

.set MB_MAGIC,    0x1BADB002
.set MB_ALIGN,    1 << 0        /* align loaded modules on page boundaries */
.set MB_MEMINFO,  1 << 1        /* provide a memory map */
.set MB_FLAGS,    (MB_ALIGN | MB_MEMINFO)
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

/* "a" (alloc) is required explicitly: recent binutils gives a
 * non-standard section name no flags by default, which would leave
 * this section out of any loadable segment -- and then out of the
 * first 8 KiB of the file where a Multiboot loader looks for it. */
.section .multiboot, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM

.section .bss
.align 16
stack_bottom:
.skip 16384                     /* 16 KiB kernel entry stack */
stack_top:

.section .text
.global _start
.type _start, @function
_start:
	mov $stack_top, %esp
	movl $0, %ebp            /* mark the bottom of the call-stack frame chain */

	/* cdecl pushes args right-to-left: info (2nd arg) first, then magic */
	push %ebx                /* multiboot info pointer -> kernel_main(magic, info) */
	push %eax                /* multiboot magic */
	call kernel_main

	/* kernel_main() does not return; halt defensively if it ever does */
	cli
.hang:
	hlt
	jmp .hang
.size _start, . - _start
