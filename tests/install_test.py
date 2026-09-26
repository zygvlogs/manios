#!/usr/bin/env python3
"""Install test (M14): the ManiOS boot loader, the ISO and the installer,
on QEMU's BIOS (SeaBIOS) -- no -kernel anywhere.

  - The ISO is well formed (read back here, and by xorriso if it is
    installed) and the same inputs give the same bytes.
  - It boots from a CD; the loader's prompt edits the command line.
  - The installer writes a blank disk (after asking), and refuses
    partitions, small disks, bad arguments and "no".
  - The installed disk boots on its own with the chosen command line,
    and can install itself on another disk.
  - The ISO written to a disk (a USB stick) boots; so does a disk read
    with CHS calls (an old BIOS), and a 6 MiB machine.
  - A much bigger build (a 5 MiB file in its boot archive: a 7 MiB
    kernel) boots, with the file whole: the loader puts the boot area
    wherever the kernel ends (0.17.1).
  - The loader stops with a message on too little memory (saying how
    much ManiOS needs), a damaged boot area, a bad header, a load
    address that would overwrite the loader, and a disk without a boot
    partition.

Usage: tests/install_test.py [build-directory]
"""
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

from console_test import SHELL_PROMPT, Machine, TestFailure, fnv1a, run_command, screen_text

SECTOR = 512
LOADER = "ManiOS boot loader "


def check(ok, what):
    if not ok:
        raise TestFailure(what)


def boot(disks=(), cdrom=None, order="c", memory=32):
    args = ["-boot", order]
    if cdrom:
        args += ["-cdrom", cdrom]
    for d in disks:
        args += ["-drive", f"file={d},format=raw,if=ide"]
    return Machine(None, args, memory)


def blank(path, mib):
    with open(path, "wb") as f:
        f.truncate(mib * 1024 * 1024)
    return path


class Install:
    def __init__(self, build, work):
        self.build, self.work = build, work
        self.iso = os.path.join(build, "manios.iso")
        self.area = open(os.path.join(build, "manios.bin"), "rb").read()
        self.version = open(os.path.join(os.path.dirname(build) or ".", "VERSION")).read().strip()

    def path(self, name):
        return os.path.join(self.work, name)

    # --- the ISO, read back -------------------------------------------------

    def iso_structure(self):
        data = open(self.iso, "rb").read()
        check(len(data) % 2048 == 0, "the ISO is not whole sectors")
        pvd = data[16 * 2048:17 * 2048]
        check(pvd[0] == 1 and pvd[1:6] == b"CD001", "no primary volume descriptor")
        check(struct.unpack("<I", pvd[80:84])[0] * 2048 == len(data), "the volume size is wrong")
        brvd = data[17 * 2048:18 * 2048]
        check(brvd[0] == 0 and brvd[7:30] == b"EL TORITO SPECIFICATION", "no El Torito record")
        catalog = struct.unpack("<I", brvd[71:75])[0] * 2048
        validation = data[catalog:catalog + 32]
        check(sum(struct.unpack("<16H", validation)) & 0xFFFF == 0 and validation[30:32] == b"\x55\xAA",
              "the boot catalog's validation entry is wrong")
        entry = data[catalog + 32:catalog + 64]
        check(entry[0] == 0x88 and entry[1] == 0, "the boot entry is not bootable no-emulation")
        cdboot_at = struct.unpack("<I", entry[8:12])[0] * 2048
        area_lba, area_len = struct.unpack("<II", data[cdboot_at + 8:cdboot_at + 16])
        check(data[area_lba * 2048:area_lba * 2048 + area_len] == self.area,
              "the boot image doesn't point at the boot area")
        # The hybrid MBR's partition covers the same bytes.
        check(data[510:512] == b"\x55\xAA", "no MBR signature in the system area")
        status, ptype, start, count = struct.unpack("<B3xB3xII", data[446:462])
        check(status == 0x80 and ptype == 0xDA and start == area_lba * 4
              and count == (area_len + 511) // 512, "the hybrid MBR's partition is wrong")
        # The root directory lists the files.
        root = struct.unpack("<I", pvd[156 + 2:156 + 6])[0] * 2048
        names, p = [], root
        while data[p]:
            names.append(data[p + 33:p + 33 + data[p + 32]])
            p += data[p]
        for name in [b"BOOT.CAT;1", b"CDBOOT.BIN;1", b"MANIOS.BIN;1", b"README.TXT;1", b"LICENSE.TXT;1"]:
            check(name in names, f"{name!r} is not in the root directory: {names}")
        # Another reader agrees, where there is one.
        if shutil.which("xorriso"):
            out = subprocess.run(["xorriso", "-indev", self.iso, "-ls", "/"], capture_output=True,
                                 text=True).stdout
            check("MANIOS.BIN" in out.upper() and "README.TXT" in out.upper(),
                  f"xorriso doesn't see the files: {out}")
        # The same inputs, the same image.
        again = self.path("again.iso")
        root_dir = os.path.dirname(self.build) or "."
        subprocess.run([sys.executable, os.path.join(root_dir, "tools", "mkiso.py"),
                        "--bootarea", os.path.join(self.build, "manios.bin"),
                        "--cdboot", os.path.join(self.build, "boot", "cdboot.bin"),
                        "--mbr", os.path.join(self.build, "boot", "mbr.bin"),
                        "--volume", "MANIOS_" + self.version.replace(".", "_"),
                        "--file", "README.TXT=" + os.path.join(root_dir, "README.md"),
                        "--file", "LICENSE.TXT=" + os.path.join(root_dir, "LICENSE"), "-o", again],
                       check=True)
        check(open(again, "rb").read() == data, "mkiso gave different bytes for the same inputs")

    # --- booting ------------------------------------------------------------

    def cd_boots(self):
        m = boot(cdrom=self.iso, order="d")
        try:
            out = m.expect(SHELL_PROMPT, timeout=120)
            for needle in [LOADER + self.version, "Boot options: (none)\r\n", "Loading ManiOS",
                           "Starting ManiOS", f"ManiOS {self.version} / ZKT", "bootmod: /dev/bootarea",
                           "Milestone M13", f"ManiOS {self.version} is ready."]:
                check(needle in out, f"CD boot output lacks {needle!r}:\n{out[-1500:]}")
            # The screen is quiet: what is checked, not the log (COM1 has that).
            screen = screen_text(m)
            for needle in [f"ManiOS {self.version} / ZKT", "Self-tests: memory, interrupts",
                           "cluster. All passed.", f"ManiOS {self.version} is ready.", "manios%"]:
                check(needle in screen, f"the screen lacks {needle!r}:\n{screen}")
            for needle in ["Milestone", "killed", "assertion", "utest"]:
                check(needle not in screen, f"the quiet screen shows {needle!r}:\n{screen}")
            run_command(m, "serial", "sum /dev/bootarea",
                        [f"{len(self.area)} bytes, fnv1a {fnv1a(self.area):08x}"], SHELL_PROMPT)
            run_command(m, "serial", "cat /dev/sysname", ["\r\nmanios\r\n"], SHELL_PROMPT)
        finally:
            m.close()

    def prompt_edits(self):
        m = boot(cdrom=self.iso, order="d")
        try:
            m.expect("Press any key within 3 seconds", timeout=60)
            m.type_serial(" ")
            m.expect("boot: ")
            m.type_serial("sysname=typxx\b\bed verbose=1\r")
            m.expect(SHELL_PROMPT, timeout=120)
            run_command(m, "serial", "cat /dev/sysname", ["\r\ntyped\r\n"], SHELL_PROMPT)
            # verbose=1: the boot log is on the screen too.
            screen = screen_text(m)
            check("Milestone M13" in screen and "cltest: all" in screen,
                  f"verbose=1 didn't show the log on the screen:\n{screen}")
        finally:
            m.close()
        # The keyboard works too, and Escape clears the line first.
        m = boot(cdrom=self.iso, order="d")
        try:
            m.expect("Press any key within 3 seconds", timeout=60)
            m.type_keyboard("x")
            m.expect("boot: ")
            m.type_serial("junk")
            m.expect("junk")  # echoed: the serial line and the keyboard don't race
            m.monitor.sendall(b"sendkey esc 10\n")
            m.expect("\b \b\b \b\b \b\b \b")
            m.type_keyboard("sysname=keys\n")
            m.expect(SHELL_PROMPT, timeout=120)
            run_command(m, "serial", "cat /dev/sysname", ["\r\nkeys\r\n"], SHELL_PROMPT)
        finally:
            m.close()

    def installs(self):
        disk, small = blank(self.path("disk.img"), 16), self.path("small.img")
        # The small disk has a pattern, to see that writes of part of a
        # block keep the rest of it.
        pattern = bytes(i % 251 for i in range(1024 * 1024))
        with open(small, "wb") as f:
            f.write(pattern)
        m = boot(disks=[disk, small], cdrom=self.iso, order="d")
        try:
            m.expect(SHELL_PROMPT, timeout=120)
            run_command(m, "serial", "install", ["usage: install [-y] DISK [KEY=VALUE...]"], SHELL_PROMPT)
            run_command(m, "serial", "install ata0p1", ["not a partition"], SHELL_PROMPT)
            run_command(m, "serial", "install nosuch", ["/dev/nosuch: no such file or directory"],
                        SHELL_PROMPT)
            run_command(m, "serial", "install -y ata0 notakeyvalue", ["not KEY=VALUE"], SHELL_PROMPT)
            run_command(m, "serial", "install -y ata1", ["the disk is too small"], SHELL_PROMPT)
            # Asked first; anything but "yes" writes nothing.
            m.type_serial("install ata0 sysname=installed\r")
            m.expect("EVERYTHING ON ata0 WILL BE LOST.")
            m.expect("Type yes to go on: ")
            m.type_serial("no\r")
            m.expect("Nothing was written.")
            m.expect(SHELL_PROMPT)
            run_command(m, "serial", "dd if=/dev/ata0 count=1 | sum",
                        [f"512 bytes, fnv1a {fnv1a(bytes(512)):08x}"], SHELL_PROMPT)
            m.type_serial("install ata0 sysname=installed rc=/boot/etc/rc.cpu\r")
            m.expect("Type yes to go on: ")
            m.type_serial("yes\r")
            m.expect(f"ManiOS {self.version} is installed on ata0.", timeout=120)
            m.expect(SHELL_PROMPT)
            # dd, on the other disk: the MOTD at block 3; and 200 bytes of
            # the boot area (from byte 10000) at byte 1000, across a block
            # boundary.
            run_command(m, "serial", "dd if=/boot/etc/motd of=/dev/ata1 seek=3",
                        ["0+1 records in\r\n0+1 records out"], SHELL_PROMPT)
            run_command(m, "serial", "dd if=/dev/bootarea of=/dev/ata1 bs=100 skip=100 seek=10 count=2",
                        ["2+0 records in\r\n2+0 records out"], SHELL_PROMPT)
        finally:
            m.close()
        # What the installer wrote, read here.
        image = open(disk, "rb").read()
        check(image[510:512] == b"\x55\xAA", "no MBR signature")
        mbr_off = struct.unpack_from("<I", self.area, 16)[0]
        check(image[:446] == self.area[mbr_off:mbr_off + 446], "the MBR boot code differs")
        status, ptype, start, count = struct.unpack("<B3xB3xII", image[446:462])
        check(status == 0x80 and ptype == 0xDA and start == 2048 and count % 2048 == 0
              and count * 512 >= len(self.area), f"the partition entry is wrong: {image[446:462]!r}")
        written = image[start * SECTOR:start * SECTOR + len(self.area)]
        want = bytearray(self.area)
        line = b"sysname=installed rc=/boot/etc/rc.cpu"
        want[256:512] = line.ljust(256, b"\0")
        check(written == bytes(want), "the boot area on the disk differs (or its command line)")
        check(zlib.crc32(written[2048:]) == struct.unpack_from("<I", written, 40)[0],
              "the installed boot area's checksum is wrong")
        motd = open(os.path.join(self.build, "bootfs", "etc", "motd"), "rb").read()
        other = open(small, "rb").read()
        check(other[3 * 512:3 * 512 + len(motd)] == motd, "dd didn't write the MOTD at block 3")
        check(other[3 * 512 + len(motd):4 * 512] == pattern[3 * 512 + len(motd):4 * 512],
              "writing the start of block 3 changed the rest of it")
        check(other[1000:1200] == self.area[10000:10200], "dd didn't write 200 bytes at byte 1000")
        check(other[:1000] == pattern[:1000] and other[1200:3 * 512] == pattern[1200:3 * 512],
              "writing bytes 1000-1199 changed the bytes around them")

    def disk_boots_and_clones(self):
        disk, clone = self.path("disk.img"), blank(self.path("clone.img"), 8)
        m = boot(disks=[disk, clone], order="c")
        try:
            out = m.expect(SHELL_PROMPT, timeout=120)
            check("Boot options: sysname=installed rc=/boot/etc/rc.cpu" in out,
                  f"the installed command line wasn't used:\n{out[-1500:]}")
            # rc.cpu ran: cpud refuses, as the machine has no key.
            check("cpud: this machine has no cluster key" in out, "the rc= script didn't run")
            run_command(m, "serial", "cat /dev/sysname", ["\r\ninstalled\r\n"], SHELL_PROMPT)
            run_command(m, "serial", "install -y ata1 sysname=clone", ["is installed on ata1"],
                        SHELL_PROMPT)
        finally:
            m.close()
        m = boot(disks=[clone], order="c")
        try:
            m.expect(SHELL_PROMPT, timeout=120)
            run_command(m, "serial", "cat /dev/sysname", ["\r\nclone\r\n"], SHELL_PROMPT)
        finally:
            m.close()

    def usb_boots(self):
        stick = self.path("stick.img")
        shutil.copy(self.iso, stick)
        m = boot(disks=[stick], order="c")
        try:
            out = m.expect(SHELL_PROMPT, timeout=120)
            check(LOADER + self.version in out and "Milestone M13" in out, out[-800:])
        finally:
            m.close()

    def chs_boots(self):
        disk = self.path("chs.img")
        subprocess.run([sys.executable, "tools/mkdisk.py", "--bootarea",
                        os.path.join(self.build, "manios-chs.bin"), "--cmdline", "sysname=oldbios",
                        "-o", disk], check=True)
        m = boot(disks=[disk], order="c")
        try:
            m.expect(SHELL_PROMPT, timeout=120)
            run_command(m, "serial", "cat /dev/sysname", ["\r\noldbios\r\n"], SHELL_PROMPT)
        finally:
            m.close()

    def small_machine(self):
        m = boot(cdrom=self.iso, order="d", memory=6)
        try:
            out = m.expect(SHELL_PROMPT, timeout=120)
            check("Milestone M13" in out, out[-800:])
        finally:
            m.close()

    def big_boots(self):
        """A build with a 5 MiB file in its boot archive (the Makefile's
        test image): a 7 MiB kernel and a 6 MiB boot area, which the
        loader puts just past the kernel -- far past the 4 MiB it allowed
        until 0.17.1."""
        big = os.path.join(self.build, "big")
        area = open(os.path.join(big, "manios.bin"), "rb").read()
        load, total = struct.unpack_from("<I", area, 64)[0], struct.unpack_from("<I", area, 12)[0]
        check(load > 0x400000 and total > 0x400000,
              f"the big image isn't big: loaded at {load:#x}, {total} bytes")
        pad = random.Random(17).randbytes(5 << 20)
        m = boot(cdrom=os.path.join(big, "manios.iso"), order="d", memory=32)
        try:
            m.expect(SHELL_PROMPT, timeout=180)
            run_command(m, "serial", "sum /boot/etc/pad",
                        [f"/boot/etc/pad: {len(pad)} bytes, fnv1a {fnv1a(pad):08x}"], SHELL_PROMPT)
        finally:
            m.close()
        # Too little memory for it: the loader says how much it needs.
        need = -(-(load + total + 2 * 1024 * 1024) // (1024 * 1024))
        m = boot(cdrom=os.path.join(big, "manios.iso"), order="d", memory=need - 2)
        try:
            m.expect(f"not enough memory: ManiOS needs {need} MiB", timeout=60)
        finally:
            m.close()

    def refusals(self):
        def loader_says(disks, cdrom, order, message, memory=32):
            m = boot(disks=disks, cdrom=cdrom, order=order, memory=memory)
            try:
                m.expect(message, timeout=60)
            finally:
                m.close()

        # What ManiOS needs: its images, up to where the boot area ends,
        # and 2 MiB to run in (boot/bootarea.h), rounded up.
        load, total = struct.unpack_from("<I", self.area, 64)[0], struct.unpack_from("<I", self.area, 12)[0]
        need = -(-(load + total + 2 * 1024 * 1024) // (1024 * 1024))
        loader_says([], self.iso, "d", f"not enough memory: ManiOS needs {need} MiB", memory=need - 2)
        good = self.path("good.img")
        subprocess.run([sys.executable, "tools/mkdisk.py", "--bootarea",
                        os.path.join(self.build, "manios.bin"), "-o", good], check=True)
        image = bytearray(open(good, "rb").read())
        kernel_off = struct.unpack_from("<I", self.area, 32)[0]
        damaged = bytearray(image)
        damaged[2048 * 512 + kernel_off + 5000] ^= 0x40
        open(self.path("damaged.img"), "wb").write(damaged)
        loader_says([self.path("damaged.img")], None, "c",
                    "the boot area is damaged (its checksum is wrong)")
        header = bytearray(image)
        header[2048 * 512] ^= 0xFF  # the magic
        open(self.path("header.img"), "wb").write(header)
        loader_says([self.path("header.img")], None, "c", "ManiOS: bad boot area")
        low = bytearray(image)
        struct.pack_into("<I", low, 2048 * 512 + 64, 0x8000)  # over stage 2 itself
        open(self.path("low.img"), "wb").write(low)
        loader_says([self.path("low.img")], None, "c", "the boot area's header is damaged")
        nopart = bytearray(image)
        nopart[446 + 4] = 0x83  # a partition, but not ManiOS's
        open(self.path("nopart.img"), "wb").write(nopart)
        loader_says([self.path("nopart.img")], None, "c", "ManiOS: no boot partition")


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    if build.endswith(".elf"):  # `make test` passes the kernel
        build = os.path.dirname(build)
    work = tempfile.mkdtemp(prefix="zkt-install-")
    t = Install(build, work)
    failures = 0
    try:
        for name, step in [
            ("the ISO: ISO 9660, El Torito, the hybrid MBR, reproducible", t.iso_structure),
            ("booting from the CD: the loader, the kernel, /dev/bootarea", t.cd_boots),
            ("the loader's prompt edits the command line (serial and keyboard)", t.prompt_edits),
            ("the installer: refusals, asking first, writing and checking a disk; dd",
             t.installs),
            ("the installed disk boots with its command line, and installs a clone", t.disk_boots_and_clones),
            ("the ISO written to a disk (a USB stick) boots", t.usb_boots),
            ("a disk read with CHS calls, as on an old BIOS", t.chs_boots),
            ("a 6 MiB machine boots the CD", t.small_machine),
            ("a much bigger ManiOS (a 7 MiB kernel) boots, and the loader says what it needs",
             t.big_boots),
            ("the loader explains: too little memory, damage, a bad header, a bad load address, "
             "no partition",
             t.refusals),
        ]:
            try:
                step()
                print(f"PASS: {name}")
            except TestFailure as e:
                print(f"FAIL: {name}: {e}")
                failures += 1
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("install test: " + ("all passed" if not failures else f"{failures} failed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
