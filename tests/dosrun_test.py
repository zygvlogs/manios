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
import time

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


def clock_com():
    # Poll the BIOS tick in memory without calling any interrupt. The runner
    # must advance it even while the guest is spinning in ordinary CPU code.
    code = bytearray([0xB8, 0x40, 0, 0x8E, 0xC0,
                      0x26, 0x8B, 0x1E, 0x6C, 0,
                      0x26, 0x3B, 0x1E, 0x6C, 0, 0x74, 0xF9])
    msg = 0x100 + len(code) + 12
    code += bytes([0xBA, msg & 255, msg >> 8, 0xB4, 9, 0xCD, 0x21,
                   0xB8, 0, 0x4C, 0xCD, 0x21]) + b"CLOCK OK\r\n$"
    return bytes(code)


def video_status_com():
    # Wait for inactive/active display and vertical retrace start/end using
    # actual IN/TEST/Jcc instructions, with interrupts disabled throughout.
    code = bytearray([0xFA, 0xBA, 0xDA, 3])
    for mask in (1, 8):
        code += bytes([0xEC, 0xA8, mask, 0x75, 0xFB,
                       0xEC, 0xA8, mask, 0x74, 0xFB,
                       0xEC, 0xA8, mask, 0x75, 0xFB])
    code += bytes([0xFB])
    msg = 0x100 + len(code) + 12
    code += bytes([0xBA, msg & 255, msg >> 8, 0xB4, 9, 0xCD, 0x21,
                   0xB8, 0, 0x4C, 0xCD, 0x21]) + b"VIDEO STATUS OK\r\n$"
    return bytes(code)


def pit_speaker_com():
    # Program channel 2 as Windows setup does, enable its gate, then wait
    # for both OUT2 edges through port 61h. Change divisor and restore it.
    code = bytearray([0xB0, 0xB6, 0xE6, 0x43,
                      0xB0, 0x33, 0xE6, 0x42, 0xB0, 5, 0xE6, 0x42,
                      0xE4, 0x61, 0x88, 0xC3, 0x0C, 3, 0xE6, 0x61,
                      0xE4, 0x61, 0xA8, 0x20, 0x75, 0xFA,
                      0xE4, 0x61, 0xA8, 0x20, 0x74, 0xFA,
                      0xB0, 0x33, 0xE6, 0x42, 0xB0, 6, 0xE6, 0x42,
                      0x88, 0xD8, 0xE6, 0x61])
    msg = 0x100 + len(code) + 12
    code += bytes([0xBA, msg & 255, msg >> 8, 0xB4, 9, 0xCD, 0x21,
                   0xB8, 0, 0x4C, 0xCD, 0x21]) + b"PIT SPEAKER OK\r\n$"
    return bytes(code)


def filetime_com():
    # Installer workflow: create/write, stamp, close, reopen and read stamp.
    # All failure paths exit before the marker. Disk metadata is also checked
    # independently below after QEMU closes.
    code = bytearray()
    refs = []
    def dx(value):
        code.extend(b"\xBA\0\0"); refs.append((len(code)-2, value))
    def check(op=b"\x73"):
        code.extend(op + b"\x05\xB8\x01\x4C\xCD\x21")
    def call(ax):
        code.extend(b"\xB8" + struct.pack("<H", ax) + b"\xCD\x21")
        check()
    dx(b"C:\\STAMP.DAT\0"); code.extend(b"\x31\xC9"); call(0x3C00)
    code.extend(b"\x89\xC3")  # BX=handle
    dx(b"abc"); code.extend(b"\xB9\x03\0"); call(0x4000)
    code.extend(b"\xB9\x5C\x64\xBA\x6F\x0B"); call(0x5701)
    call(0x3E00)
    dx(b"C:\\STAMP.DAT\0"); call(0x3D00); code.extend(b"\x89\xC3")
    call(0x5700)
    code.extend(b"\x81\xF9\x5C\x64"); check(b"\x74")
    code.extend(b"\x81\xFA\x6F\x0B"); check(b"\x74")
    call(0x3E00)
    # Closed handle must fail with DOS invalid handle, not invented metadata.
    code.extend(b"\xB8\0\x57\xCD\x21"); check(b"\x72")
    code.extend(b"\x3D\x06\0"); check(b"\x74")
    # A valid console handle has no FAT timestamp: report unsupported.
    code.extend(b"\xBB\x01\0\xB8\0\x57\xCD\x21"); check(b"\x72")
    code.extend(b"\x3D\x01\0"); check(b"\x74")
    # A 512-byte cluster holds only 14 files plus dot entries. Grow to
    # 40 files so directory insertion traverses several cluster boundaries.
    dx(b"C:\\GROW\0"); call(0x3900)
    for i in range(40):
        dx(f"C:\\GROW\\F{i:02}.DAT\0".encode()); code.extend(b"\x31\xC9")
        call(0x3C00); code.extend(b"\x89\xC3"); call(0x3E00)
    dx(b"FILETIME OK\r\n$"); code.extend(b"\xB4\x09\xCD\x21\xB8\0\x4C\xCD\x21")
    for at, value in refs:
        code[at:at+2] = struct.pack("<H", 0x100+len(code)); code.extend(value)
    return bytes(code)


def fileops_com():
    """Create NEW.TXT exclusively, write to it, then rename it."""
    code = bytearray()
    refs = {}

    def emit(data):
        code.extend(data)

    def imm16(op, value):
        emit(bytes(op) + b"\0\0")
        refs.setdefault(value, []).append(len(code) - 2)

    def jcc(op, label):
        emit(bytes([op, 0]))
        refs.setdefault((label, "rel"), []).append(len(code) - 1)

    # Create NEW.TMP with AH=5Bh, then write four bytes through its handle.
    imm16([0xBA], "old")                     # mov dx,old
    emit(b"\xB8\x00\x5B\xB9\x00\x00\xCD\x21")  # create new
    jcc(0x72, "fail")                        # jc fail
    emit(b"\x89\xC3")                       # mov bx,ax
    imm16([0xBA], "data")                    # mov dx,data
    emit(b"\xB9\x04\x00\xB4\x40\xCD\x21")  # write 4 bytes
    emit(b"\x3D\x04\x00")                 # cmp ax,4
    jcc(0x75, "fail")                        # jne fail
    emit(b"\xB4\x3E\xCD\x21")            # close handle
    emit(b"\xB8\x00\x56")                 # mov ax,5600h
    imm16([0xBA], "old")                     # mov dx,old
    imm16([0xBF], "new")                     # mov di,new
    emit(b"\xCD\x21")                       # rename DS:DX to ES:DI
    jcc(0x72, "fail")                        # jc fail
    imm16([0xBA], "ok")                      # print success marker
    emit(b"\xB4\x09\xCD\x21\xB8\x00\x4C\xCD\x21")
    labels = {"fail": len(code)}
    imm16([0xBA], "failed")
    emit(b"\xB4\x09\xCD\x21\xB8\x01\x4C\xCD\x21")
    labels.update({"old": len(code)})
    code.extend(b"C:\\OLD.TMP\0")
    labels["new"] = len(code)
    code.extend(b"C:\\NEW.TXT\0")
    labels["data"] = len(code)
    code.extend(b"DATA")
    labels["ok"] = len(code)
    code.extend(b"FILEOPS OK\r\n$")
    labels["failed"] = len(code)
    code.extend(b"FILEOPS FAIL\r\n$")

    for label, offsets in refs.items():
        if isinstance(label, tuple):
            name = label[0]
            for pos in offsets:
                rel = labels[name] - (pos + 1)
                if not -128 <= rel <= 127:
                    raise AssertionError("file operation branch out of range")
                code[pos] = rel & 0xFF
        else:
            for pos in offsets:
                code[pos] = (0x100 + labels[label]) & 0xFF
                code[pos + 1] = (0x100 + labels[label]) >> 8
    return bytes(code)


def pgdn_probe_com():
    """Wait for BIOS PgDn and print a marker only for scan code 51h."""
    code = bytearray(bytes.fromhex("b400 cd16 80fc51 7500"))
    branch = len(code) - 1
    code.extend(b"\xBA\0\0\xB4\x09\xCD\x21\xB8\x00\x4C\xCD\x21")
    bad = len(code)
    code.extend(b"\xBA\0\0\xB4\x09\xCD\x21\xB8\x01\x4C\xCD\x21")
    ok_msg, bad_msg = len(code), len(code) + len(b"PGDN OK\r\n$")
    code.extend(b"PGDN OK\r\n$PGDN BAD\r\n$")
    code[branch] = (bad - (branch + 1)) & 0xFF
    code[10] = (0x100 + ok_msg) & 0xFF
    code[11] = (0x100 + ok_msg) >> 8
    code[bad + 1] = (0x100 + bad_msg) & 0xFF
    code[bad + 2] = (0x100 + bad_msg) >> 8
    return bytes(code)


def keyboard_port_probe_com():
    """Read an extended PgDn from the emulated 8042 data/status ports."""
    code = bytearray(bytes.fromhex(
        "ba6400 ec a801 74f8 "       # wait for output-buffer-full
        "ba6000 ec 3ce0 7500 "       # extended-key prefix
        "ba6400 ec a801 7400 "       # second scan byte is queued
        "ba6000 ec 3c51 7500"))      # PgDn scan 51h
    fail_branches = [15, 23, 31]
    code.extend(bytes.fromhex("ba0000 b409 cd21 b8004ccd21"))
    fail = len(code)
    code.extend(bytes.fromhex("ba0000 b409 cd21 b8014ccd21"))
    ok_msg = len(code)
    code.extend(b"KBD PORT OK\r\n$")
    fail_msg = len(code)
    code.extend(b"KBD PORT FAIL\r\n$")
    for at in fail_branches:
        code[at] = (fail - (at + 1)) & 0xFF
    # The first success message pointer starts at code offset 33.
    code[33] = (0x100 + ok_msg) & 0xFF
    code[34] = (0x100 + ok_msg) >> 8
    code[fail + 1] = (0x100 + fail_msg) & 0xFF
    code[fail + 2] = (0x100 + fail_msg) >> 8
    return bytes(code)


def fcb_rename_com():
    """Rename NEW.TXT to RENAMED.TXT with legacy INT 21h/AH=17h."""
    code = bytearray(b"\xBA\0\0\xB4\x17\xCD\x21\x3C\x00\x75\0")
    branch = len(code) - 1
    code.extend(b"\xBA\0\0\xB4\x09\xCD\x21\xB8\x00\x4C\xCD\x21")
    fail = len(code)
    code.extend(b"\xBA\0\0\xB4\x09\xCD\x21\xB8\x01\x4C\xCD\x21")
    fcb = len(code)
    code.extend(bytes([3]) + b"NEW     TXT" + bytes(5) + b"RENAMED TXT")
    ok_msg = len(code)
    code.extend(b"FCB RENAME OK\r\n$")
    fail_msg = len(code)
    code.extend(b"FCB RENAME FAIL\r\n$")
    code[branch] = (fail - (branch + 1)) & 0xFF
    code[1] = (0x100 + fcb) & 0xFF
    code[2] = (0x100 + fcb) >> 8
    # Each `mov dx,imm16` points at the corresponding '$'-terminated result.
    code[12] = (0x100 + ok_msg) & 0xFF
    code[13] = (0x100 + ok_msg) >> 8
    failure_dx = fail
    code[failure_dx + 1] = (0x100 + fail_msg) & 0xFF
    code[failure_dx + 2] = (0x100 + fail_msg) >> 8
    return bytes(code)


def readonly_filetime_com():
    # Get metadata on a read-only FAT volume; setting it must be refused.
    code = bytearray(b"\xBA\0\0\xB8\0\x3D\xCD\x21")
    def check(op=b"\x73"):
        code.extend(op+b"\x05\xB8\x01\x4C\xCD\x21")
    check(); code.extend(b"\x89\xC3\xB8\0\x57\xCD\x21"); check()
    code.extend(b"\xB8\x01\x57\xCD\x21"); check(b"\x72")
    code.extend(b"\x3D\x05\0"); check(b"\x74")
    code.extend(b"\xB4\x3E\xCD\x21"); check()
    at=len(code)+1; code.extend(b"\xBA\0\0\xB4\x09\xCD\x21\xB8\0\x4C\xCD\x21")
    code[1:3]=struct.pack("<H",0x100+len(code)); code.extend(b"C:\\HELLO.COM\0")
    code[at:at+2]=struct.pack("<H",0x100+len(code)); code.extend(b"READONLY TIME OK\r\n$")
    return bytes(code)


def directory_com():
    # Create a real nested FAT directory tree, change into it, then remove
    # it again. Each failed DOS call exits before the success marker.
    code = bytearray()
    patches = []
    for ah, path in [(0x39, b"C:\\WTEST"), (0x3B, b"C:\\WTEST"),
                     (0x39, b"SUB"), (0x3B, b"C:\\"),
                     (0x3A, b"C:\\WTEST\\SUB"), (0x3A, b"C:\\WTEST")]:
        code += bytes([0xBA, 0, 0])
        patches.append((len(code) - 2, path))
        code += bytes([0xB4, ah, 0xCD, 0x21, 0x73, 5, 0xB8, 1, 0x4C, 0xCD, 0x21])
    code += bytes([0xBA, 0, 0])
    patches.append((len(code) - 2, b"DIRECTORY OK\r\n$"))
    code += bytes([0xB4, 9, 0xCD, 0x21, 0xB8, 0, 0x4C, 0xCD, 0x21])
    for at, value in patches:
        code[at:at + 2] = (0x100 + len(code)).to_bytes(2, "little")
        code += value + b"\0"
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


def vga_text_com():
    code = bytearray([0x06, 0xB8, 0, 0xB8, 0x8E, 0xC0,
                      0xBE, 0x1B, 0x01, 0xBF, 0, 0, 0xB9, 6, 0,
                      0xF3, 0xA5, 0x07, 0xB8, 0, 0x4C, 0xCD, 0x21])
    # Table starts at the next byte after this code.
    code[7:9] = (0x100 + len(code)).to_bytes(2, "little")
    return bytes(code) + b"".join(bytes([ch, 0x1E]) for ch in b"VGA OK")


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
    put(path, "CLOCK.COM", clock_com(), srcdir)
    put(path, "VIDSTAT.COM", video_status_com(), srcdir)
    put(path, "PIT.COM", pit_speaker_com(), srcdir)
    put(path, "BADIO.COM", bytes([0xB0, 0xB7, 0xE6, 0x43, 0xF4]), srcdir)
    put(path, "RTIME.COM", readonly_filetime_com(), srcdir)
    put(path, "FTIME.COM", filetime_com(), srcdir)
    put(path, "FILEOPS.COM", fileops_com(), srcdir)
    put(path, "PGDN.COM", pgdn_probe_com(), srcdir)
    put(path, "KBDPORT.COM", keyboard_port_probe_com(), srcdir)
    put(path, "FCBREN.COM", fcb_rename_com(), srcdir)
    put(path, "MKDIR.COM", directory_com(), srcdir)
    put(path, "IRET.COM", iret_com(), srcdir)
    put(path, "VGA.COM", vga_text_com(), srcdir)
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
        step("an explicit A: mount reads the selected media directory",
             lambda: run_command(m, "serial", "dosrun --mount A=/n/ata0p1 A:/HELLO.COM",
                                 ["Hello from DOS!"], SHELL_PROMPT))
        step("an invalid mount directory is rejected",
             lambda: run_command(m, "serial", "dosrun --mount A=/n/ata0p1/HELLO.COM C:/HELLO.COM",
                                 ["--mount requires"], SHELL_PROMPT))
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
        step("direct BIOS clock polling advances during a busy loop",
             lambda: run_command(m, "serial", "dosrun C:/CLOCK.COM",
                                 ["CLOCK OK"], SHELL_PROMPT))
        step("video status polling crosses both bit edges with interrupts disabled",
             lambda: run_command(m, "serial", "dosrun C:/VIDSTAT.COM",
                                 ["VIDEO STATUS OK"], SHELL_PROMPT))
        step("PIT channel 2 programming and speaker OUT2 polling",
             lambda: run_command(m, "serial", "dosrun C:/PIT.COM",
                                 ["PIT SPEAKER OK"], SHELL_PROMPT))
        step("unsupported PIT command faults with port and AL diagnostics",
             lambda: run_command(m, "serial", "dosrun C:/BADIO.COM",
                                 ["can't run opcode E6", "OUT port 0043", "AL=B7"], SHELL_PROMPT))
        step("DOS mkdir/chdir/rmdir operate on a real FAT directory tree",
             lambda: run_command(m, "serial", "dosrun C:/MKDIR.COM",
                                 ["DIRECTORY OK"], SHELL_PROMPT))
        step("DOS timestamps persist across close/reopen and reject unsupported handles",
             lambda: run_command(m, "serial", "dosrun C:/FTIME.COM",
                                 ["FILETIME OK"], SHELL_PROMPT))
        step("DOS create-new and rename work through INT 21h",
             lambda: run_command(m, "serial", "dosrun C:/FILEOPS.COM",
                                 ["FILEOPS OK"], SHELL_PROMPT))
        step("DOS legacy FCB rename works through INT 21h/AH=17h",
             lambda: run_command(m, "serial", "dosrun C:/FCBREN.COM",
                                 ["FCB RENAME OK"], SHELL_PROMPT))
        def probe_pgdn():
            m.type_serial("dosrun C:/PGDN.COM\n")
            time.sleep(0.25)
            m.monitor.sendall(b"sendkey pgdn 100\n")
            out = m.expect(SHELL_PROMPT, timeout=5)
            if "PGDN OK" not in out:
                raise TestFailure(f"PgDn BIOS scan probe failed:\n{out}")
        step("PS/2 Page Down reaches DOS BIOS INT 16h with scan 51h", probe_pgdn)
        def probe_keyboard_ports():
            m.type_serial("dosrun C:/KBDPORT.COM\n")
            time.sleep(0.25)
            m.monitor.sendall(b"sendkey pgdn 100\n")
            out = m.expect(SHELL_PROMPT, timeout=5)
            if "KBD PORT OK" not in out:
                raise TestFailure(f"8042 keyboard-port probe failed:\n{out}")
        step("PS/2 Page Down reaches DOS 8042 status/data ports", probe_keyboard_ports)
        step("a short direct-VGA text program renders before exit",
             lambda: run_command(m, "serial", "dosrun C:/VGA.COM",
                                 ["VGA OK"], SHELL_PROMPT))
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
    # Verify DOS create/write/rename on the persistent FAT16 image independently.
    renamed = os.path.join(workdir, "renamed.txt")
    mt(disk, "mcopy", "::/RENAMED.TXT", renamed)
    with open(renamed, "rb") as f:
        if f.read() != b"DATA":
            print("FAIL: DOS create/write/FCB rename file bytes"); failures += 1
        else:
            print("PASS: mtools confirms RENAMED.TXT contains DATA")
    m = Machine(kernel, ["-drive", f"file={disk},format=raw,if=none,id=ro,readonly=on",
                         "-device", "virtio-blk-pci,drive=ro"])
    try:
        m.expect(SHELL_PROMPT, timeout=60)
        step("read-only FAT timestamps can be read but cannot be changed",
             lambda: run_command(m, "serial", "dosrun --mount C=/n/vd0p1 C:/RTIME.COM",
                                 ["READONLY TIME OK"], SHELL_PROMPT))
    finally:
        m.close()
    listing = subprocess.check_output(["mdir", "-b", "-i", f"{disk}@@{PART_START*512}", "::/GROW"], text=True)
    if len(listing.splitlines()) != 40:
        print("FAIL: persistent directory growth to 40 files"); failures += 1
    else:
        print("PASS: mtools independently lists all 40 files across directory clusters")
    # Inspect the persistent FAT16 root entry, independently of DOS get-time.
    with open(disk, "rb") as f:
        f.seek(PART_START * 512); bpb = f.read(512)
        reserved = int.from_bytes(bpb[14:16], "little")
        fats, fatsectors = bpb[16], int.from_bytes(bpb[22:24], "little")
        roots = int.from_bytes(bpb[17:19], "little")
        f.seek((PART_START + reserved + fats * fatsectors) * 512)
        entries = f.read(roots * 32)
    entry = next((entries[i:i+32] for i in range(0, len(entries), 32)
                  if entries[i:i+11] == b"STAMP   DAT"), None)
    if entry is None or entry[22:26] != bytes.fromhex("5c646f0b") or int.from_bytes(entry[28:32], "little") != 3:
        print("FAIL: persisted FAT modification stamp/size"); failures += 1
    else:
        print("PASS: independent on-disk FAT timestamp and size after VM shutdown")
    print(f"\n{'FAILED' if failures else 'OK'}: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
