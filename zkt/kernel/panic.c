#include "panic.h"
#include "kconsole.h"
#include "cpu.h"

static void panic_header(const char *msg)
{
	kconsole_write("\n*** ZKT PANIC: ");
	kconsole_write(msg);
	kconsole_write(" ***\n");
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
