#!/usr/bin/env python3
"""ManiDOS test (M19): the `dos` command on a disk made for it.

The disk has two FAT volumes, built with mtools: C: (FAT16, labelled
MANIDOS) with text files, batch files and a DOS program ManiDOS must
refuse; and D: (FAT12) with faults put in it on purpose -- a lost chain,
a cross-linked file, a file longer than its chain, and FAT copies that
differ -- for CHKDSK to find. What CHKDSK and DIR report is checked
against the image itself (its boot sector, its FAT) and against what
mtools says about it.

ManiDOS is driven over the serial line, at its own prompt; and a machine
booted with shell=dos starts in ManiDOS.

Usage: tests/dos_test.py [path-to-kernel-elf]
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

import console_test
from console_test import SHELL_PROMPT, Machine, TestFailure, mbr_entry, run_command

DISK_SECTORS = 32768                   # 16 MiB
C_START, C_SECTORS = 2048, 16384       # FAT16, 1 sector per cluster
D_START, D_SECTORS = 18432, 8192       # FAT12, 8 sectors per cluster
VERSION = console_test.VERSION

# (Text shown whole, by TYPE, has ManiOS's line ends: the serial line
# would show a DOS file's CR LF as CR CR LF. FIND, SORT and MORE read
# lines, and take DOS's CR LF too.)
README = b"Welcome to ManiDOS.\nA disk operating system for ManiOS.\n"
NAMES = b"mango\r\nApple\r\ncherry\r\nbanana\r\n"
LONG = b"".join(b"line %02d\r\n" % i for i in range(1, 41))
TEST_BAT = b"""@ECHO OFF
REM Exercises what a batch file can do.
ECHO first=%1 all=%0
SET GREETING=hello from a batch file
IF "%1"=="" GOTO NOARGS
IF %1==one ECHO one given
IF NOT %2==two ECHO no two
IF EXIST README.TXT ECHO README is here
IF NOT EXIST NOPE.TXT ECHO NOPE is not
SHIFT
ECHO shifted=%1
FOR %%F IN (DOCS\\*.TXT) DO ECHO found %%F
CALL SUB.BAT called
ECHO back in TEST
FIND "zzz" README.TXT > NUL
IF ERRORLEVEL 1 ECHO find said 1
GOTO END
:NOARGS
ECHO no arguments
:END
ECHO done
""".replace(b"\n", b"\r\n")
SUB_BAT = b"ECHO in SUB with %1\r\n"


def mt(path, part, tool, *args):
    subprocess.run([tool, "-i", f"{path}@@{part * 512}", *args], check=True,
                   stdout=subprocess.DEVNULL)


def put(path, part, name, data, srcdir):
    src = os.path.join(srcdir, name.replace("/", "_"))
    with open(src, "wb") as f:
        f.write(data)
    mt(path, part, "mcopy", src, "::/" + name)


class Volume:
    """A FAT12/16 volume in the image, read as ManiDOS reads it."""

    def __init__(self, img, start):
        self.img, self.base = img, start * 512
        b = img[self.base:self.base + 512]
        self.bps, self.spc = struct.unpack_from("<H", b, 11)[0], b[13]
        self.reserved, self.fats = struct.unpack_from("<H", b, 14)[0], b[16]
        self.root_entries = struct.unpack_from("<H", b, 17)[0]
        total = struct.unpack_from("<H", b, 19)[0] or struct.unpack_from("<I", b, 32)[0]
        self.fat_sectors = struct.unpack_from("<H", b, 22)[0]
        self.serial = struct.unpack_from("<I", b, 39)[0]
        self.root_start = self.reserved + self.fats * self.fat_sectors
        self.data_start = self.root_start + (self.root_entries * 32 + self.bps - 1) // self.bps
        self.clusters = (total - self.data_start) // self.spc
        self.bits = 12 if self.clusters < 4085 else 16
        self.cluster_size = self.bps * self.spc

    def fat_offset(self, copy=0):
        return self.base + (self.reserved + copy * self.fat_sectors) * self.bps

    def entry(self, n, copy=0):
        o = self.fat_offset(copy)
        if self.bits == 16:
            return struct.unpack_from("<H", self.img, o + 2 * n)[0]
        v = struct.unpack_from("<H", self.img, o + n + n // 2)[0]
        return v >> 4 if n & 1 else v & 0xFFF

    def set_entry(self, n, value, copies=(0, 1)):
        for copy in copies:
            o = self.fat_offset(copy)
            if self.bits == 16:
                struct.pack_into("<H", self.img, o + 2 * n, value)
                continue
            at = o + n + n // 2
            v = struct.unpack_from("<H", self.img, at)[0]
            v = (v & 0x000F) | (value << 4) if n & 1 else (v & 0xF000) | value
            struct.pack_into("<H", self.img, at, v)

    def free(self):
        return sum(1 for c in range(2, self.clusters + 2) if self.entry(c) == 0)

    def root_entry(self, name83):
        o = self.base + self.root_start * self.bps
        for i in range(self.root_entries):
            e = o + 32 * i
            if self.img[e:e + 11] == name83:
                return e
        raise TestFailure(f"no {name83!r} in the root directory")

    def serial_text(self):
        return f"{self.serial >> 16:04X}-{self.serial & 0xFFFF:04X}"


def build_disk(path):
    """The disk, and what the checks need to know about it."""
    img = bytearray(DISK_SECTORS * 512)
    img[446:462] = mbr_entry(0x00, 0x06, C_START, C_SECTORS)
    img[462:478] = mbr_entry(0x00, 0x01, D_START, D_SECTORS)
    img[510:512] = b"\x55\xaa"
    with open(path, "wb") as f:
        f.write(img)
    src = path + ".src"
    os.makedirs(src)
    mt(path, C_START, "mformat", "-T", str(C_SECTORS), "-h", "16", "-s", "63", "-H", str(C_START),
       "-c", "1", "-v", "MANIDOS", "::")
    mt(path, D_START, "mformat", "-T", str(D_SECTORS), "-h", "16", "-s", "63", "-H", str(D_START),
       "-c", "8", "::")
    for name, data in [("README.TXT", README), ("NAMES.TXT", NAMES), ("LONG.TXT", LONG),
                       ("TEST.BAT", TEST_BAT), ("SUB.BAT", SUB_BAT),
                       ("GAME.EXE", b"MZ" + bytes(62))]:
        put(path, C_START, name, data, src)
    mt(path, C_START, "mmd", "::/DOCS")
    put(path, C_START, "DOCS/LETTER.TXT", b"Dear reader,\r\n", src)
    put(path, C_START, "DOCS/NOTES.TXT", b"notes\n", src)
    for name, data in [("OK.TXT", b"fine\r\n"), ("CROSS1.TXT", b"one\r\n"),
                       ("CROSS2.TXT", b"two\r\n"), ("SHORT.TXT", b"x" * 100)]:
        put(path, D_START, name, data, src)
    mdir = subprocess.run(["mdir", "-i", f"{path}@@{C_START * 512}", "::"], check=True,
                          capture_output=True, text=True).stdout
    c_free = int(re.search(r"([\d ]+) bytes free", mdir).group(1).replace(" ", ""))

    with open(path, "rb") as f:
        img = bytearray(f.read())
    # C:'s label is in its root directory; its boot sector is made to say
    # "NO NAME", so a label shown is the root directory's, as DOS has it.
    img[C_START * 512 + 43:C_START * 512 + 54] = b"NO NAME    "
    c, d = Volume(img, C_START), Volume(img, D_START)
    # D:'s faults. A lost chain: two free clusters linked, in both FATs.
    free = [n for n in range(2, d.clusters + 2) if d.entry(n) == 0]
    lost_a, lost_b = free[-2], free[-1]
    d.set_entry(lost_a, lost_b)
    d.set_entry(lost_b, 0xFFF)
    # CROSS2.TXT made to start where CROSS1.TXT does: its own cluster is lost.
    cross1, cross2 = d.root_entry(b"CROSS1  TXT"), d.root_entry(b"CROSS2  TXT")
    shared = struct.unpack_from("<H", img, cross1 + 26)[0]
    struct.pack_into("<H", img, cross2 + 26, shared)
    # SHORT.TXT's size says three clusters; its chain has one.
    short = d.root_entry(b"SHORT   TXT")
    struct.pack_into("<I", img, short + 28, 3 * d.cluster_size)
    # And the second FAT differs from the first at an unused entry.
    d.set_entry(free[-3], 0xFF7, copies=(1,))
    with open(path, "wb") as f:
        f.write(img)
    return {"c": c, "d": Volume(bytes(img), D_START), "c_free": c_free, "shared": shared}


def dos(m, command, needles, prompt="C:\\>"):
    """Types a command at ManiDOS's prompt; checks its output."""
    m.type_serial(command + "\r")
    out = m.expect(("\r\n" + prompt).encode(), timeout=30)
    out = out.decode("latin-1") if isinstance(out, bytes) else out
    for needle in needles:
        if needle.startswith("!"):
            if needle[1:] in out:
                raise TestFailure(f"{command!r}: unexpected {needle[1:]!r} in:\n{out}")
        elif needle not in out:
            raise TestFailure(f"{command!r}: expected {needle!r} in:\n{out}")
    return out


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/manios-zkt.elf"
    workdir = tempfile.mkdtemp(prefix="zkt-dos-")
    disk = os.path.join(workdir, "dos.img")
    facts = build_disk(disk)
    c, d = facts["c"], facts["d"]
    failures = 0

    def step(name, fn):
        nonlocal failures
        try:
            fn()
            print(f"PASS: {name}")
            return True
        except TestFailure as e:
            print(f"FAIL: {name}: {e}")
            failures += 1
            return False

    m = Machine(kernel, ["-drive", f"file={disk},format=raw,if=ide"])
    try:
        m.expect(SHELL_PROMPT, timeout=60)
        cases = [
            ("dos /C: one command from ManiOS's shell",
             lambda: run_command(m, "serial", "dos /c VER",
                                 [f"ManiDOS Version 1.0, on ManiOS {VERSION} (ZKT)"], SHELL_PROMPT)),
            ("dos /C DRIVES: the drives and where they are",
             lambda: run_command(m, "serial", "dos /c DRIVES",
                                 ["A:  boot disk", "/boot", "C:  FAT16", "/n/ata0p1  on /dev/ata0p1",
                                  "D:  FAT12", "/n/ata0p2", "Z:  ManiOS"], SHELL_PROMPT)),
            ("dos: the banner, AUTOEXEC.BAT quietly, and the C:\\> prompt",
             lambda: (m.type_serial("dos\r"),
                      m.expect("Drives:  A: boot disk  C: ata0p1  D: ata0p2  E: cd0  Z: ManiOS"),
                      m.expect("\r\nC:\\>"))),
            ("DIR: the label and serial number from the boot sector, bytes free as mtools says",
             lambda: dos(m, "DIR", [" Volume in drive C is MANIDOS",
                                    f" Volume Serial Number is {c.serial_text()}",
                                    " Directory of C:\\", "README   TXT            56",
                                    "DOCS            <DIR>", "GAME     EXE            64",
                                    "        6 file(s)", "        1 dir(s)",
                                    f"{facts['c_free']:,} bytes free"])),
            ("DIR with a pattern, /B, and a pattern matching nothing",
             lambda: (dos(m, "DIR *.BAT", ["TEST     BAT", "SUB      BAT", "!README", "2 file(s)"]),
                      dos(m, "DIR /B DOCS", ["\r\nLETTER.TXT\r\nNOTES.TXT\r\n", "!Volume"]),
                      dos(m, "DIR /B ????.TXT", ["\r\nLONG.TXT\r\n", "!README", "!NAMES"]),
                      dos(m, "DIR NOPE.*", ["File not found"]))),
            ("CD, the prompt following it, CD.. and CD\\, and a bad directory",
             lambda: (dos(m, "CD DOCS", [], "C:\\DOCS>"),
                      dos(m, "CD", ["C:\\DOCS"], "C:\\DOCS>"),
                      dos(m, "DIR", [" Directory of C:\\DOCS", ".            <DIR>",
                                     "..           <DIR>", "LETTER   TXT"], "C:\\DOCS>"),
                      dos(m, "CD..", []),
                      dos(m, "CD DOCS", [], "C:\\DOCS>"),
                      dos(m, "CD\\", []),
                      dos(m, "CD DOCS\\..\\..\\..\\DOCS", [], "C:\\DOCS>"),  # never above C:\\
                      dos(m, "CD \\", []),
                      dos(m, "CD NOPE", ["Invalid directory"]))),
            ("TYPE, and TYPE of what isn't a file",
             lambda: (dos(m, "TYPE README.TXT", ["Welcome to ManiDOS.\r\nA disk operating system"]),
                      dos(m, "TYPE DOCS\\LETTER.TXT", ["Dear reader,"]),
                      dos(m, "TYPE NOPE.TXT", ["File not found - NOPE.TXT"]),
                      dos(m, "TYPE DOCS", ["Access denied - DOCS is a directory"]))),
            ("COPY to CON; the drives are read-only for the rest",
             lambda: (dos(m, "COPY DOCS\\*.TXT CON", ["C:\\DOCS\\LETTER.TXT", "Dear reader,",
                                                      "C:\\DOCS\\NOTES.TXT", "2 file(s) copied"]),
                      dos(m, "COPY README.TXT NEW.TXT", ["Access denied - ManiOS's drives are read-only"]),
                      dos(m, "DEL README.TXT", ["Access denied", "DEL can't work yet"]),
                      dos(m, "MD NEWDIR", ["Access denied"]),
                      dos(m, "FORMAT D:", ["FORMAT isn't in ManiDOS"]))),
            ("drive letters: D:, a drive that isn't there",
             lambda: (dos(m, "D:", [], "D:\\>"),
                      dos(m, "DIR", [" Volume in drive D has no label", "CROSS1   TXT"], "D:\\>"),
                      dos(m, "C:", []),
                      dos(m, "Q:", ["Invalid drive specification"]),
                      dos(m, "VOL Q:", ["Invalid drive specification"]))),
            ("VOL: C:'s label, D:'s serial number",
             lambda: (dos(m, "VOL", [" Volume in drive C is MANIDOS"]),
                      dos(m, "VOL D:", [" Volume in drive D has no label",
                                        f" Volume Serial Number is {d.serial_text()}"]))),
            ("CHKDSK C:: a sound disk, its figures as the FAT and mtools have them",
             lambda: dos(m, "CHKDSK", [
                 f"Volume MANIDOS\r\nVolume Serial Number is {c.serial_text()}",
                 f"{c.clusters * c.cluster_size:13,} bytes total disk space",
                 "          512 bytes in 1 directory",
                 "bytes in 8 user files",
                 f"{facts['c_free']:13,} bytes available on disk",
                 "          512 bytes in each allocation unit",
                 f"{c.clusters:13,} total allocation units on disk",
                 f"{c.free():13,} available allocation units on disk",
                 "!lost", "!problem", "!cross"])),
            ("CHKDSK D:: the faults put there -- cross-link, sizes, a lost chain, FAT copies",
             lambda: dos(m, "CHKDSK D:", [
                 f"D:\\CROSS2.TXT  Is cross-linked on allocation unit {facts['shared']}",
                 "D:\\CROSS2.TXT  Allocation error: its size needs 1 allocation units, its chain has 0",
                 "D:\\SHORT.TXT  Allocation error: its size needs 3 allocation units, its chain has 1",
                 "3 lost allocation units found in 2 chains.",
                 "The copies of the file allocation table differ.",
                 "5 problems found. ManiDOS doesn't correct them",
                 f"{(d.clusters) * d.cluster_size:13,} bytes total disk space",
                 f"{3 * d.cluster_size:13,} bytes in lost chains",
                 f"{d.free() * d.cluster_size:13,} bytes available on disk"])),
            ("CHKDSK of the boot disk, which isn't FAT",
             lambda: dos(m, "CHKDSK A:", ["CHKDSK checks FAT disks; drive A is the boot disk"])),
            ("TREE /F",
             lambda: dos(m, "TREE /F", ["Directory PATH listing for Volume MANIDOS",
                                        f"Volume serial number is {c.serial_text()}",
                                        "\r\nC:\\\r\n|   README.TXT", "\\---DOCS\r\n        LETTER.TXT"])),
            ("FIND, its switches, and its errorlevel",
             lambda: (dos(m, "FIND \"ManiOS\" README.TXT", ["---------- C:\\README.TXT",
                                                            "A disk operating system for ManiOS."]),
                      dos(m, "FIND /C \"line\" LONG.TXT", ["---------- C:\\LONG.TXT: 40"]),
                      dos(m, "FIND /I /N \"APPLE\" NAMES.TXT", ["[2]Apple"]),
                      dos(m, "FIND /V /C \"a\" NAMES.TXT", ["NAMES.TXT: 2"]),  # Apple, cherry
                      dos(m, "FIND \"zzz\" README.TXT", ["!Welcome"]),
                      dos(m, "IF ERRORLEVEL 1 ECHO nothing found", ["\r\nnothing found\r\n"]))),
            ("SORT from a file, and pipelines",
             lambda: (dos(m, "SORT < NAMES.TXT", ["\r\nApple\r\nbanana\r\ncherry\r\nmango\r\n"]),
                      dos(m, "TYPE NAMES.TXT | SORT /R", ["\r\nmango\r\ncherry\r\nbanana\r\nApple\r\n"]),
                      dos(m, "DIR /B | FIND \"BAT\"", ["\r\nTEST.BAT\r\nSUB.BAT\r\n", "!README"]),
                      dos(m, "TYPE LONG.TXT | FIND \"line 3\" | FIND /C \"line\"", ["\r\n10\r\n"]))),  # line 30-39
            ("MORE: a screen, then Enter for the rest",
             lambda: (m.type_serial("MORE < LONG.TXT\r"),
                      m.expect("line 23\r\n-- More --"),
                      m.type_serial("\r"),
                      m.expect("line 40\r\n"),
                      m.expect("\r\nC:\\>"))),
            ("redirection: to NUL, CON and a device; not to a file",
             lambda: (dos(m, "TYPE README.TXT > NUL", ["!Welcome"]),
                      dos(m, "ECHO to the screen > CON", ["\r\nto the screen\r\n"]),
                      dos(m, "TYPE README.TXT > Z:\\DEV\\NULL", ["!Welcome", "!denied"]),
                      dos(m, "DIR > LIST.TXT", ["Access denied - ManiOS's drives are read-only"]))),
            ("ManiOS programs: DOS paths in their arguments, and their errorlevel",
             lambda: (dos(m, "wc README.TXT", ["      2       9      56 /n/ata0p1/readme.txt"]),
                      dos(m, "grep -c line C:\\LONG.TXT", ["\r\n40\r\n"]),
                      dos(m, "head -n 1 DOCS\\NOTES.TXT", ["\r\nnotes\r\n"]),
                      dos(m, "grep zzz README.TXT", []),
                      dos(m, "IF ERRORLEVEL 1 ECHO grep found nothing", ["\r\ngrep found nothing\r\n"]))),
            ("what isn't a command, and a DOS program",
             lambda: (dos(m, "NOPE", ["Bad command or file name"]),
                      dos(m, "GAME", ["C:\\GAME.EXE: ManiDOS runs batch files and ManiOS programs, "
                                      "not DOS programs"]))),
            ("SET, %VARIABLES%, PROMPT and PATH",
             lambda: (dos(m, "SET X=hello", []),
                      dos(m, "ECHO %X% there", ["\r\nhello there\r\n"]),
                      dos(m, "SET", ["PATH=Z:\\BIN", "PROMPT=$P$G", "X=hello"]),
                      dos(m, "SET X=", []),
                      dos(m, "SET", ["!X=hello"]),
                      dos(m, "PROMPT [$N]", [], "[C]"),
                      dos(m, "PROMPT", []),
                      dos(m, "PATH", ["PATH=Z:\\BIN"]))),
            ("a batch file: parameters, IF, GOTO, SHIFT, FOR, CALL, ERRORLEVEL",
             lambda: (dos(m, "TEST one two", [
                 "\r\nfirst=one all=TEST\r\none given\r\nREADME is here\r\nNOPE is not\r\n"
                 "shifted=two\r\nfound DOCS\\LETTER.TXT\r\nfound DOCS\\NOTES.TXT\r\n"
                 "in SUB with called\r\nback in TEST\r\nfind said 1\r\ndone\r\n",
                 "!no two", "!ECHO first", "!no arguments"]),
                 dos(m, "TEST", ["no arguments", "done", "!shifted"]),
                 dos(m, "ECHO %GREETING%", ["\r\nhello from a batch file\r\n"]))),
            ("FOR at the prompt, and ECHO OFF hiding the prompt",
             lambda: (dos(m, "FOR %F IN (*.BAT) DO ECHO batch %F", ["batch TEST.BAT", "batch SUB.BAT"]),
                      m.type_serial("ECHO OFF\r"),
                      m.expect("ECHO OFF\r\n"),
                      dos(m, "ECHO ON", ["!C:\\>"]))),
            ("A:, the boot disk: AUTOEXEC.BAT; Z:, all of ManiOS",
             lambda: (dos(m, "A:", [], "A:\\>"),
                      dos(m, "DIR", [" Volume in drive A is MANIOS-BOOT", "AUTOEXEC BAT", "BIN             <DIR>"],
                          "A:\\>"),
                      dos(m, "TYPE AUTOEXEC.BAT", ["PATH Z:\\BIN"], "A:\\>"),
                      dos(m, "Z:", [], "Z:\\>"),
                      dos(m, "TYPE DEV\\SYSNAME", ["\r\nmanios\r\n"], "Z:\\>"),
                      dos(m, "C:", []))),
            ("VER, MEM, DATE, TIME, HELP, TRUENAME",
             lambda: (dos(m, "VER", [f"ManiDOS Version 1.0, on ManiOS {VERSION} (ZKT)"]),
                      dos(m, "MEM", ["K total memory", "K free", "ManiDOS is using "]),
                      dos(m, "DATE", ["Current date is "]),
                      dos(m, "TIME", ["Current time is ", "(UTC)"]),
                      dos(m, "HELP", ["CHKDSK", "TREE", "EXIT"]),
                      dos(m, "HELP DIR", ["DIR       [PATH] [/W] [/B] [/P] [/S]"]),
                      dos(m, "TRUENAME DOCS\\LETTER.TXT", ["C:\\DOCS\\LETTER.TXT = /n/ata0p1/docs/letter.txt"]))),
            ("EXIT: back to ManiOS",
             lambda: (m.type_serial("EXIT\r"), m.expect(SHELL_PROMPT))),
        ]
        for name, fn in cases:
            if not step(name, fn):
                break
    finally:
        m.close()

    # shell=dos: the machine starts in ManiDOS; EXIT reaches the monitor.
    m = Machine(kernel, ["-append", "shell=dos"])
    try:
        step("shell=dos: ManiOS boots into ManiDOS (no FAT disk: A:)",
             lambda: (m.expect("ManiDOS 1.0 -- a disk operating system for ManiOS", timeout=60),
                      m.expect("\r\nA:\\>"),
                      dos(m, "DIR BIN\\DOS", ["DOS"], "A:\\>"),
                      m.type_serial("EXIT\r"),
                      m.expect("console: the shell has exited; this is the kernel monitor "
                               "(run /bin/dos to go back)")))
    finally:
        m.close()

    print("dos test: " + ("all passed" if not failures else f"{failures} failed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
