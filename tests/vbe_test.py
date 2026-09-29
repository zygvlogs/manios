#!/usr/bin/env python3
"""VESA BIOS Extensions test (M21): the boot loader's own VESA probe,
for real display adapters that have no Bochs VBE registers
(zkt/drivers/fb.c's other path) -- boot/stage2_entry.S's vbe_probe.

Only "gfx=auto" on the command line asks for it, and that command line
can only take effect through a real boot -- from a CD, as the boot
loader (not QEMU's own Multiboot loader, which `-kernel` uses and which
never runs stage2_entry.S at all) -- so this builds small boot areas and
ISOs of its own, each with a different baked-in default command line
(tools/mkbootarea.py --cmdline), and boots them from CD.

Three real, unmodified QEMU display adapters give three outcomes,
without ManiOS needing to be changed to reach any of them:
  - `-vga std` (or VirtualBox's, or VMware's): has the Bochs VBE
    registers zkt/drivers/fb.c drives directly. gfx=auto still probes
    and sets a VESA mode over real INT 10h calls (proving that path
    works even here), but the kernel prefers the registers, which can
    do more (any size, changed at will) -- the loader's mode goes
    unused, and everything is exactly as without gfx=auto.
  - `-vga qxl`: a different PCI device, with no Bochs registers, whose
    BIOS still answers real VBE calls with a usable 1024x768x32 linear
    framebuffer -- the kernel's other path, actually used: this is
    where M21's own code (fb.c's LFB kind) is exercised, checked by
    drawing with gfxdemo and reading the screen back (as gfx_test.py
    does for the Bochs path).
  - `-vga cirrus`: a real VESA BIOS with no mode of the 8:8:8:8 XRGB,
    32-bit, linear-framebuffer shape fb.c needs -- vbe_probe finds
    nothing to set, says so, and the machine boots exactly as if
    gfx=auto had not been given.

Usage: tests/vbe_test.py [path-to-kernel-elf]
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(__file__))
from console_test import VERSION, Machine, SHELL_PROMPT, TestFailure, run_command
from gfx_test import screendump, check_swatches


def check(ok, what):
    if not ok:
        raise TestFailure(what)


def build_iso(build, workdir, cmdline, name):
    """A boot area and ISO like the release's, but with `cmdline` baked
    in as the default (tools/mkbootarea.py --cmdline: what a real disc's
    loader would offer to edit, here taken as is, unedited)."""
    boot = os.path.join(build, "boot")
    area = os.path.join(workdir, name + ".bin")
    iso = os.path.join(workdir, name + ".iso")
    subprocess.run([sys.executable, "tools/mkbootarea.py", "--mbr", os.path.join(boot, "mbr.bin"),
                    "--stage2", os.path.join(boot, "stage2.bin"), "--kernel",
                    os.path.join(build, "manios-zkt.stripped.elf"), "--version", VERSION,
                    "--cmdline", cmdline, "-o", area], check=True)
    subprocess.run([sys.executable, "tools/mkiso.py", "--bootarea", area, "--cdboot",
                    os.path.join(boot, "cdboot.bin"), "--mbr", os.path.join(boot, "mbr.bin"),
                    "--volume", "MANIOS_VBE", "--file", "README.TXT=README.md", "--file",
                    "LICENSE.TXT=LICENSE", "-o", iso], check=True)
    return iso


def qemu_has_device(name):
    """Whether this QEMU build has the device (some are separate
    packages: Debian and Ubuntu ship qxl-vga in qemu-system-modules-spice)."""
    out = subprocess.run(["qemu-system-i386", "-device", "help"], capture_output=True, text=True)
    return re.search(rf'^name "{re.escape(name)}"', out.stdout + out.stderr, re.M) is not None


def boot(iso, vga, memory=64):
    return Machine(None, ["-boot", "d", "-cdrom", iso, "-vga", vga], memory)


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/manios-zkt.elf"
    build = os.path.dirname(kernel)
    workdir = tempfile.mkdtemp(prefix="zkt-vbe-")
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
        plain = build_iso(build, workdir, "", "plain")
        auto = build_iso(build, workdir, "gfx=auto", "auto")
        bogus = build_iso(build, workdir, "gfx=1024x768", "bogus")  # not "auto": not supported

        def no_gfx():
            m = boot(plain, "std")
            try:
                out = m.expect(SHELL_PROMPT, timeout=120)
                check("graphics:" not in out, f"a plain boot printed a graphics: line:\n{out}")
                check("fb: Bochs VBE at pci" in out, f"the usual Bochs VBE line is missing:\n{out}")
                check("ManiOS " + VERSION + " is ready" in out, "the machine didn't reach the shell")
            finally:
                m.close()
        step("no gfx=: unchanged -- no probe, the usual boot", no_gfx)

        def bogus_gfx():
            m = boot(bogus, "std")
            try:
                out = m.expect(SHELL_PROMPT, timeout=120)
                check("graphics:" not in out, f"an unsupported gfx= value still probed:\n{out}")
            finally:
                m.close()
        step("gfx=1024x768 (not \"auto\"): ignored, like no gfx= at all", bogus_gfx)

        def std_precedence():
            m = boot(auto, "std")
            try:
                out = m.expect(SHELL_PROMPT, timeout=120)
                check(re.search(r"graphics: VESA \d+x\d+ found", out),
                      f"gfx=auto didn't probe a real mode over Bochs VBE too:\n{out}")
                check("fb: Bochs VBE at pci" in out, "the Bochs VBE line is missing")
                check("fb: VESA linear framebuffer" not in out,
                      "the kernel used the loader's mode instead of preferring Bochs VBE")
                run_command(m, "serial", "cat /dev/fbctl", ["\r\ntext\r\n"], SHELL_PROMPT)
            finally:
                m.close()
        step("gfx=auto with Bochs VBE too: probed, but the registers still win", std_precedence)

        def cirrus_none():
            m = boot(auto, "cirrus")
            try:
                out = m.expect(SHELL_PROMPT, timeout=120)
                check("graphics: no VESA linear framebuffer" in out,
                      f"no usable Cirrus mode should have been found:\n{out}")
                check("fb: VESA linear framebuffer" not in out, "no framebuffer was set up")
                check("fb: Bochs VBE" not in out, "Cirrus isn't Bochs VBE")
                run_command(m, "serial", "gfxdemo", ["no such device"], SHELL_PROMPT)
            finally:
                m.close()
        step("gfx=auto, no usable mode (Cirrus): says so, boots as usual", cirrus_none)

        def qxl_lfb():
            # The only test of the kernel's own LFB path: a missing
            # device fails it, rather than skipping it unseen.
            check(qemu_has_device("qxl-vga"),
                  "this QEMU has no qxl-vga device (Debian/Ubuntu: apt install qemu-system-modules-spice)")
            m = boot(auto, "qxl")
            try:
                out = m.expect(SHELL_PROMPT, timeout=120)
                check(re.search(r"graphics: VESA \d+x\d+ found", out), "no VESA mode probed")
                mo = re.search(r"fb: VESA linear framebuffer at (0x[0-9a-f]+), (\d+)x(\d+)", out)
                check(mo, f"the kernel didn't take the loader's framebuffer:\n{out}")
                check("fb: Bochs VBE" not in out, "QXL isn't Bochs VBE")
                run_command(m, "serial", "cat /dev/fbctl", ["\r\ntext\r\n"], SHELL_PROMPT)
                run_command(m, "serial", "cat /dev/fb", ["no such device or address"], SHELL_PROMPT)
                # mode 640 480: on a fixed-size LFB, any size asked for
                # is answered with the one there is (M21's own design;
                # gfxdemo does the same through libgfx, exercised below).
                m.type_serial("gfxdemo\r")
                banner = m.expect(b"press Enter", timeout=15)
                bm = re.search(r"(\d+)x(\d+)x(\d+) xrgb8888", banner)
                check(bm, f"gfxdemo's banner is wrong: {banner}")
                w, h = int(bm[1]), int(bm[2])
                check(f"{w}x{h}" == f"{mo[2]}x{mo[3]}",
                      f"gfxdemo got {w}x{h}, the loader set {mo[2]}x{mo[3]}")
                time.sleep(0.5)
                s = screendump(m, workdir, "qxl")
                check((s.w, s.h) == (w, h), f"the screen is {s.w}x{s.h}, gfxdemo says {w}x{h}")
                check_swatches(s, w, 1, 0)
                m.type_serial("\r")
                m.expect(b"back to text mode", timeout=15)
                m.expect(SHELL_PROMPT, timeout=15)
                run_command(m, "serial", "cat /dev/fbctl", ["\r\ntext\r\n"], SHELL_PROMPT)
            finally:
                m.close()
        step("gfx=auto, a real linear framebuffer (QXL): drawn to, read back, exact colours",
             qxl_lfb)
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    print(f"vbe_test: {failures} failed" if failures else "vbe_test: all passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
