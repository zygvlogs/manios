#include "isr.h"
#include "cpu.h"
#include "kconsole.h"
#include "panic.h"
#include "process.h"

#define EXC_PAGE_FAULT 14
#define PF_PRESENT (1u << 0) /* 0 = page not present, 1 = protection violation */
#define PF_WRITE   (1u << 1)
#define PF_USER    (1u << 2)

/* Exception names/order match the Intel SDM vol. 3 vector list (public
 * architecture reference); vectors without an assigned Intel meaning
 * are marked Reserved. */
static const char *const EXCEPTION_NAMES[32] = {
	"Divide-by-zero",
	"Debug",
	"Non-maskable interrupt",
	"Breakpoint",
	"Overflow",
	"Bound range exceeded",
	"Invalid opcode",
	"Device not available",
	"Double fault",
	"Coprocessor segment overrun",
	"Invalid TSS",
	"Segment not present",
	"Stack-segment fault",
	"General protection fault",
	"Page fault",
	"Reserved",
	"x87 FPU error",
	"Alignment check",
	"Machine check",
	"SIMD floating-point exception",
	"Virtualization exception",
	"Control protection exception",
	"Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
	"Hypervisor injection exception",
	"VMM communication exception",
	"Security exception",
	"Reserved",
};

__attribute__((noreturn)) void exception_handle(registers_t *regs)
{
	const char *name = EXCEPTION_NAMES[regs->int_no];

	/* A fault in user mode is the program's, not the kernel's. */
	if ((regs->cs & 3) == 3) {
		process_kill_current(regs->int_no, name,
		                     regs->int_no == EXC_PAGE_FAULT ? cpu_read_cr2() : 0);
	}

	if (regs->int_no == EXC_PAGE_FAULT) {
		kconsole_write("\npage fault at 0x");
		kconsole_write_hex32(cpu_read_cr2());
		kconsole_write((regs->err_code & PF_PRESENT) ? ": protection violation"
		                                             : ": page not present");
		kconsole_write((regs->err_code & PF_WRITE) ? ", write" : ", read");
		kconsole_write((regs->err_code & PF_USER) ? ", user mode\n" : ", kernel mode\n");
	}
	panic_dump(name, regs);
}
