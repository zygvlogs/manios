#ifndef ZKT_ARCH_I386_IDT_H
#define ZKT_ARCH_I386_IDT_H

/* Builds the IDT and wires vectors 0-31 to the CPU exception stubs in
 * isr.S. IRQ vectors (32-47, after pic_remap()) are wired starting at
 * M3/M5 once there are handlers for them -- see
 * docs/FOUNDING-PROPOSAL.md §7. */
void idt_init(void);

#endif
