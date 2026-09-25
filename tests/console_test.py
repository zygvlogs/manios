#!/usr/bin/env python3
"""Interactive console test.

Boots the kernel in QEMU and drives the ZKT monitor two ways: typing
over the serial line (QEMU's stdio), and pressing keys on the emulated
PS/2 keyboard (QEMU monitor `sendkey`). Checks echo, line editing and
command output, across several machine configurations (disk images are
generated here; the partitionless-FAT one needs mtools).

Usage: tests/console_test.py [path-to-kernel-elf]
"""
import os
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

PROMPT = b"ZKT> "
TIMEOUT = 15

# QEMU `sendkey` names for the characters the tests type.
KEY_NAMES = {" ": "spc", "\n": "ret", "\b": "backspace", "-": "minus",
             "=": "equal", ",": "comma", ".": "dot", "/": "slash",
             ";": "semicolon", "'": "apostrophe"}
SHIFTED = {"!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6",
           "&": "7", "*": "8", "(": "9", ")": "0", "_": "minus",
           "+": "equal", ":": "semicolon", "<": "comma", ">": "dot",
           "?": "slash", '"': "apostrophe"}


class TestFailure(Exception):
    pass


class Machine:
    def __init__(self, kernel, extra_args=()):
        self.tmpdir = tempfile.mkdtemp(prefix="zkt-console-")
        mon_path = os.path.join(self.tmpdir, "monitor.sock")
        self.proc = subprocess.Popen(
            ["qemu-system-i386", "-kernel", kernel, "-cpu", "486", "-m", "32",
             "-display", "none", "-no-reboot", "-serial", "stdio",
             "-monitor", f"unix:{mon_path},server,nowait", *extra_args],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.output = b""
        self.mark = 0
        self.monitor = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        deadline = time.time() + TIMEOUT
        while True:
            try:
                self.monitor.connect(mon_path)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.time() > deadline or self.proc.poll() is not None:
                    raise TestFailure("QEMU monitor socket never appeared")
                time.sleep(0.05)

    def _pump(self, wait):
        ready, _, _ = select.select([self.proc.stdout], [], [], wait)
        if ready:
            chunk = os.read(self.proc.stdout.fileno(), 4096)
            if not chunk:
                raise TestFailure("QEMU exited")
            self.output += chunk

    def expect(self, needle, timeout=TIMEOUT):
        """Waits for `needle` after the previous match; returns the text
        between, and moves the mark past the match."""
        needle = needle.encode() if isinstance(needle, str) else needle
        deadline = time.time() + timeout
        while True:
            pos = self.output.find(needle, self.mark)
            if pos >= 0:
                between = self.output[self.mark:pos]
                self.mark = pos + len(needle)
                return between.decode(errors="replace")
            if time.time() > deadline:
                tail = self.output[self.mark:].decode(errors="replace")
                raise TestFailure(f"timed out waiting for {needle!r}; got:\n{tail}")
            self._pump(0.1)

    def type_serial(self, text):
        self.proc.stdin.write(text.encode())
        self.proc.stdin.flush()

    def type_keyboard(self, text):
        for ch in text:
            if ch in KEY_NAMES:
                key = KEY_NAMES[ch]
            elif ch in SHIFTED:
                key = "shift-" + SHIFTED[ch]
            elif ch.isupper():
                key = "shift-" + ch.lower()
            else:
                key = ch
            self.monitor.sendall(f"sendkey {key} 10\n".encode())
            time.sleep(0.03)

    def close(self):
        self.proc.kill()
        self.proc.wait()
        self.monitor.close()
        shutil.rmtree(self.tmpdir, ignore_errors=True)


def run_command(m, how, command, expect_in_output):
    """Types a command, checks its echo and output, waits for the next prompt."""
    (m.type_serial if how == "serial" else m.type_keyboard)(command + ("\r" if how == "serial" else "\n"))
    out = m.expect(PROMPT)
    for needle in expect_in_output:
        if needle not in out:
            raise TestFailure(f"{how}: {command!r}: expected {needle!r} in:\n{out}")
    return out


# --- disk images -------------------------------------------------------

DISK_SECTORS = 16384                  # 8 MiB
PART_START, PART_SECTORS = 2048, 14336


def mbr_entry(status, ptype, start, count):
    return struct.pack("<B3sB3sII", status, b"\0\0\0", ptype, b"\0\0\0", start, count)


def write_patterned_disk(path):
    """MBR with one FAT16-typed partition. Every sector in the gap before
    it says which sector it is, so misaddressed reads (the CHS
    cross-check, partition offsets) can't pass by comparing zeros."""
    img = bytearray(DISK_SECTORS * 512)
    for n in range(1, PART_START):
        text = f"ZKT disk sector {n}".encode()
        img[n * 512:n * 512 + len(text)] = text
    for k in range(16):
        text = f"ZKT part sector {k}".encode()
        off = (PART_START + k) * 512
        img[off:off + len(text)] = text
    img[446:462] = mbr_entry(0x00, 0x06, PART_START, PART_SECTORS)
    img[510:512] = b"\x55\xaa"
    with open(path, "wb") as f:
        f.write(img)


def write_superfloppy(path):
    """A partitionless FAT disk whose boot sector also holds bytes that
    parse as a plausible partition entry: must not yield a partition."""
    with open(path, "wb") as f:
        f.write(bytes(DISK_SECTORS * 512))
    subprocess.run(["mformat", "-i", path, "-T", str(DISK_SECTORS), "-h", "16",
                    "-s", "63", "::"], check=True)
    with open(path, "r+b") as f:
        f.seek(446)
        f.write(mbr_entry(0x80, 0x06, 100, 1000))


# --- scenarios -----------------------------------------------------------

# The serial line turns "\n" into "\r\n", so line ends are matched as that.
SCENARIOS = [
    {
        "name": "no disk",
        "disk": None,
        "boot": ["devices: cons com1 vga\r\n"],
        "cases": [
            ("serial", "help", ["commands:", "threads", "devices"]),
            ("serial", "echo over serial", ["\r\nover serial\r\n"]),
            ("serial", "devices", ["cons", "com1", "vga"]),
            ("keyboard", "uptime", ["uptime: "]),
            ("keyboard", "threads", ["monitor", "idle", "running"]),
            ("keyboard", "echo Hello, World! (x_y)", ["\r\nHello, World! (x_y)\r\n"]),
            # Backspace must erase on screen ("\b \b") and in the line buffer.
            ("keyboard", "uptimx\be", ["uptimx\b \be", "uptime: "]),
            ("serial", "nosuchcommand", ["unknown command: nosuchcommand"]),
        ],
    },
    {
        "name": "partitioned ATA disk",
        "disk": write_patterned_disk,
        "boot": ["ata0: QEMU HARDDISK, 8 MiB, LBA", "ata0: CHS cross-check passed",
                 "ata0p1: type 0x06, sectors 2048-16383",
                 "devices: cons com1 vga ata0 ata0p1\r\n"],
        "cases": [
            ("serial", "devices", ["ata0     block  16384 x 512 bytes (8 MiB)",
                                   "ata0p1   block  14336 x 512 bytes (7 MiB)"]),
            # 16 bytes per dump line: the digit after "sector " starts line 2.
            ("serial", "read ata0 1", ["ZKT disk sector \r\n0010  31 00"]),
            ("serial", "read ata0p1 5", ["ZKT part sector \r\n0010  35 00"]),
            ("serial", "read ata0p1 14336", ["beyond end of device"]),
            ("serial", "read ata0 x", ["usage: read DEVICE BLOCK"]),
        ],
    },
    {
        "name": "partitionless FAT disk",
        "disk": write_superfloppy,
        "boot": ["devices: cons com1 vga ata0\r\n"],
        "cases": [],
    },
]


def run_scenario(kernel, scenario, workdir):
    extra = []
    if scenario["disk"]:
        path = os.path.join(workdir, "disk.img")
        scenario["disk"](path)
        extra = ["-drive", f"file={path},format=raw,if=ide"]
    m = Machine(kernel, extra)
    failures = 0
    try:
        boot = m.expect(PROMPT)
        for needle in scenario["boot"]:
            if needle not in boot:
                raise TestFailure(f"boot output lacks {needle!r}:\n{boot}")
        print(f"PASS: [{scenario['name']}] boot")
        for how, command, needles in scenario["cases"]:
            try:
                run_command(m, how, command, needles)
                print(f"PASS: [{scenario['name']}] {how}: {command!r}")
            except TestFailure as e:
                print(f"FAIL: [{scenario['name']}] {e}")
                failures += 1
                m.mark = len(m.output)
                m.type_serial("\r")
                m.expect(PROMPT)
    except TestFailure as e:
        print(f"FAIL: [{scenario['name']}] {e}")
        failures += 1
    finally:
        m.close()
    return failures


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/manios-zkt.elf"
    workdir = tempfile.mkdtemp(prefix="zkt-disks-")
    try:
        failures = sum(run_scenario(kernel, sc, workdir) for sc in SCENARIOS)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
