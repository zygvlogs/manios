/*
 * ZKT is loaded at 1 MiB physical and runs at KERNEL_VIRT_BASE + 1 MiB.
 * The Multiboot header and _start (.multiboot.*) are linked at their
 * physical addresses because they run before paging is enabled; every
 * other section has VMA = LMA + KERNEL_VIRT_BASE. See boot.S and
 * docs/milestones/M2-memory-management.md.
 */
ENTRY(_start)

KERNEL_VIRT_BASE = 0xC0000000; /* must match memlayout.h */
BOOT_MAPPED_PHYS_LIMIT = 0x00400000; /* must match memlayout.h */

SECTIONS
{
	. = 1M;

	.multiboot.data :
	{
		*(.multiboot.data)
	}

	.multiboot.text :
	{
		*(.multiboot.text)
	}

	. += KERNEL_VIRT_BASE;

	.text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VIRT_BASE)
	{
		*(.text)
		*(.text.*)
	}

	.rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VIRT_BASE)
	{
		*(.rodata)
		*(.rodata.*)
	}

	.data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VIRT_BASE)
	{
		*(.data)
		*(.data.*)
	}

	.bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VIRT_BASE)
	{
		*(COMMON)
		*(.bss)
		*(.bss.*)
	}

	. = ALIGN(4K);
	_kernel_end = .;

	ASSERT(_kernel_end - KERNEL_VIRT_BASE <= BOOT_MAPPED_PHYS_LIMIT,
	       "kernel image exceeds the boot page table's 4 MiB mapping (boot.S)")
}
