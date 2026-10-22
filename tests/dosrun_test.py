#!/usr/bin/env python3
"""dosrun test (M24): running DOS programs on ManiOS.

A disk is built with mtools, holding hand-assembled .COM and .EXE
programs. dosrun is run from ManiOS's shell and what the programs print
is checked, along with the exit code and the errors for a program
dosrun can't run.

The programs are assembled here, byte by byte, so the test doesn't
depend on an assembler being installed.

Usage: tests/dosrun_test.py [path-to-kernel-elf]
"""
import os
import struct
import subprocess
import sys
import tempfile

import console_test
from console_test import SHELL_PROMPT, Machine, TestFailure, run_command
from dosrun_host_test import protected_exe

VERSION = console_test.VERSION
DISK_SECTORS = 16384
PART_START, PART_SECTORS = 2048, 14336


def mt(path, tool, *args):
    subprocess.run([tool, "-i", f"{path}@@{PART_START * 512}", *args],
                   check=True, stdout=subprocess.DEVNULL)


def put(path, name, data, srcdir):
    src = os.path.join(srcdir, name.replace("/", "_"))
    with open(src, "wb") as f:
        f.write(data)
    mt(path, "mcopy", src, "::/" + name)


# --- the programs, assembled by hand -----------------------------------

def com(code):
    """A .COM: the code at offset 0x100, so a message's offset is
    0x100 plus its place in the code."""
    return code


def hello_com():
    # mov ah,9; mov dx,msg; int 21h; mov ax,4C00; int 21h; msg
    code = bytes([0xB4, 0x09, 0xBA, 0x00, 0x00, 0xCD, 0x21,
                  0xB8, 0x00, 0x4C, 0xCD, 0x21])
    msg = b"Hello from DOS!\r\n$"
    off = 0x100 + len(code)
    code = code[:3] + bytes([off & 0xFF, off >> 8]) + code[5:]
    return com(code + msg)


def digits_com():
    # mov cx,10; mov dl,'0'; loop: mov ah,2; int 21h; inc dl; loop loop;
    # mov ax,4C00; int 21h
    return com(bytes([
        0xB9, 0x0A, 0x00,
        0xB2, 0x30,
        0xB4, 0x02, 0xCD, 0x21,
        0xFE, 0xC2,
        0xE2, 0xF8,
        0xB8, 0x00, 0x4C, 0xCD, 0x21,
    ]))


def exit7_com():
    # mov ax,4C07; int 21h
    return com(bytes([0xB8, 0x07, 0x4C, 0xCD, 0x21]))


def flags_com():
    # Test service results with actual conditional branches after INT/IRET.
    code = bytearray()
    for service, branch in [
        ([0xF8, 0xB4, 0xFF, 0xCD, 0x21], 0x72),  # unsupported: JC
        ([0x31, 0xC9, 0xBB, 1, 0, 0xBA, 0, 1, 0xF9,
          0xB4, 0x40, 0xCD, 0x21], 0x73),         # write: JNC
        ([0xB8, 1, 1, 0x08, 0xC0, 0xCD, 0x16], 0x74),  # no key: JZ
    ]:
        code += bytes(service) + bytes([branch, 5, 0xB8, 1, 0x4C, 0xCD, 0x21])
    msg = 0x100 + len(code) + 12
    code += bytes([0xB4, 9, 0xBA, msg & 255, msg >> 8, 0xCD, 0x21,
                   0xB8, 0, 0x4C, 0xCD, 0x21]) + b"FLAGS OK\r\n$"
    return bytes(code)


def hello_exe():
    # The same as hello_com, as an .EXE: CS:IP = 0:0, so the image is
    # the code with the message's offset within the image.
    img = bytes([0xB4, 0x09, 0xBA, 0x00, 0x00, 0xCD, 0x21,
                 0xB8, 0x00, 0x4C, 0xCD, 0x21])
    msg = b"EXE OK\r\n$"
    off = len(img)
    img = img[:3] + bytes([off & 0xFF, off >> 8]) + img[5:] + msg
    hdr = bytearray(32)
    hdr[0:2] = b"MZ"
    total = len(hdr) + len(img)
    hdr[2:4] = (total % 512).to_bytes(2, "little")
    hdr[4:6] = ((total + 511) // 512).to_bytes(2, "little")
    hdr[6:8] = (0).to_bytes(2, "little")            # no relocations
    hdr[8:10] = (2).to_bytes(2, "little")           # 32-byte header
    hdr[10:12] = (0).to_bytes(2, "little")          # min BSS
    hdr[12:14] = (0xFFFF).to_bytes(2, "little")     # max BSS
    hdr[14:16] = (0).to_bytes(2, "little")          # SS
    hdr[16:18] = (0xFFFE).to_bytes(2, "little")     # SP
    hdr[20:22] = (0).to_bytes(2, "little")          # IP
    hdr[22:24] = (0).to_bytes(2, "little")          # CS
    hdr[24:26] = (0x1C).to_bytes(2, "little")       # reloc table
    return bytes(hdr) + img


def unsupported_com():
    # BOUND (62 /r), which the interpreter doesn't have: dosrun must
    # name it, not run it.
    return com(bytes([0x62, 0x00, 0xB8, 0x00, 0x4C, 0xCD, 0x21]))


def movzx_com():
    # movzx ax, byte [0x110]; mov ah,4Ch; int 21h. The .COM is at 0x100,
    # so the byte at 0x110 is 16 bytes into the file: pad to there, put
    # 5, and the program exits with AL = 5. (MOVZX is a 386
    # instruction: the interpreter must run it.)
    code = bytearray([0x0F, 0xB6, 0x06, 0x10, 0x01, 0xB4, 0x4C, 0xCD, 0x21])
    while len(code) < 16:
        code.append(0x00)
    code.append(5)
    return com(bytes(code))


def iret_com():
    # A guest IRET restores its own frame; the runner must not pop it again.
    code = bytes([0x9C, 0x0E, 0x68, 0x06, 0x01, 0xCF,
                  0xBA, 0x12, 0x01, 0xB4, 0x09, 0xCD, 0x21,
                  0xB8, 0, 0x4C, 0xCD, 0x21])
    return code + b"IRET OK\r\n$"


def build_disk(path):
    """An MBR with one FAT16 partition, and the programs on it, built
    with mtools as dos_test.py builds its disk."""
    img = bytearray(DISK_SECTORS * 512)
    img[446:462] = console_test.mbr_entry(0x00, 0x06, PART_START, PART_SECTORS)
    img[510:512] = b"\x55\xaa"
    with open(path, "wb") as f:
        f.write(img)
    mt(path, "mformat", "-T", str(PART_SECTORS), "-h", "16", "-s", "63",
       "-H", str(PART_START), "-c", "1", "-v", "DOSRUN", "::")
    srcdir = tempfile.mkdtemp(prefix="zkt-dosrun-src-")
    put(path, "HELLO.COM", hello_com(), srcdir)
    put(path, "DIGITS.COM", digits_com(), srcdir)
    put(path, "EXIT7.COM", exit7_com(), srcdir)
    put(path, "HELLO.EXE", hello_exe(), srcdir)
    put(path, "BAD386.COM", unsupported_com(), srcdir)
    put(path, "MOVZX.COM", movzx_com(), srcdir)
    put(path, "FLAGS.COM", flags_com(), srcdir)
    put(path, "IRET.COM", iret_com(), srcdir)
    put(path, "EXTENDER.EXE", protected_exe(), srcdir)
    put(path, "PACKED.EXA", b'\x1e\x00-lh5-' + bytes(40), srcdir)
    return srcdir


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/manios-zkt.elf"
    workdir = tempfile.mkdtemp(prefix="zkt-dosrun-")
    disk = os.path.join(workdir, "dosrun.img")
    build_disk(disk)
    failures = 0

    def step(name, fn):
        nonlocal failures
        try:
            fn()
            print(f"PASS: {name}")
        except TestFailure as e:
            print(f"FAIL: {name}: {e}")
            failures += 1

    m = Machine(kernel, ["-drive", f"file={disk},format=raw,if=ide"])
    try:
        m.expect(SHELL_PROMPT, timeout=60)
        step("dosrun HELLO.COM: a .COM prints its string",
             lambda: run_command(m, "serial", "dosrun C:/HELLO.COM",
                                 ["Hello from DOS!"], SHELL_PROMPT))
        step("dosrun with an absolute ManiOS path",
             lambda: run_command(m, "serial", "dosrun /n/ata0p1/HELLO.COM",
                                 ["Hello from DOS!"], SHELL_PROMPT))
        step("dosrun DIGITS.COM: a loop, INC and LOOP",
             lambda: run_command(m, "serial", "dosrun C:/DIGITS.COM",
                                 ["0123456789"], SHELL_PROMPT))
        step("dosrun HELLO.EXE: an .EXE loads and runs",
             lambda: run_command(m, "serial", "dosrun C:/HELLO.EXE",
                                 ["EXE OK"], SHELL_PROMPT))
        # The shell has no $?, so an exit code can't be echoed; the
        # host test (tests/dosrun_host_test.py) checks it directly.
        step("dosrun EXIT7.COM: it runs and returns to the prompt",
             lambda: run_command(m, "serial", "dosrun C:/EXIT7.COM",
                                 ["!can't", "!error"], SHELL_PROMPT))
        step("dosrun BAD386.COM: an instruction it can't run is named",
             lambda: run_command(m, "serial", "dosrun C:/BAD386.COM",
                                 ["can't run", "BOUND"], SHELL_PROMPT))
        step("dosrun MOVZX.COM: a 386 instruction runs",
             lambda: run_command(m, "serial", "dosrun C:/MOVZX.COM",
                                 ["!can't", "!error"], SHELL_PROMPT))
        step("dosrun FLAGS.COM: DOS and BIOS results survive IRET",
             lambda: run_command(m, "serial", "dosrun C:/FLAGS.COM",
                                 ["FLAGS OK"], SHELL_PROMPT))
        step("a guest IRET restores its frame exactly once",
             lambda: run_command(m, "serial", "dosrun C:/IRET.COM",
                                 ["IRET OK"], SHELL_PROMPT))
        step("a DOS extender is refused with its actual requirement",
             lambda: run_command(m, "serial", "dosrun C:/EXTENDER.EXE",
                                 ["requires 32-bit protected mode"], SHELL_PROMPT))
        step("a packed installation file is identified as an archive",
             lambda: run_command(m, "serial", "dosrun C:/PACKED.EXA",
                                 ["LHA archive", "extract"], SHELL_PROMPT))
        step("dosrun with no program: a usage message",
             lambda: run_command(m, "serial", "dosrun",
                                 ["usage"], SHELL_PROMPT))
    finally:
        m.close()
    print(f"\n{'FAILED' if failures else 'OK'}: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
