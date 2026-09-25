#include <stdint.h>
#include "cpu.h"
#include "heap.h"
#include "kconsole.h"
#include "mm_selftest.h"
#include "multiboot.h"
#include "panic.h"
#include "pmm.h"
#include "timer.h"
#include "vmm.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"

#define MAX_MEM_REGIONS 64

void kernel_main(uint32_t multiboot_magic, uint32_t multiboot_info_phys)
{
	gdt_init();
	idt_init();
	pic_remap_and_mask();

	kconsole_init();

	kconsole_write("ManiOS / ZKT (ZygKernel Technology)\n");
	kconsole_write("Milestone M1: kernel console reached.\n");

	if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
		panic("not booted by a Multiboot loader; no memory map available");
	}

	struct mem_region regions[MAX_MEM_REGIONS];
	size_t region_count = multiboot_memory_regions(multiboot_info_phys, regions,
	                                               MAX_MEM_REGIONS);
	pmm_init(regions, region_count);
	vmm_init();
	heap_init();

	kconsole_write("memory: ");
	kconsole_write_dec(pmm_usable_frames() * 4);
	kconsole_write(" KiB usable, ");
	kconsole_write_dec(pmm_free_frames());
	kconsole_write(" frames free\n");

	mm_selftest();
	kconsole_write("Milestone M2: memory manager online (self-test passed).\n");

	timer_init();
	cpu_enable_interrupts();
	/* Returns only after several ticks, i.e. only if IRQs are delivered
	 * and acknowledged (the PIC sends nothing more until EOI). */
	timer_sleep_ms(50);
	kconsole_write("Milestone M3: interrupts online (PIT timer at ");
	kconsole_write_dec(TIMER_HZ);
	kconsole_write(" Hz).\n");

	kconsole_write("ZKT> ");

	for (;;) {
		cpu_wait_for_interrupt();
	}
}
