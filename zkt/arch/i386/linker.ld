/*
 * M1 links ZKT as a flat (non-higher-half) kernel at the conventional
 * 1 MiB physical/virtual load address, since paging is not enabled
 * until M2 (see docs/FOUNDING-PROPOSAL.md §2.4) -- higher-half linking
 * is introduced together with paging, not before it.
 */
ENTRY(_start)

SECTIONS
{
	. = 1M;

	.multiboot ALIGN(4K) :
	{
		*(.multiboot)
	}

	.text ALIGN(4K) :
	{
		*(.text)
		*(.text.*)
	}

	.rodata ALIGN(4K) :
	{
		*(.rodata)
		*(.rodata.*)
	}

	.data ALIGN(4K) :
	{
		*(.data)
		*(.data.*)
	}

	.bss ALIGN(4K) :
	{
		*(COMMON)
		*(.bss)
		*(.bss.*)
	}
}
