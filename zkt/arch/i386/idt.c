#include "idt.h"
#include <stdint.h>

struct idt_entry {
	uint16_t base_low;
	uint16_t sel;
	uint8_t always0;
	uint8_t flags;
	uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
	uint16_t limit;
	uint32_t base;
} __attribute__((packed));

#define IDT_ENTRIES 256
#define KERNEL_CODE_SEL 0x08 /* see gdt.c */
#define IDT_FLAG_PRESENT_RING0_INT32 0x8E

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idtp;

extern void idt_flush(uint32_t idt_ptr_addr); /* idt_flush.S */

/* Exception stubs, one per CPU vector 0-31 (isr.S). */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

static void idt_set_gate(int idx, uint32_t base, uint16_t sel, uint8_t flags)
{
	idt[idx].base_low = (uint16_t)(base & 0xFFFF);
	idt[idx].base_high = (uint16_t)((base >> 16) & 0xFFFF);
	idt[idx].sel = sel;
	idt[idx].always0 = 0;
	idt[idx].flags = flags;
}

void idt_init(void)
{
	idtp.limit = (uint16_t)(sizeof(idt) - 1);
	idtp.base = (uint32_t)&idt;

	for (int i = 0; i < IDT_ENTRIES; i++) {
		idt_set_gate(i, 0, 0, 0);
	}

	void (*const stubs[32])(void) = {
		isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
		isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
		isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
		isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
	};
	for (int i = 0; i < 32; i++) {
		idt_set_gate(i, (uint32_t)stubs[i], KERNEL_CODE_SEL,
		             IDT_FLAG_PRESENT_RING0_INT32);
	}

	idt_flush((uint32_t)&idtp);
}
