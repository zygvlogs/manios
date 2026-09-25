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
import random
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


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def fat_chain(img, part_start, name83):
    """First-cluster chain of a root-directory file, read straight from
    the image, so the test can assert its own preconditions."""
    base = part_start * 512
    bs = img[base:base + 512]
    reserved, fats = struct.unpack_from("<H", bs, 14)[0], bs[16]
    root_entries, fat_size = struct.unpack_from("<H", bs, 17)[0], struct.unpack_from("<H", bs, 22)[0]
    total = struct.unpack_from("<H", bs, 19)[0] or struct.unpack_from("<I", bs, 32)[0]
    root_sectors = (root_entries * 32 + 511) // 512
    clusters = (total - reserved - fats * fat_size - root_sectors) // bs[13]
    fat12 = clusters < 4085
    fat = img[base + reserved * 512: base + (reserved + fat_size) * 512]
    root = base + (reserved + fats * fat_size) * 512
    for i in range(root_entries):
        e = img[root + i * 32: root + i * 32 + 32]
        if e[:11] != name83:
            continue
        c, chain = struct.unpack_from("<H", e, 26)[0], []
        while 2 <= c < (0xFF8 if fat12 else 0xFFF8):
            chain.append(c)
            if fat12:
                v = fat[c + c // 2] | fat[c + c // 2 + 1] << 8
                c = v >> 4 if c & 1 else v & 0xFFF
            else:
                c = struct.unpack_from("<H", fat, c * 2)[0]
        return chain
    raise TestFailure(f"test image lacks {name83!r}")


FAT_DISK_SECTORS = 32768                # 16 MiB
P1, P1_SECTORS = 2048, 16384            # FAT16, 1 sector per cluster
P2, P2_SECTORS = 18432, 14336           # FAT12, 8 sectors per cluster


def write_fat_disk(path):
    """Two FAT volumes built with mtools. A.TMP is deleted after B.TMP is
    written, so the next big file fills A's hole and continues past B:
    a fragmented chain. Returns the cases whose expected output depends
    on the generated data."""
    img = bytearray(FAT_DISK_SECTORS * 512)
    img[446:462] = mbr_entry(0x00, 0x06, P1, P1_SECTORS)
    img[462:478] = mbr_entry(0x00, 0x01, P2, P2_SECTORS)
    img[510:512] = b"\x55\xaa"
    with open(path, "wb") as f:
        f.write(img)
    srcdir = path + ".src"
    os.makedirs(srcdir)
    files = {}

    def mt(tool, part, *args):
        subprocess.run([tool, "-i", f"{path}@@{part * 512}", *args], check=True,
                       stdout=subprocess.DEVNULL)

    def put(part, name, data):
        src = os.path.join(srcdir, name.replace("/", "_"))
        with open(src, "wb") as f:
            f.write(data)
        mt("mcopy", part, src, "::/" + name)
        files[(part, name)] = data

    rnd = random.Random(7)
    mt("mformat", P1, "-T", str(P1_SECTORS), "-h", "16", "-s", "63", "-H", str(P1), "-c", "1", "::")
    mt("mformat", P2, "-T", str(P2_SECTORS), "-h", "16", "-s", "63", "-H", str(P2), "-c", "8", "::")
    put(P1, "README.TXT", b"Welcome to ManiOS.\nThis file lives on a FAT16 volume.\n")
    put(P1, "SAME.TXT", b"from p1\n")
    mt("mmd", P1, "::/DOCS")
    put(P1, "DOCS/NESTED.TXT", b"nested file\n")
    for i in range(20):  # 21 entries: more than one 512-byte cluster holds
        put(P1, f"DOCS/F{i:02d}.TXT", f"file {i}\n".encode())
    put(P1, "manios-notes.txt", b"long name, 8.3 alias only\n")
    put(P1, "A.TMP", bytes(3000))
    put(P1, "B.TMP", b"b" * 1000)
    mt("mdel", P1, "::/A.TMP")
    put(P1, "FRAG.BIN", bytes(rnd.randrange(256) for _ in range(20000)))
    put(P1, "BIG.BIN", bytes(rnd.randrange(256) for _ in range(50000)))
    put(P2, "SAME.TXT", b"from p2\n")
    put(P2, "ONLY12.TXT", b"only on the FAT12 volume\n")
    put(P2, "A.TMP", bytes(9000))
    put(P2, "B.TMP", b"b" * 5000)
    mt("mdel", P2, "::/A.TMP")
    put(P2, "FRAG12.BIN", bytes(rnd.randrange(256) for _ in range(40000)))

    with open(path, "rb") as f:
        img = f.read()
    for part, name in ((P1, b"FRAG    BIN"), (P2, b"FRAG12  BIN")):
        chain = fat_chain(img, part, name)
        if all(b == a + 1 for a, b in zip(chain, chain[1:])):
            raise TestFailure(f"test image: {name!r} is not fragmented, so it tests nothing")

    def sum_case(dev_path, data):
        return ("serial", f"sum {dev_path}", [f"{len(data)} bytes, fnv1a {fnv1a(data):08x}"])

    return [
        sum_case("/n/ata0p1/frag.bin", files[(P1, "FRAG.BIN")]),
        sum_case("/n/ata0p1/big.bin", files[(P1, "BIG.BIN")]),
        sum_case("/n/ata0p2/frag12.bin", files[(P2, "FRAG12.BIN")]),
        ("serial", "sum /dev/ata0 4096", [f"4096 bytes, fnv1a {fnv1a(img[:4096]):08x}"]),
        ("serial", "sum /dev/ata0p2 1024",
         [f"1024 bytes, fnv1a {fnv1a(img[P2 * 512:P2 * 512 + 1024]):08x}"]),
    ]


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
        "name": "FAT volumes",
        "disk": write_fat_disk,
        "boot": ["ata0p1: FAT16, 8 MiB, mounted at /n/ata0p1",
                 "ata0p2: FAT12, 7 MiB, mounted at /n/ata0p2"],
        "cases": [
            ("serial", "ls /", ["dir         0  dev/", "dir         0  n/"]),
            ("serial", "ls /n/ata0p1", ["file       54  readme.txt", "dir         0  docs/",
                                        "manios~1.txt", "file    20000  frag.bin"]),
            ("serial", "cat /n/ata0p1/readme.txt",
             ["Welcome to ManiOS.\r\nThis file lives on a FAT16 volume.\r\n"]),
            ("serial", "cat /n/ata0p1/DOCS/NESTED.TXT", ["nested file\r\n"]),  # case-insensitive
            ("serial", "ls /n/ata0p1/docs", ["nested.txt", "f00.txt", "f19.txt"]),
            ("serial", "cat /n/ata0p1/manios~1.txt", ["long name, 8.3 alias only"]),
            ("keyboard", "cat /n/ata0p2/only12.txt", ["only on the FAT12 volume"]),
            ("serial", "cat /n/ata0p1/missing.txt", ["no such file or directory"]),
            ("serial", "cat /n/ata0p1/docs", ["is a directory"]),
            ("serial", "ls /n/ata0p1/readme.txt", ["not a directory"]),
            ("serial", "cat n/ata0p1/readme.txt", ["invalid argument"]),
            ("serial", "bind /n/ata0p1/readme.txt /n/ata0p2", ["bind: not a directory"]),
            # Unions: the first member that has a name wins.
            ("serial", "bind -a /n/ata0p2 /n/ata0p1", []),
            ("serial", "cat /n/ata0p1/same.txt", ["from p1"]),
            ("serial", "cat /n/ata0p1/only12.txt", ["only on the FAT12 volume"]),
            ("serial", "ls /n/ata0p1", ["readme.txt", "only12.txt", "frag12.bin"]),
            ("serial", "ns", ["/n/ata0p1 = fat:ata0p1 fat:ata0p2"]),
            ("serial", "bind -b /n/ata0p2 /n/ata0p1", []),
            ("serial", "cat /n/ata0p1/same.txt", ["from p2"]),
            ("serial", "ns", ["/n/ata0p1 = fat:ata0p2 fat:ata0p1 fat:ata0p2"]),
            ("serial", "unbind /n/ata0p1", []),
            ("serial", "cat /n/ata0p1/same.txt", ["no such file or directory"]),
            ("serial", "bind /n/ata0p2 /n/ata0p1", []),
            ("serial", "cat /n/ata0p1/only12.txt", ["only on the FAT12 volume"]),
            ("serial", "unbind /n/nothing", ["unbind: /n/nothing: no such file or directory"]),
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
    extra, generated_cases = [], []
    if scenario["disk"]:
        path = os.path.join(workdir, scenario["name"].replace(" ", "-") + ".img")
        generated_cases = scenario["disk"](path) or []
        extra = ["-drive", f"file={path},format=raw,if=ide"]
    m = Machine(kernel, extra)
    failures = 0
    try:
        boot = m.expect(PROMPT)
        for needle in scenario["boot"]:
            if needle not in boot:
                raise TestFailure(f"boot output lacks {needle!r}:\n{boot}")
        print(f"PASS: [{scenario['name']}] boot")
        # Generated cases are plain reads; run them before any binds.
        for how, command, needles in generated_cases + scenario["cases"]:
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
