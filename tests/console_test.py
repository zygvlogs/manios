#!/usr/bin/env python3
"""Interactive console test.

Boots the kernel in QEMU and drives the ZKT monitor two ways: typing
over the serial line (QEMU's stdio), and pressing keys on the emulated
PS/2 keyboard (QEMU monitor `sendkey`). Checks echo, line editing and
command output.

Usage: tests/console_test.py [path-to-kernel-elf]
"""
import os
import select
import shutil
import socket
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


# The serial line turns "\n" into "\r\n", so line ends are matched as that.
CASES = [
    ("serial", "help", ["commands:", "threads", "devices"]),
    ("serial", "echo over serial", ["\r\nover serial\r\n"]),
    ("serial", "devices", ["cons", "com1", "vga"]),
    ("keyboard", "uptime", ["uptime: "]),
    ("keyboard", "threads", ["monitor", "idle", "running"]),
    ("keyboard", "echo Hello, World! (x_y)", ["\r\nHello, World! (x_y)\r\n"]),
    # Backspace must erase on screen ("\b \b") and in the line buffer.
    ("keyboard", "uptimx\be", ["uptimx\b \be", "uptime: "]),
    ("serial", "nosuchcommand", ["unknown command: nosuchcommand"]),
]


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/manios-zkt.elf"
    m = Machine(kernel)
    failed = False
    try:
        m.expect(PROMPT)
        for how, command, needles in CASES:
            try:
                run_command(m, how, command, needles)
                print(f"PASS: {how}: {command!r}")
            except TestFailure as e:
                print(f"FAIL: {e}")
                failed = True
                m.mark = len(m.output)
                m.type_serial("\r")
                m.expect(PROMPT)
    except TestFailure as e:
        print(f"FAIL: {e}")
        failed = True
    finally:
        m.close()
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
