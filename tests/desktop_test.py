#!/usr/bin/env python3
"""Desktop test (M12): the ManiOS desktop driven like a user would --
keys through QEMU's `sendkey`, the pointer through `mouse_move` and
`mouse_button` -- and checked on the screen itself (`screendump`) and on
the serial line, where the desktop logs what it does.

Text is read back off the screen by matching every character cell
against the glyphs of desktop/libgfx/font.txt, so a check like "the
terminal's second row says 'hello, desktop'" is exact.

Usage: tests/desktop_test.py [path-to-kernel-elf]
"""
import os
import re
import sys
import tempfile
import time

from console_test import SHELL_PROMPT, Machine, TestFailure, run_command
from gfx_test import screendump

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import mkfont  # noqa: E402

FONT = os.path.join(os.path.dirname(__file__), "..", "desktop", "libgfx", "font.txt")
CELL_W, CELL_H = 6, 11

# desktop/wm/desktop.h and the applications' colours.
PANEL = (0x1E, 0x24, 0x30)
ACCENT = (0xE0, 0x7A, 0x2E)
INACTIVE = (0x5A, 0x62, 0x70)
LIGHT = (0xE8, 0xE8, 0xE8)
DARK = (0x1A, 0x1A, 0x1A)
TERM_FG = (0xD8, 0xDE, 0xE9)
CLOCK_DATE = (0xC8, 0xCE, 0xD8)

TERM_MARGIN = 4
TITLE_H = 18


def load_glyphs():
    with open(FONT) as f:
        glyphs = mkfont.parse(f.read())
    table = {}
    for ch, rows in glyphs.items():
        table[tuple(rows)] = ch
    return table


GLYPHS = load_glyphs()


def read_text(screen, x, y, cells, color, scale=1):
    """The characters in `cells` cells from (x, y), drawn in `color` (a
    glyph occupies rows 1-9 and columns 0-4 of its cell)."""
    out = []
    for i in range(cells):
        cx = x + i * CELL_W * scale
        rows = []
        for r in range(9):
            row = ""
            for b in range(5):
                px, py = cx + b * scale, y + (r + 1) * scale
                inside = 0 <= px < screen.w and 0 <= py < screen.h
                row += "#" if inside and screen.pixel(px, py) == color else "."
            rows.append(row)
        out.append(GLYPHS.get(tuple(rows), "?"))
    return "".join(out).rstrip()


class Desktop:
    def __init__(self, m, tmpdir):
        self.m, self.tmpdir, self.shots = m, tmpdir, 0

    def screen(self):
        self.shots += 1
        return screendump(self.m, self.tmpdir, f"shot{self.shots}")

    def wait(self, predicate, what, timeout=8):
        """Screendumps until predicate(screen) holds."""
        deadline = time.time() + timeout
        while True:
            s = self.screen()
            if predicate(s):
                return s
            if time.time() > deadline:
                raise TestFailure(f"the screen never showed {what}")
            time.sleep(0.2)

    def key(self, name):
        self.m.monitor.sendall(f"sendkey {name} 10\n".encode())
        time.sleep(0.08)

    def move(self, dx, dy):
        self.m.monitor.sendall(f"mouse_move {dx} {dy}\n".encode())
        time.sleep(0.1)

    def button(self, state):
        self.m.monitor.sendall(f"mouse_button {state}\n".encode())
        time.sleep(0.1)

    def type(self, text):
        self.m.type_keyboard(text)


def term_rows(s, x, y, count=24):
    """The rows of a terminal whose content is at (x, y)."""
    return [read_text(s, x + TERM_MARGIN, y + TERM_MARGIN + r * CELL_H, 80, TERM_FG)
            for r in range(count)]


def title(s, x, y, width_cells, color):
    """A window's title, for content at (x, y)."""
    return read_text(s, x + 6, y - TITLE_H + 4, width_cells, color)


def check(ok, what):
    if not ok:
        raise TestFailure(what)


class Scenario:
    """One desktop session; `pointer` tracks where the desktop's pointer
    is (it starts at the centre and moves exactly as told)."""

    def __init__(self, m, tmpdir):
        self.m = m
        self.d = Desktop(m, tmpdir)
        self.pointer = (400, 300)
        self.term = (60, 50)

    def opened(self, name, size):
        """Waits for the desktop to log a new window; its number and
        content position (they depend on what came before)."""
        before = self.m.expect(f' "{name}" {size} at ')
        where = self.m.expect("\r\n")
        number = re.search(r"window (\d+)$", before)
        pos = re.fullmatch(r"(-?\d+),(-?\d+)", where)
        check(number and pos, f"window log line: {before[-40:]!r} {where!r}")
        return int(number.group(1)), int(pos.group(1)), int(pos.group(2))

    def point(self, x, y):
        px, py = self.pointer
        self.d.move(x - px, y - py)
        self.pointer = (x, y)

    def click(self, x, y):
        self.point(x, y)
        self.d.button(1)
        self.d.button(0)

    def start(self):
        self.m.type_serial("desktop\r")
        self.m.expect("800x600, serving /dev/wsys; F1 opens the menu")
        s = self.d.wait(lambda s: s.w == 800 and read_text(s, 22, 5, 6, LIGHT) == "ManiOS",
                        "the panel")
        check(s.pixel(400, 10) == PANEL and s.pixel(400, 21) == ACCENT, "panel colours")
        clock = read_text(s, 800 - 8 - 48, 5, 8, LIGHT)
        check(re.fullmatch(r"\d\d:\d\d:\d\d", clock), f"the panel's clock reads {clock!r}")
        check(s.pixel(400, 300) == (0, 0, 0) and s.pixel(401, 302) == (255, 255, 255),
              "the pointer is drawn at the centre")
        top, bottom = s.pixel(5, 30), s.pixel(5, 590)
        check(top != bottom and top[2] > bottom[2], f"the background is a gradient: {top} {bottom}")

    def menu(self):
        self.d.key("f1")
        s = self.d.wait(lambda s: read_text(s, 11, 29, 8, DARK) == "Terminal", "the menu")
        items = [read_text(s, 11, 49, 12, LIGHT), read_text(s, 11, 69, 12, LIGHT),
                 read_text(s, 11, 96, 12, LIGHT)]
        check(items == ["Clock", "About ManiOS", "Exit desktop"], f"menu items: {items}")
        check(s.pixel(3, 30) == ACCENT, "the first item is highlighted")
        self.d.key("esc")
        self.d.wait(lambda s: read_text(s, 11, 29, 8, DARK) != "Terminal", "the menu closing")

    def terminal(self):
        self.d.key("f1")
        self.d.key("ret")
        self.m.expect('window 1 "Terminal" 488x272 at 60,50')
        x, y = self.term
        self.d.wait(lambda s: term_rows(s, x, y, 1)[0] == "manios%", "the shell's prompt")
        self.d.type("echo hello, desktop\n")
        s = self.d.wait(lambda s: term_rows(s, x, y, 3)[1:] == ["hello, desktop", "manios%"],
                        "echo's output")
        check(s.pixel(x + 100, y - 10) == ACCENT and title(s, x, y, 8, DARK) == "Terminal",
              "the terminal's title bar shows it has the focus")
        # The window system is files: listed, counted and read from the shell.
        self.d.type("ls /dev/wsys | wc; cat /dev/wsys/1/ctl\n")
        self.d.wait(lambda s: term_rows(s, x, y, 6)[3:5]
                    == ["      2       2       7", "1 60 50 488 272 1 Terminal"],
                    "ls and cat of /dev/wsys")

    def wintest(self):
        self.d.type("/boot/test/wintest\n")
        # Its summary also goes to /dev/cons: "all N checks passed" or
        # "F of N checks failed" (the details are in the terminal).
        before = self.m.expect(" checks ", timeout=60)
        verdict = self.m.expect("\r\n")
        check(verdict == "passed" and "wintest: all " in before, f"{before[-60:]} checks {verdict}")

    def drag(self):
        x, y = self.term
        self.point(x + 140, y - 10)
        self.d.button(1)
        self.point(x + 240, y + 70)
        self.d.button(0)
        self.m.expect("window 1 moved to 160,130")
        self.term = (160, 130)
        x, y = self.term
        s = self.d.wait(lambda s: title(s, x, y, 8, DARK) == "Terminal", "the terminal moved")
        check(s.pixel(60 + 20, 50 - 10) != ACCENT, "nothing is left where the title bar was")

    def clock(self):
        self.d.key("f1")
        self.d.key("down")
        self.d.key("ret")
        self.clock_id, cx, cy = self.opened("Clock", "200x70")
        self.clock_at = (cx, cy)

        def reading(s):
            return read_text(s, cx + 28, cy + 10, 8, ACCENT, 3)

        s = self.d.wait(lambda s: re.fullmatch(r"\d\d:\d\d:\d\d", reading(s)), "the clock's time")
        first = reading(s)
        date = read_text(s, cx + 58, cy + 50, 14, CLOCK_DATE)
        check(re.fullmatch(r"20\d\d-\d\d-\d\d UTC", date), f"the clock's date reads {date!r}")
        self.d.wait(lambda s: re.fullmatch(r"\d\d:\d\d:\d\d", reading(s)) and reading(s) != first,
                    "the clock ticking")
        tx, ty = self.term
        s = self.screen_now()
        # (The clock may cover the terminal's title text: look at its right end.)
        check(title(s, cx, cy, 5, DARK) == "Clock" and s.pixel(tx + 400, ty - 10) == INACTIVE,
              "the new window has the focus; the terminal's title bar is grey")

    def screen_now(self):
        return self.d.screen()

    def focus(self):
        # F2 brings the bottom window (the terminal) up; keys follow it.
        tx, ty = self.term
        self.d.key("f2")
        self.d.wait(lambda s: title(s, tx, ty, 8, DARK) == "Terminal", "F2 focusing the terminal")
        self.d.type("echo focus\n")
        self.d.wait(lambda s: "focus" in term_rows(s, tx, ty), "keys reaching the terminal")
        # And back to the clock, which quits on q -- the terminal gets no q.
        self.d.key("f2")
        cx, cy = self.clock_at
        self.d.wait(lambda s: title(s, cx, cy, 5, DARK) == "Clock", "F2 focusing the clock")
        self.d.key("q")
        self.m.expect(f"window {self.clock_id} closed")
        self.m.expect("exited (status 0)")
        s = self.screen_now()
        rows = term_rows(s, tx, ty)
        last = max(i for i, r in enumerate(rows) if r)
        check(rows[last] == "manios%", f"the terminal's line is untouched: {rows[last]!r}")

    def about_close_box(self):
        self.d.key("f1")
        self.d.key("down")
        self.d.key("down")
        self.d.key("ret")
        number, ax, ay = self.opened("About ManiOS", "340x170")
        self.d.wait(lambda s: read_text(s, ax + 48, ay + 16, 6, LIGHT, 2) == "ManiOS",
                    "the About window")
        # The close box: 12x12 at the title bar's right, 3 pixels in.
        self.click(ax + 340 - 15 + 6, ay - 18 + 3 + 6)
        self.m.expect(f"window {number} closed")
        self.m.expect("exited (status 0)")

    def nested(self):
        tx, ty = self.term
        self.d.type("desktop\n")
        self.d.wait(lambda s: any(r.startswith("desktop: already running") for r in term_rows(s, tx, ty)),
                    "a second desktop refusing to start")

    def terminal_close_box(self):
        tx, ty = self.term
        self.click(tx + 488 - 15 + 6, ty - 18 + 3 + 6)
        self.m.expect("window 1 closed")
        self.m.expect("exited (status 0)")
        self.d.wait(lambda s: s.pixel(tx + 100, ty - 10) not in (ACCENT, INACTIVE),
                    "the terminal gone from the screen")

    def exit(self):
        self.d.key("f1")
        self.d.key("up")  # wraps round to the last item
        self.d.key("ret")
        self.m.expect("desktop: bye")
        self.m.expect(SHELL_PROMPT)
        s = self.d.wait(lambda s: s.w == 720, "text mode")
        check(s.w == 720 and s.h == 400, f"text mode is back: {s.w}x{s.h}")


def main():
    kernel = sys.argv[1] if len(sys.argv) > 1 else "build/manios-zkt.elf"
    tmpdir = tempfile.mkdtemp(prefix="zkt-desktop-")
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

    m = Machine(kernel)
    try:
        m.expect(SHELL_PROMPT)
        step("a screen below 640x480 is refused",
             lambda: run_command(m, "serial", "desktop 320 200",
                                 ["desktop: the screen must be at least 640x480"], SHELL_PROMPT))
        sc = Scenario(m, tmpdir)
        # Each step builds on the one before, so the first failure ends the run.
        for name, fn in [
            ("the desktop starts: panel, clock, background, pointer", sc.start),
            ("F1 opens the launcher menu; Escape closes it", sc.menu),
            ("the menu starts a terminal running sh; keys reach it", sc.terminal),
            ("wintest: the window system's files, from inside the desktop", sc.wintest),
            ("dragging a window by its title bar", sc.drag),
            ("the menu starts the clock, which ticks and takes the focus", sc.clock),
            ("F2 moves the focus; keys go to the focused window only", sc.focus),
            ("About ManiOS, closed with its close box", sc.about_close_box),
            ("a desktop inside the desktop is refused", sc.nested),
            ("the terminal's close box ends it and its shell", sc.terminal_close_box),
            ("Exit desktop restores the text console", sc.exit),
        ]:
            if not step(name, fn):
                break
        else:
            step("the keyboard is the console's again",
                 lambda: run_command(m, "keyboard", "echo back", ["\r\nback\r\n"], SHELL_PROMPT))
            step("the desktop's namespace was its own: no /dev/wsys here",
                 lambda: run_command(m, "serial", "ls /dev", ["cons", "!wsys"], SHELL_PROMPT))
            step("the desktop starts again (640x480) and exits from the keyboard",
                 lambda: (m.type_serial("desktop 640 480\r"),
                          m.expect("640x480, serving /dev/wsys"),
                          sc.exit()))
    finally:
        m.close()

    # Without Bochs VBE (a Cirrus card), the desktop says so and the
    # console carries on.
    m = Machine(kernel, ["-vga", "cirrus"])
    try:
        m.expect(SHELL_PROMPT)
        step("without a linear framebuffer the desktop explains and exits",
             lambda: run_command(m, "serial", "desktop",
                                 ["desktop: cannot set a 800x600 mode", "Bochs VBE display is needed"],
                                 SHELL_PROMPT))
        step("... and the console still works",
             lambda: run_command(m, "serial", "echo still here", ["\r\nstill here\r\n"], SHELL_PROMPT))
    finally:
        m.close()

    print("desktop test: " + ("all passed" if not failures else f"{failures} failed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
