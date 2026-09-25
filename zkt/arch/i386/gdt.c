#include "gdt.h"
#include <stdint.h>

struct gdt_entry {
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_middle;
	uint8_t access;
	uint8_t granularity;
	uint8_t base_high;
} __attribute__((packed));

struct gdt_ptr {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed));

#define GDT_ENTRIES 5 /* null, kernel code, kernel data, kernel TSS, double-fault TSS */

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr gdtp;

/* Defined in gdt_flush.S: loads GDTR and reloads CS/DS/ES/FS/GS/SS. */
extern void gdt_flush(uint32_t gdt_ptr_addr);

static void gdt_set_gate(int idx, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t gran)
{
	gdt[idx].base_low = (uint16_t)(base & 0xFFFF);
	gdt[idx].base_middle = (uint8_t)((base >> 16) & 0xFF);
	gdt[idx].base_high = (uint8_t)((base >> 24) & 0xFF);

	gdt[idx].limit_low = (uint16_t)(limit & 0xFFFF);
	gdt[idx].granularity = (uint8_t)((limit >> 16) & 0x0F);
	gdt[idx].granularity |= gran & 0xF0;

	gdt[idx].access = access;
}

void gdt_init(void)
{
	gdtp.limit = (uint16_t)(sizeof(gdt) - 1);
	gdtp.base = (uint32_t)&gdt;

	gdt_set_gate(0, 0, 0, 0, 0); /* null descriptor */

	/* Flat 4 GiB kernel code: base 0, limit 0xFFFFF with 4K granularity,
	 * present, ring 0, code segment, readable. Access 0x9A, gran 0xCF. */
	gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);

	/* Flat 4 GiB kernel data: present, ring 0, data segment, writable. */
	gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);

	gdt_flush((uint32_t)&gdtp);
}

void gdt_set_tss(uint16_t selector, uint32_t base, uint32_t limit)
{
	/* Access 0x89: present, ring 0, 32-bit available TSS; byte granularity. */
	gdt_set_gate(selector / 8, base, limit, 0x89, 0x00);
}
