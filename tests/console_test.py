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

PROMPT = b"ZKT> "          # the kernel monitor
SHELL_PROMPT = b"manios% "  # /bin/sh, where boot ends
TIMEOUT = 15
BOOTFS_DIR = "build/bootfs"  # the boot archive's contents; set from the kernel path

# QEMU `sendkey` names for the characters the tests type.
KEY_NAMES = {" ": "spc", "\n": "ret", "\b": "backspace", "-": "minus",
             "=": "equal", ",": "comma", ".": "dot", "/": "slash",
             ";": "semicolon", "'": "apostrophe"}
SHIFTED = {"!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6",
           "&": "7", "*": "8", "(": "9", ")": "0", "_": "minus",
           "+": "equal", ":": "semicolon", "<": "comma", ">": "dot",
           "?": "slash", '"': "apostrophe", "|": "backslash"}


class TestFailure(Exception):
    pass


class Machine:
    """A QEMU machine with the serial line on a pipe and a monitor socket.
    kernel=None boots from the machine's disks (M14's boot loader)."""

    def __init__(self, kernel, extra_args=(), memory=32):
        self.tmpdir = tempfile.mkdtemp(prefix="zkt-console-")
        mon_path = os.path.join(self.tmpdir, "monitor.sock")
        boot = ["-kernel", kernel] if kernel else []
        # No network card unless the test asks for one: otherwise QEMU
        # adds an e1000 on its user network, which ManiOS drives (0.15).
        wants_net = any(a in ("-netdev", "-nic", "-net") for a in extra_args)
        nic = [] if wants_net else ["-nic", "none"]
        self.proc = subprocess.Popen(
            ["qemu-system-i386", *boot, "-cpu", "486", "-m", str(memory),
             "-display", "none", "-no-reboot", "-serial", "stdio",
             "-monitor", f"unix:{mon_path},server,nowait", *nic, *extra_args],
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


def run_command(m, how, command, expect_in_output, prompt=PROMPT):
    """Types a command, checks its echo and output, waits for the next
    prompt. A needle starting with "!" must not appear. A command ending
    in ^D (end of input for the program it started) gets no Enter."""
    enter = "" if command.endswith("\x04") else "\r" if how == "serial" else "\n"
    (m.type_serial if how == "serial" else m.type_keyboard)(command + enter)
    out = m.expect(prompt)
    for needle in expect_in_output:
        if needle.startswith("!"):
            if needle[1:] in out:
                raise TestFailure(f"{how}: {command!r}: unexpected {needle[1:]!r} in:\n{out}")
        elif needle not in out:
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


def malformed_elves(elf):
    """Variants of a working ELF executable, each broken in one field the
    kernel's loader must validate; "good" is the unmodified control."""
    phoff = struct.unpack_from("<I", elf, 28)[0]
    text_vaddr = struct.unpack_from("<I", elf, phoff + 8)[0]
    text_memsz = struct.unpack_from("<I", elf, phoff + 20)[0]
    phnum = struct.unpack_from("<H", elf, 44)[0]
    notes = [struct.unpack_from("<I", elf, phoff + i * 32 + 4)[0] for i in range(phnum)
             if struct.unpack_from("<I", elf, phoff + i * 32)[0] == 4]  # PT_NOTE offsets
    if len(notes) != 1:
        raise TestFailure("test ELF: expected exactly one PT_NOTE (the ZKT ABI note)")
    note = notes[0]  # namesz, descsz, type, "ZKT\0", version

    def patch(fmt, off, *values):
        out = bytearray(elf)
        struct.pack_into(fmt, out, off, *values)
        return bytes(out)

    return {
        "good": elf,
        "trunc": elf[:40],
        "machine": patch("<H", 18, 62),                        # x86-64
        "phoff": patch("<I", 28, 0xFFFFFFF0),
        "phnum": patch("<H", 44, 0xFFFF),
        "entry": patch("<I", 24, 0x10000000),                  # in no segment
        "kseg": patch("<I", phoff + 8, 0xC0000000),            # kernel half
        "stackseg": patch("<I", phoff + 8, 0xBFFF0000),        # over the stack
        "wrap": patch("<I", phoff + 20, 0x100000000 - text_vaddr),
        "filesz": patch("<I", phoff + 16, text_memsz + 1),     # filesz > memsz
        "offset": patch("<I", phoff + 4, len(elf)),            # data past the end
        "noabi": patch("<I", note + 8, 2),                     # not the ABI note's type
        "abi99": patch("<I", note + 16, 99),                   # a future ABI version
    }


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
    mt("mmd", P1, "::/BIN")
    with open(os.path.join(BOOTFS_DIR, "test", "fault"), "rb") as f:
        put(P1, "BIN/FAULT", f.read())
    mt("mmd", P1, "::/BAD")
    with open(os.path.join(BOOTFS_DIR, "bin", "hello"), "rb") as f:
        hello = f.read()
    for name, elf in malformed_elves(hello).items():
        put(P1, f"BAD/{name.upper()}", elf)
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

    frag = files[(P1, "FRAG.BIN")]
    shell = [
        ("serial", "cd /n/ata0p1; ls", ["readme.txt", "docs/", "frag.bin"]),
        # /bin/sum prints what the monitor's sum does; relative paths.
        ("serial", "sum frag.bin", [f"frag.bin: {len(frag)} bytes, fnv1a {fnv1a(frag):08x}"]),
        ("serial", "cd docs; cat ../readme.txt", ["This file lives on a FAT16 volume."]),
        ("serial", "wc nested.txt", ["      1       2      12 nested.txt"]),
        ("serial", "cd /", []),
    ]
    return {"shell": shell, "monitor": [
        sum_case("/n/ata0p1/frag.bin", files[(P1, "FRAG.BIN")]),
        sum_case("/n/ata0p1/big.bin", files[(P1, "BIG.BIN")]),
        sum_case("/n/ata0p2/frag12.bin", files[(P2, "FRAG12.BIN")]),
        ("serial", "sum /dev/ata0 4096", [f"4096 bytes, fnv1a {fnv1a(img[:4096]):08x}"]),
        ("serial", "sum /dev/ata0p2 1024",
         [f"1024 bytes, fnv1a {fnv1a(img[P2 * 512:P2 * 512 + 1024]):08x}"]),
        # The loader treats every ELF field as untrusted (M8).
        ("serial", "run /n/ata0p1/bad/good", ["Hello from ManiOS userspace!"]),
        *[("serial", f"run /n/ata0p1/bad/{name}", [f"run: /n/ata0p1/bad/{name}: not an executable"])
          for name in malformed_elves(hello) if name != "good"],
        # A program on disk, found through a union with /bin (M8).
        ("serial", "bind -a /n/ata0p1/bin /bin", []),
        ("serial", "ls /bin", ["hello", "fault"]),
        ("serial", "run /bin/fault exit42", ["run: /bin/fault exited with status 42"]),
    ]}


# --- scenarios -----------------------------------------------------------

# The serial line turns "\n" into "\r\n", so line ends are matched as that.
SCENARIOS = [
    {
        "name": "no disk",
        "disk": None,
        "boot": ["devices: cons com1 vga null sysname kbd mouse fb fbctl time\r\n", "Milestone M9: libc, ABI v2 and shell online"],
        "shell": [
            ("serial", "ls /bin", ["cat", "echo", "ls", "sh", "wc"]),
            ("serial", "echo 'a;b' c\\;d; echo e", ["\r\na;b c;d\r\ne\r\n"]),
            ("keyboard", "cd /boot; pwd", ["\r\n/boot\r\n"]),
            ("serial", "ls", ["bin/", "etc/", "test/"]),
            ("serial", "cat etc/motd", ["\r\nWelcome to ManiOS.\r\n"]),
            ("serial", "wc etc/motd", ["      1       3      19 etc/motd"]),
            # Typed ahead: the shell reads one line, cat the next, then ^D.
            ("serial", "cat\rtwo words\r\x04", ["two words\r\ntwo words\r\n"]),
            ("serial", "nosuch", ["sh: nosuch: not found"]),
            ("serial", "test/fault null",
             ["]: killed: Page fault at 0x00000000", "sh: test/fault: killed (vector 14)"]),
            ("serial", "newns; bind /boot /n; ls /n", ["bin/", "etc/"]),
            ("serial", "sh -c 'ls /n; exit 3'; echo done", ["bin/", "\r\ndone\r\n"]),
            ("serial", "uptime", ["up "]),
            # Pipelines and redirection (M12); the shell is still in /boot.
            ("serial", "echo a b c | wc", ["\r\n      1       3       6\r\n"]),
            ("keyboard", "cat < etc/motd | cat | wc", ["\r\n      1       3      19\r\n"]),
            ("serial", "echo hidden > /dev/null; echo shown", ["\r\nshown\r\n", "!\r\nhidden\r\n"]),
            ("serial", "echo x > /boot/nosuch", ["sh: /boot/nosuch: no such file or directory"]),
            ("serial", "nosuch | wc", ["sh: nosuch: not found", "      0       0       0"]),
            ("serial", "| wc", ["sh: empty command in a pipeline"]),
            ("serial", "cat <", ["sh: < needs a file"]),
            ("serial", "cd / | wc", ["sh: cd: a builtin can't be in a pipeline or redirected"]),
            ("serial", "echo '|' \"<\"", ["\r\n| <\r\n"]),
            ("serial", "/boot/test/ctest", ["ctest: all ", " checks passed", "!FAIL"]),
            # Cluster roles (M13) on a machine with no key and no network card.
            ("serial", "cat /dev/sysname /dev/zrp", ["\r\nmanios\r\nkey none\r\n"]),
            ("serial", "cpud", ["cpud: this machine has no cluster key"]),
            ("serial", "cpu udp!10.0.0.2 ls", ["cpu: this machine has no network address"]),
            ("serial", "cpu", ["usage: cpu HOST [COMMAND [ARGS...]]"]),
        ],
        "cases": [
            ("serial", "help", ["commands:", "threads", "devices"]),
            ("serial", "echo over serial", ["\r\nover serial\r\n"]),
            ("serial", "devices", ["cons", "com1", "vga"]),
            ("serial", "pci", ["00:02.0  1234:1111  class 03.00.00", "00:01.1  8086:7010  class 01.01"]),
            ("keyboard", "uptime", ["uptime: "]),
            ("keyboard", "threads", ["console", "idle", "running"]),
            ("keyboard", "echo Hello, World! (x_y)", ["\r\nHello, World! (x_y)\r\n"]),
            # Backspace must erase on screen ("\b \b") and in the line buffer.
            ("keyboard", "uptimx\be", ["uptimx\b \be", "uptime: "]),
            ("serial", "nosuchcommand", ["unknown command: nosuchcommand"]),
            # User programs from the boot archive (M8).
            ("serial", "ls /boot", ["bin/", "test/", "etc/"]),
            ("serial", "ls /bin", ["hello"]),
            ("serial", "run /bin/hello one two",
             ["\r\nHello from ManiOS userspace!\r\nargs: one two\r\n"]),
            ("keyboard", "run /bin/hello", ["Hello from ManiOS userspace!\r\n", "!args:"]),
            ("serial", "run /boot/test/fault null",
             ["]: killed: Page fault at 0x00000000", "run: /boot/test/fault killed (vector 14)"]),
            ("serial", "run /boot/test/fault priv",
             ["]: killed: General protection fault", "killed (vector 13)"]),
            ("serial", "run /boot/test/fault exit42", ["run: /boot/test/fault exited with status 42"]),
            ("serial", "run /no/such", ["run: /no/such: no such file or directory"]),
            ("serial", "run /boot/etc/motd", ["run: /boot/etc/motd: not an executable"]),
            ("serial", "run /boot/test/utest", ["utest: all ", " checks passed", "!FAIL"]),
            # Every process thread is gone once its program has exited.
            ("serial", "threads", ["console", "!hello", "!fault", "!utest", "!isotest"]),
            # The shell's newns kept its bind of /boot on /n private.
            ("serial", "ls /n", ["!bin/"]),
        ],
    },
    {
        "name": "partitioned ATA disk",
        "disk": write_patterned_disk,
        "boot": ["ata0: QEMU HARDDISK, 8 MiB, LBA", "ata0: CHS cross-check passed",
                 "ata0p1: type 0x06, sectors 2048-16383",
                 "devices: cons com1 vga null sysname kbd mouse ata0 ata0p1 fb fbctl time\r\n"],
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
        "boot": ["devices: cons com1 vga null sysname kbd mouse ata0 fb fbctl time\r\n"],
        "cases": [],
    },
]


def run_cases(m, name, cases, prompt):
    failures = 0
    for how, command, needles in cases:
        try:
            run_command(m, how, command, needles, prompt)
            print(f"PASS: [{name}] {how}: {command!r}")
        except TestFailure as e:
            print(f"FAIL: [{name}] {e}")
            failures += 1
            m.mark = len(m.output)
            m.type_serial("\r")
            m.expect(prompt)
    return failures


def screen_text(m):
    """The VGA text screen, 25 lines (QEMU's pmemsave of 0xB8000)."""
    path = os.path.join(m.tmpdir, "screen.bin")
    if os.path.exists(path):
        os.remove(path)
    m.monitor.sendall(f'pmemsave 0xb8000 4000 "{path}"\n'.encode())
    deadline = time.time() + 10
    while not (os.path.exists(path) and os.path.getsize(path) == 4000):
        if time.time() > deadline:
            raise TestFailure("pmemsave wrote no screen")
        time.sleep(0.1)
    time.sleep(0.1)
    data = open(path, "rb").read()
    return "\n".join(bytes(data[r * 160 + c * 2] for c in range(80)).decode("latin-1").rstrip()
                     for r in range(25))


def run_scenario(kernel, scenario, workdir):
    """Boot ends at the shell: its cases run first, then `exit` drops to
    the kernel monitor for the monitor cases. Generated cases (which
    depend on the disk image's data) are plain reads, so they run before
    the scenario's own cases, which may rebind things."""
    extra, generated = [], {}
    if scenario["disk"]:
        path = os.path.join(workdir, scenario["name"].replace(" ", "-") + ".img")
        generated = scenario["disk"](path) or {}
        extra = ["-drive", f"file={path},format=raw,if=ide"]
    m = Machine(kernel, extra)
    name = scenario["name"]
    failures = 0
    try:
        boot = m.expect(SHELL_PROMPT)
        for needle in scenario["boot"]:
            if needle not in boot:
                raise TestFailure(f"boot output lacks {needle!r}:\n{boot}")
        print(f"PASS: [{name}] boot")
        failures += run_cases(m, name, generated.get("shell", []) + scenario.get("shell", []),
                              SHELL_PROMPT)
        run_command(m, "serial", "exit", ["console: the shell has exited"])
        print(f"PASS: [{name}] exit to the monitor")
        failures += run_cases(m, name, generated.get("monitor", []) + scenario["cases"], PROMPT)
    except TestFailure as e:
        print(f"FAIL: [{name}] {e}")
        failures += 1
    finally:
        m.close()
    return failures


def failed_boot_on_screen(kernel):
    """The boot screen is quiet (0.14.1), but not about failures: 3 MiB is
    too little for the programs of the user self-test, and the panic must
    be on the screen, after the checks that passed."""
    m = Machine(kernel, memory=3)
    try:
        m.expect("*** ZKT PANIC: selftest: user", timeout=60)
        m.expect("System halted.")
        screen = screen_text(m)
        for needle in ["Self-tests: memory, interrupts, threads, devices, disks, files",
                       "*** ZKT PANIC: selftest: user", "System halted."]:
            if needle not in screen:
                raise TestFailure(f"the screen lacks {needle!r}:\n{screen}")
        print("PASS: [3 MiB] a failed self-test stops the quiet boot, on the screen")
        return 0
    except TestFailure as e:
        print(f"FAIL: [3 MiB] {e}")
        return 1
    finally:
        m.close()


def main():
    global BOOTFS_DIR
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/manios-zkt.elf"
    BOOTFS_DIR = os.path.join(os.path.dirname(kernel), "bootfs")
    workdir = tempfile.mkdtemp(prefix="zkt-disks-")
    try:
        failures = sum(run_scenario(kernel, sc, workdir) for sc in SCENARIOS)
        failures += failed_boot_on_screen(kernel)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
