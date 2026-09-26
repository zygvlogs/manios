#!/usr/bin/env python3
"""Graphics test: the framebuffer driver and libgfx, checked on the
screen itself through QEMU's `screendump` (a PPM image of the display).

Runs gfxdemo in each kind of mode -- Bochs VBE at two sizes, VGA mode
13h -- and checks the colours where gfxdemo's layout puts its swatches.
After each, text mode must be back exactly as it was: the same line of
text must look the same, pixel for pixel, as before graphics (the font
lives in video memory that graphics overwrite).

Usage: tests/gfx_test.py [path-to-kernel-elf]
"""
import os
import re
import sys
import tempfile
import time

from console_test import SHELL_PROMPT, Machine, TestFailure, run_command

SWATCHES = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255),
            (255, 255, 0), (0, 255, 255), (255, 0, 255), (0, 0, 0)]


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    fields, i = [], 0
    while len(fields) < 4:
        while data[i:i + 1].isspace():
            i += 1
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        fields.append(data[i:j])
        i = j
    if fields[0] != b"P6" or fields[3] != b"255":
        raise TestFailure(f"not a P6 PPM: {fields}")
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[i + 1:i + 1 + w * h * 3]


class Screen:
    def __init__(self, path):
        self.w, self.h, self.rgb = read_ppm(path)

    def pixel(self, x, y):
        o = (y * self.w + x) * 3
        return tuple(self.rgb[o:o + 3])

    def band(self, row, cell_h=16):
        """The pixels of text row `row` (QEMU draws 9x16 cells)."""
        return self.rgb[row * cell_h * self.w * 3:(row + 1) * cell_h * self.w * 3]


def screendump(m, tmpdir, name):
    path = os.path.join(tmpdir, name + ".ppm")
    m.monitor.sendall(f"screendump {path}\n".encode())
    deadline = time.time() + 5
    while time.time() < deadline:
        time.sleep(0.1)
        try:
            if os.path.getsize(path) > 15:
                time.sleep(0.2)  # let QEMU finish writing
                return Screen(path)
        except FileNotFoundError:
            pass
    raise TestFailure("screendump produced nothing")


def check(ok, what):
    if not ok:
        raise TestFailure(what)


def close(a, b, tolerance):
    return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def rgb332(color):
    """What a colour becomes in VGA's 3-3-2 palette (before dithering)."""
    r, g, b = color
    return (r >> 5) * 255 // 7, (g >> 5) * 255 // 7, (b >> 6) * 255 // 3


def check_swatches(s, width, scale, tolerance):
    """gfxdemo's layout: side = width / 16, swatch i at
    (side / 2 + i * (side + side / 4), 3 * side), `side` pixels square.
    The screen image may be larger than the mode (QEMU doubles 320x200)."""
    side = width // 16
    gap = side + side // 4
    quantise = rgb332 if tolerance else (lambda c: c)
    for i, want in enumerate(SWATCHES):
        cx = (side // 2 + i * gap + side // 2) * scale
        cy = (3 * side + side // 2) * scale
        got = s.pixel(cx, cy)
        check(close(got, want, tolerance), f"swatch {i} at ({cx},{cy}) is {got}, not {want}")
    # The grey outline just outside the first swatch.
    ox, oy = (side // 2 - 1) * scale, (3 * side + side // 2) * scale
    want = quantise((0xC0, 0xC0, 0xC0))
    got = s.pixel(ox, oy)
    check(close(got, want, tolerance), f"the swatch outline is {got}, not {want}")


def text_line(m, tmpdir, name, text="QWERTYUIOP asdfghjkl 0123456789 @#$%"):
    """Echoes a line and returns the pixels of its text row: the screen
    is full after boot, so output lands on row 23."""
    run_command(m, "serial", f"echo {text}", [], SHELL_PROMPT)
    s = screendump(m, tmpdir, name)
    check((s.w, s.h) == (720, 400), f"text mode should be 720x400, is {s.w}x{s.h}")
    return s.band(23)


def run_demo(m, tmpdir, args, name, expect_size, width, scale, tolerance, reference,
             printed_meanwhile=None):
    m.type_serial(f"gfxdemo {args}\r")
    m.expect(b"press Enter")
    time.sleep(0.5)
    s = screendump(m, tmpdir, name)
    m.type_serial("\r")  # back to text before judging, so a failure can't leave it waiting
    m.expect(b"back to text mode")
    m.expect(SHELL_PROMPT)
    if printed_meanwhile:
        # gfxdemo's "press Enter" line was printed while graphics were
        # up; it must be on the text screen now, three rows up.
        after = screendump(m, tmpdir, name + "-back")
        check(after.band(21) == printed_meanwhile,
              f"{name}: text printed during graphics is missing from the screen")
    check((s.w, s.h) == expect_size, f"{name}: screen is {s.w}x{s.h}, not {expect_size}")
    check_swatches(s, width, scale, tolerance)
    check(text_line(m, tmpdir, name + "-text") == reference,
          f"{name}: text looks different after graphics (font not restored)")


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/manios-zkt.elf"
    tmpdir = tempfile.mkdtemp(prefix="zkt-gfx-")
    m = Machine(kernel)
    failures = 0

    def step(name, fn):
        nonlocal failures
        try:
            fn()
            print(f"PASS: {name}")
        except TestFailure as e:
            print(f"FAIL: {name}: {e}")
            failures += 1

    try:
        boot = m.expect(SHELL_PROMPT)
        step("boot finds the Bochs VBE adapter on PCI",
             lambda: check("fb: Bochs VBE at pci 00:02.0, framebuffer 0xfd000000" in boot
                           and "pci: 6 devices" in boot, boot[-600:]))
        step("fbctl reads 'text' in text mode",
             lambda: run_command(m, "serial", "cat /dev/fbctl", ["\r\ntext\r\n"], SHELL_PROMPT))
        step("the framebuffer can't be read in text mode",
             lambda: run_command(m, "serial", "cat /dev/fb", ["no such device or address"],
                                 SHELL_PROMPT))
        reference = text_line(m, tmpdir, "before")
        press = text_line(m, tmpdir, "press", "gfxdemo: 640x480x32 xrgb8888, press Enter")
        step("Bochs VBE 640x480: swatches; text restored, with what was printed meanwhile",
             lambda: run_demo(m, tmpdir, "", "bga640", (640, 480), 640, 1, 0, reference, press))
        step("Bochs VBE 800x600: swatches, then text restored",
             lambda: run_demo(m, tmpdir, "800 600", "bga800", (800, 600), 800, 1, 0, reference))
        # 3-3-2 colour through the 6-bit DAC and QEMU's rendering of it:
    # close to the quantised colour, not exact.
        step("VGA mode 13h: swatches, then text restored",
             lambda: run_demo(m, tmpdir, "vga", "vga", (640, 400), 320, 2, 12, reference))
        step("unsupported modes are refused",
             lambda: (run_command(m, "serial", "gfxdemo 100 100", ["invalid argument"], SHELL_PROMPT),
                      run_command(m, "serial", "gfxdemo 5000 4000", ["invalid argument"], SHELL_PROMPT),
                      run_command(m, "serial", "gfxdemo 642 480", ["invalid argument"], SHELL_PROMPT)))
        step("the fb device: offsets, bounds, reading back, fbctl errors",
             lambda: run_command(m, "serial", "/boot/test/fbtest", ["fbtest: all ", "!FAIL"],
                                 SHELL_PROMPT))
        step("a mode change back and forth leaves text intact",
             lambda: check(text_line(m, tmpdir, "after") == reference, "text differs at the end"))
    except TestFailure as e:
        print(f"FAIL: {e}")
        failures += 1
    finally:
        m.close()

    # VMware's SVGA II, which is also VirtualBox's VMSVGA: the Bochs VBE
    # registers, but the video memory in BAR 1, after its I/O ports.
    m = Machine(kernel, ["-vga", "vmware"])
    try:
        boot = m.expect(SHELL_PROMPT)
        step("SVGA II (VirtualBox's VMSVGA): found, with its video memory BAR",
             lambda: check(re.search(r"fb: VMware SVGA II at pci 00:02\.0, framebuffer 0x[0-9a-f]{8}, \d+ KiB",
                                     boot), boot[-600:]))
        reference = text_line(m, tmpdir, "svga-before")
        step("SVGA II 800x600: swatches, then text restored",
             lambda: run_demo(m, tmpdir, "800 600", "svga800", (800, 600), 800, 1, 0, reference))
        step("SVGA II: the fb device",
             lambda: run_command(m, "serial", "/boot/test/fbtest", ["fbtest: all ", "!FAIL"],
                                 SHELL_PROMPT))
    except TestFailure as e:
        print(f"FAIL: {e}")
        failures += 1
    finally:
        m.close()
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
