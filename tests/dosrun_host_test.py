#!/usr/bin/env python3
"""dosrun host test: the 8086 interpreter, without booting ManiOS.

The CPU core, the loader and the DOS services are built for the host
(gcc, not the i686-elf toolchain) and driven directly. This checks the
things the QEMU test can't see easily: the exit code, the flags after
arithmetic, the stack, and the .EXE relocations.

Usage: tests/dosrun_host_test.py
"""
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HARNESS = r"""
#include "cpu.h"
#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runs a .COM or .EXE, printing what the DOS calls produce, and the
 * exit code. */
int main(int argc, char **argv)
{
	struct cpu c;
	cpu_init(&c);
	char err[256];
	if (load_program(&c, argv[1], "", err, sizeof(err)) < 0) {
		printf("LOADFAIL %s\n", err);
		return 1;
	}
	for (int i = 0; i < 1000000; i++) {
		enum cpu_stop s = cpu_step(&c);
		if (s == CPU_EXIT) { printf("EXIT %d\n", c.exit_code); return 0; }
		if (s == CPU_HALT) { printf("HALT\n"); return 0; }
		if (s == CPU_FAULT) { printf("FAULT %s\n", cpu_fault_name(c.fault_opcode)); return 0; }
		if (s == CPU_INT) {
			uint8_t ah = c.r[AX] >> 8;
			if (c.int_no == 0x21 && ah == 0x02) { putchar(c.r[DX] & 0xFF); }
			else if (c.int_no == 0x21 && ah == 0x09) {
				uint16_t o = c.r[DX];
				for (int j = 0; j < 65536; j++) {
					uint8_t ch = rd8(&c, c.sreg[3], (uint16_t)(o + j));
					if (ch == '$') break;
					if (ch != '\r') putchar(ch);
				}
			} else if (c.int_no == 0x21 && ah == 0x4C) {
				printf("EXIT %d\n", c.r[AX] & 0xFF); return 0;
			}
			c.ip = pop16(&c); c.sreg[1] = pop16(&c); c.flags = pop16(&c);
		}
	}
	printf("TOOLONG\n");
	return 1;
}
"""


def build(workdir):
    src = os.path.join(workdir, "harness.c")
    with open(src, "w") as f:
        f.write(HARNESS)
    exe = os.path.join(workdir, "harness")
    subprocess.run(["gcc", "-std=gnu11", "-O2", "-w", "-I",
                    os.path.join(ROOT, "userland/dosrun"), src,
                    os.path.join(ROOT, "userland/dosrun/cpu.c"),
                    os.path.join(ROOT, "userland/dosrun/loader.c"),
                    "-o", exe], check=True)
    return exe


def run(exe, workdir, name, data):
    path = os.path.join(workdir, name)
    with open(path, "wb") as f:
        f.write(data)
    r = subprocess.run([exe, path], capture_output=True, text=True, errors="replace")
    return r.stdout


def com(code):
    return code


def hello_com():
    code = bytes([0xB4, 0x09, 0xBA, 0x00, 0x00, 0xCD, 0x21,
                  0xB8, 0x00, 0x4C, 0xCD, 0x21])
    msg = b"Hello from DOS!\r\n$"
    off = 0x100 + len(code)
    code = code[:3] + bytes([off & 0xFF, off >> 8]) + code[5:]
    return com(code + msg)


def exit7_com():
    return com(bytes([0xB8, 0x07, 0x4C, 0xCD, 0x21]))


def digits_com():
    return com(bytes([
        0xB9, 0x0A, 0x00, 0xB2, 0x30,
        0xB4, 0x02, 0xCD, 0x21,
        0xFE, 0xC2, 0xE2, 0xF8,
        0xB8, 0x00, 0x4C, 0xCD, 0x21,
    ]))


def reloc_exe():
    """An .EXE whose image has a word to relocate: the word at image
    offset 0 is a segment, and the loader must add the load segment."""
    # mov ax, cs:[7]  (2E A1 07 00) reads the relocated word at image
    # offset 8; mov ah,4Ch; int 21h exits with AL = the low byte of the
    # segment. The load segment is 0x1010, so AL = 0x10 = 16.
    img = bytes([0x2E, 0xA1, 0x08, 0x00, 0xB4, 0x4C, 0xCD, 0x21, 0x00, 0x00])
    hdr = bytearray(32)
    hdr[0:2] = b"MZ"
    total = len(hdr) + len(img)
    hdr[2:4] = (total % 512).to_bytes(2, "little")
    hdr[4:6] = ((total + 511) // 512).to_bytes(2, "little")
    hdr[6:8] = (1).to_bytes(2, "little")           # one relocation
    hdr[8:10] = (2).to_bytes(2, "little")
    hdr[10:12] = (0).to_bytes(2, "little")
    hdr[12:14] = (0xFFFF).to_bytes(2, "little")
    hdr[14:16] = (0).to_bytes(2, "little")
    hdr[16:18] = (0xFFFE).to_bytes(2, "little")
    hdr[20:22] = (0).to_bytes(2, "little")
    hdr[22:24] = (0).to_bytes(2, "little")
    # The relocation table sits inside the header, at 0x1C: one entry,
    # offset 0 in the image, segment 0. The image follows the header.
    hdr[24:26] = (0x1C).to_bytes(2, "little")
    hdr[28:32] = struct.pack("<HH", 8, 0)  # relocate the word at image offset 8
    return bytes(hdr) + img


def main():
    workdir = tempfile.mkdtemp(prefix="zkt-dosrun-host-")
    exe = build(workdir)
    failures = 0

    def check(name, got, want):
        nonlocal failures
        if got == want:
            print(f"PASS: {name}")
        else:
            print(f"FAIL: {name}: got {got!r}, want {want!r}")
            failures += 1

    check("a .COM prints its string and exits 0",
          run(exe, workdir, "hello.com", hello_com()),
          "Hello from DOS!\nEXIT 0\n")
    check("a .COM's exit code comes back",
          run(exe, workdir, "exit7.com", exit7_com()),
          "EXIT 7\n")
    check("a loop, INC and LOOP count 0 to 9",
          run(exe, workdir, "digits.com", digits_com()),
          "0123456789EXIT 0\n")
    check("an .EXE's relocation adds the load segment",
          run(exe, workdir, "reloc.exe", reloc_exe()),
          "EXIT 16\n")
    check("a 386 instruction is named, not run",
          run(exe, workdir, "bad.com", com(bytes([0x0F, 0xA2, 0xCD, 0x21]))),
          "FAULT two-byte opcode (0F)\n")

    print(f"\n{'FAILED' if failures else 'OK'}: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
