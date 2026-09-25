#include "arch.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "tss.h"

void arch_early_init(void)
{
	gdt_init();
	tss_init();
	idt_init();
	pic_remap_and_mask();
}
