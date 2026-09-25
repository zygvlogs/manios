#include <stdint.h>
#include "kconsole.h"
#include "../arch/i386/gdt.h"
#include "../arch/i386/idt.h"
#include "../arch/i386/pic.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_addr)
{
	(void)multiboot_info_addr; /* memory map parsing arrives with M2 */

	gdt_init();
	idt_init();
	pic_remap_and_mask();

	kconsole_init();

	kconsole_write("ManiOS / ZKT (ZygKernel Technology)\n");
	kconsole_write("Milestone M1: kernel console reached.\n");

	if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
		kconsole_write("warning: not booted via a Multiboot-compliant loader\n");
	}

	kconsole_write("ZKT> ");

	__asm__ volatile ("sti");
	for (;;) {
		__asm__ volatile ("hlt");
	}
}
