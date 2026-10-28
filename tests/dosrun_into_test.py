#!/usr/bin/env python3
"""Focused state-level regressions for the 8086 INTO instruction."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "cpu.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static struct cpu c;

static void start(void)
{
	cpu_init(&c);
	c.sreg[1] = 0x100;
	c.ip = 0x20;
	wr8(&c, c.sreg[1], c.ip, 0xCE);
	wr8(&c, c.sreg[1], c.ip + 1, 0x90); /* following NOP */
	c.sreg[2] = 0x300;
	c.r[SP] = 0x8000;
	wr16(&c, 0, 4 * 4, 0x3456);
	wr16(&c, 0, 4 * 4 + 2, 0x4567);
}

int main(void)
{
	/* OF clear: consume INTO and continue without touching interrupt state. */
	start();
	c.r[AX] = 0xA55A;
	c.int_no = 0x77;
	c.flags = FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF |
		FLAG_TF | FLAG_IF | FLAG_DF | 2;
	uint16_t before = c.flags;
	CHECK(cpu_step(&c) == CPU_OK);
	CHECK(c.ip == 0x21 && c.sreg[1] == 0x100);
	CHECK(c.r[SP] == 0x8000 && c.flags == before && c.int_no == 0x77);
	CHECK(c.r[AX] == 0xA55A);
	CHECK(cpu_step(&c) == CPU_OK && c.ip == 0x22);
	CHECK(c.r[SP] == 0x8000 && c.flags == before && c.int_no == 0x77);
	cpu_free(&c);

	/* OF set: enter IVT vector 4 with a post-INTO return address and a
	 * pre-entry FLAGS image; IF/TF are cleared only in live FLAGS. */
	start();
	c.flags = before = FLAG_CF | FLAG_PF | FLAG_AF | FLAG_ZF | FLAG_SF |
		FLAG_TF | FLAG_IF | FLAG_DF | FLAG_OF | 2;
	CHECK(cpu_step(&c) == CPU_INT);
	CHECK(c.int_no == 4 && c.sreg[1] == 0x4567 && c.ip == 0x3456);
	CHECK(c.r[SP] == 0x7FFA);
	CHECK(rd16(&c, 0x300, 0x7FFA) == 0x21);  /* return IP */
	CHECK(rd16(&c, 0x300, 0x7FFC) == 0x100); /* return CS */
	CHECK(rd16(&c, 0x300, 0x7FFE) == before); /* original FLAGS */
	CHECK(c.flags == (before & (uint16_t)~(FLAG_IF | FLAG_TF)));
	cpu_free(&c);

	puts("PASS: INTO continues with OF clear; OF set enters INT 4 with exact frame and post-opcode return IP");
	return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="dosrun-into-") as tmp:
    src = Path(tmp) / "test.c"
    src.write_text(HARNESS)
    exe = Path(tmp) / "test"
    subprocess.run(["gcc", "-std=gnu11", "-O2", "-Wall", "-Wextra",
                    "-I", str(ROOT / "userland/dosrun"), str(src),
                    str(ROOT / "userland/dosrun/cpu.c"), "-o", str(exe)],
                   check=True)
    subprocess.run([str(exe)], check=True)
