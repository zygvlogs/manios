#include "irq.h"
#include "panic.h"
#include "pic.h"

static irq_handler_t handlers[IRQ_LINES];

void irq_install_handler(unsigned irq, irq_handler_t handler)
{
	if (irq >= IRQ_LINES || handlers[irq]) {
		panic("irq_install_handler: no such line, or line already taken");
	}
	handlers[irq] = handler;
	pic_unmask(irq);
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

	if (handlers[irq]) {
		handlers[irq]();
	} else {
		pic_mask(irq); /* lines start masked, so this is a stray; silence it */
	}
}
