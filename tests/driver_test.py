#!/usr/bin/env python3
"""Driver test (M20): the drivers of 0.20 on QEMU's emulated hardware.

Storage: floppies (1.44 MB and 720 KB disks, a second drive, a
write-protected disk; reads checked by hash, writes checked in the image
file, and a read after a write through the driver's cylinder cache), CDs
(Rock Ridge, Joliet and plain ISO 9660 names, built with xorriso; a disc
taken out and others put in while ManiOS runs; the drive on IDE and on
SATA), the ManiOS ISO as a hard disk, AHCI disks, virtio disks (and a
read-only one), ManiDOS's drive letters for all of them, and installing
ManiOS on a SATA disk and on a virtio disk, then booting it.

Sound: QEMU records what the machine plays into a WAV file (its "wav"
audio backend), and this script measures it -- each tone's pitch (by
counting zero crossings) and length: the PC speaker, the Sound Blaster
16, AC'97 (tones, and WAV files at other rates and sizes, which `play`
converts), and the AdLib's FM synthesizer.

The rest: COM2 both ways and its speed (a socket on the host side), COM3
into a file, the parallel port into a file, /dev/nvram, and /dev/power:
`poweroff` and `reboot` must make QEMU exit (it runs with -no-reboot),
on the PC machine (keyboard controller reset) and on q35 (ACPI's reset
register).

Network cards are tested by net_test.py, which runs its whole suite on
every card. Needs mtools and xorriso.

Usage: tests/driver_test.py [path-to-kernel-elf [storage|sound|misc]...]
"""
import os
import random
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

import console_test
from console_test import SHELL_PROMPT, Machine, TestFailure, fnv1a, mbr_entry, run_command

RATE = 44100


def sh(m, command, needles=(), timeout=None):
    """A command at the shell; its output, checked for needles."""
    if timeout:
        m.type_serial(command + "\r")
        out = m.expect(SHELL_PROMPT, timeout=timeout)
        for needle in needles:
            if (needle[1:] in out) if needle.startswith("!") else (needle not in out):
                raise TestFailure(f"{command!r}: {needle!r} in:\n{out}")
        return out
    return run_command(m, "serial", command, needles, SHELL_PROMPT)


def check(ok, message):
    if not ok:
        raise TestFailure(message)


# --- images --------------------------------------------------------------

def floppy(path, size_kb, label, files):
    subprocess.run(["mformat", "-C", "-f", str(size_kb), "-v", label, "-i", path, "::"],
                   check=True, stdout=subprocess.DEVNULL)
    for name, data in files.items():
        src = path + "." + name
        with open(src, "wb") as f:
            f.write(data)
        subprocess.run(["mcopy", "-i", path, src, "::" + name], check=True)


def fat_disk(path, label, files):
    """16 MiB, one FAT partition from sector 2048."""
    img = bytearray(16 * 1024 * 1024)
    img[446:462] = mbr_entry(0, 0x06, 2048, 30720)
    img[510:512] = b"\x55\xaa"
    with open(path, "wb") as f:
        f.write(img)
    part = f"{path}@@{2048 * 512}"
    subprocess.run(["mformat", "-i", part, "-T", "30720", "-h", "16", "-s", "63", "-H", "2048",
                    "-v", label, "::"], check=True, stdout=subprocess.DEVNULL)
    for name, data in files.items():
        src = path + "." + name
        with open(src, "wb") as f:
            f.write(data)
        subprocess.run(["mcopy", "-i", part, src, "::" + name], check=True)


def iso(path, tree, names, volid):
    """An ISO of `tree` (a directory) with Rock Ridge, Joliet or neither."""
    flags = {"rr": ["-rockridge", "on", "-joliet", "off"],
             "joliet": ["-rockridge", "off", "-joliet", "on"],
             "plain": ["-rockridge", "off", "-joliet", "off"]}[names]
    subprocess.run(["xorriso", "-outdev", path, *flags, "-volid", volid, "-map", tree, "/"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def wav_file(path, rate, channels, bits, hz, ms):
    """A PCM WAV file of a sine tone (Python has floating point)."""
    import math
    n = rate * ms // 1000
    frames = bytearray()
    for i in range(n):
        v = math.sin(2 * math.pi * hz * i / rate)
        for _ in range(channels):
            if bits == 16:
                frames += struct.pack("<h", int(v * 20000))
            else:
                frames += bytes([128 + int(v * 90)])
    fmt = struct.pack("<HHIIHH", 1, channels, rate, rate * channels * bits // 8,
                      channels * bits // 8, bits)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(frames)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<I", 16) + fmt)
        f.write(b"data" + struct.pack("<I", len(frames)) + frames)


# --- what QEMU recorded ------------------------------------------------------

def wav_left(path):
    """The left channel of a WAV file QEMU wrote (its header's sizes may
    not be final: the data is taken to the end of the file)."""
    with open(path, "rb") as f:
        d = f.read()
    i, rate, channels = 12, RATE, 2
    while i + 8 <= len(d):
        tag, size = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        if tag == b"fmt ":
            channels, rate = struct.unpack("<HI", d[i + 10:i + 16])
        if tag == b"data":
            body = d[i + 8:]
            n = len(body) // 2
            return rate, struct.unpack(f"<{n}h", body[:2 * n])[0::channels]
        i += 8 + size
    raise TestFailure(f"{path}: no data in the recording")


def pitch(rate, s):
    mean = sum(s) / len(s)
    ups = [i for i in range(1, len(s)) if s[i - 1] - mean < 0 <= s[i] - mean]
    return (len(ups) - 1) * rate / (ups[-1] - ups[0]) if len(ups) > 2 else 0


def tones(path, window=0.05, loud=800):
    """The recording as runs of sound: [(Hz, seconds)], silences as 0 Hz.
    A window takes the pitch of the run it continues (within 3%)."""
    rate, s = wav_left(path)
    n = int(rate * window)
    runs = []
    for i in range(0, len(s) - n, n):
        w = s[i:i + n]
        hz = pitch(rate, w) if max(abs(v) for v in w) > loud else 0
        if runs and abs(runs[-1][0] - hz) <= max(8, 0.03 * hz):
            runs[-1][1] += window
        else:
            runs.append([hz, window])
    # Drop the one-window runs where a note starts or stops mid-window.
    return [(round(hz), round(t, 2)) for hz, t in runs if t > window * 1.5]


def expect_tones(path, wanted, what):
    """`wanted`: (Hz, ms) in order, each found within 2% and 80 ms."""
    runs = [r for r in tones(path) if r[0]]
    pos = 0
    for hz, ms in wanted:
        while pos < len(runs) and not (abs(runs[pos][0] - hz) <= 0.02 * hz
                                       and abs(runs[pos][1] * 1000 - ms) <= 80):
            pos += 1
        check(pos < len(runs), f"{what}: no {hz} Hz tone of {ms} ms in the recording: {tones(path)}")
        pos += 1


# --- the machines ------------------------------------------------------------

class Suite:
    def __init__(self):
        self.failures = 0

    def step(self, name, fn):
        try:
            fn()
            print(f"PASS: {name}")
        except TestFailure as e:
            print(f"FAIL: {name}: {e}")
            self.failures += 1

    def machine(self, name, kernel, args, body, memory=32):
        """Boots a machine and runs body(m, boot_text); a failure to boot fails the lot."""
        m = Machine(kernel, args, memory=memory)
        try:
            boot = m.expect(SHELL_PROMPT, timeout=90)
            body(m, boot)
        except TestFailure as e:
            print(f"FAIL: [{name}] {e}")
            self.failures += 1
        finally:
            m.close()


def storage(suite, kernel, work):
    rnd = random.Random(20)
    big = bytes(rnd.randrange(256) for _ in range(300000))
    fd = os.path.join(work, "fd144.img")
    floppy(fd, 1440, "FLOPPY", {"HELLO.TXT": b"hello from a floppy\n", "BIG.BIN": big})
    tree = os.path.join(work, "tree")
    os.makedirs(os.path.join(tree, "docs", "deeper"))
    cd_big = bytes(rnd.randrange(256) for _ in range(1000000))
    for rel, data in {"Read Me First.txt": b"a CD for ManiOS\n", "UPPER.TXT": b"upper\n",
                      "big.bin": cd_big, "docs/deeper/note.txt": b"in a deep dir\n"}.items():
        with open(os.path.join(tree, rel), "wb") as f:
            f.write(data)
    isos = {}
    for names in ("rr", "joliet", "plain"):
        isos[names] = os.path.join(work, f"{names}.iso")
        iso(isos[names], tree, names, f"TEST_{names.upper()}")
    sata_big = bytes(rnd.randrange(256) for _ in range(2000000))
    sata = os.path.join(work, "sata.img")
    fat_disk(sata, "SATA", {"NOTE.TXT": b"on the sata disk\n", "BIG.BIN": sata_big})
    virt = os.path.join(work, "virt.img")
    fat_disk(virt, "VIRT", {"NOTE.TXT": b"on the virtio disk\n", "BIG.BIN": sata_big[::-1]})

    def cd_shows(m, names, tries=3):
        """ls /n/cd0 until it shows `names` (a changed disc is, at first,
        "not ready", as a real drive is while it spins up)."""
        for _ in range(tries):
            out = sh(m, "ls /n/cd0")
            if all(n in out for n in names):
                return out
        raise TestFailure(f"/n/cd0 never showed {names}:\n{out}")

    def change(m, drive, path):
        m.monitor.sendall((f"change {drive} {path}\n" if path else f"eject -f {drive}\n").encode())
        time.sleep(1)

    def body(m, boot):
        def image(offset, length):
            with open(fd, "rb") as f:
                f.seek(offset)
                return f.read(length)
        suite.step("floppy: the drive, the disk's format, FAT12 mounted", lambda: [
            check(n in boot, f"boot lacks {n!r}") for n in
            ['fd0: 3.5" 1.44 MB drive, 1.44 MB disk, on the 82077 controller',
             "fd0: FAT12, 1440 KiB, mounted at /n/fd0"]])
        suite.step("floppy: reads (by DMA, through the cylinder cache)", lambda: (
            sh(m, "cat /n/fd0/hello.txt", ["hello from a floppy"]),
            sh(m, "sum /n/fd0/big.bin", [f"300000 bytes, fnv1a {fnv1a(big):08x}"])))
        old = image(2000 * 512, 40 * 512)

        def floppy_writes():
            # Read the sectors first (into the cache), write across a
            # cylinder's end -- from the boot archive, so that nothing
            # else is read from the floppy meanwhile -- and read again:
            # the new data, from the cache.
            with open(os.path.join(console_test.BOOTFS_DIR, "bin", "grep"), "rb") as f:
                new = f.read(20480)
            sh(m, "dd if=/dev/fd0 bs=512 skip=2000 count=40 | sum", [f"20480 bytes, fnv1a {fnv1a(old):08x}"])
            sh(m, "dd if=/bin/grep of=/dev/fd0 bs=512 seek=2000 count=40", ["40+0 records out"])
            check(image(2000 * 512, 40 * 512) == new, "the image doesn't hold what was written")
            sh(m, "dd if=/dev/fd0 bs=512 skip=2000 count=40 | sum", [f"20480 bytes, fnv1a {fnv1a(new):08x}"])
        suite.step("floppy: writes reach the disk, and later reads see them", floppy_writes)

        suite.step("CD (IDE): Rock Ridge names, a deep directory, a 1 MB file", lambda: (
            check("cd0: QEMU DVD-ROM, ATAPI, disc of 2 MiB" in boot, "no cd0 at boot"),
            check("cd0: ISO 9660, Rock Ridge, 2 MiB, TEST_RR" in boot, "the disc not described"),
            cd_shows(m, ["Read Me First.txt", "UPPER.TXT", "big.bin", "docs/"]),
            sh(m, "cat '/n/cd0/Read Me First.txt'", ["a CD for ManiOS"]),
            sh(m, "cat /n/cd0/docs/deeper/note.txt", ["in a deep dir"]),
            sh(m, "cat /n/cd0/upper.txt", ["no such file"]),  # Rock Ridge names are exact
            sh(m, "sum /n/cd0/big.bin", [f"1000000 bytes, fnv1a {fnv1a(cd_big):08x}"])))

        def swaps():
            change(m, "ide1-cd0", None)
            sh(m, "ls /n/cd0", ["!big.bin"])
            sh(m, "cat /n/cd0/UPPER.TXT", ["no such file"])
            change(m, "ide1-cd0", isos["joliet"])
            cd_shows(m, ["Read Me First.txt", "UPPER.TXT"])
            sh(m, "cat '/n/cd0/read me first.txt'", ["a CD for ManiOS"])  # Joliet: any case
            change(m, "ide1-cd0", isos["plain"])
            cd_shows(m, ["read_me_first.txt", "upper.txt", "docs/"])
            sh(m, "cat /n/cd0/READ_ME_FIRST.TXT", ["a CD for ManiOS"])
            sh(m, "cat /n/cd0/Docs/Deeper/Note.txt", ["in a deep dir"])
            sh(m, "sum /n/cd0/big.bin", [f"1000000 bytes, fnv1a {fnv1a(cd_big):08x}"])
        suite.step("CD: the disc taken out, then Joliet and plain ISO discs put in", swaps)

        suite.step("AHCI: the disk, its partition, reads", lambda: (
            check("sata0: QEMU HARDDISK, 16 MiB, on AHCI port 0" in boot, "no sata0"),
            check("sata0p1: FAT12, 15 MiB, mounted at /n/sata0p1" in boot, "sata0p1 not mounted"),
            sh(m, "cat /n/sata0p1/note.txt", ["on the sata disk"]),
            sh(m, "sum /n/sata0p1/big.bin", [f"2000000 bytes, fnv1a {fnv1a(sata_big):08x}"])))

        def sata_write():
            sh(m, "echo written-to-sata | dd of=/dev/sata0 seek=100")
            with open(sata, "rb") as f:
                f.seek(100 * 512)
                check(f.read(16) == b"written-to-sata\n", "the SATA write isn't in the image")
        suite.step("AHCI: writes", sata_write)

        suite.step("virtio-blk: the disk, reads, writes", lambda: (
            check("vd0: virtio disk at pci " in boot and "vd0p1: FAT12, 15 MiB, mounted at /n/vd0p1" in boot,
                  "no vd0"),
            sh(m, "cat /n/vd0p1/note.txt", ["on the virtio disk"]),
            sh(m, "sum /n/vd0p1/big.bin", [f"2000000 bytes, fnv1a {fnv1a(sata_big[::-1]):08x}"]),
            sh(m, "echo written-to-virtio | dd of=/dev/vd0 seek=100"),
            check(open(virt, "rb").read()[100 * 512:100 * 512 + 18] == b"written-to-virtio\n",
                  "the virtio write isn't in the image")))

        suite.step("ManiDOS: B: the floppy, C: the SATA disk, D: the virtio disk, E: the CD", lambda: (
            sh(m, "dos /c DRIVES", ["B:  FAT12", "/n/fd0  on /dev/fd0", "C:  FAT12", "/n/sata0p1",
                                    "D:  FAT12", "/n/vd0p1", "E:  CD-ROM", "/n/cd0"]),
            sh(m, "dos /c VOL E:", ["Volume in drive E is TEST_PLAIN"]),
            sh(m, "dos /c VOL B:", ["Volume in drive B is FLOPPY"]),
            sh(m, "dos /c TYPE B:\\HELLO.TXT", ["hello from a floppy"]),
            sh(m, "dos /c CHKDSK E:", ["CHKDSK checks FAT disks; drive E is the CD-ROM"])))

    suite.machine("storage", kernel, [
        "-fda", fd, "-cdrom", isos["rr"],
        "-device", "ahci,id=ahci", "-drive", f"id=s0,file={sata},if=none,format=raw",
        "-device", "ide-hd,drive=s0,bus=ahci.0",
        "-drive", f"id=v0,file={virt},if=none,format=raw", "-device", "virtio-blk-pci,drive=v0"], body)

    # Second machine: a 720 KB disk; a write-protected second floppy; a
    # read-only virtio disk; the ManiOS ISO as a hard disk.
    fd720 = os.path.join(work, "fd720.img")
    floppy(fd720, 720, "SMALL", {"A720.TXT": b"a 720 KB disk\n"})
    manios_iso = os.path.join(work, "manios.iso")
    shutil.copy(os.path.join(os.path.dirname(kernel), "manios.iso"), manios_iso)

    def body2(m, boot):
        suite.step("floppy: a 720 KB disk found by its data rate and sectors", lambda: (
            check('fd0: 3.5" 1.44 MB drive, 720 KB disk' in boot, "the 720 KB disk not found"),
            sh(m, "cat /n/fd0/a720.txt", ["a 720 KB disk"])))
        suite.step("floppy: the second drive, write-protected: reads, and writes refused", lambda: (
            check("fd1: FAT12, 1440 KiB, mounted at /n/fd1" in boot, "no fd1"),
            sh(m, "cat /n/fd1/hello.txt", ["hello from a floppy"]),
            sh(m, "dd if=/n/fd1/hello.txt of=/dev/fd1", ["dd: write: read-only file system"])))
        suite.step("virtio-blk: a read-only disk refuses writes", lambda: (
            check("vd0: virtio disk at pci " in boot and ", read-only" in boot, "not read-only at boot"),
            sh(m, "echo x | dd of=/dev/vd0", ["dd: write: read-only file system"])))
        suite.step("ISO 9660 on a hard disk: the ManiOS ISO as ata0", lambda: (
            check("ata0: ISO 9660, ISO names" in boot, "the ISO on ata0 not mounted"),
            sh(m, "ls /n/ata0", ["cdboot.bin", "manios.bin", "readme.txt", "license.txt"]),
            sh(m, "head -1 /n/ata0/readme.txt", ["# ManiOS"])))

    suite.machine("storage 2", kernel, [
        "-drive", f"if=floppy,index=0,format=raw,file={fd720}",
        "-drive", f"if=floppy,index=1,format=raw,readonly=on,file={fd}",
        "-drive", f"id=v0,file={virt},if=none,format=raw,readonly=on", "-device", "virtio-blk-pci,drive=v0",
        "-drive", f"file={manios_iso},format=raw,if=ide,index=0"], body2)

    # q35: its CD drive is on the SATA controller; its FADT has a reset register.
    def body3(m, boot):
        suite.step("CD (SATA, q35): the drive on an AHCI port, the disc read", lambda: (
            check("cd0: QEMU DVD-ROM, SATA, disc of 2 MiB" in boot, "no SATA cd0"),
            sh(m, "sum /n/cd0/big.bin", [f"1000000 bytes, fnv1a {fnv1a(cd_big):08x}"])))

        def swap_q35():
            change(m, "ide2-cd0", isos["plain"])
            cd_shows(m, ["read_me_first.txt"])
        suite.step("CD (SATA): a disc changed", swap_q35)

        def reboot():
            sh(m, "cat /dev/power", ["reboot by the reset register"])
            m.type_serial("reboot\r")
            try:
                m.proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                raise TestFailure("reboot didn't reset the machine")
        suite.step("reboot on q35: the ACPI reset register", reboot)

    suite.machine("q35", kernel, ["-M", "q35", "-cdrom", isos["rr"]], body3)

    # Installing on a SATA disk (q35) and on a virtio disk, from the
    # ManiOS CD, and booting the installed disk.
    for bus, name, machine, device in [
            ("SATA", "sata0", ["-M", "q35"], "ide-hd,drive=d0,bus=ide.0"),
            ("virtio", "vd0", [], "virtio-blk-pci,drive=d0")]:
        disk = os.path.join(work, f"install-{name}.img")
        with open(disk, "wb") as f:
            f.truncate(64 * 1024 * 1024)
        drive = ["-drive", f"file={disk},format=raw,if=none,id=d0", "-device", device]

        def install(name=name, machine=machine, drive=drive):
            m = Machine(None, machine + ["-cdrom", manios_iso, "-boot", "d"] + drive)
            try:
                m.expect(SHELL_PROMPT, timeout=120)
                sh(m, f"install -y {name}p1", ["not a partition"])
                sh(m, f"install -y {name} sysname=on-{name}",
                   [f"is installed on {name}. Remove the CD"], timeout=120)
            finally:
                m.close()
            m = Machine(None, machine + ["-boot", "c"] + drive)
            try:
                m.expect(SHELL_PROMPT, timeout=120)
                sh(m, "cat /dev/sysname", [f"on-{name}"])
            finally:
                m.close()
        suite.step(f"install on a {bus} disk ({name}), and boot it", install)


def sound(suite, kernel, work):
    def audio_args(path, card):
        args = ["-audiodev", f"wav,id=snd0,path={path}", "-machine", "pcspk-audiodev=snd0"]
        return args + [a for c in card for a in ("-device", f"{c},audiodev=snd0")]

    # The PC speaker, the Sound Blaster 16 and the AdLib: one after another.
    rec = os.path.join(work, "sb16.wav")

    def body(m, boot):
        suite.step("sound: the cards found", lambda: [
            check(n in boot, f"boot lacks {n!r}") for n in
            ["sb16: Sound Blaster 16 (DSP 4.05) at 0x220, irq 5, dma 5: /dev/audio",
             "opl: Yamaha OPL2 (AdLib) FM synthesizer at 0x388: /dev/opl"]])
        sh(m, "beep 440 600")
        sh(m, "sleep 0.3")
        sh(m, "play -t 1000 500 0 200 660 300")
        sh(m, "sleep 0.3")
        sh(m, "fm 523 400 0 200 262 400")
        sh(m, "cat /dev/audio", ["Sound Blaster 16: 44100 Hz, 16-bit signed, stereo; 0 underruns"])
        sh(m, "beep 1 2 3", ["usage: beep"])
        sh(m, "beep 440 x", ["beep: not a frequency and a length"])
        m.monitor.sendall(b"quit\n")
        m.proc.wait(timeout=10)

    suite.machine("sound", kernel, audio_args(rec, ["sb16", "adlib"]), body)
    suite.step("PC speaker: 440 Hz for 600 ms", lambda: expect_tones(rec, [(440, 600)], "speaker"))
    suite.step("Sound Blaster 16: 1000 Hz 500 ms, a rest, 660 Hz 300 ms",
               lambda: expect_tones(rec, [(1000, 500), (660, 300)], "SB16"))
    suite.step("AdLib (OPL2 FM): 523 Hz 400 ms, a rest, 262 Hz 400 ms",
               lambda: expect_tones(rec, [(523, 400), (262, 400)], "AdLib"))

    # AC'97, and WAV files: 16-bit mono at 22,050 Hz, 8-bit stereo at
    # 11,025, 16-bit stereo at 48,000 -- on a CD.
    tree = os.path.join(work, "wavs")
    os.makedirs(tree)
    wav_file(os.path.join(tree, "MONO22.WAV"), 22050, 1, 16, 880, 500)
    wav_file(os.path.join(tree, "EIGHT11.WAV"), 11025, 2, 8, 330, 500)
    wav_file(os.path.join(tree, "STEREO48.WAV"), 48000, 2, 16, 1200, 400)
    with open(os.path.join(tree, "NOTWAV.TXT"), "wb") as f:
        f.write(b"just text\n")
    cd = os.path.join(work, "wavs.iso")
    iso(cd, tree, "plain", "WAVS")
    rec2 = os.path.join(work, "ac97.wav")

    def body2(m, boot):
        suite.step("AC'97: found, at 44,100 Hz", lambda: check(
            "ac97: AC'97 (8086:2415) at pci " in boot and ", 44100 Hz: /dev/audio" in boot, "no AC'97"))
        sh(m, "play -t 440 400")
        sh(m, "sleep 0.3")
        sh(m, "play /n/cd0/mono22.wav /n/cd0/eight11.wav /n/cd0/stereo48.wav", timeout=30)
        sh(m, "play /n/cd0/notwav.txt", ["play: /n/cd0/notwav.txt: not a WAV file"])
        sh(m, "cat /dev/audio", ["AC'97: 44100 Hz"])
        m.monitor.sendall(b"quit\n")
        m.proc.wait(timeout=10)

    suite.machine("AC'97", kernel, audio_args(rec2, ["AC97"]) + ["-cdrom", cd], body2)
    suite.step("AC'97: a 440 Hz tone of 400 ms", lambda: expect_tones(rec2, [(440, 400)], "AC'97"))
    suite.step("play: WAV files of other rates, sizes and channels, converted",
               lambda: expect_tones(rec2, [(880, 500), (330, 500), (1200, 400)], "WAV files"))

    # No sound card: /dev/audio isn't there, and play says so.
    def body3(m, boot):
        suite.step("no sound card: play says so", lambda: (
            sh(m, "play -t 440 100", ["play: /dev/audio: no sound card"]),
            sh(m, "fm", ["fm: /dev/opl: no FM synthesizer"])))
    suite.machine("no sound", kernel, [], body3)


def misc(suite, kernel, work):
    sock = os.path.join(work, "com2.sock")
    com3_file = os.path.join(work, "com3.out")
    lpt = os.path.join(work, "lpt.out")

    def body(m, boot):
        host = socket.socket(socket.AF_UNIX)
        host.connect(sock)
        host.settimeout(5)
        suite.step("serial ports: COM2 and COM3 found", lambda: [
            check(n in boot, f"boot lacks {n!r}") for n in
            ["com2: 16550A UART at 0x2f8, irq 3, 9600 baud", "com3: 16550A UART at 0x3e8, irq 4, 9600 baud"]])

        def com2():
            sh(m, "cat /dev/com2ctl", ["b9600 l8 pn s1"])
            sh(m, "echo b115200 > /dev/com2ctl")
            sh(m, "cat /dev/com2ctl", ["b115200 l8 pn s1"])
            sh(m, "echo b0 | dd of=/dev/com2ctl", ["dd: write: invalid argument"])
            sh(m, "echo hello over com2 > /dev/com2")
            got = b""
            while not got.endswith(b"\n"):
                got += host.recv(100)
            check(got == b"hello over com2\n", f"the host got {got!r}")
            host.sendall(b"from the host\n")
            sh(m, "head -1 /dev/com2", ["from the host"])
        suite.step("COM2: written and read, its speed set", com2)

        def com3():
            sh(m, "echo into com3 > /dev/com3")
            time.sleep(0.3)
            check(open(com3_file, "rb").read() == b"into com3\n", "COM3's file lacks the line")
        suite.step("COM3: written (sharing IRQ 4 with the console's COM1)", com3)

        def printer():
            check("lpt1: parallel port at 0x378, printer ready" in boot, "no lpt1")
            sh(m, "echo printed on paper > /dev/lpt1")
            sh(m, "cat /boot/autoexec.bat > /dev/lpt1")
            time.sleep(0.3)
            want = b"printed on paper\n" + open(os.path.join(console_test.BOOTFS_DIR, "autoexec.bat"), "rb").read()
            check(open(lpt, "rb").read() == want, "the printer's file isn't what was printed")
        suite.step("LPT1: bytes to the printer, in order", printer)

        def nvram():
            sh(m, "ls -l /dev/nvram", ["114  /dev/nvram"])
            # CMOS 0x10, the floppy drive types: QEMU's drive A is a 2.88 MB (5).
            out = sh(m, "dd if=/dev/nvram bs=1 skip=2 count=1 | vis", ["1+0 records out"])
            check(out.endswith("P"), f"CMOS 0x10 isn't 0x50 ('P'):\n{out}")
            sh(m, "echo ManiOS | dd of=/dev/nvram bs=1 seek=90")
            sh(m, "dd if=/dev/nvram bs=1 skip=90 count=6", ["ManiOS"])
        suite.step("NVRAM: the BIOS's bytes, and ours kept", nvram)

        def poweroff():
            check("acpi: BOCHS, PM1a at 0x604, S5 found, reset by keyboard controller" in boot, "no ACPI")
            sh(m, "cat /dev/power", ["ACPI 1.0 (BOCHS): off by PM1a 0x604 (SLP_TYP 0)"])
            sh(m, "echo sideways | dd of=/dev/power", ["dd: write: invalid argument"])
            m.type_serial("poweroff\r")
            try:
                m.proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                raise TestFailure("poweroff didn't turn the machine off")
            check(m.proc.returncode == 0, f"QEMU exited with {m.proc.returncode}")
        suite.step("ACPI: poweroff turns the machine off", poweroff)
        host.close()

    suite.machine("misc", kernel, [
        "-chardev", f"socket,id=c2,path={sock},server=on,wait=off", "-serial", f"chardev:c2",
        "-serial", f"file:{com3_file}", "-parallel", f"file:{lpt}"], body)

    def body2(m, boot):
        def reboot():
            sh(m, "cat /dev/power", ["reboot by the keyboard controller"])
            m.type_serial("reboot\r")
            try:
                m.proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                raise TestFailure("reboot didn't reset the machine")
        suite.step("reboot on the PC machine: the keyboard controller", reboot)
    suite.machine("reboot", kernel, [], body2)


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/manios-zkt.elf"
    console_test.BOOTFS_DIR = os.path.join(os.path.dirname(kernel), "bootfs")
    for tool in ("mformat", "xorriso"):
        if not shutil.which(tool):
            print(f"FAIL: driver_test needs {tool}")
            sys.exit(1)
    work = tempfile.mkdtemp(prefix="zkt-drivers-")
    suite = Suite()
    parts = {"storage": storage, "sound": sound, "misc": misc}
    try:
        for name in sys.argv[2:] or parts:
            parts[name](suite, kernel, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print(f"driver_test: {suite.failures} failed" if suite.failures else "driver_test: all passed")
    sys.exit(1 if suite.failures else 0)


if __name__ == "__main__":
    main()
