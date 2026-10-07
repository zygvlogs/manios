/* dosrun: an 8086 real-mode CPU interpreter, enough to run DOS
 * programs. ManiOS's own, written for ManiOS from nothing; no MS-DOS
 * or other DOS code. docs/dosrun.md describes it.
 *
 * The CPU is a struct: eight 16-bit general registers, four segment
 * registers, the flags, and a 1 MiB memory. Instructions are decoded
 * and executed one at a time (cpu_step). Anything the interpreter
 * can't do -- an interrupt, a port access, a halt -- is handed to the
 * caller through a callback, so the DOS and BIOS services live outside
 * the CPU and the CPU stays testable on its own.
 */
#ifndef DOSRUN_CPU_H
#define DOSRUN_CPU_H

#include <stdbool.h>
#include <stdint.h>

#define MEM_SIZE (1u << 20) /* 1 MiB, the 8086's address space */

/* The 16-bit general registers, in the encoding the ModR/M byte uses
 * (AL, CL, DL, BL, AH, CH, DH, BH for the 8-bit halves). */
enum {
	AX, CX, DX, BX, SP, BP, SI, DI
};

/* Flags (the low 16 bits of the FLAGS register). */
#define FLAG_CF 0x0001
#define FLAG_PF 0x0004
#define FLAG_AF 0x0010
#define FLAG_ZF 0x0040
#define FLAG_SF 0x0080
#define FLAG_TF 0x0100
#define FLAG_IF 0x0200
#define FLAG_DF 0x0400
#define FLAG_OF 0x0800

/* Why cpu_step stopped. */
enum cpu_stop {
	CPU_OK,      /* the instruction ran; keep going */
	CPU_HALT,    /* HLT */
	CPU_INT,     /* an INT instruction or a hardware interrupt: see cpu->int_no */
	CPU_IRET,    /* IRET: the caller may treat it as the end of an interrupt */
	CPU_FAULT,   /* an instruction the interpreter can't run: cpu->fault says which */
	CPU_EXIT,    /* the program asked to end (INT 21h AH=4Ch): cpu->exit_code */
};

struct cpu {
	uint16_t r[8];
	uint16_t sreg[4]; /* ES, CS, SS, DS */
	uint16_t ip;
	uint16_t flags;

	uint8_t *mem; /* MEM_SIZE bytes */

	/* Set by cpu_step when it stops. */
	uint8_t int_no;
	uint8_t fault_opcode;
	uint8_t exit_code;
	uint32_t insns; /* instructions run, for a runaway guard */

	/* Interrupts the caller has raised (a key is ready, the timer
	 * ticked): a bit per IRQ line, checked between instructions when
	 * IF is set. */
	uint16_t irq_pending;
};

void cpu_init(struct cpu *c);
void cpu_free(struct cpu *c);

/* Runs one instruction. */
enum cpu_stop cpu_step(struct cpu *c);

/* Memory access, with the 20-bit wrap-around the 8086 has. */
uint8_t mem_r8(struct cpu *c, uint32_t addr);
uint16_t mem_r16(struct cpu *c, uint32_t addr);
void mem_w8(struct cpu *c, uint32_t addr, uint8_t v);
void mem_w16(struct cpu *c, uint32_t addr, uint16_t v);

/* The linear address a segment:offset pair names (seg * 16 + off, mod
 * 1 MiB). */
static inline uint32_t lin(uint16_t seg, uint16_t off)
{
	return ((uint32_t)seg << 4) + off;
}

/* Reads/writes a byte or word at seg:off. */
static inline uint8_t rd8(struct cpu *c, uint16_t seg, uint16_t off)
{
	return mem_r8(c, lin(seg, off));
}
static inline uint16_t rd16(struct cpu *c, uint16_t seg, uint16_t off)
{
	return mem_r16(c, lin(seg, off));
}
static inline void wr8(struct cpu *c, uint16_t seg, uint16_t off, uint8_t v)
{
	mem_w8(c, lin(seg, off), v);
}
static inline void wr16(struct cpu *c, uint16_t seg, uint16_t off, uint16_t v)
{
	mem_w16(c, lin(seg, off), v);
}

/* The stack. */
static inline void push16(struct cpu *c, uint16_t v)
{
	c->r[SP] -= 2;
	wr16(c, c->sreg[2], c->r[SP], v);
}
static inline uint16_t pop16(struct cpu *c)
{
	uint16_t v = rd16(c, c->sreg[2], c->r[SP]);
	c->r[SP] += 2;
	return v;
}

/* Sets the flags a result of `value` implies (SF, ZF, PF); CF and OF
 * are the caller's. `bits` is 8 or 16. */
void cpu_set_flags(struct cpu *c, uint32_t value, int bits);

/* Raises a software interrupt, as the INT instruction does: pushes the
 * flags, CS and IP, clears IF and TF, and loads the vector from the
 * interrupt table. Returns the new CS:IP in *cs and *ip. */
void cpu_enter_interrupt(struct cpu *c, uint8_t vector, uint16_t *cs, uint16_t *ip);

/* The string of the fault, for a message. */
const char *cpu_fault_name(uint8_t opcode);

#endif
