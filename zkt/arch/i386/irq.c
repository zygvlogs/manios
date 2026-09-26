#include "irq.h"
#include "panic.h"
#include "pic.h"

/* PCI cards share interrupt lines (INTA#-INTD# are routed onto a few
 * ISA IRQs, and level-triggered), so a line has a short list of
 * handlers; each checks its own device and returns if it wasn't it. */
#define HANDLERS_PER_LINE 4

static irq_handler_t handlers[IRQ_LINES][HANDLERS_PER_LINE];

void irq_install_handler(unsigned irq, irq_handler_t handler)
{
	if (irq >= IRQ_LINES) {
		panic("irq_install_handler: no such line");
	}
	for (int i = 0; i < HANDLERS_PER_LINE; i++) {
		if (!handlers[irq][i]) {
			handlers[irq][i] = handler;
			pic_unmask(irq);
			return;
		}
	}
	panic("irq_install_handler: too many handlers on one line");
}

void irq_dispatch(unsigned irq)
{
	if ((irq == 7 || irq == 15) && pic_is_spurious(irq)) {
		/* Not acknowledged -- except that for a spurious IRQ15 the master
		 * did see a real request on the cascade line. */
		if (irq == 15) {
			pic_send_eoi(2);
		}
		return;
	}

	/* Acknowledge before running the handler: from M4 on, the timer
	 * handler may switch threads and not return here for a while. IF
	 * stays clear until iret, so this cannot cause nesting. */
	pic_send_eoi(irq);

	if (!handlers[irq][0]) {
		pic_mask(irq); /* lines start masked, so this is a stray; silence it */
		return;
	}
	for (int i = 0; i < HANDLERS_PER_LINE && handlers[irq][i]; i++) {
		handlers[irq][i]();
	}
}
