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
	uint32_t mask = bits == 8 ? 0xFF : bits == 16 ? 0xFFFF : UINT32_MAX;
	uint32_t sign = bits == 8 ? 0x80 : bits == 16 ? 0x8000 : 0x80000000u;
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
	return (rm == 2 || rm == 3 || rm == 6) ? c->sreg[2] : c->sreg[3];
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

/* Register bit indexes address signed memory bit strings. */
static int bit_address(struct operand *rm, int32_t index, int bits)
{
	if (rm->is_reg) return 1;
	int32_t words = index / bits;
	if (index < 0 && index % bits) words--;
	int32_t off = (int32_t)rm->off + words * (bits / 8);
	if (off < 0 || off > 0x10000 - bits / 8) return 0;
	rm->off = (uint16_t)off;
	return 1;
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

/* The 386 masks counts before execution, including byte/word operands.
 * Iterate at most 31 times to avoid C shifts by the operand width. */
static uint32_t shift(struct cpu *c, int op, uint32_t v, unsigned count, int bits)
{
	uint32_t mask = bits == 32 ? UINT32_MAX : (1u << bits) - 1;
	uint32_t sign = 1u << (bits - 1), r = v & mask;
	count &= 31;
	unsigned steps = count;
	if ((op == 2 || op == 3) && bits < 32) steps %= bits + 1;
	if (!steps) return r;
	for (unsigned i = 0; i < steps; i++) {
		unsigned cf = (c->flags & FLAG_CF) != 0;
		switch (op) {
		case 0: set_cf(c, r & sign); r = ((r << 1) | (r >> (bits - 1))) & mask; break;
		case 1: set_cf(c, r & 1); r = (r >> 1) | ((r & 1) ? sign : 0); break;
		case 2: set_cf(c, r & sign); r = ((r << 1) | cf) & mask; break;
		case 3: set_cf(c, r & 1); r = (r >> 1) | (cf ? sign : 0); break;
		case 4: set_cf(c, r & sign); r = (r << 1) & mask; break;
		case 5: set_cf(c, r & 1); r >>= 1; break;
		case 7: set_cf(c, r & 1); r = (r >> 1) | (r & sign); break;
		}
	}
	if (op >= 4) cpu_set_flags(c, r, bits);
	/* OF is defined only for a one-bit count; preserve it otherwise. */
	if (count == 1) {
		if (op == 0 || op == 2 || op == 4)
			set_of(c, !!(r & sign) ^ !!(c->flags & FLAG_CF));
		else if (op == 1 || op == 3)
			set_of(c, !!(r & sign) ^ !!(r & (sign >> 1)));
		else set_of(c, op == 5 && (v & sign));
	}
	return r;
}

/* SHLD/SHRD consume source bits without modifying the source register.
 * Word counts above 16 are undefined by x86; use deterministic zero fill. */
static uint32_t double_shift(struct cpu *c, uint32_t value, uint32_t source,
                             unsigned count, int bits, int right)
{
	count &= 31;
	if (!count) return value;
	uint32_t mask = bits == 32 ? UINT32_MAX : 0xFFFF;
	uint32_t sign = 1u << (bits - 1), original = value;
	for (unsigned i = 0; i < count; i++) {
		if (right) {
			set_cf(c, value & 1);
			value = (value >> 1) | ((source & 1) ? sign : 0);
			source >>= 1;
		} else {
			set_cf(c, value & sign);
			value = ((value << 1) | !!(source & sign)) & mask;
			source = (source << 1) & mask;
		}
	}
	cpu_set_flags(c, value, bits);
	if (count == 1) set_of(c, (original ^ value) & sign);
	return value;
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
static void do_string(struct cpu *c, uint8_t op, uint8_t rep, uint16_t source)
{
	int df = (c->flags & FLAG_DF) ? -1 : 1;
	int w = (op & 1) ? 2 : 1; /* the low bit: byte or word */
	uint8_t sub = op & 0xFE;
	if (rep && !c->r[CX]) return;

	do {
		switch (sub) {
		case 0xA4: { /* MOVS */
			if (w == 2) {
				wr16(c, c->sreg[0], c->r[DI], rd16(c, source, c->r[SI]));
			} else {
				wr8(c, c->sreg[0], c->r[DI], rd8(c, source, c->r[SI]));
			}
			c->r[SI] = (uint16_t)(c->r[SI] + df * w);
			c->r[DI] = (uint16_t)(c->r[DI] + df * w);
			break;
		}
		case 0xA6: { /* CMPS */
			uint16_t a = w == 2 ? rd16(c, source, c->r[SI]) : rd8(c, source, c->r[SI]);
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
		case 0xAA: { /* STOS */
			if (w == 2) {
				wr16(c, c->sreg[0], c->r[DI], c->r[AX]);
			} else {
				wr8(c, c->sreg[0], c->r[DI], get8(c, 0));
			}
			c->r[DI] = (uint16_t)(c->r[DI] + df * w);
			break;
		}
		case 0xAC: { /* LODS */
			if (w == 2) {
				c->r[AX] = rd16(c, source, c->r[SI]);
			} else {
				put8(c, 0, rd8(c, source, c->r[SI]));
			}
			c->r[SI] = (uint16_t)(c->r[SI] + df * w);
			break;
		}
		case 0xAE: { /* SCAS */
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
		if (sub == 0xA6 || sub == 0xAE) {
			if (n == 0 || ((c->flags & FLAG_ZF) != 0) != (rep == 0xF3)) { /* REPE/REPNE */
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

/* The 386's two-byte instructions (the 0F prefix). Only what a DOS
 * program and its libraries use is here; the rest stops with a fault. */
static enum cpu_stop op_0f(struct cpu *c, int override)
{
	uint8_t op = rd8(c, c->sreg[1], c->ip++);
	c->fault_opcode = op;

	switch (op) {
	case 0x00: case 0x01: { /* the group 6: SLDT, STR, LLDT, LTR, VERR,
	                         * VERW -- protected mode, not for a DOS
	                         * program; refuse rather than mis-run. */
		return CPU_FAULT;
	}
	case 0x02: case 0x03: { /* LAR, LSL: also protected mode */
		return CPU_FAULT;
	}
	case 0x06: return CPU_FAULT; /* CLTS requires CR0.TS state. */
	case 0x08: case 0x09: /* INVD, WBINVD: no cache to flush */
		return CPU_OK;
	case 0x0B: return CPU_FAULT; /* UD2 */
	case 0x20: case 0x21: case 0x22: case 0x23: /* MOV to/from CR/DR */
		return CPU_FAULT;
	case 0x30: return CPU_FAULT; /* WRMSR: no modeled MSR bank. */
	case 0x31: /* RDTSC: synthetic instruction counter in EDX:EAX. */
		cpu_put_reg32(c, AX, c->insns);
		cpu_put_reg32(c, DX, 0);
		return CPU_OK;
	case 0x40: case 0x41: case 0x42: case 0x43: /* CMOVcc */
	case 0x44: case 0x45: case 0x46: case 0x47:
	case 0x48: case 0x49: case 0x4A: case 0x4B:
	case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
			rm.seg = override;
		}
		uint16_t v = get16(c, &rm);
		if (cond(c, op & 0x0F)) {
			c->r[reg] = v;
		}
		return CPU_OK;
	}
	case 0x80: case 0x81: case 0x82: case 0x83: /* Jcc rel16 */
	case 0x84: case 0x85: case 0x86: case 0x87:
	case 0x88: case 0x89: case 0x8A: case 0x8B:
	case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
		int16_t rel = (int16_t)rd16(c, c->sreg[1], c->ip);
		c->ip += 2;
		if (cond(c, op & 0x0F)) {
			c->ip = (uint16_t)(c->ip + rel);
		}
		return CPU_OK;
	}
	case 0x90: case 0x91: case 0x92: case 0x93: /* SETcc rm8 */
	case 0x94: case 0x95: case 0x96: case 0x97:
	case 0x98: case 0x99: case 0x9A: case 0x9B:
	case 0x9C: case 0x9D: case 0x9E: case 0x9F: {
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
			rm.seg = override;
		}
		put8_rm(c, &rm, cond(c, op & 0x0F) ? 1 : 0);
		return CPU_OK;
	}
	case 0xA0: case 0xA1: return CPU_FAULT; /* FS is not modeled. */
	case 0xA2: { /* CPUID always reads/writes full 32-bit registers. */
		uint32_t leaf = cpu_reg32(c, AX);
		cpu_put_reg32(c, AX, leaf == 0 ? 1 : leaf == 1 ? 0x400 : 0);
		cpu_put_reg32(c, BX, leaf == 0 ? 0x756E6547 : 0);
		cpu_put_reg32(c, DX, leaf == 0 ? 0x49656E69 : 0);
		cpu_put_reg32(c, CX, leaf == 0 ? 0x6C65746E : 0);
		/* No x87, TSC or protected-mode features are advertised. */
		return CPU_OK;
	}
	case 0xA3: case 0xAB: case 0xB3: case 0xBB: case 0xBA: {
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) rm.seg = override;
		if (op == 0xBA && reg < 4) return CPU_FAULT;
		int32_t index = op == 0xBA ? rd8(c, c->sreg[1], c->ip++) & 15 : (int16_t)c->r[reg];
		if (!bit_address(&rm, index, 16)) return CPU_FAULT;
		uint16_t v = get16(c, &rm), bit = 1u << (index & 15);
		int operation = op == 0xBA ? reg - 4 : (op >> 3) & 3;
		set_cf(c, v & bit);
		if (operation) put16(c, &rm, operation == 1 ? v | bit : operation == 2 ? v & ~bit : v ^ bit);
		return CPU_OK;
	}
	case 0xA4: case 0xA5: case 0xAC: case 0xAD: {
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) rm.seg = override;
		unsigned count = op & 1 ? c->r[CX] : rd8(c, c->sreg[1], c->ip++);
		put16(c, &rm, double_shift(c, get16(c, &rm), c->r[reg], count, 16, !!(op & 8)));
		return CPU_OK;
	}
	case 0xA8: case 0xA9: return CPU_FAULT; /* GS is not modeled. */
	case 0xAF: { /* IMUL reg16, rm16 */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
			rm.seg = override;
		}
		int32_t r = (int16_t)c->r[reg] * (int16_t)get16(c, &rm);
		c->r[reg] = (uint16_t)r;
		set_cf(c, r != (int16_t)r);
		set_of(c, r != (int16_t)r);
		return CPU_OK;
	}
	case 0xB0: case 0xB1: { /* CMPXCHG rm8/rm16, reg */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) rm.seg = override;
		if (op == 0xB1) {
			uint16_t v = get16(c, &rm), a = c->r[AX];
			alu16(c, CMP, a, v);
			if (a == v) put16(c, &rm, c->r[reg]);
			else c->r[AX] = v;
		} else {
			uint8_t v = get8_rm(c, &rm), a = get8(c, 0);
			alu8(c, CMP, a, v);
			if (a == v) put8_rm(c, &rm, get8(c, reg));
			else put8(c, 0, v);
		}
		return CPU_OK;
	}
	case 0xB6: case 0xB7: { /* MOVZX reg16, rm8/rm16 */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
			rm.seg = override;
		}
		c->r[reg] = op == 0xB7 ? get16(c, &rm) : get8_rm(c, &rm);
		return CPU_OK;
	}
	case 0xBC: case 0xBD: { /* BSF/BSR reg16, rm16 */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
			rm.seg = override;
		}
		uint16_t v = get16(c, &rm);
		if (v == 0) {
			c->flags |= FLAG_ZF;
		} else {
			c->flags &= ~FLAG_ZF;
			int i = 0;
			if (op == 0xBC) { /* BSF: the lowest set bit */
				while (!((v >> i) & 1)) {
					i++;
				}
			} else { /* BSR: the highest set bit */
				for (i = 15; !((v >> i) & 1); i--) {
				}
			}
			c->r[reg] = (uint16_t)i;
		}
		return CPU_OK;
	}
	case 0xBE: case 0xBF: { /* MOVSX reg16, rm8/rm16 */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
			rm.seg = override;
		}
		c->r[reg] = op == 0xBF ? get16(c, &rm) : (uint16_t)(int16_t)(int8_t)get8_rm(c, &rm);
		return CPU_OK;
	}
	case 0xC0: case 0xC1: { /* XADD rm8/rm16, reg */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
			rm.seg = override;
		}
		if (op == 0xC1) {
			uint16_t v = get16(c, &rm);
			uint16_t r = alu16(c, ADD, v, c->r[reg]);
			c->r[reg] = v;
			put16(c, &rm, r);
		} else {
			uint8_t v = get8_rm(c, &rm);
			uint8_t r = alu8(c, ADD, v, get8(c, reg));
			put8(c, reg, v);
			put8_rm(c, &rm, r);
		}
		return CPU_OK;
	}
	case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD:
	case 0xCE: case 0xCF: { /* BSWAP reg32: the low 16 bits swap, the
	                         * high 16 are zero in an 8086 view */
		int reg = op & 7;
		uint16_t v = c->r[reg];
		c->r[reg] = (uint16_t)((v >> 8) | (v << 8));
		return CPU_OK;
	}
	default:
		return CPU_FAULT;
	}
}

/* 386 operand-size subset in real mode. Address and stack sizes remain 16.
 * Decode and validate the complete form before changing architectural state.
 * Unsupported control transfers, strings, x87 and system forms fault. */
static uint32_t read32(struct cpu *c, uint16_t seg, uint16_t off)
{
	return (uint32_t)mem_r16(c, lin(seg, off)) |
	       ((uint32_t)mem_r16(c, lin(seg, off) + 2) << 16);
}

static void write32(struct cpu *c, uint16_t seg, uint16_t off, uint32_t v)
{
	mem_w16(c, lin(seg, off), (uint16_t)v);
	mem_w16(c, lin(seg, off) + 2, (uint16_t)(v >> 16));
}

static uint32_t get32(struct cpu *c, const struct operand *o)
{
	return o->is_reg ? cpu_reg32(c, o->reg) : read32(c, o->seg, o->off);
}

static void put32(struct cpu *c, const struct operand *o, uint32_t v)
{
	if (o->is_reg) cpu_put_reg32(c, o->reg, v);
	else write32(c, o->seg, o->off, v);
}

static uint32_t alu32(struct cpu *c, int op, uint32_t a, uint32_t b)
{
	uint32_t r;
	uint64_t wide;
	unsigned carry = (c->flags & FLAG_CF) != 0;
	if (op == ADD || op == ADC) {
		wide = (uint64_t)a + b + (op == ADC ? carry : 0);
		r = (uint32_t)wide;
		set_cf(c, wide > UINT32_MAX);
		set_of(c, (~(a ^ b) & (a ^ r) & 0x80000000u) != 0);
		set_af(c, ((a ^ b ^ r) & 0x10) != 0);
	} else if (op == SUB || op == SBB || op == CMP) {
		wide = (uint64_t)b + (op == SBB ? carry : 0);
		r = a - (uint32_t)wide;
		set_cf(c, (uint64_t)a < wide);
		set_of(c, ((a ^ b) & (a ^ r) & 0x80000000u) != 0);
		set_af(c, ((a ^ b ^ r) & 0x10) != 0);
	} else {
		r = op == AND ? a & b : op == OR ? a | b : a ^ b;
		set_cf(c, 0); set_of(c, 0); set_af(c, 0); /* AF undefined; deterministic. */
	}
	cpu_set_flags(c, r, 32);
	return r;
}

/* Address size controls CX/SI/DI; 66h changes only the element to a
 * dword. Completed REP iterations remain committed if a later access faults. */
static enum cpu_stop string32(struct cpu *c, uint8_t op, uint8_t rep, int override)
{
	if (rep && !c->r[CX]) return CPU_OK;
	int delta = c->flags & FLAG_DF ? -4 : 4;
	do {
		if ((op == 0xA5 || op == 0xAD) && c->r[SI] > 0xFFFC) return CPU_FAULT;
		if ((op == 0xA5 || op == 0xAB) && c->r[DI] > 0xFFFC) return CPU_FAULT;
		uint32_t value = op == 0xAB ? cpu_reg32(c, AX) :
			read32(c, override >= 0 ? override : c->sreg[3], c->r[SI]);
		if (op == 0xAD) cpu_put_reg32(c, AX, value);
		else write32(c, c->sreg[0], c->r[DI], value);
		if (op != 0xAB) c->r[SI] = (uint16_t)(c->r[SI] + delta);
		if (op != 0xAD) c->r[DI] = (uint16_t)(c->r[DI] + delta);
		if (rep) c->r[CX]--;
	} while (rep && c->r[CX]);
	return CPU_OK;
}

static enum cpu_stop divide_error(struct cpu *c, uint16_t start)
{
	c->ip = start; /* 386 divide error is a fault, saved IP names the instruction. */
	c->int_no = 0;
	uint16_t cs, ip;
	cpu_enter_interrupt(c, 0, &cs, &ip);
	c->sreg[1] = cs; c->ip = ip;
	return CPU_INT;
}

/* Divide a 64-bit magnitude by a dword without a freestanding libgcc
 * division dependency. The quotient must fit in 32 bits before starting. */
static enum cpu_stop divide32(struct cpu *c, uint32_t divisor, int is_signed, uint16_t start)
{
	uint64_t dividend = ((uint64_t)cpu_reg32(c, DX) << 32) | cpu_reg32(c, AX);
	int negative = is_signed && (dividend >> 63);
	int neg_divisor = is_signed && (divisor & 0x80000000u);
	if (negative) dividend = ~dividend + 1;
	if (neg_divisor) divisor = 0u - divisor;
	uint64_t remainder = dividend >> 32;
	uint32_t quotient = 0, low = (uint32_t)dividend;
	if (!divisor || remainder >= divisor) return divide_error(c, start);
	for (int i = 31; i >= 0; i--) {
		remainder = (remainder << 1) | ((low >> i) & 1);
		if (remainder >= divisor) { remainder -= divisor; quotient |= 1u << i; }
	}
	int neg_quotient = negative != neg_divisor;
	if (is_signed && quotient > (neg_quotient ? 0x80000000u : 0x7FFFFFFFu))
		return divide_error(c, start);
	cpu_put_reg32(c, AX, neg_quotient ? 0u - quotient : quotient);
	cpu_put_reg32(c, DX, negative ? 0u - (uint32_t)remainder : (uint32_t)remainder);
	return CPU_OK;
}

static enum cpu_stop op32(struct cpu *c, uint8_t op, int override, uint16_t start)
{
	int reg = 0, immediate = 0, modrm = 0, ext = -1;
	struct operand rm = {0};
	uint32_t a, b, value;
	int alu = op < 0x40 && ((op & 7) == 1 || (op & 7) == 3 || (op & 7) == 5);
	if (alu) {
		modrm = (op & 7) != 5;
		immediate = modrm ? 0 : 4;
	} else if (op >= 0xB8 && op <= 0xBF) immediate = 4;
	else if (op >= 0x40 && op <= 0x5F) { /* INC/DEC/PUSH/POP register */ }
	else if (op >= 0x90 && op <= 0x99) { /* XCHG, CWDE, CDQ */ }
	else switch (op) {
	case 0x9C: case 0x9D: break; /* PUSHFD / POPFD */
	case 0x68: immediate = 4; break;
	case 0x69: modrm = 1; immediate = 4; break;
	case 0x6B: modrm = 1; immediate = 1; break;
	case 0x6A: immediate = 1; break;
	case 0x81: modrm = 1; immediate = 4; break;
	case 0x83: modrm = 1; immediate = 1; break;
	case 0x85: case 0x87: case 0x89: case 0x8B: case 0x8D:
	case 0x8F: case 0xF7: case 0xFF: modrm = 1; break;
	case 0xA1: case 0xA3: immediate = 2; break; /* moffs stays 16-bit */
	case 0xA9: immediate = 4; break;
	case 0xC7: modrm = 1; immediate = 4; break;
	case 0xC1: modrm = 1; immediate = 1; break;
	case 0xD1: case 0xD3: modrm = 1; break;
	case 0x0F:
		ext = rd8(c, c->sreg[1], c->ip++);
		if (ext == 0xB6 || ext == 0xB7 || ext == 0xBE || ext == 0xBF || ext == 0xAF) modrm = 1;
		else if (ext == 0xA3 || ext == 0xAB || ext == 0xB3 || ext == 0xBB || ext == 0xBA) { modrm = 1; immediate = ext == 0xBA; }
		else if (ext == 0xA4 || ext == 0xA5 || ext == 0xAC || ext == 0xAD) { modrm = 1; immediate = (ext & 1) ? 0 : 1; }
		else if (ext != 0xA2 && (ext < 0xC8 || ext > 0xCF)) return CPU_FAULT;
		break;
	default: return CPU_FAULT;
	}
	if (modrm) {
		rm = decode_rm(c, &reg);
		if (override >= 0) rm.seg = (uint16_t)override;
		if ((op == 0xC1 || op == 0xD1 || op == 0xD3) && reg == 6) return CPU_FAULT;
		if (op == 0xC7 && reg != 0) return CPU_FAULT;
		if (op == 0x8F && reg != 0) return CPU_FAULT;
		if (op == 0x8D && rm.is_reg) return CPU_FAULT;
		if (op == 0xFF && reg != 0 && reg != 1 && reg != 6) return CPU_FAULT;
		if (ext == 0xBA && reg < 4) return CPU_FAULT;
		if (ext == 0xA3 || ext == 0xAB || ext == 0xB3 || ext == 0xBB)
			if (!bit_address(&rm, (int32_t)cpu_reg32(c, reg), 32)) return CPU_FAULT;
		if (op == 0xF7) {
			if (reg == 0) immediate = 4;
			else if (reg == 1) return CPU_FAULT;
		}
		/* Real-mode segment limits: no dword may straddle offset FFFF.
		 * LEA does not access memory; extensions read a byte or word. */
		unsigned width = ext == 0xB6 || ext == 0xBE ? 1 : ext == 0xB7 || ext == 0xBF ? 2 : 4;
		if (!rm.is_reg && op != 0x8D && (unsigned)rm.off + width > 0x10000u) return CPU_FAULT;
	}
	if ((unsigned)(uint16_t)(c->ip - start) + immediate > 15) return CPU_FAULT;
	/* A 16-bit stack address uses SP, even for PUSH/POP of a dword. */
	int pushing = (op >= 0x50 && op <= 0x57) || op == 0x9C || op == 0x68 || op == 0x6A || (op == 0xFF && reg == 6);
	int popping = (op >= 0x58 && op <= 0x5F) || op == 0x8F || op == 0x9D;
	if (pushing && (uint16_t)(c->r[SP] - 4) > 0xFFFC) return CPU_FAULT;
	if (popping && c->r[SP] > 0xFFFC) return CPU_FAULT;
	b = 0;
	if (immediate == 4) b = (uint32_t)rd16(c, c->sreg[1], c->ip) |
		((uint32_t)rd16(c, c->sreg[1], (uint16_t)(c->ip + 2)) << 16);
	else if (immediate == 2) b = rd16(c, c->sreg[1], c->ip);
	else if (immediate == 1) b = (uint32_t)(int32_t)(int8_t)rd8(c, c->sreg[1], c->ip);
	if ((op == 0xA1 || op == 0xA3) && b > 0xFFFC) return CPU_FAULT;
	c->ip += immediate;
	if (op == 0x69 || op == 0x6B || ext == 0xAF) {
		int64_t product = (int64_t)(int32_t)get32(c, &rm) *
		                  (int32_t)(ext == 0xAF ? cpu_reg32(c, reg) : b);
		value = (uint32_t)product;
		cpu_put_reg32(c, reg, value);
		set_cf(c, product != (int32_t)value); set_of(c, product != (int32_t)value);
		return CPU_OK;
	}
	if (alu) {
		int operation = (op >> 3) & 7;
		int direction = (op & 7) == 3;
		a = modrm ? (direction ? cpu_reg32(c, reg) : get32(c, &rm)) : cpu_reg32(c, AX);
		if (modrm) b = direction ? get32(c, &rm) : cpu_reg32(c, reg);
		value = alu32(c, operation, a, b);
		if (operation != CMP) {
			if (!modrm || direction) cpu_put_reg32(c, modrm ? reg : AX, value);
			else put32(c, &rm, value);
		}
	} else if (op >= 0xB8 && op <= 0xBF) cpu_put_reg32(c, op & 7, b);
	else if (op >= 0x40 && op <= 0x4F) {
		unsigned cf = c->flags & FLAG_CF;
		value = alu32(c, op < 0x48 ? ADD : SUB, cpu_reg32(c, op & 7), 1);
		cpu_put_reg32(c, op & 7, value);
		set_cf(c, cf);
	} else if (pushing) {
		value = op == 0x9C ? ((c->flags & 0x7FD7u) | 2u | ((uint32_t)(c->flags_hi & 0x24) << 16)) :
		        op == 0x68 || op == 0x6A ? b : op == 0xFF ? get32(c, &rm) : cpu_reg32(c, op & 7);
		c->r[SP] -= 4;
		write32(c, c->sreg[2], c->r[SP], value);
	} else if (popping) {
		value = read32(c, c->sreg[2], c->r[SP]);
		c->r[SP] += 4;
		if (op == 0x9D) {
			c->flags = (value & 0x7FD7u) | 2;
			c->flags_hi = (value >> 16) & 0x24;
		} else if (op == 0x8F) put32(c, &rm, value);
		else cpu_put_reg32(c, op & 7, value);
	} else if (op >= 0x90 && op <= 0x97) {
		value = cpu_reg32(c, AX); a = cpu_reg32(c, op & 7);
		cpu_put_reg32(c, AX, a); cpu_put_reg32(c, op & 7, value);
	} else switch (op) {
	case 0x98: cpu_put_reg32(c, AX, (uint32_t)(int32_t)(int16_t)c->r[AX]); break;
	case 0x99: cpu_put_reg32(c, DX, c->r_hi[AX] & 0x8000 ? UINT32_MAX : 0); break;
	case 0x81: case 0x83:
		value = alu32(c, reg, get32(c, &rm), b);
		if (reg != CMP) put32(c, &rm, value);
		break;
	case 0x85: alu32(c, AND, get32(c, &rm), cpu_reg32(c, reg)); break;
	case 0x87:
		a = get32(c, &rm); b = cpu_reg32(c, reg);
		put32(c, &rm, b); cpu_put_reg32(c, reg, a); break;
	case 0x89: put32(c, &rm, cpu_reg32(c, reg)); break;
	case 0x8B: cpu_put_reg32(c, reg, get32(c, &rm)); break;
	case 0x8D: cpu_put_reg32(c, reg, rm.off); break;
	case 0xA1: cpu_put_reg32(c, AX, read32(c, override >= 0 ? override : c->sreg[3], (uint16_t)b)); break;
	case 0xA3: write32(c, override >= 0 ? override : c->sreg[3], (uint16_t)b, cpu_reg32(c, AX)); break;
	case 0xA9: alu32(c, AND, cpu_reg32(c, AX), b); break;
	case 0xC7: put32(c, &rm, b); break;
	case 0xC1: case 0xD1: case 0xD3:
		put32(c, &rm, shift(c, reg, get32(c, &rm), op == 0xD1 ? 1 : op == 0xD3 ? c->r[CX] : b, 32));
		break;
	case 0xF7:
		a = get32(c, &rm);
		if (reg == 0) alu32(c, AND, a, b);
		else if (reg == 2 || reg == 3) put32(c, &rm, reg == 2 ? ~a : alu32(c, SUB, 0, a));
		else if (reg >= 6) return divide32(c, a, reg == 7, start);
		else {
			uint64_t product = reg == 4 ? (uint64_t)cpu_reg32(c, AX) * a :
			                  (uint64_t)((int64_t)(int32_t)cpu_reg32(c, AX) * (int32_t)a);
			cpu_put_reg32(c, AX, (uint32_t)product); cpu_put_reg32(c, DX, (uint32_t)(product >> 32));
			int overflow = reg == 4 ? (product >> 32) != 0 : (int64_t)product != (int32_t)product;
			set_cf(c, overflow); set_of(c, overflow);
		}
		break;
	case 0xFF: {
		unsigned cf = c->flags & FLAG_CF;
		put32(c, &rm, alu32(c, reg == 0 ? ADD : SUB, get32(c, &rm), 1));
		set_cf(c, cf); break;
	}
	case 0x0F:
		if (ext == 0xA2) { c->ip--; return op_0f(c, override); }
		if (ext == 0xA3 || ext == 0xAB || ext == 0xB3 || ext == 0xBB || ext == 0xBA) {
			unsigned bit = 1u << ((ext == 0xBA ? b : cpu_reg32(c, reg)) & 31);
			int operation = ext == 0xBA ? reg - 4 : (ext >> 3) & 3;
			a = get32(c, &rm); set_cf(c, a & bit);
			if (operation) put32(c, &rm, operation == 1 ? a | bit : operation == 2 ? a & ~bit : a ^ bit);
		} else if (ext == 0xA4 || ext == 0xA5 || ext == 0xAC || ext == 0xAD) {
			put32(c, &rm, double_shift(c, get32(c, &rm), cpu_reg32(c, reg), ext & 1 ? c->r[CX] : b, 32, !!(ext & 8)));
		} else if (ext >= 0xC8) {
			a = cpu_reg32(c, ext & 7);
			cpu_put_reg32(c, ext & 7, (a >> 24) | ((a >> 8) & 0xFF00) | ((a << 8) & 0xFF0000) | (a << 24));
		} else {
			if (ext == 0xB6) value = get8_rm(c, &rm);
			else if (ext == 0xB7) value = get16(c, &rm);
			else if (ext == 0xBE) value = (uint32_t)(int32_t)(int8_t)get8_rm(c, &rm);
			else value = (uint32_t)(int32_t)(int16_t)get16(c, &rm);
			cpu_put_reg32(c, reg, value);
		}
		break;
	}
	return CPU_OK;
}

const char *cpu_fault_name(uint8_t opcode)
{
	static char buf[32];
	/* A short mnemonic for the common ones, else the number. */
	switch (opcode) {
	case 0x0F: return "two-byte opcode (0F)";
	case 0x0B: return "UD2";
	case 0x27: return "DAA";
	case 0x2F: return "DAS";
	case 0x37: return "AAA";
	case 0x3F: return "AAS";
	case 0xD4: return "AAM";
	case 0xD5: return "AAD";
	case 0xD7: return "XLAT";
	case 0x66: return "unsupported operand-size override (66)";
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
	int inhibited = c->irq_shadow != 0;
	if (inhibited) c->irq_shadow--;
	if (!inhibited && (c->flags & FLAG_IF)) {
		int vector = c->irq_ack ? c->irq_ack(c) : -1;
		if (vector < 0 && c->irq_pending) {
			for (int i = 0; i < 16; i++) if (c->irq_pending & (1u << i)) {
				c->irq_pending &= (uint16_t)~(1u << i);
				vector = i < 8 ? 0x08 + i : 0x70 + i - 8;
				break;
			}
		}
		if (vector >= 0) {
			c->int_no = (uint8_t)vector;
			uint16_t cs, ip;
			cpu_enter_interrupt(c, c->int_no, &cs, &ip);
			c->sreg[1] = cs; c->ip = ip;
			return CPU_INT;
		}
	}
	/* Segment and repeat prefixes may occur in either order. */
	int override = -1, operand32 = 0;
	uint16_t instruction_ip = c->ip;
	uint8_t op, rep = 0;
	int prefixes = 0;
	for (;;) {
		op = rd8(c, c->sreg[1], c->ip++);
		if (++prefixes > 15) {
			c->fault_opcode = operand32 ? 0x66 : op;
			c->ip = instruction_ip;
			return CPU_FAULT;
		}
		if (op == 0x66) { operand32 = 1; continue; }
		if (op == 0x26) { override = c->sreg[0]; continue; }
		if (op == 0x2E) { override = c->sreg[1]; continue; }
		if (op == 0x36) { override = c->sreg[2]; continue; }
		if (op == 0x3E) { override = c->sreg[3]; continue; }
		if (op == 0xF2 || op == 0xF3) { rep = op; continue; }
		break;
	}
	c->insns++;
	c->fault_opcode = op;
	if (operand32) {
		enum cpu_stop result = (op == 0xA5 || op == 0xAB || op == 0xAD) ?
			string32(c, op, rep, override) :
			rep ? CPU_FAULT : op32(c, op, override, instruction_ip);
		if (result == CPU_FAULT) { c->fault_opcode = 0x66; c->ip = instruction_ip; }
		return result;
	}
	if ((op >= 0xA4 && op <= 0xA7) || (op >= 0xAA && op <= 0xAF)) {
		do_string(c, op, rep, override >= 0 ? override : c->sreg[3]);
		return CPU_OK;
	}
	if (rep) return CPU_FAULT;

	/* The 386's two-byte instructions (0F xx): what DOS programs and
	 * their libraries use -- MOVZX, MOVSX, SETcc, the bit operations,
	 * the double shifts, XADD, CMPXCHG, BSWAP and CPUID. */
	if (op == 0x0F) {
		return op_0f(c, override);
	}

	/* The ALU group: 00-3F, with the direction and size bits. The low
	 * three bits 0-3 are the rm,reg forms; 4-5 are the accumulator
	 * forms (below), and 6-7 are the segment prefixes (already read). */
	if (op < 0x40 && (op & 7) <= 3) {
		int alu_op = (op >> 3) & 7;
		int w = op & 1;
		int dir = op & 2; /* 0: rm,reg  2: reg,rm */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
			rm.seg = override;
		}
		if (w) {
			uint16_t a = dir ? c->r[reg] : get16(c, &rm);
			uint16_t b = dir ? get16(c, &rm) : c->r[reg];
			uint16_t r = alu16(c, alu_op, a, b);
			if (alu_op != CMP) {
				if (dir) {
					c->r[reg] = r;
				} else {
					put16(c, &rm, r);
				}
			}
		} else {
			uint8_t a = dir ? get8(c, reg) : get8_rm(c, &rm);
			uint8_t b = dir ? get8_rm(c, &rm) : get8(c, reg);
			uint8_t r = alu8(c, alu_op, a, b);
			if (alu_op != CMP) {
				if (dir) {
					put8(c, reg, r);
				} else {
					put8_rm(c, &rm, r);
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
			set_af(c, (v & 15) == 15);
			c->r[r] = res;
		} else if (op < 0x50) { /* DEC */
			uint16_t v = c->r[r];
			uint16_t res = (uint16_t)(v - 1);
			int cf = (c->flags & FLAG_CF) != 0;
			cpu_set_flags(c, res, 16);
			set_cf(c, cf);
			set_of(c, res == 0x7FFF);
			set_af(c, (v & 15) == 0);
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
	case 0x17: c->sreg[2] = pop16(c); c->irq_shadow = 1; return CPU_OK;      /* POP SS */
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
			uint16_t value = pop16(c);
			if (i != SP) c->r[i] = value;
		}
		return CPU_OK;
	}
	case 0x68: push16(c, rd16(c, c->sreg[1], c->ip)); c->ip += 2; return CPU_OK; /* PUSH imm16 */
	case 0x6A: push16(c, (uint16_t)(int16_t)(int8_t)rd8(c, c->sreg[1], c->ip++)); return CPU_OK; /* PUSH imm8 */
	case 0x69: case 0x6B: { /* IMUL r, rm, imm */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
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
		if (override >= 0) {
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
		if (override >= 0) {
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
		if (override >= 0) {
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
		if (override >= 0) {
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
		if (override >= 0) {
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
		if (override >= 0) {
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
		if (override >= 0) {
			rm.seg = override;
		}
		c->sreg[reg & 3] = get16(c, &rm);
		if ((reg & 3) == 2) c->irq_shadow = 1;
		return CPU_OK;
	}
	case 0x8F: { /* POP rm */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
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
	case 0x9A: { /* CALL ptr16:16 */
		uint16_t off = rd16(c, c->sreg[1], c->ip);
		uint16_t seg = rd16(c, c->sreg[1], c->ip + 2);
		c->ip += 4;
		push16(c, c->sreg[1]); push16(c, c->ip);
		c->sreg[1] = seg; c->ip = off;
		return CPU_OK;
	}
	case 0x98: c->r[AX] = (uint16_t)(int16_t)(int8_t)c->r[AX]; return CPU_OK; /* CBW */
	case 0x99: c->r[DX] = (c->r[AX] & 0x8000) ? 0xFFFF : 0; return CPU_OK;    /* CWD */
	case 0x9C: push16(c, (c->flags & 0x7FD7) | 2); return CPU_OK;  /* PUSHF */
	case 0x9D: c->flags = (pop16(c) & 0x7FD7) | 2; return CPU_OK;  /* POPF */
	case 0x9E: c->flags = (uint16_t)((c->flags & ~0x00D5) | ((c->r[AX] >> 8) & 0xD5) | 0x02); return CPU_OK; /* SAHF */
	case 0x9F: c->r[AX] = (uint16_t)((c->r[AX] & 0x00FF) | (((c->flags & 0xD5) | 0x02) << 8)); return CPU_OK; /* LAHF */
	case 0x27: { /* DAA: packed-decimal adjustment after addition. */
		uint8_t old = get8(c, 0), value = old;
		int old_cf = (c->flags & FLAG_CF) != 0;
		int low = (old & 15) > 9 || (c->flags & FLAG_AF);
		if (low) value = (uint8_t)(value + 6);
		if (old > 0x99 || old_cf) value = (uint8_t)(value + 0x60);
		set_cf(c, old > 0x99 || old_cf);
		set_af(c, low);
		put8(c, 0, value); cpu_set_flags(c, value, 8);
		return CPU_OK;
	}
	case 0x2F: { /* DAS: decimal adjust AL after subtraction. OF is undefined. */
		uint8_t old = get8(c, 0), value = old;
		int old_cf = (c->flags & FLAG_CF) != 0;
		int adjust_low = (old & 15) > 9 || (c->flags & FLAG_AF);
		int carry = 0;
		if (adjust_low) {
			value = (uint8_t)(value - 6);
			carry = old_cf || old < 6;
		}
		if (old > 0x99 || old_cf) {
			value = (uint8_t)(value - 0x60);
			carry = 1;
		}
		set_cf(c, carry);
		if (adjust_low) c->flags |= FLAG_AF;
		else c->flags &= ~FLAG_AF;
		put8(c, 0, value);
		cpu_set_flags(c, value, 8);
		return CPU_OK;
	}
	case 0xA0: c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | rd8(c, override >= 0 ? override : c->sreg[3], rd16(c, c->sreg[1], c->ip))); c->ip += 2; return CPU_OK; /* MOV AL, moffs */
	case 0xA1: c->r[AX] = rd16(c, override >= 0 ? override : c->sreg[3], rd16(c, c->sreg[1], c->ip)); c->ip += 2; return CPU_OK; /* MOV AX, moffs */
	case 0xA2: wr8(c, override >= 0 ? override : c->sreg[3], rd16(c, c->sreg[1], c->ip), get8(c, 0)); c->ip += 2; return CPU_OK; /* MOV moffs, AL */
	case 0xA3: wr16(c, override >= 0 ? override : c->sreg[3], rd16(c, c->sreg[1], c->ip), c->r[AX]); c->ip += 2; return CPU_OK; /* MOV moffs, AX */
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
		if (override >= 0) {
			rm.seg = override;
		}
		c->r[reg] = rd16(c, rm.seg, rm.off);
		c->sreg[op == 0xC4 ? 0 : 3] = rd16(c, rm.seg, (uint16_t)(rm.off + 2));
		return CPU_OK;
	}
	case 0xC6: case 0xC7: { /* MOV rm, imm */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
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
	case 0xD4: { /* AAM imm8: unpack AL into quotient (AH) and remainder (AL). */
		uint8_t base = rd8(c, c->sreg[1], c->ip++);
		if (base == 0) {
			/* A zero base raises the 8086 divide-error interrupt.  The
			 * return address is after the two-byte AAM instruction. */
			c->int_no = 0;
			uint16_t cs, ip;
			cpu_enter_interrupt(c, c->int_no, &cs, &ip);
			c->sreg[1] = cs;
			c->ip = ip;
			return CPU_INT;
		}
		uint8_t al = get8(c, 0);
		put8(c, 4, (uint8_t)(al / base));
		put8(c, 0, (uint8_t)(al % base));
		cpu_set_flags(c, get8(c, 0), 8);
		return CPU_OK;
	}
	case 0xCE: { /* INTO: interrupt 4 only when the overflow flag is set. */
		if (!(c->flags & FLAG_OF)) return CPU_OK;
		c->int_no = 4;
		uint16_t cs, ip;
		cpu_enter_interrupt(c, c->int_no, &cs, &ip);
		c->sreg[1] = cs;
		c->ip = ip;
		return CPU_INT;
	}
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
	case 0xD5: { /* AAD imm8: fold unpacked digits into AL, clear AH. */
		uint8_t base = rd8(c, c->sreg[1], c->ip++);
		c->r[AX] = (uint8_t)(get8(c, 0) + get8(c, 4) * base);
		cpu_set_flags(c, c->r[AX], 8); /* CF/AF/OF are undefined. */
		return CPU_OK;
	}
	case 0xD7: /* XLAT: unsigned AL indexes the table at DS:BX. */
		put8(c, 0, rd8(c, override >= 0 ? override : c->sreg[3],
		              (uint16_t)(c->r[BX] + get8(c, 0))));
		return CPU_OK;
	case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: { /* shifts by 1, CL, or imm */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
			rm.seg = override;
		}
		if (reg == 6) { c->ip = instruction_ip; return CPU_FAULT; }
		int count = (op == 0xD0 || op == 0xD1) ? 1
		          : (op == 0xD2 || op == 0xD3) ? c->r[CX] : rd8(c, c->sreg[1], c->ip++);
		if (op == 0xC1 || op == 0xD1 || op == 0xD3) {
			put16(c, &rm, shift(c, reg, get16(c, &rm), count, 16));
		} else {
			put8_rm(c, &rm, shift(c, reg, get8_rm(c, &rm), count, 8));
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
	case 0xE4: case 0xEC: { /* IN AL,imm8 / IN AL,DX */
		uint16_t port = op == 0xE4 ? rd8(c, c->sreg[1], c->ip) : c->r[DX];
		uint8_t value;
		if (!c->port_in8 || c->port_in8(c, port, &value) != 0)
			return CPU_FAULT;
		if (op == 0xE4) c->ip++;
		c->r[AX] = (uint16_t)((c->r[AX] & 0xFF00) | value);
		return CPU_OK;
	}
	case 0xE6: case 0xEE: { /* OUT imm8,AL / OUT DX,AL */
		uint16_t port = op == 0xE6 ? rd8(c, c->sreg[1], c->ip) : c->r[DX];
		if (!c->port_out8 || c->port_out8(c, port, (uint8_t)c->r[AX]) != 0)
			return CPU_FAULT;
		if (op == 0xE6) c->ip++;
		return CPU_OK;
	}
	case 0xE5: case 0xE7: case 0xED: case 0xEF: /* word I/O */
		return CPU_FAULT;
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
		if (override >= 0) {
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
					return divide_error(c, instruction_ip); /* divide by zero: the caller raises INT 0 */
				}
				uint32_t n = ((uint32_t)c->r[DX] << 16) | c->r[AX];
				if (n / d > 0xFFFF) {
					return divide_error(c, instruction_ip);
				}
				c->r[AX] = (uint16_t)(n / d);
				c->r[DX] = (uint16_t)(n % d);
			} else {
				uint8_t d = get8_rm(c, &rm);
				if (d == 0) {
					return divide_error(c, instruction_ip);
				}
				uint16_t n = c->r[AX];
				if (n / d > 0xFF) {
					return divide_error(c, instruction_ip);
				}
				put8(c, 0, (uint8_t)(n / d));
				put8(c, 4, (uint8_t)(n % d));
			}
		} else { /* IDIV */
			if (w) {
				int16_t d = (int16_t)get16(c, &rm);
				if (d == 0) {
					return divide_error(c, instruction_ip);
				}
				int32_t n = (int32_t)(((uint32_t)c->r[DX] << 16) | c->r[AX]);
				if (n == INT32_MIN && d == -1) return divide_error(c, instruction_ip);
				int32_t q = n / d;
				if (q > 32767 || q < -32768) {
					return divide_error(c, instruction_ip);
				}
				c->r[AX] = (uint16_t)q;
				c->r[DX] = (uint16_t)(n % d);
			} else {
				int8_t d = (int8_t)get8_rm(c, &rm);
				if (d == 0) {
					return divide_error(c, instruction_ip);
				}
				int16_t n = (int16_t)c->r[AX];
				int16_t q = n / d;
				if (q > 127 || q < -128) {
					return divide_error(c, instruction_ip);
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
	case 0xFB: c->flags |= FLAG_IF; c->irq_shadow = 1; return CPU_OK;  /* STI */
	case 0xFC: c->flags &= ~FLAG_DF; return CPU_OK; /* CLD */
	case 0xFD: c->flags |= FLAG_DF; return CPU_OK;  /* STD */
	case 0xFE: { /* INC/DEC rm8 */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
			rm.seg = override;
		}
		uint8_t v = get8_rm(c, &rm);
		int cf = (c->flags & FLAG_CF) != 0;
		uint8_t r = reg == 0 ? (uint8_t)(v + 1) : (uint8_t)(v - 1);
		cpu_set_flags(c, r, 8);
		set_cf(c, cf);
		set_of(c, reg == 0 ? r == 0x80 : r == 0x7F);
		set_af(c, reg == 0 ? (v & 15) == 15 : (v & 15) == 0);
		put8_rm(c, &rm, r);
		return CPU_OK;
	}
	case 0xFF: { /* INC/DEC/CALL/JMP/PUSH rm16 */
		int reg;
		struct operand rm = decode_rm(c, &reg);
		if (override >= 0) {
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
			set_af(c, reg == 0 ? (v & 15) == 15 : (v & 15) == 0);
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
