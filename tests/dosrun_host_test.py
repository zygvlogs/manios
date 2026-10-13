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
#include "cpu.h"
#include "dosapi.h"
#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Runs a .COM or .EXE through the real DOS services (dosapi.c), so the
 * test covers the interpreter, the loader and the services together.
 * The console is stdout; a program's writes to handle 1 come here. */
int main(int argc, char **argv)
{
	struct cpu c;
	cpu_init(&c);
	char err[256];
	if (load_program(&c, argv[1], "", err, sizeof(err)) < 0) {
		printf("LOADFAIL %s\n", err);
		return 1;
	}
	struct dos_state *d = dos_new(&c);
	dos_set_program(d, 0x1000, 0x1010);
	dos_build_psp(d);

	for (int i = 0; i < 5000000; i++) {
		enum cpu_stop s = cpu_step(&c);
		if (s == CPU_EXIT) { printf("EXIT %d\n", c.exit_code); return 0; }
		if (s == CPU_HALT) { printf("HALT\n"); return 0; }
		if (s == CPU_FAULT) { printf("FAULT %s\n", cpu_fault_name(c.fault_opcode)); return 0; }
		if (s == CPU_INT) {
			enum cpu_stop r = dos_int(d, &c, c.int_no);
			if (r == CPU_EXIT) { printf("EXIT %d\n", c.exit_code); return 0; }
			c.ip = pop16(&c); c.sreg[1] = pop16(&c); c.flags = pop16(&c);
		}
	}
	printf("TOOLONG\n");
	return 1;
}
"""


# The host test doesn't have ManiOS's screen, so the video calls are
# stubbed. Everything else is the real dosapi.c.
GFX_STUB = r"""
#include <gfx.h>
int gfx_screen_open(struct gfx_screen *s, int w, int h) { (void)s; (void)w; (void)h; return -1; }
void gfx_screen_close(struct gfx_screen *s) { (void)s; }
void gfx_present(struct gfx_screen *s, const struct gfx_canvas *c, struct gfx_rect r) { (void)s; (void)c; (void)r; }
"""


def build(workdir):
    src = os.path.join(workdir, "harness.c")
    with open(src, "w") as f:
        f.write(HARNESS)
    stub = os.path.join(workdir, "gfxstub.c")
    with open(stub, "w") as f:
        f.write(GFX_STUB)
    exe = os.path.join(workdir, "harness")
    subprocess.run(["gcc", "-std=gnu11", "-O2", "-w",
                    "-I", os.path.join(ROOT, "userland/dosrun"),
                    "-I", os.path.join(ROOT, "desktop/libgfx"), src,
                    os.path.join(ROOT, "userland/dosrun/cpu.c"),
                    os.path.join(ROOT, "userland/dosrun/loader.c"),
                    os.path.join(ROOT, "userland/dosrun/dosapi.c"),
                    os.path.join(ROOT, "userland/dosrun/drives.c"),
                    stub,
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


def movzx_com():
    # movzx ax, byte [0x110]; mov ah,4Ch; int 21h. The .COM is at 0x100,
    # so the byte at 0x110 is 16 bytes into the file: pad to there, put
    # 5, and the program exits with AL = 5.
    code = bytearray([0x0F, 0xB6, 0x06, 0x10, 0x01, 0xB4, 0x4C, 0xCD, 0x21])
    while len(code) < 16:
        code.append(0x00)
    code.append(5)
    return com(bytes(code))


def setcc_com():
    # xor al,al; cmp al,0; sete al; mov ah,4Ch; int 21h -> AL = 1.
    return com(bytes([0x30, 0xC0, 0x3C, 0x00, 0x0F, 0x94, 0xC0, 0xB4, 0x4C, 0xCD, 0x21]))


def cpuid_com():
    # mov ax,1; cpuid; mov al,ah; mov ah,4Ch; int 21h -> AL = family (4).
    return com(bytes([0xB8, 0x01, 0x00, 0x0F, 0xA2, 0x88, 0xE0, 0xB4, 0x4C, 0xCD, 0x21]))


def xms_com():
    # Ask for the XMS driver, get its entry point, call it: version,
    # allocate 1 KB, move a string into it, blank the string, move it
    # back, and print it -- so the round trip through XMS is what puts
    # the text there.
    #
    # The XMS move descriptor is at DESC, in the layout the XMS spec
    # gives and dos_xms reads:
    #   +0  count (4 bytes)      +4  source handle
    #   +6  source offset (2)    +8  source segment
    #   +10 dest handle          +12 dest offset (2)
    #   +14 dest segment
    # The string is at STR; the entry point and handle are kept at
    # ENTRY and ENTRY+4.
    DESC, ENTRY, STR = 0x1E0, 0x200, 0x220
    code = bytearray()

    def w(addr, val):
        code.extend([0xC7, 0x06, addr & 0xFF, addr >> 8, val & 0xFF, val >> 8])

    # mov ax,0x4300; int 2Fh; cmp al,0x80; jne fail
    code += bytes([0xB8, 0x00, 0x43, 0xCD, 0x2F, 0x3C, 0x80, 0x75, 0x00])
    jne_at = len(code) - 1
    # mov ax,0x4310; int 2Fh; mov [ENTRY],bx; mov [ENTRY+2],es
    code += bytes([0xB8, 0x10, 0x43, 0xCD, 0x2F,
                   0x89, 0x1E, ENTRY & 0xFF, ENTRY >> 8,
                   0x8C, 0x06, (ENTRY + 2) & 0xFF, (ENTRY + 2) >> 8])
    # mov ah,9; mov dx,1; call far [ENTRY] -> allocate 1 KB; dx = handle
    code += bytes([0xB4, 0x09, 0xBA, 0x01, 0x00, 0xFF, 0x1E, ENTRY & 0xFF, ENTRY >> 8])
    # mov [ENTRY+4],dx  (save the handle)
    code += bytes([0x89, 0x16, (ENTRY + 4) & 0xFF, (ENTRY + 4) >> 8])
    # Build the descriptor: count=6, source handle 0, source offset STR,
    # source segment = CS.
    w(DESC + 0, 6)
    w(DESC + 2, 0)
    w(DESC + 4, 0)
    w(DESC + 6, STR)
    code += bytes([0x8C, 0x0E, (DESC + 8) & 0xFF, (DESC + 8) >> 8])   # mov [DESC+8],cs
    # dest handle = the XMS handle, dest offset 0, dest segment 0
    code += bytes([0xA1, (ENTRY + 4) & 0xFF, (ENTRY + 4) >> 8,
                   0xA3, (DESC + 10) & 0xFF, (DESC + 10) >> 8])
    w(DESC + 12, 0)
    w(DESC + 14, 0)
    # mov si,DESC; mov dx,6; mov ah,0x0B; call far -> move to XMS
    code += bytes([0xBE, DESC & 0xFF, DESC >> 8, 0xBA, 0x06, 0x00,
                   0xB4, 0x0B, 0xFF, 0x1E, ENTRY & 0xFF, ENTRY >> 8])
    # Blank the string, so the move back has to restore it.
    w(STR + 0, 0)
    w(STR + 2, 0)
    w(STR + 4, 0)
    # Move it back: source = the XMS handle at offset 0, dest = the
    # string in CS.
    code += bytes([0xA1, (ENTRY + 4) & 0xFF, (ENTRY + 4) >> 8,   # src handle
                   0xA3, (DESC + 4) & 0xFF, (DESC + 4) >> 8])
    w(DESC + 6, 0)                                    # src offset 0
    w(DESC + 8, 0)                                    # src segment 0
    w(DESC + 10, 0)                                   # dest handle 0
    w(DESC + 12, STR)                                 # dest offset
    code += bytes([0x8C, 0x0E, (DESC + 14) & 0xFF, (DESC + 14) >> 8])  # mov [DESC+14],cs
    # mov si,DESC; mov dx,6; mov ah,0x0B; call far -> move back
    code += bytes([0xBE, DESC & 0xFF, DESC >> 8, 0xBA, 0x06, 0x00,
                   0xB4, 0x0B, 0xFF, 0x1E, ENTRY & 0xFF, ENTRY >> 8])
    # print the string: mov ah,9; mov dx,STR; int 21h
    code += bytes([0xB4, 0x09, 0xBA, STR & 0xFF, STR >> 8, 0xCD, 0x21])
    # mov ax,4C00; int 21h
    code += bytes([0xB8, 0x00, 0x4C, 0xCD, 0x21])
    fail_at = len(code)
    code += bytes([0xB8, 0x01, 0x4C, 0xCD, 0x21])       # fail: exit 1
    code[jne_at] = (fail_at - (jne_at + 1)) & 0xFF
    # Pad to the string at STR.
    while len(code) < STR - 0x100:
        code.append(0x00)
    code += b"XMS OK\r\n$"
    return com(bytes(code))


def ems_com():
    # Ask for EMS, get the page frame, allocate 2 pages, map page 0,
    # write to the frame, map page 1, then map page 0 back and print --
    # so the write-back through the window is what the test checks.
    # FRAME and HANDLE must sit in the zero padding after the code,
    # not inside it: the code ends around 0x168, so 0x170 is safe.
    FRAME, HANDLE = 0x170, 0x172
    code = bytearray()

    # mov ah,0x41; int 67h; cmp ah,0; jne fail; mov [FRAME],bx
    code += bytes([0xB4, 0x41, 0xCD, 0x67, 0x80, 0xFC, 0x00, 0x75, 0x00])
    jne_at = len(code) - 1
    code += bytes([0x89, 0x1E, FRAME & 0xFF, FRAME >> 8])
    # mov ah,0x43; mov bx,2; int 67h; cmp ah,0; jne fail; mov [HANDLE],dx
    code += bytes([0xB4, 0x43, 0xBB, 0x02, 0x00, 0xCD, 0x67,
                   0x80, 0xFC, 0x00, 0x75, 0x00])
    jne2_at = len(code) - 1
    code += bytes([0x89, 0x16, HANDLE & 0xFF, HANDLE >> 8])
    # Map logical page 0 to physical page 0: mov ah,0x44; mov al,0;
    # mov bx,0; mov dx,[HANDLE]; int 67h
    code += bytes([0xB4, 0x44, 0xB0, 0x00, 0xBB, 0x00, 0x00,
                   0x8B, 0x16, HANDLE & 0xFF, HANDLE >> 8, 0xCD, 0x67])
    # Write "EMS OK\r\n$" into the frame: mov ax,[FRAME]; mov es,ax;
    # mov si,msg; mov di,0; mov cx,9; rep movsb
    movsi_at = len(code) + 5
    code += bytes([0xA1, FRAME & 0xFF, FRAME >> 8, 0x8E, 0xC0,
                   0xBE, 0x00, 0x00, 0xBF, 0x00, 0x00, 0xB9, 0x09, 0x00,
                   0xF3, 0xA4])
    # Map logical page 1 to physical page 0, so the window shows other
    # data: mov ah,0x44; mov al,0; mov bx,1; mov dx,[HANDLE]; int 67h
    code += bytes([0xB4, 0x44, 0xB0, 0x00, 0xBB, 0x01, 0x00,
                   0x8B, 0x16, HANDLE & 0xFF, HANDLE >> 8, 0xCD, 0x67])
    # Map logical page 0 back: mov ah,0x44; mov al,0; mov bx,0;
    # mov dx,[HANDLE]; int 67h
    code += bytes([0xB4, 0x44, 0xB0, 0x00, 0xBB, 0x00, 0x00,
                   0x8B, 0x16, HANDLE & 0xFF, HANDLE >> 8, 0xCD, 0x67])
    # Print the frame: mov ax,[FRAME]; mov ds,ax; mov ah,9; xor dx,dx; int 21h
    code += bytes([0xA1, FRAME & 0xFF, FRAME >> 8, 0x8E, 0xD8,
                   0xB4, 0x09, 0xBA, 0x00, 0x00, 0xCD, 0x21])
    # mov ax,4C00; int 21h
    code += bytes([0xB8, 0x00, 0x4C, 0xCD, 0x21])
    fail_at = len(code)
    code += bytes([0xB8, 0x01, 0x4C, 0xCD, 0x21])
    code[jne_at] = (fail_at - (jne_at + 1)) & 0xFF
    code[jne2_at] = (fail_at - (jne2_at + 1)) & 0xFF
    # The message the program copies into the frame. It must not sit on
    # top of the frame-segment/handle variables at 0x150..0x153, so it
    # goes at 0x180.
    while len(code) < 0x80:
        code.append(0x00)
    msg_at = 0x100 + len(code)
    code += b"EMS OK\r\n$"
    # Patch the `mov si,0` to the message's offset.
    code[movsi_at + 1] = msg_at & 0xFF
    code[movsi_at + 2] = msg_at >> 8
    return com(bytes(code))


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
    check("an instruction it doesn't have is named, not run",
          run(exe, workdir, "bad.com", com(bytes([0x0F, 0x0B, 0xCD, 0x21]))),
          "FAULT UD2\n")
    check("MOVZX zero-extends a byte",
          run(exe, workdir, "movzx.com", movzx_com()),
          "EXIT 5\n")
    check("SETcc sets a byte from a condition",
          run(exe, workdir, "setcc.com", setcc_com()),
          "EXIT 1\n")
    check("CPUID reports a 486",
          run(exe, workdir, "cpuid.com", cpuid_com()),
          "EXIT 4\n")
    check("XMS: allocate, move to it and back",
          run(exe, workdir, "xms.com", xms_com()),
          "XMS OK\nEXIT 0\n")
    check("EMS: allocate a page and map it",
          run(exe, workdir, "ems.com", ems_com()),
          "EMS OK\nEXIT 0\n")

    print(f"\n{'FAILED' if failures else 'OK'}: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
