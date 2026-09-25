#include "panic.h"
#include "kconsole.h"

__attribute__((noreturn)) void panic_dump(const char *msg, registers_t *regs)
{
	kconsole_write("\n*** ZKT PANIC: ");
	kconsole_write(msg);
	kconsole_write(" ***\n");

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

	__asm__ volatile ("cli");
	for (;;) {
		__asm__ volatile ("hlt");
	}
}
