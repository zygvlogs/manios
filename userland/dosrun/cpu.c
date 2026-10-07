/* The 8086 interpreter (cpu.h). Instruction encodings are from the
 * Intel 8086 family user's manual; only the instructions DOS programs
 * actually use are here, and anything else stops with CPU_FAULT rather
 * than doing the wrong thing.
 *
 * The ModR/M decoder is the heart: mod/reg/rm, with the 16-bit
 * addressing modes (BX+SI, BX+DI, BP+SI, BP+DI, SI, DI, BP, BX, and the
 * disp16 form), the segment override prefixes, and the 8-bit register
 * halves. */
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- memory --- */

uint8_t mem_r8(struct cpu *c, uint32_t addr)
{
	return c->mem[addr & (MEM_SIZE - 1)];
}

uint16_t mem_r16(struct cpu *c, uint32_t addr)
{
	addr &= MEM_SIZE - 1;
	uint16_t lo = c->mem[addr];
	uint16_t hi = c->mem[(addr + 1) & (MEM_SIZE - 1)];
	return (uint16_t)(lo | (hi << 8));
}

void mem_w8(struct cpu *c, uint32_t addr, uint8_t v)
{
	c->mem[addr & (MEM_SIZE - 1)] = v;
}

void mem_w16(struct cpu *c, uint32_t addr, uint16_t v)
{
	addr &= MEM_SIZE - 1;
	c->mem[addr] = (uint8_t)v;
	c->mem[(addr + 1) & (MEM_SIZE - 1)] = (uint8_t)(v >> 8);
}

void cpu_init(struct cpu *c)
{
	memset(c, 0, sizeof(*c));
	c->mem = calloc(1, MEM_SIZE);
	if (c->mem) {
		c->sreg[1] = 0xFFFF; /* CS: a reset 8086 starts at FFFF:0000 */
		c->ip = 0;
		c->flags = 0x0002; /* bit 1 is always set */
	}
}

void cpu_free(struct cpu *c)
{
	free(c->mem);
	c->mem = NULL;
}

/* --- flags --- */

static int parity(uint8_t v)
{
	v ^= v >> 4;
	v ^= v >> 2;
	v ^= v >> 1;
	return (v & 1) ? 0 : FLAG_PF; /* even number of set bits */
}

void cpu_set_flags(struct cpu *c, uint32_t value, int bits)
{
	uint32_t mask = bits == 8 ? 0xFF : 0xFFFF;
	uint32_t sign = bits == 8 ? 0x80 : 0x8000;
	value &= mask;
	c->flags &= ~(uint16_t)(FLAG_SF | FLAG_ZF | FLAG_PF);
	if (value == 0) {
		c->flags |= FLAG_ZF;
	}
	if (value & sign) {
		c->flags |= FLAG_SF;
	}
	c->flags |= (uint16_t)parity((uint8_t)value);
}

static void set_cf(struct cpu *c, int on)
{
	if (on) {
		c->flags |= FLAG_CF;
	} else {
		c->flags &= ~FLAG_CF;
	}
}

static void set_of(struct cpu *c, int on)
{
	if (on) {
		c->flags |= FLAG_OF;
	} else {
		c->flags &= ~FLAG_OF;
	}
}

static void set_af(struct cpu *c, int on)
{
	if (on) {
		c->flags |= FLAG_AF;
	} else {
		c->flags &= ~FLAG_AF;
	}
}

/* --- the ModR/M byte --- */

/* What an operand is: a register, or a memory address. */
struct operand {
	int is_reg;
	int reg;       /* register number, 0-7 */
	uint16_t seg;  /* memory: the segment */
	uint16_t off;  /* memory: the offset */
};

/* The default segment for a 16-bit addressing form (BP-based forms use
 * SS, everything else DS). */
static uint16_t default_seg(struct cpu *c, int rm)
{
	return (rm == 4 || rm == 5 || rm == 6) ? c->sreg[2] : c->sreg[3];
}

/* Decodes the ModR/M byte at CS:IP, advancing IP past it and any
 * displacement. `reg` receives the reg field; the operand is returned. */
static struct operand decode_rm(struct cpu *c, int *reg)
{
	uint8_t m = rd8(c, c->sreg[1], c->ip++);
	int mod = m >> 6, r = (m >> 3) & 7, rm = m & 7;
	struct operand op;
	*reg = r;

	if (mod == 3) {
		op.is_reg = 1;
		op.reg = rm;
		return op;
	}
	op.is_reg = 0;
	op.seg = default_seg(c, rm);
	uint16_t base = 0;
	switch (rm) {
	case 0: base = c->r[BX] + c->r[SI]; break;
	case 1: base = c->r[BX] + c->r[DI]; break;
	case 2: base = c->r[BP] + c->r[SI]; break;
	case 3: base = c->r[BP] + c->r[DI]; break;
	case 4: base = c->r[SI]; break;
	case 5: base = c->r[DI]; break;
	case 6: base = c->r[BP]; break;
	case 7: base = c->r[BX]; break;
	}
	if (mod == 0 && rm == 6) {
		base = rd16(c, c->sreg[1], c->ip); /* disp16, DS by default */
		c->ip += 2;
		op.seg = c->sreg[3];
	} else if (mod == 1) {
		base += (int8_t)rd8(c, c->sreg[1], c->ip++);
	} else if (mod == 2) {
		base += rd16(c, c->sreg[1], c->ip);
		c->ip += 2;
	}
	op.off = base;
	return op;
}

/* Reads an operand as a 16-bit value. */
static uint16_t get16(struct cpu *c, const struct operand *op)
{
	return op->is_reg ? c->r[op->reg] : rd16(c, op->seg, op->off);
}

static void put16(struct cpu *c, const struct operand *op, uint16_t v)
{
	if (op->is_reg) {
		c->r[op->reg] = v;
	} else {
		wr16(c, op->seg, op->off, v);
	}
}

/* The 8-bit half of a register number (AL, CL, DL, BL, AH, CH, DH, BH). */
static uint8_t get8(struct cpu *c, int reg)
{
	uint16_t v = c->r[reg & 3];
	return (reg & 4) ? (uint8_t)(v >> 8) : (uint8_t)v;
}

static void put8(struct cpu *c, int reg, uint8_t v)
{
	uint16_t *r = &c->r[reg & 3];
	if (reg & 4) {
		*r = (uint16_t)((*r & 0x00FF) | ((uint16_t)v << 8));
	} else {
		*r = (uint16_t)((*r & 0xFF00) | v);
	}
}

static uint8_t get8_rm(struct cpu *c, const struct operand *op)
{
	return op->is_reg ? get8(c, op->reg) : rd8(c, op->seg, op->off);
}

static void put8_rm(struct cpu *c, const struct operand *op, uint8_t v)
{
	if (op->is_reg) {
		put8(c, op->reg, v);
	} else {
		wr8(c, op->seg, op->off, v);
	}
}

/* --- ALU --- */

enum { ADD, OR, ADC, SBB, AND, SUB, XOR, CMP };

static uint16_t alu16(struct cpu *c, int op, uint16_t a, uint16_t b)
{
	uint32_t r;
	int cf = (c->flags & FLAG_CF) != 0;
	switch (op) {
	case ADD: r = a + b; set_cf(c, r > 0xFFFF); set_of(c, ((a ^ r) & (b ^ r) & 0x8000) != 0); set_af(c, ((a ^ b ^ r) & 0x10) != 0); break;
	case OR:  r = a | b; set_cf(c, 0); set_of(c, 0); set_af(c, 0); break;
	case ADC: r = a + b + cf; set_cf(c, r > 0xFFFF); set_of(c, ((a ^ r) & (b ^ r) & 0x8000) != 0); set_af(c, ((a ^ b ^ r) & 0x10) != 0); break;
	case SBB: r = a - b - cf; set_cf(c, (int32_t)a - b - cf < 0); set_of(c, ((a ^ b) & (a ^ r) & 0x8000) != 0); set_af(c, ((a ^ b ^ r) & 0x10) != 0); break;
	case AND: r = a & b; set_cf(c, 0); set_of(c, 0); set_af(c, 0); break;
	case SUB:
	case CMP: r = a - b; set_cf(c, (int32_t)a - b < 0); set_of(c, ((a ^ b) & (a ^ r) & 0x8000) != 0); set_af(c, ((a ^ b ^ r) & 0x10) != 0); break;
	default:  r = a ^ b; set_cf(c, 0); set_of(c, 0); set_af(c, 0); break;
	}
	cpu_set_flags(c, r, 16);
	return (uint16_t)r;
}

static uint8_t alu8(struct cpu *c, int op, uint8_t a, uint8_t b)
{
	uint32_t r;
	int cf = (c->flags & FLAG_CF) != 0;
	switch (op) {
	case ADD: r = a + b; set_cf(c, r > 0xFF); set_of(c, ((a ^ r) & (b ^ r) & 0x80) != 0); set_af(c, ((a ^ b ^ r) & 0x10) != 0); break;
	case OR:  r = a | b; set_cf(c, 0); set_of(c, 0); set_af(c, 0); break;
	case ADC: r = a + b + cf; set_cf(c, r > 0xFF); set_of(c, ((a ^ r) & (b ^ r) & 0x80) != 0); set_af(c, ((a ^ b ^ r) & 0x10) != 0); break;
	case SBB: r = a - b - cf; set_cf(c, (int32_t)a - b - cf < 0); set_of(c, ((a ^ b) & (a ^ r) & 0x80) != 0); set_af(c, ((a ^ b ^ r) & 0x10) != 0); break;
	case AND: r = a & b; set_cf(c, 0); set_of(c, 0); set_af(c, 0); break;
	case SUB:
	case CMP: r = a - b; set_cf(c, (int32_t)a - b < 0); set_of(c, ((a ^ b) & (a ^ r) & 0x80) != 0); set_af(c, ((a ^ b ^ r) & 0x10) != 0); break;
	default:  r = a ^ b; set_cf(c, 0); set_of(c, 0); set_af(c, 0); break;
	}
	cpu_set_flags(c, r, 8);
	return (uint8_t)r;
}

/* --- shifts and rotates --- */

static uint16_t shift16(struct cpu *c, int op, uint16_t v, int count)
{
	if (count == 0) {
		return v;
	}
	uint16_t r = v;
	for (int i = 0; i < count; i++) {
		switch (op) {
		case 0: set_cf(c, r & 0x8000); r <<= 1; break;                 /* ROL */
		case 1: set_cf(c, r & 1); r = (uint16_t)((r >> 1) | (r << 15)); break; /* ROR */
		case 2: { int cf = (c->flags & FLAG_CF) != 0; set_cf(c, r & 0x8000); r = (uint16_t)((r << 1) | cf); break; } /* RCL */
		case 3: { int cf = (c->flags & FLAG_CF) != 0; set_cf(c, r & 1); r = (uint16_t)((r >> 1) | (cf << 15)); break; } /* RCR */
		case 4: set_cf(c, r & 0x8000); r <<= 1; break;                 /* SHL/SAL */
		case 5: set_cf(c, r & 1); r >>= 1; break;                      /* SHR */
		case 7: set_cf(c, r & 1); r = (uint16_t)((int16_t)r >> 1); break; /* SAR */
		}
	}
	if (op != 0 && op != 1 && op != 2 && op != 3) {
		cpu_set_flags(c, r, 16);
		set_of(c, ((r ^ v) & 0x8000) != 0 && count == 1);
	}
	return r;
}

static uint8_t shift8(struct cpu *c, int op, uint8_t v, int count)
{
	if (count == 0) {
		return v;
	}
	uint8_t r = v;
	for (int i = 0; i < count; i++) {
		switch (op) {
		case 0: set_cf(c, r & 0x80); r = (uint8_t)((r << 1) | (r >> 7)); break;
		case 1: set_cf(c, r & 1); r = (uint8_t)((r >> 1) | (r << 7)); break;
		case 2: { int cf = (c->flags & FLAG_CF) != 0; set_cf(c, r & 0x80); r = (uint8_t)((r << 1) | cf); break; }
		case 3: { int cf = (c->flags & FLAG_CF) != 0; set_cf(c, r & 1); r = (uint8_t)((r >> 1) | (cf << 7)); break; }
		case 4: set_cf(c, r & 0x80); r <<= 1; break;
		case 5: set_cf(c, r & 1); r >>= 1; break;
		case 7: set_cf(c, r & 1); r = (uint8_t)((int8_t)r >> 1); break;
		}
	}
	if (op != 0 && op != 1 && op != 2 && op != 3) {
		cpu_set_flags(c, r, 8);
		set_of(c, ((r ^ v) & 0x80) != 0 && count == 1);
	}
	return r;
}

/* --- interrupts --- */

void cpu_enter_interrupt(struct cpu *c, uint8_t vector, uint16_t *cs, uint16_t *ip)
{
	push16(c, c->flags);
	push16(c, c->sreg[1]);
	push16(c, c->ip);
	c->flags &= ~(uint16_t)(FLAG_IF | FLAG_TF);
	uint16_t off = rd16(c, 0, (uint16_t)(vector * 4));
	uint16_t seg = rd16(c, 0, (uint16_t)(vector * 4 + 2));
	*cs = seg;
	*ip = off;
}

/* --- the string operations --- */

/* Runs one REP-prefixed string instruction. */
static void do_string(struct cpu *c, uint8_t op)
{
	int df = (c->flags & FLAG_DF) ? -1 : 1;
	int w = (op & 1) ? 2 : 1; /* the low bit: byte or word */
	int rep = (op & 2) ? 1 : 0;
	uint8_t sub = op >> 2;

	do {
		switch (sub) {
		case 0: { /* MOVS */
			if (w == 2) {
				wr16(c, c->sreg[0], c->r[DI], rd16(c, c->sreg[3], c->r[SI]));
			} else {
				wr8(c, c->sreg[0], c->r[DI], rd8(c, c->sreg[3], c->r[SI]));
			}
			c->r[SI] = (uint16_t)(c->r[SI] + df * w);
			c->r[DI] = (uint16_t)(c->r[DI] + df * w);
			break;
		}
		case 1: { /* CMPS */
			uint16_t a = w == 2 ? rd16(c, c->sreg[3], c->r[SI]) : rd8(c, c->sreg[3], c->r[SI]);
			uint16_t b = w == 2 ? rd16(c, c->sreg[0], c->r[DI]) : rd8(c, c->sreg[0], c->r[DI]);
			if (w == 2) {
				alu16(c, CMP, a, b);
			} else {
				alu8(c, CMP, (uint8_t)a, (uint8_t)b);
			}
			c->r[SI] = (uint16_t)(c->r[SI] + df * w);
			c->r[DI] = (uint16_t)(c->r[DI] + df * w);
			break;
		}
		case 2: { /* STOS */
			if (w == 2) {
				wr16(c, c->sreg[0], c->r[DI], c->r[AX]);
			} else {
				wr8(c, c->sreg[0], c->r[DI], get8(c, 0));
			}
			c->r[DI] = (uint16_t)(c->r[DI] + df * w);
			break;
		}
		case 3: { /* LODS */
			if (w == 2) {
				c->r[AX] = rd16(c, c->sreg[3], c->r[SI]);
			} else {
				put8(c, 0, rd8(c, c->sreg[3], c->r[SI]));
			}
			c->r[SI] = (uint16_t)(c->r[SI] + df * w);
			break;
		}
		case 4: { /* SCAS */
			uint16_t a = w == 2 ? c->r[AX] : get8(c, 0);
			uint16_t b = w == 2 ? rd16(c, c->sreg[0], c->r[DI]) : rd8(c, c->sreg[0], c->r[DI]);
			if (w == 2) {
				alu16(c, CMP, a, b);
			} else {
				alu8(c, CMP, (uint8_t)a, (uint8_t)b);
			}
			c->r[DI] = (uint16_t)(c->r[DI] + df * w);
			break;
		}
		}
		if (!rep) {
			break;
		}
		uint16_t n = c->r[CX] - 1;
		c->r[CX] = n;
		if (sub == 1 || sub == 4) {
			if (n == 0 || !(c->flags & FLAG_ZF)) { /* REPE/REPNE */
				break;
			}
		} else if (n == 0) {
			break;
		}
	} while (1);
}

/* --- the interpreter --- */

/* The condition codes of the Jcc instructions (the low nibble of 70-7F):
 * O, NO, B, AE, E, NE, BE, A, S, NS, P, NP, L, GE, LE, G. */
static int cond(struct cpu *c, int cc)
{
	int cf = (c->flags & FLAG_CF) != 0;
	int zf = (c->flags & FLAG_ZF) != 0;
	int sf = (c->flags & FLAG_SF) != 0;
	int of = (c->flags & FLAG_OF) != 0;
	int pf = (c->flags & FLAG_PF) != 0;
	switch (cc) {
	case 0x0: return of;
	case 0x1: return !of;
	case 0x2: return cf;
	case 0x3: return !cf;
	case 0x4: return zf;
	case 0x5: return !zf;
	case 0x6: return cf || zf;
	case 0x7: return !cf && !zf;
	case 0x8: return sf;
	case 0x9: return !sf;
	case 0xA: return pf;
	case 0xB: return !pf;
	case 0xC: return sf != of;
	case 0xD: return sf == of;
	case 0xE: return zf || (sf != of);
	default:  return !zf && (sf == of);
	}
}

const char *cpu_fault_name(uint8_t opcode)
{
	static char buf[32];
	/* A short mnemonic for the common ones, else the number. */
	switch (opcode) {
	case 0x0F: return "two-byte opcode (0F)";
	case 0x27: return "DAA";
	case 0x2F: return "DAS";
	case 0x37: return "AAA";
	case 0x3F: return "AAS";
	case 0xD4: return "AAM";
	case 0xD5: return "AAD";
	case 0xD7: return "XLAT";
	case 0x62: return "BOUND";
	case 0x63: return "ARPL";
	case 0x9B: return "FWAIT";
	case 0xCC: return "INT3";
	case 0xCE: return "INTO";
	case 0xF1: return "INT1";
	}
	snprintf(buf, sizeof(buf), "opcode %02X", opcode);
	return buf;
}

enum cpu_stop cpu_step(struct cpu *c)
{
	/* A pending hardware interrupt, if the program allows them. The
	 * frame is pushed and CS:IP set here, as the CPU does; the caller
	 * runs the handler and IRETs. */
	if (c->irq_pending && (c->flags & FLAG_IF)) {
		for (int i = 0; i < 16; i++) {
			if (c->irq_pending & (1u << i)) {
				c->irq_pending &= (uint16_t)~(1u << i);
				c->int_no = (uint8_t)(0x08 + i); /* the PIC's vectors */
				uint16_t cs, ip;
				cpu_enter_interrupt(c, c->int_no, &cs, &ip);
				c->sreg[1] = cs;
				c->ip = ip;
				return CPU_INT;
			}
		}
	}

	/* Segment override prefixes: up to one each, then the instruction. */
	uint16_t override = 0;
	uint8_t op;
	for (;;) {
		op = rd8(c, c->sreg[1], c->ip);
		if (op == 0x26) { override = c->sreg[0]; c->ip++; continue; } /* ES */
		if (op == 0x2E) { override = c->sreg[1]; c->ip++; continue; } /* CS */
		if (op == 0x36) { override = c->sreg[2]; c->ip++; continue; } /* SS */
		if (op == 0x3E) { override = c->sreg[3]; c->ip++; continue; } /* DS */
		break;
	}
	c->ip++;
	c->insns++;
	c->fault_opcode = op; /* what a CPU_FAULT reports */

	/* The string instructions, with their REP prefixes. */
	if (op == 0xF2 || op == 0xF3) {
		uint8_t next = rd8(c, c->sreg[1], c->ip++);
		if (next >= 0xA4 && next <= 0xAF) {
			do_string(c, (uint8_t)((next - 0xA4) << 2 | (op == 0xF3 ? 2 : 0) | (next & 1)));
			return CPU_OK;
		}
		return CPU_FAULT; /* REP on something else: not supported */
	}
	if (op >= 0xA4 && op <= 0xAF) {
		do_string(c, (uint8_t)((op - 0xA4) << 2 | (op & 1)));
		return CPU_OK;
	}

	/* The ALU group: 00-3F, with the direction and size bits. */
	if (op < 0x40 && (op & 7) <= 5 && (op & 0xC0) == 0) {
		int alu_op = (op >> 3) & 7;
		int w = op & 1;
		int dir = op & 2; /* 0: rm,reg  2: reg,rm */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		if (w) {
			uint16_t a = dir ? c->r[reg] : get16(c, &rm);
			uint16_t b = dir ? get16(c, &rm) : c->r[reg];
			uint16_t r = alu16(c, alu_op, a, b);
			if (alu_op != CMP) {
				if (dir) {
					put16(c, &rm, r);
				} else {
					c->r[reg] = r;
				}
			}
		} else {
			uint8_t a = dir ? get8(c, reg) : get8_rm(c, &rm);
			uint8_t b = dir ? get8_rm(c, &rm) : get8(c, reg);
			uint8_t r = alu8(c, alu_op, a, b);
			if (alu_op != CMP) {
				if (dir) {
					put8_rm(c, &rm, r);
				} else {
					put8(c, reg, r);
				}
			}
		}
		return CPU_OK;
	}

	/* The ALU accumulator forms: 04/05/0C/0D/14/15/.../3C/3D. */
	if (op < 0x40 && (op & 7) >= 4 && (op & 7) <= 5) {
		int alu_op = (op >> 3) & 7;
		int w = op & 1;
		if (w) {
			uint16_t imm = rd16(c, c->sreg[1], c->ip);
			c->ip += 2;
			uint16_t r = alu16(c, alu_op, c->r[AX], imm);
			if (alu_op != CMP) {
				c->r[AX] = r;
			}
		} else {
			uint8_t imm = rd8(c, c->sreg[1], c->ip++);
			uint8_t r = alu8(c, alu_op, get8(c, 0), imm);
			if (alu_op != CMP) {
				put8(c, 0, r);
			}
		}
		return CPU_OK;
	}

	/* INC/DEC/PUSH/POP of a register: 40-5F. */
	if (op >= 0x40 && op <= 0x5F) {
		int r = op & 7;
		if (op < 0x48) { /* INC */
			uint16_t v = c->r[r];
			uint16_t res = (uint16_t)(v + 1);
			set_cf(c, (c->flags & FLAG_CF) != 0); /* INC leaves CF alone */
			int cf = (c->flags & FLAG_CF) != 0;
			cpu_set_flags(c, res, 16);
			set_cf(c, cf);
			set_of(c, res == 0x8000);
			c->r[r] = res;
		} else if (op < 0x50) { /* DEC */
			uint16_t v = c->r[r];
			uint16_t res = (uint16_t)(v - 1);
			int cf = (c->flags & FLAG_CF) != 0;
			cpu_set_flags(c, res, 16);
			set_cf(c, cf);
			set_of(c, res == 0x7FFF);
			c->r[r] = res;
		} else if (op < 0x58) { /* PUSH */
			push16(c, c->r[r]);
		} else { /* POP */
			c->r[r] = pop16(c);
		}
		return CPU_OK;
	}

	/* The one-byte instructions. */
	switch (op) {
	case 0x06: push16(c, c->sreg[0]); return CPU_OK;      /* PUSH ES */
	case 0x07: c->sreg[0] = pop16(c); return CPU_OK;      /* POP ES */
	case 0x0E: push16(c, c->sreg[1]); return CPU_OK;      /* PUSH CS */
	case 0x16: push16(c, c->sreg[2]); return CPU_OK;      /* PUSH SS */
	case 0x17: c->sreg[2] = pop16(c); return CPU_OK;      /* POP SS */
	case 0x1E: push16(c, c->sreg[3]); return CPU_OK;      /* PUSH DS */
	case 0x1F: c->sreg[3] = pop16(c); return CPU_OK;      /* POP DS */
	case 0x60: { /* PUSHA: pushes the original SP, not the changing one */
		uint16_t sp = c->r[SP];
		for (int i = 0; i < 8; i++) {
			push16(c, i == SP ? sp : c->r[i]);
		}
		return CPU_OK;
	}
	case 0x61: { /* POPA */
		for (int i = 7; i >= 0; i--) {
			c->r[i] = pop16(c);
		}
		return CPU_OK;
	}
	case 0x68: push16(c, rd16(c, c->sreg[1], c->ip)); c->ip += 2; return CPU_OK; /* PUSH imm16 */
	case 0x6A: push16(c, (uint16_t)(int16_t)(int8_t)rd8(c, c->sreg[1], c->ip++)); return CPU_OK; /* PUSH imm8 */
	case 0x69: case 0x6B: { /* IMUL r, rm, imm */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		uint16_t imm = op == 0x69 ? rd16(c, c->sreg[1], c->ip) : (uint16_t)(int16_t)(int8_t)rd8(c, c->sreg[1], c->ip);
		c->ip += op == 0x69 ? 2 : 1;
		int32_t r = (int16_t)get16(c, &rm) * (int16_t)imm;
		c->r[reg] = (uint16_t)r;
		set_cf(c, r != (int16_t)r);
		set_of(c, r != (int16_t)r);
		return CPU_OK;
	}
	case 0x70: case 0x71: case 0x72: case 0x73: /* Jcc rel8 */
	case 0x74: case 0x75: case 0x76: case 0x77:
	case 0x78: case 0x79: case 0x7A: case 0x7B:
	case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
		int8_t rel = (int8_t)rd8(c, c->sreg[1], c->ip++);
		if (cond(c, op & 0x0F)) {
			c->ip = (uint16_t)(c->ip + rel);
		}
		return CPU_OK;
	}
	case 0x80: case 0x81: case 0x82: case 0x83: { /* the ALU group with an immediate */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		if (op == 0x80 || op == 0x82) {
			uint8_t imm = rd8(c, c->sreg[1], c->ip++);
			uint8_t r = alu8(c, reg, get8_rm(c, &rm), imm);
			if (reg != CMP) {
				put8_rm(c, &rm, r);
			}
		} else {
			uint16_t imm = op == 0x81 ? rd16(c, c->sreg[1], c->ip)
			                          : (uint16_t)(int16_t)(int8_t)rd8(c, c->sreg[1], c->ip);
			c->ip += op == 0x81 ? 2 : 1;
			uint16_t r = alu16(c, reg, get16(c, &rm), imm);
			if (reg != CMP) {
				put16(c, &rm, r);
			}
		}
		return CPU_OK;
	}
	case 0x84: case 0x85: { /* TEST rm, reg */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		if (op == 0x85) {
			alu16(c, AND, get16(c, &rm), c->r[reg]);
		} else {
			alu8(c, AND, get8_rm(c, &rm), get8(c, reg));
		}
		return CPU_OK;
	}
	case 0x86: case 0x87: { /* XCHG rm, reg */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		if (op == 0x87) {
			uint16_t t = get16(c, &rm);
			put16(c, &rm, c->r[reg]);
			c->r[reg] = t;
		} else {
			uint8_t t = get8_rm(c, &rm);
			put8_rm(c, &rm, get8(c, reg));
			put8(c, reg, t);
		}
		return CPU_OK;
	}
	case 0x88: case 0x89: { /* MOV rm, reg */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		if (op == 0x89) {
			put16(c, &rm, c->r[reg]);
		} else {
			put8_rm(c, &rm, get8(c, reg));
		}
		return CPU_OK;
	}
	case 0x8A: case 0x8B: { /* MOV reg, rm */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		if (op == 0x8B) {
			c->r[reg] = get16(c, &rm);
		} else {
			put8(c, reg, get8_rm(c, &rm));
		}
		return CPU_OK;
	}
	case 0x8C: { /* MOV rm, sreg */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		put16(c, &rm, c->sreg[reg & 3]);
		return CPU_OK;
	}
	case 0x8D: { /* LEA */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		c->r[reg] = rm.off;
		return CPU_OK;
	}
	case 0x8E: { /* MOV sreg, rm */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		c->sreg[reg & 3] = get16(c, &rm);
		return CPU_OK;
	}
	case 0x8F: { /* POP rm */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		put16(c, &rm, pop16(c));
		return CPU_OK;
	}
	case 0x90: return CPU_OK; /* NOP */
	case 0x91: case 0x92: case 0x93: /* XCHG AX, r */
	case 0x94: case 0x95: case 0x96: case 0x97: {
		int r = op & 7;
		uint16_t t = c->r[AX];
		c->r[AX] = c->r[r];
		c->r[r] = t;
		return CPU_OK;
	}
	case 0x98: c->r[AX] = (uint16_t)(int16_t)(int8_t)c->r[AX]; return CPU_OK; /* CBW */
	case 0x99: c->r[DX] = (c->r[AX] & 0x8000) ? 0xFFFF : 0; return CPU_OK;    /* CWD */
	case 0x9C: push16(c, c->flags); return CPU_OK;  /* PUSHF */
	case 0x9D: c->flags = pop16(c); return CPU_OK;  /* POPF */
	case 0x9E: c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | (c->flags & 0xFF)); return CPU_OK; /* SAHF */
	case 0x9F: c->flags = (uint16_t)((c->flags & 0xFF00) | (c->r[AX] & 0xFF) | 0x02); return CPU_OK; /* LAHF */
	case 0xA0: c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | rd8(c, override ? override : c->sreg[3], rd16(c, c->sreg[1], c->ip))); c->ip += 2; return CPU_OK; /* MOV AL, moffs */
	case 0xA1: c->r[AX] = rd16(c, override ? override : c->sreg[3], rd16(c, c->sreg[1], c->ip)); c->ip += 2; return CPU_OK; /* MOV AX, moffs */
	case 0xA2: wr8(c, override ? override : c->sreg[3], rd16(c, c->sreg[1], c->ip), get8(c, 0)); c->ip += 2; return CPU_OK; /* MOV moffs, AL */
	case 0xA3: wr16(c, override ? override : c->sreg[3], rd16(c, c->sreg[1], c->ip), c->r[AX]); c->ip += 2; return CPU_OK; /* MOV moffs, AX */
	case 0xA8: alu8(c, AND, get8(c, 0), rd8(c, c->sreg[1], c->ip++)); return CPU_OK; /* TEST AL, imm */
	case 0xA9: alu16(c, AND, c->r[AX], rd16(c, c->sreg[1], c->ip)); c->ip += 2; return CPU_OK; /* TEST AX, imm */
	case 0xB0: case 0xB1: case 0xB2: case 0xB3: /* MOV r8, imm8 */
	case 0xB4: case 0xB5: case 0xB6: case 0xB7:
		put8(c, op & 7, rd8(c, c->sreg[1], c->ip++));
		return CPU_OK;
	case 0xB8: case 0xB9: case 0xBA: case 0xBB: /* MOV r16, imm16 */
	case 0xBC: case 0xBD: case 0xBE: case 0xBF:
		c->r[op & 7] = rd16(c, c->sreg[1], c->ip);
		c->ip += 2;
		return CPU_OK;
	case 0xC2: { /* RET imm16 */
		uint16_t n = rd16(c, c->sreg[1], c->ip);
		c->ip = pop16(c);
		c->r[SP] = (uint16_t)(c->r[SP] + n);
		return CPU_OK;
	}
	case 0xC3: c->ip = pop16(c); return CPU_OK; /* RET */
	case 0xC4: case 0xC5: { /* LES/LDS */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		c->r[reg] = rd16(c, rm.seg, rm.off);
		c->sreg[op == 0xC4 ? 0 : 3] = rd16(c, rm.seg, (uint16_t)(rm.off + 2));
		return CPU_OK;
	}
	case 0xC6: case 0xC7: { /* MOV rm, imm */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		if (op == 0xC7) {
			put16(c, &rm, rd16(c, c->sreg[1], c->ip));
			c->ip += 2;
		} else {
			put8_rm(c, &rm, rd8(c, c->sreg[1], c->ip++));
		}
		return CPU_OK;
	}
	case 0xC8: { /* ENTER */
		uint16_t size = rd16(c, c->sreg[1], c->ip);
		uint8_t level = rd8(c, c->sreg[1], c->ip + 2);
		c->ip += 3;
		push16(c, c->r[BP]);
		uint16_t frame = c->r[SP];
		for (int i = 0; i < level; i++) {
			c->r[BP] = (uint16_t)(c->r[BP] - 2);
			push16(c, rd16(c, c->sreg[2], c->r[BP]));
		}
		push16(c, frame);
		c->r[BP] = frame;
		c->r[SP] = (uint16_t)(c->r[SP] - size);
		return CPU_OK;
	}
	case 0xC9: { /* LEAVE */
		c->r[SP] = c->r[BP];
		c->r[BP] = pop16(c);
		return CPU_OK;
	}
	case 0xCA: { /* RETF imm16 */
		uint16_t n = rd16(c, c->sreg[1], c->ip);
		c->ip = pop16(c);
		c->sreg[1] = pop16(c);
		c->r[SP] = (uint16_t)(c->r[SP] + n);
		return CPU_OK;
	}
	case 0xCB: c->ip = pop16(c); c->sreg[1] = pop16(c); return CPU_OK; /* RETF */
	case 0xCD: { /* INT imm8: push the frame and vector, as the CPU does */
		c->int_no = rd8(c, c->sreg[1], c->ip++);
		uint16_t cs, ip;
		cpu_enter_interrupt(c, c->int_no, &cs, &ip);
		c->sreg[1] = cs;
		c->ip = ip;
		return CPU_INT;
	}
	case 0xCF: { /* IRET */
		c->ip = pop16(c);
		c->sreg[1] = pop16(c);
		c->flags = pop16(c);
		return CPU_IRET;
	}
	case 0xD0: case 0xD1: case 0xD2: case 0xD3: { /* shifts by 1, CL, or imm */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		int count = (op == 0xD0 || op == 0xD1) ? 1
		          : (op == 0xD2 || op == 0xD3) ? c->r[CX] : rd8(c, c->sreg[1], c->ip++);
		if (op == 0xD1 || op == 0xD3) {
			put16(c, &rm, shift16(c, reg, get16(c, &rm), count & 31));
		} else {
			put8_rm(c, &rm, shift8(c, reg, get8_rm(c, &rm), count & 31));
		}
		return CPU_OK;
	}
	case 0xE0: case 0xE1: case 0xE2: { /* LOOPNE/LOOPE/LOOP */
		int8_t rel = (int8_t)rd8(c, c->sreg[1], c->ip++);
		c->r[CX]--;
		int zf = (c->flags & FLAG_ZF) != 0;
		int take = (op == 0xE2) || (op == 0xE0 && !zf) || (op == 0xE1 && zf);
		if (c->r[CX] != 0 && take) {
			c->ip = (uint16_t)(c->ip + rel);
		}
		return CPU_OK;
	}
	case 0xE3: { /* JCXZ */
		int8_t rel = (int8_t)rd8(c, c->sreg[1], c->ip++);
		if (c->r[CX] == 0) {
			c->ip = (uint16_t)(c->ip + rel);
		}
		return CPU_OK;
	}
	case 0xE4: case 0xE5: case 0xE6: case 0xE7: /* IN/OUT imm8 */
	case 0xEC: case 0xED: case 0xEE: case 0xEF: /* IN/OUT DX */
		return CPU_FAULT; /* port I/O: the caller decides (a DOS program's hardware) */
	case 0xE8: { /* CALL rel16 */
		int16_t rel = (int16_t)rd16(c, c->sreg[1], c->ip);
		c->ip += 2;
		push16(c, c->ip);
		c->ip = (uint16_t)(c->ip + rel);
		return CPU_OK;
	}
	case 0xE9: { /* JMP rel16 */
		int16_t rel = (int16_t)rd16(c, c->sreg[1], c->ip);
		c->ip = (uint16_t)(c->ip + 2 + rel);
		return CPU_OK;
	}
	case 0xEA: { /* JMP far */
		uint16_t off = rd16(c, c->sreg[1], c->ip);
		uint16_t seg = rd16(c, c->sreg[1], c->ip + 2);
		c->ip = off;
		c->sreg[1] = seg;
		return CPU_OK;
	}
	case 0xEB: { /* JMP rel8 */
		int8_t rel = (int8_t)rd8(c, c->sreg[1], c->ip++);
		c->ip = (uint16_t)(c->ip + rel);
		return CPU_OK;
	}
	case 0xF4: return CPU_HALT; /* HLT */
	case 0xF5: c->flags ^= FLAG_CF; return CPU_OK; /* CMC */
	case 0xF6: case 0xF7: { /* the group 3: TEST/NOT/NEG/MUL/IMUL/DIV/IDIV */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		int w = op == 0xF7;
		if (reg == 0 || reg == 1) { /* TEST rm, imm */
			if (w) {
				alu16(c, AND, get16(c, &rm), rd16(c, c->sreg[1], c->ip));
				c->ip += 2;
			} else {
				alu8(c, AND, get8_rm(c, &rm), rd8(c, c->sreg[1], c->ip++));
			}
		} else if (reg == 2) { /* NOT */
			if (w) {
				put16(c, &rm, (uint16_t)~get16(c, &rm));
			} else {
				put8_rm(c, &rm, (uint8_t)~get8_rm(c, &rm));
			}
		} else if (reg == 3) { /* NEG */
			if (w) {
				uint16_t v = get16(c, &rm);
				put16(c, &rm, (uint16_t)(0 - v));
				set_cf(c, v != 0);
				set_of(c, v == 0x8000);
				cpu_set_flags(c, (uint16_t)(0 - v), 16);
			} else {
				uint8_t v = get8_rm(c, &rm);
				put8_rm(c, &rm, (uint8_t)(0 - v));
				set_cf(c, v != 0);
				set_of(c, v == 0x80);
				cpu_set_flags(c, (uint8_t)(0 - v), 8);
			}
		} else if (reg == 4) { /* MUL */
			if (w) {
				uint32_t r = (uint32_t)c->r[AX] * get16(c, &rm);
				c->r[AX] = (uint16_t)r;
				c->r[DX] = (uint16_t)(r >> 16);
				set_cf(c, c->r[DX] != 0);
				set_of(c, c->r[DX] != 0);
			} else {
				uint16_t r = (uint16_t)(get8(c, 0) * get8_rm(c, &rm));
				c->r[AX] = r;
				set_cf(c, (r >> 8) != 0);
				set_of(c, (r >> 8) != 0);
			}
		} else if (reg == 5) { /* IMUL */
			if (w) {
				int32_t r = (int16_t)c->r[AX] * (int16_t)get16(c, &rm);
				c->r[AX] = (uint16_t)r;
				c->r[DX] = (uint16_t)(r >> 16);
				set_cf(c, r != (int16_t)r);
				set_of(c, r != (int16_t)r);
			} else {
				int16_t r = (int16_t)(int8_t)get8(c, 0) * (int8_t)get8_rm(c, &rm);
				c->r[AX] = (uint16_t)r;
				set_cf(c, r != (int8_t)r);
				set_of(c, r != (int8_t)r);
			}
		} else if (reg == 6) { /* DIV */
			if (w) {
				uint16_t d = get16(c, &rm);
				if (d == 0) {
					return CPU_FAULT; /* divide by zero: the caller raises INT 0 */
				}
				uint32_t n = ((uint32_t)c->r[DX] << 16) | c->r[AX];
				if (n / d > 0xFFFF) {
					return CPU_FAULT;
				}
				c->r[AX] = (uint16_t)(n / d);
				c->r[DX] = (uint16_t)(n % d);
			} else {
				uint8_t d = get8_rm(c, &rm);
				if (d == 0) {
					return CPU_FAULT;
				}
				uint16_t n = c->r[AX];
				if (n / d > 0xFF) {
					return CPU_FAULT;
				}
				put8(c, 0, (uint8_t)(n / d));
				put8(c, 4, (uint8_t)(n % d));
			}
		} else { /* IDIV */
			if (w) {
				int16_t d = (int16_t)get16(c, &rm);
				if (d == 0) {
					return CPU_FAULT;
				}
				int32_t n = (int32_t)(((uint32_t)c->r[DX] << 16) | c->r[AX]);
				int32_t q = n / d;
				if (q > 32767 || q < -32768) {
					return CPU_FAULT;
				}
				c->r[AX] = (uint16_t)q;
				c->r[DX] = (uint16_t)(n % d);
			} else {
				int8_t d = (int8_t)get8_rm(c, &rm);
				if (d == 0) {
					return CPU_FAULT;
				}
				int16_t n = (int16_t)c->r[AX];
				int16_t q = n / d;
				if (q > 127 || q < -128) {
					return CPU_FAULT;
				}
				put8(c, 0, (uint8_t)q);
				put8(c, 4, (uint8_t)(n % d));
			}
		}
		return CPU_OK;
	}
	case 0xF8: set_cf(c, 0); return CPU_OK; /* CLC */
	case 0xF9: set_cf(c, 1); return CPU_OK; /* STC */
	case 0xFA: c->flags &= ~FLAG_IF; return CPU_OK; /* CLI */
	case 0xFB: c->flags |= FLAG_IF; return CPU_OK;  /* STI */
	case 0xFC: c->flags &= ~FLAG_DF; return CPU_OK; /* CLD */
	case 0xFD: c->flags |= FLAG_DF; return CPU_OK;  /* STD */
	case 0xFE: { /* INC/DEC rm8 */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		uint8_t v = get8_rm(c, &rm);
		int cf = (c->flags & FLAG_CF) != 0;
		uint8_t r = reg == 0 ? (uint8_t)(v + 1) : (uint8_t)(v - 1);
		cpu_set_flags(c, r, 8);
		set_cf(c, cf);
		set_of(c, reg == 0 ? r == 0x80 : r == 0x7F);
		put8_rm(c, &rm, r);
		return CPU_OK;
	}
	case 0xFF: { /* INC/DEC/CALL/JMP/PUSH rm16 */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override) {
			rm.seg = override;
		}
		switch (reg) {
		case 0: case 1: {
			uint16_t v = get16(c, &rm);
			int cf = (c->flags & FLAG_CF) != 0;
			uint16_t r = reg == 0 ? (uint16_t)(v + 1) : (uint16_t)(v - 1);
			cpu_set_flags(c, r, 16);
			set_cf(c, cf);
			set_of(c, reg == 0 ? r == 0x8000 : r == 0x7FFF);
			put16(c, &rm, r);
			break;
		}
		case 2: { /* CALL near */
			uint16_t target = get16(c, &rm);
			push16(c, c->ip);
			c->ip = target;
			break;
		}
		case 3: { /* CALL far */
			uint16_t off = rd16(c, rm.seg, rm.off);
			uint16_t seg = rd16(c, rm.seg, (uint16_t)(rm.off + 2));
			push16(c, c->sreg[1]);
			push16(c, c->ip);
			c->ip = off;
			c->sreg[1] = seg;
			break;
		}
		case 4: c->ip = get16(c, &rm); break; /* JMP near */
		case 5: { /* JMP far */
			uint16_t off = rd16(c, rm.seg, rm.off);
			uint16_t seg = rd16(c, rm.seg, (uint16_t)(rm.off + 2));
			c->ip = off;
			c->sreg[1] = seg;
			break;
		}
		case 6: push16(c, get16(c, &rm)); break; /* PUSH rm */
		default: return CPU_FAULT;
		}
		return CPU_OK;
	}
	}
	return CPU_FAULT;
}
