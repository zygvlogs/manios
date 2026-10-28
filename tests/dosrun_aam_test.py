#!/usr/bin/env python3
"""Focused state-level regressions for the 8086 AAM imm8 instruction."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static struct cpu c;
static void start(uint8_t base)
{
	cpu_init(&c);
	c.sreg[1] = 0x100;
	c.ip = 0;
	wr8(&c, c.sreg[1], 0, 0xD4);
	wr8(&c, c.sreg[1], 1, base);
}

static void check_preserved_flags(uint16_t before)
{
	const uint16_t defined = FLAG_SF | FLAG_ZF | FLAG_PF;
	CHECK((c.flags & (uint16_t)~defined) == (before & (uint16_t)~defined));
}

int main(void)
{
	uint16_t before;
	start(10);
	c.r[AX] = 0xA063; /* 99 -> quotient 9, remainder 9 */
	c.r[CX] = 0x5678;
	c.flags = before = FLAG_CF | FLAG_AF | FLAG_OF | FLAG_IF | FLAG_DF | 2;
	CHECK(cpu_step(&c) == CPU_OK);
	CHECK(c.r[AX] == 0x0909 && c.ip == 2 && c.r[CX] == 0x5678);
	CHECK(!(c.flags & (FLAG_SF | FLAG_ZF)) && (c.flags & FLAG_PF));
	check_preserved_flags(before);
	cpu_free(&c);

	start(16);
	c.r[AX] = 0xABFF;
	c.flags = before = FLAG_CF | FLAG_AF | FLAG_OF | FLAG_IF | 2;
	CHECK(cpu_step(&c) == CPU_OK);
	CHECK(c.r[AX] == 0x0F0F && c.ip == 2);
	CHECK(!(c.flags & (FLAG_SF | FLAG_ZF)) && (c.flags & FLAG_PF));
	check_preserved_flags(before);
	cpu_free(&c);

	start(250);
	c.r[AX] = 0x55C8; /* 200 -> quotient 0, remainder C8: negative, odd parity */
	c.flags = before = FLAG_CF | FLAG_AF | FLAG_OF | FLAG_IF | 2;
	CHECK(cpu_step(&c) == CPU_OK);
	CHECK(c.r[AX] == 0x00C8 && c.ip == 2);
	CHECK((c.flags & FLAG_SF) && !(c.flags & (FLAG_ZF | FLAG_PF)));
	check_preserved_flags(before);
	cpu_free(&c);

	start(10);
	c.r[AX] = 0xAA07; /* Three set bits in AL: parity flag cleared. */
	CHECK(cpu_step(&c) == CPU_OK);
	CHECK(c.r[AX] == 0x0007 && !(c.flags & (FLAG_SF | FLAG_ZF | FLAG_PF)));
	cpu_free(&c);

	start(10);
	c.r[AX] = 0xAA00;
	CHECK(cpu_step(&c) == CPU_OK);
	CHECK(c.r[AX] == 0 && (c.flags & (FLAG_ZF | FLAG_PF)) == (FLAG_ZF | FLAG_PF));
	CHECK(!(c.flags & FLAG_SF));
	cpu_free(&c);

	/* D4 00 is INT 0 divide error; verify the vector and 8086 interrupt frame. */
	start(0);
	c.ip = 0x20;
	wr8(&c, c.sreg[1], 0x20, 0xD4);
	wr8(&c, c.sreg[1], 0x21, 0);
	c.sreg[2] = 0x300;
	c.r[SP] = 0x8000;
	c.r[AX] = 0xAB37;
	c.r[BX] = 0x1234;
	c.flags = before = FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF |
		FLAG_TF | FLAG_IF | FLAG_DF | FLAG_OF | 2;
	wr16(&c, 0, 0, 0x3456);
	wr16(&c, 0, 2, 0x4567);
	CHECK(cpu_step(&c) == CPU_INT);
	CHECK(c.int_no == 0 && c.sreg[1] == 0x4567 && c.ip == 0x3456);
	CHECK(c.r[AX] == 0xAB37 && c.r[BX] == 0x1234);
	CHECK(c.r[SP] == 0x7FFA);
	CHECK(rd16(&c, 0x300, 0x7FFA) == 0x22);
	CHECK(rd16(&c, 0x300, 0x7FFC) == 0x100);
	CHECK(rd16(&c, 0x300, 0x7FFE) == before);
	CHECK(c.flags == (before & (uint16_t)~(FLAG_IF | FLAG_TF)));
	cpu_free(&c);

	puts("PASS: AAM quotient/remainder, imm8 base, SF/ZF/PF, preserved flags and INT 0 frame");
	return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="dosrun-aam-") as tmp:
    src = Path(tmp) / "test.c"
    src.write_text(HARNESS)
    exe = Path(tmp) / "test"
    subprocess.run(["gcc", "-std=gnu11", "-O2", "-Wall", "-Wextra",
                    "-I", str(ROOT / "userland/dosrun"), str(src),
                    str(ROOT / "userland/dosrun/cpu.c"), "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
