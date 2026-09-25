#include "tss.h"
#include "context.h"
#include <stdint.h>
#include "cpu.h"
#include "gdt.h"
#include "kconsole.h"
#include "memlayout.h"
#include "panic.h"

/* Intel SDM vol. 3, figure 7-2. */
struct tss {
	uint16_t link, reserved0;
	uint32_t esp0;
	uint16_t ss0, reserved1;
	uint32_t esp1;
	uint16_t ss1, reserved2;
	uint32_t esp2;
	uint16_t ss2, reserved3;
	uint32_t cr3, eip, eflags;
	uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
	uint16_t es, reserved4, cs, reserved5, ss, reserved6;
	uint16_t ds, reserved7, fs, reserved8, gs, reserved9;
	uint16_t ldt, reserved10;
	uint16_t trap, iomap_base;
} __attribute__((packed));

_Static_assert(sizeof(struct tss) == 104, "i386 TSS is 104 bytes");

#define EFLAGS_RESERVED_1 0x2 /* bit 1 always reads as 1; IF left clear */

static struct tss kernel_tss;
static struct tss double_fault_tss;
static uint8_t double_fault_stack[4096] __attribute__((aligned(16)));

/* Entered by a hardware task switch, so it has no caller to return to.
 * The CPU stored the faulting context in kernel_tss on the way in. */
__attribute__((noreturn)) static void double_fault_task(void)
{
	kconsole_write("\ndouble fault: eip=0x");
	kconsole_write_hex32(kernel_tss.eip);
	kconsole_write(" esp=0x");
	kconsole_write_hex32(kernel_tss.esp);
	kconsole_write(" cr2=0x");
	kconsole_write_hex32(cpu_read_cr2());
	kconsole_write("\n");
	panic("Double fault (a kernel stack overflow, if cr2 is just below esp)");
}

void arch_set_kernel_stack(uintptr_t top)
{
	kernel_tss.esp0 = (uint32_t)top;
}

void tss_init(void)
{
	/* No I/O permission bitmap: the offset points past the segment. */
	kernel_tss.ss0 = GDT_KERNEL_DATA_SEL;
	kernel_tss.iomap_base = sizeof(struct tss);

	double_fault_tss.eip = (uint32_t)double_fault_task;
	double_fault_tss.esp = (uint32_t)(double_fault_stack + sizeof(double_fault_stack));
	double_fault_tss.cs = GDT_KERNEL_CODE_SEL;
	double_fault_tss.ss = GDT_KERNEL_DATA_SEL;
	double_fault_tss.ds = GDT_KERNEL_DATA_SEL;
	double_fault_tss.es = GDT_KERNEL_DATA_SEL;
	double_fault_tss.fs = GDT_KERNEL_DATA_SEL;
	double_fault_tss.gs = GDT_KERNEL_DATA_SEL;
	double_fault_tss.cr3 = V2P(boot_page_directory);
	double_fault_tss.eflags = EFLAGS_RESERVED_1;
	double_fault_tss.iomap_base = sizeof(struct tss);

	gdt_set_tss(GDT_KERNEL_TSS_SEL, (uint32_t)&kernel_tss, sizeof(struct tss) - 1);
	gdt_set_tss(GDT_DOUBLE_FAULT_TSS_SEL, (uint32_t)&double_fault_tss,
	            sizeof(struct tss) - 1);
	__asm__ volatile ("ltr %0" : : "r"((uint16_t)GDT_KERNEL_TSS_SEL));
}
