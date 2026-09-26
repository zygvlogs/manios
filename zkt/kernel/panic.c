#include "panic.h"
#include "kconsole.h"
#include "cpu.h"
#include "fb.h"
#include "sched.h"

/* Interrupts go off for good, so no other thread runs past a panic. */
static void panic_header(const char *msg)
{
	cpu_irq_save();
	fb_emergency_text(); /* a graphics mode would hide the message */
	kconsole_set_quiet(false); /* and so would a quiet boot */
	kconsole_write("\n*** ZKT PANIC: ");
	kconsole_write(msg);
	kconsole_write(" ***\n");
	const char *thread = thread_current_name();
	if (thread) {
		kconsole_write("in thread: ");
		kconsole_write(thread);
		kconsole_write("\n");
	}
}

__attribute__((noreturn)) void panic(const char *msg)
{
	panic_header(msg);
	kconsole_write("System halted.\n");
	cpu_halt_forever();
}

__attribute__((noreturn)) void panic_dump(const char *msg, registers_t *regs)
{
	panic_header(msg);

	kconsole_write("int_no=0x");
	kconsole_write_hex32(regs->int_no);
	kconsole_write(" err_code=0x");
	kconsole_write_hex32(regs->err_code);
	kconsole_write("\n");

	kconsole_write("eip=0x");
	kconsole_write_hex32(regs->eip);
	kconsole_write(" cs=0x");
	kconsole_write_hex32(regs->cs);
	kconsole_write(" eflags=0x");
	kconsole_write_hex32(regs->eflags);
	kconsole_write("\n");

	kconsole_write("eax=0x");
	kconsole_write_hex32(regs->eax);
	kconsole_write(" ebx=0x");
	kconsole_write_hex32(regs->ebx);
	kconsole_write(" ecx=0x");
	kconsole_write_hex32(regs->ecx);
	kconsole_write(" edx=0x");
	kconsole_write_hex32(regs->edx);
	kconsole_write("\n");

	kconsole_write("System halted.\n");
	cpu_halt_forever();
}
