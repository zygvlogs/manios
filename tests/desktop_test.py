#!/usr/bin/env python3
"""Desktop test (M12, M18): ManiDE driven like a user would -- keys
through QEMU's `sendkey` (Alt bindings included), the pointer through
`mouse_move` and `mouse_button` -- and checked on the screen itself
(`screendump`) and on the serial line, where ManiDE logs what it does.

Text is read back off the screen by matching every character cell
against the glyphs of desktop/libgfx/font.txt, so a check like "the
terminal's second row says 'hello, ManiDE'" is exact. Where panes are
comes from the same arithmetic as desktop/manide/tile.c.

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

ROOT = os.path.join(os.path.dirname(__file__), "..")
FONT = os.path.join(ROOT, "desktop", "libgfx", "font.txt")
VERSION = open(os.path.join(ROOT, "VERSION")).read().strip()
CELL_W, CELL_H = 6, 11

# desktop/manide/manide.h, draw.c and the applications' colours.
BAR_H, HEAD_H, GAP = 18, 15, 4
TEXT_Y = 4
WS_X, WS_STEP = 84, 14
LAYOUT_X = WS_X + 9 * WS_STEP + 2
FACTS_X = LAYOUT_X + 9 * CELL_W
DESK = (0x0A, 0x0D, 0x12)
BAR = (0x12, 0x16, 0x1E)
EDGE = (0x2A, 0x31, 0x3C)
FOCUS = (0x3F, 0xC8, 0xD8)
ACCENT = (0xE0, 0x7A, 0x2E)
TEXT = (0xD8, 0xDE, 0xE9)
DIM = (0x6B, 0x74, 0x83)
DARK = (0x10, 0x12, 0x16)
TERM_FG = (0xD8, 0xDE, 0xE9)
TERM_BG = (0x0F, 0x13, 0x19)
WELCOME_CYAN = (0x3F, 0xC8, 0xD8)
TERM_MARGIN = 4
MENU_ITEMS = ["Terminal", "System info", "Processes", "Welcome", "Clock", "About ManiOS",
              None, "Exit ManiDE"]


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


# --- where ManiDE puts things (tile.c) ---

def area(w=800, h=600):
    return (GAP, BAR_H + GAP, w - 2 * GAP, h - BAR_H - 2 * GAP)


def share(start, length, n, i):
    each = (length - (n - 1) * GAP) // n
    at = start + i * (each + GAP)
    return at, (start + length - at if i == n - 1 else each)


def grid(n, w=800, h=600):
    ax, ay, aw, ah = area(w, h)
    cols = 1
    while cols * cols < n:
        cols += 1
    panes = []
    for c in range(cols):
        rows = n // cols + (1 if c >= cols - n % cols else 0)
        x, pw = share(ax, aw, cols, c)
        for r in range(rows):
            y, ph = share(ay, ah, rows, r)
            panes.append((x, y, pw, ph))
    return panes


def tall(n, master=55, w=800, h=600):
    ax, ay, aw, ah = area(w, h)
    mw = (aw - GAP) * master // 100
    panes = [(ax, ay, mw, ah)]
    for i in range(1, n):
        y, ph = share(ay, ah, n - 1, i - 1)
        panes.append((ax + mw + GAP, y, aw - mw - GAP, ph))
    return panes


def content(pane):
    x, y, w, h = pane
    return (x + 1, y + HEAD_H, w - 2, h - HEAD_H - 1)


def term_size(pane):
    _, _, w, h = content(pane)
    return (w - 2 * TERM_MARGIN) // CELL_W, (h - 2 * TERM_MARGIN) // CELL_H


def term_rows(s, pane, color=TERM_FG):
    """The text rows of a terminal in `pane`."""
    x, y, _, _ = content(pane)
    cols, rows = term_size(pane)
    return [read_text(s, x + TERM_MARGIN, y + TERM_MARGIN + r * CELL_H, cols, color)
            for r in range(rows)]


def clock_face(pane):
    """Where the clock (desktop/apps/clock.c) draws the time in a pane:
    x, y and scale."""
    x, y, w, h = content(pane)
    scale = 1
    while 48 * (scale + 1) + 20 <= w and CELL_H * (scale + 1) + 30 <= h and scale < 12:
        scale += 1
    small = 2 if scale >= 6 else 1
    block = CELL_H * scale + 8 + CELL_H * small
    return x + (w - 48 * scale) // 2, y + (h - block) // 2, scale


def title(s, pane, color, cells=12):
    return read_text(s, pane[0] + 6, pane[1] + 3, cells, color)


def edge(s, pane):
    """The colour of a pane's left edge, halfway down."""
    return s.pixel(pane[0], pane[1] + pane[3] // 2)


def check(ok, what):
    if not ok:
        raise TestFailure(what)


class Desktop:
    def __init__(self, m, tmpdir):
        self.m, self.tmpdir, self.shots = m, tmpdir, 0

    def screen(self):
        self.shots += 1
        return screendump(self.m, self.tmpdir, f"shot{self.shots}")

    def wait(self, predicate, what, timeout=10):
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
        time.sleep(0.1)

    def move(self, dx, dy):
        self.m.monitor.sendall(f"mouse_move {dx} {dy}\n".encode())
        time.sleep(0.1)

    def button(self, state):
        self.m.monitor.sendall(f"mouse_button {state}\n".encode())
        time.sleep(0.1)

    def type(self, text):
        self.m.type_keyboard(text)


class Scenario:
    """One ManiDE session; `pointer` tracks where ManiDE's pointer is (it
    starts at the centre and moves exactly as told). The session file
    opens fetch, welcome, top and a terminal, in a 2x2 grid."""

    def __init__(self, m, tmpdir):
        self.m = m
        self.d = Desktop(m, tmpdir)
        self.pointer = (400, 300)

    def point(self, x, y):
        px, py = self.pointer
        self.d.move(x - px, y - py)
        self.pointer = (x, y)

    def click(self, x, y):
        self.point(x, y)
        self.d.button(1)
        self.d.button(0)

    def start(self):
        self.m.type_serial("manide\r")
        self.m.expect("800x600, serving /dev/wsys; Alt+Enter: terminal, F1: menu")
        for n, name in enumerate(["fetch", "Welcome", "top", "Terminal"], 1):
            self.m.expect(f'window {n} "{name}"')
        s = self.d.wait(lambda s: s.w == 800 and read_text(s, 6, TEXT_Y, 6, ACCENT) == "ManiDE",
                        "the status bar")
        check(s.pixel(400, 8) == BAR and s.pixel(400, BAR_H - 1) == EDGE, "the bar's colours")
        check(s.pixel(WS_X + 1, 3) == ACCENT and read_text(s, WS_X + 3, TEXT_Y, 1, DARK) == "1"
              and read_text(s, WS_X + WS_STEP + 3, TEXT_Y, 1, DIM) == "2",
              "workspace 1 is shown, 2 is empty")
        check(read_text(s, LAYOUT_X, TEXT_Y, 6, DIM) == "[grid]", "the layout: grid")
        stamp = read_text(s, 800 - 6 - 19 * CELL_W, TEXT_Y, 19, TEXT)
        check(re.fullmatch(r"20\d\d-\d\d-\d\d \d\d:\d\d:\d\d", stamp), f"the date and time: {stamp!r}")
        # (The | between the facts are dimmer: they read as spaces here.)
        facts = read_text(s, FACTS_X, TEXT_Y, (800 - 6 - 19 * CELL_W - 12 - FACTS_X) // CELL_W, TEXT)
        check(re.fullmatch(rf"manios +ZKT {re.escape(VERSION)} +up \d+[smhd]( \d+[mh])? +cpu \d+% +mem \d+%"
                           r" +no network",
                           facts), f"host, kernel, uptime, cpu, memory, network: {facts!r}")
        check(s.pixel(400, 300) == (0, 0, 0) and s.pixel(401, 302) == (255, 255, 255),
              "the pointer is drawn at the centre")

    def panes(self):
        fetch, welcome, top, term = grid(4)
        self.term = term
        self.point(799, 599)  # the corner: out of the way of what is read
        s = self.d.wait(lambda s: any(f"ManiOS {VERSION} i386" in r for r in term_rows(s, fetch))
                        and term_rows(s, top)[0].startswith("top - up ")
                        and term_rows(s, term)[0] == "manios%",
                        "fetch, top and a shell in their panes")
        check([title(s, p, DIM) for p in (fetch, welcome, top)] == ["fetch", "Welcome", "top"]
              and title(s, term, FOCUS) == "Terminal", "the panes' titles; the terminal has the focus")
        check(edge(s, term) == FOCUS and edge(s, fetch) == EDGE, "the focused pane's edge is cyan")
        # top asked the terminal its size: its table fits the pane, the
        # first process (row 5) right under the header (row 4, black on
        # cyan) -- at 80 columns its lines would wrap onto those rows. (A screendump can
        # catch a frame half drawn: wait for a whole one.)
        s = self.d.wait(lambda s: re.match(r"\s*\d+\s+\d+\s+[a-z]+\s", term_rows(s, top)[5])
                        and any(r.endswith(" manide") for r in term_rows(s, top)),
                        "top fitting its pane, ManiDE among its processes")
        x, y, w, h = content(welcome)
        colours = {s.pixel(px, py) for px in range(x, x + w, 2) for py in range(y, y + h, 2)}
        check(ACCENT in colours and WELCOME_CYAN in colours, "the welcome pane's amber and cyan")
        check(s.pixel(GAP + 792 // 2, 100) == DESK, "the desk shows between the panes")

    def terminal(self):
        self.d.type("echo hello, ManiDE\n")
        self.d.wait(lambda s: term_rows(s, self.term)[1:3] == ["hello, ManiDE", "manios%"],
                    "echo's output")
        # The window system is files: listed, counted and read from the shell.
        x, y, w, h = content(self.term)
        self.d.type("ls /dev/wsys | wc; cat /dev/wsys/4/ctl\n")
        self.d.wait(lambda s: term_rows(s, self.term)[3:5]
                    == ["      5       5      16", f"4 {x} {y} {w} {h} 1 Terminal"],
                    "ls and cat of /dev/wsys: the window has the size of its pane")

    def layouts(self):
        self.d.key("alt-spc")
        self.m.expect("layout tall")
        panes = tall(4)
        s = self.d.wait(lambda s: read_text(s, LAYOUT_X, TEXT_Y, 6, DIM) == "[tall]"
                        and edge(s, panes[3]) == FOCUS and term_rows(s, panes[3])[1] == "hello, ManiDE",
                        "the tall layout: fetch on the left, the rest stacked")
        check(s.pixel(panes[0][0], 590) == EDGE, "the first pane is the whole height")
        self.d.key("alt-l")
        wider = tall(4, 60)
        self.d.wait(lambda s: edge(s, wider[3]) == FOCUS, "Alt+l widening the first pane")
        self.d.key("alt-spc")
        self.m.expect("layout mono")
        whole = area()
        self.d.wait(lambda s: read_text(s, LAYOUT_X, TEXT_Y, 6, DIM) == "[mono]"
                    and term_rows(s, whole)[1] == "hello, ManiDE" and edge(s, whole) == FOCUS,
                    "the mono layout: the focused terminal alone, as large as the screen")
        self.d.key("alt-spc")
        self.m.expect("layout grid")
        self.d.wait(lambda s: term_rows(s, self.term)[1] == "hello, ManiDE" and edge(s, self.term) == FOCUS,
                    "back to the grid")

    def focus(self):
        fetch, welcome, top, term = grid(4)
        self.d.key("alt-k")
        self.d.wait(lambda s: edge(s, top) == FOCUS and edge(s, term) == EDGE, "Alt+k: the pane before")
        self.d.key("alt-j")
        self.d.wait(lambda s: edge(s, term) == FOCUS, "Alt+j: the next pane")
        # The mouse: a click in a pane focuses it.
        self.click(fetch[0] + 100, fetch[1] + 150)
        self.d.wait(lambda s: edge(s, fetch) == FOCUS and title(s, fetch, FOCUS) == "fetch",
                    "a click focusing a pane")
        self.click(term[0] + 100, term[1] + 150)
        self.d.wait(lambda s: edge(s, term) == FOCUS, "a click focusing the terminal again")
        self.d.type("echo focus\n")
        self.d.wait(lambda s: "focus" in term_rows(s, term), "keys reaching the focused terminal")

    def workspaces(self):
        self.d.key("alt-2")
        self.m.expect("workspace 2")
        s = self.d.wait(lambda s: s.pixel(WS_X + WS_STEP + 1, 3) == ACCENT and s.pixel(200, 200) == DESK,
                        "workspace 2, empty")
        check(read_text(s, WS_X + 3, TEXT_Y, 1, TEXT) == "1", "workspace 1 shows as in use")
        self.d.key("alt-ret")
        self.m.expect('window 5 "Terminal"')
        whole = area()
        self.d.wait(lambda s: term_rows(s, whole)[0] == "manios%", "Alt+Enter: a terminal filling workspace 2")
        self.d.type("echo two\n")
        self.d.wait(lambda s: term_rows(s, whole)[1] == "two", "typing on workspace 2")
        self.d.key("alt-shift-1")
        self.m.expect("window 5 to workspace 1")
        self.d.wait(lambda s: s.pixel(200, 200) == DESK, "Alt+Shift+1 sending it to workspace 1")
        # The bar's numbers pick workspaces too.
        self.click(WS_X + 6, 9)
        self.m.expect("workspace 1")
        five = grid(5)
        self.d.wait(lambda s: term_rows(s, five[4])[1] == "two" and edge(s, five[3]) == FOCUS,
                    "five panes on workspace 1; the focus where it was")
        self.d.key("alt-j")
        self.d.wait(lambda s: edge(s, five[4]) == FOCUS, "Alt+j to the new pane")
        self.d.key("alt-q")
        self.m.expect("window 5 closed")
        self.m.expect("exited (status 0)")
        self.d.wait(lambda s: edge(s, self.term) == FOCUS, "Alt+q closing it; four panes again")

    def prompt(self):
        self.d.key("alt-d")
        self.d.wait(lambda s: read_text(s, FACTS_X, TEXT_Y, 5, ACCENT) == "run:", "Alt+d: the run prompt")
        self.d.type("clo")
        self.d.wait(lambda s: read_text(s, FACTS_X, TEXT_Y, 8, ACCENT) == "run: clo"
                    and read_text(s, FACTS_X + 8 * CELL_W + 8, TEXT_Y, 5, DIM) == "clock",
                    "what Tab would complete")
        self.d.key("tab")
        self.d.wait(lambda s: read_text(s, FACTS_X, TEXT_Y, 11, ACCENT) == "run: clock", "Tab completing")
        self.d.key("ret")
        self.m.expect("started clock (pid")
        self.m.expect('window 6 "Clock"')
        five = grid(5)
        self.d.wait(lambda s: title(s, five[4], FOCUS) == "Clock"
                    and read_text(s, FACTS_X, TEXT_Y, 6, TEXT) == "manios", "the clock in a fifth pane")
        self.d.key("alt-q")
        self.m.expect("window 6 closed")
        self.m.expect("exited (status 0)")
        # Escape leaves the prompt without running anything.
        self.d.key("alt-d")
        self.d.type("x")
        self.d.wait(lambda s: read_text(s, FACTS_X, TEXT_Y, 6, ACCENT) == "run: x", "the prompt again")
        self.d.key("esc")
        self.d.wait(lambda s: read_text(s, FACTS_X, TEXT_Y, 6, TEXT) == "manios", "Escape closing it")

    def menu(self):
        def item_y(i):
            return BAR_H + 2 + sum(20 if label else 7 for label in MENU_ITEMS[:i])

        self.d.key("f1")
        s = self.d.wait(lambda s: read_text(s, 11, item_y(0) + 5, 8, DARK) == "Terminal", "the menu")
        items = [read_text(s, 11, item_y(i) + 5, 12, TEXT) for i, label in enumerate(MENU_ITEMS)
                 if label and i]
        check(items == [label for label in MENU_ITEMS[1:] if label], f"the menu's items: {items}")
        check(s.pixel(3, item_y(0) + 1) == FOCUS, "the first item is highlighted")
        self.d.key("esc")
        self.d.wait(lambda s: read_text(s, 11, item_y(0) + 5, 8, DARK) != "Terminal",
                    "Escape closing the menu")

    def close_box(self):
        welcome = grid(4)[1]
        sx, sy, sw = welcome[0] + 1, welcome[1] + 1, welcome[2] - 2
        self.click(sx + sw - 8, sy + 7)
        self.m.expect("window 2 closed")
        self.m.expect("exited (status 0)")
        fetch, top, term = grid(3)
        check(term == self.term, "the terminal keeps its place")
        self.d.wait(lambda s: s.pixel(fetch[0], 500) in (EDGE, FOCUS), "fetch taking the whole left side")
        self.click(term[0] + 100, term[1] + 150)
        self.d.wait(lambda s: edge(s, term) == FOCUS, "the terminal focused again")

    def wintest(self):
        self.d.type("/boot/test/wintest\n")
        # Its summary also goes to /dev/cons: "all N checks passed" or
        # "F of N checks failed" (the details are in the terminal).
        before = self.m.expect(" checks ", timeout=90)
        verdict = self.m.expect("\r\n")
        check(verdict == "passed" and "wintest: all " in before, f"{before[-60:]} checks {verdict}")

    def scrollback(self):
        term = self.term
        cols, rows = term_size(term)
        # Over two screens of distinct lines, however many programs /bin holds.
        self.d.type("ls /bin | head -n 30; ls /bin | head -n 30; echo last line\n")
        live = self.d.wait(lambda s: term_rows(s, term)[-2:] == ["last line", "manios%"],
                           "two listings of /bin", timeout=30)
        live = term_rows(live, term)
        half = rows // 2
        # PgUp: half a screen back, and a note saying so on the top row.
        self.d.key("pgup")
        note = f" {half} lines back (PgDn) "
        x, y, w, _ = content(term)
        nx = x + w - TERM_MARGIN - len(note) * CELL_W
        s = self.d.wait(lambda s: read_text(s, nx, y + TERM_MARGIN, len(note), TERM_BG) == note.rstrip()
                        and term_rows(s, term)[half:] == live[:rows - half],
                        f"{half} lines back, and a note saying so")
        first = term_rows(s, term)
        check(first[half - 1] and first[half - 1] != live[0], "a line from before the screen shows")
        # Again: the lines that were at the top move down by as many.
        # (Row 0 carries the note, so it is left out.)
        self.d.key("pgup")
        self.d.wait(lambda s: term_rows(s, term)[half + 1:2 * half] == first[1:half],
                    "PgUp again: further back")
        self.d.key("pgdn")
        self.d.key("pgdn")
        self.d.wait(lambda s: term_rows(s, term) == live, "PgDn back to the live screen")
        self.d.key("pgup")
        self.d.wait(lambda s: term_rows(s, term)[half:] == live[:rows - half], "PgUp")
        self.d.type("echo back\n")
        self.d.wait(lambda s: term_rows(s, term)[-2:] == ["back", "manios%"],
                    "typing returning to the screen")

    def resize(self):
        # A smaller pane keeps the lines around the cursor; the rest go
        # into the history. A bigger one leaves the text where it was.
        self.d.key("alt-ret")
        self.m.expect(' "Terminal" ')
        self.d.key("alt-spc")
        self.m.expect("layout tall")
        small = tall(4, 60)[2]  # (Alt+l widened the first pane to 60%)
        rows = term_size(small)[1]
        check(rows < term_size(self.term)[1], "the stacked pane is shorter")
        self.d.wait(lambda s: term_rows(s, small)[rows - 2:] == ["back", "manios%"],
                    f"the terminal in a pane of {rows} rows, its last lines kept")
        for _ in range(2):
            self.d.key("alt-spc")
        self.m.expect("layout grid")
        bigger = grid(4)[2]
        self.d.wait(lambda s: term_rows(s, bigger)[rows - 2:rows] == ["back", "manios%"]
                    and not any(term_rows(s, bigger)[rows:]),
                    "the grid again: the text where it was, the rows below it empty")
        self.d.key("alt-q")
        self.m.expect("exited (status 0)")
        self.d.wait(lambda s: edge(s, self.term) == FOCUS and term_rows(s, self.term)[rows - 1] == "manios%",
                    "the new terminal closed; the old one focused, where it was")

    def nested(self):
        self.d.type("manide\n")
        self.d.wait(lambda s: any(r.startswith("manide: already running") for r in term_rows(s, self.term)),
                    "a second ManiDE refusing to start")

    def exit(self, keys=("alt-shift-q",)):
        for k in keys:
            self.d.key(k)
        self.m.expect("manide: bye")
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
             lambda: run_command(m, "serial", "manide 320 200",
                                 ["manide: the screen must be at least 640x480"], SHELL_PROMPT))
        sc = Scenario(m, tmpdir)
        # Each step builds on the one before, so the first failure ends the run.
        for name, fn in [
            ("ManiDE starts its session: the status bar, four panes", sc.start),
            ("fetch, welcome, top and a terminal tile a 2x2 grid", sc.panes),
            ("keys reach the terminal; /dev/wsys from its shell", sc.terminal),
            ("Alt+Space: tall and mono layouts, and back to the grid", sc.layouts),
            ("Alt+j/k and the mouse move the focus", sc.focus),
            ("workspaces: Alt+2, Alt+Enter, Alt+Shift+1, the bar, Alt+q", sc.workspaces),
            ("Alt+d: the run prompt completes and starts the clock", sc.prompt),
            ("F1 opens the menu; Escape closes it", sc.menu),
            ("a pane's close mark closes it; the others fill the room", sc.close_box),
            ("wintest: the window system's files, from inside ManiDE", sc.wintest),
            ("the terminal scrolls back with PgUp and PgDn", sc.scrollback),
            ("the terminal follows its pane's size", sc.resize),
            ("ManiDE inside ManiDE is refused", sc.nested),
            ("Alt+Shift+Q exits, restoring the text console", sc.exit),
        ]:
            if not step(name, fn):
                break
        else:
            step("the keyboard is the console's again",
                 lambda: run_command(m, "keyboard", "echo back", ["\r\nback\r\n"], SHELL_PROMPT))
            step("ManiDE's namespace was its own: no /dev/wsys here",
                 lambda: run_command(m, "serial", "ls /dev", ["cons", "!wsys"], SHELL_PROMPT))
            step("`desktop -n 640 480` starts ManiDE without a session; the menu exits",
                 lambda: (m.type_serial("desktop -n 640 480\r"),
                          m.expect("640x480, serving /dev/wsys"),
                          sc.d.wait(lambda s: s.w == 640 and s.pixel(320, 240) != DESK
                                    and read_text(s, 6, TEXT_Y, 6, ACCENT) == "ManiDE", "ManiDE at 640x480"),
                          sc.exit(("f1", "up", "ret"))))
    finally:
        m.close()

    # Without Bochs VBE (a Cirrus card), ManiDE says so and the console
    # carries on.
    m = Machine(kernel, ["-vga", "cirrus"])
    try:
        m.expect(SHELL_PROMPT)
        step("without a linear framebuffer ManiDE explains and exits",
             lambda: run_command(m, "serial", "manide",
                                 ["manide: cannot set a 800x600 mode", "Bochs VBE display is needed"],
                                 SHELL_PROMPT))
        step("... and the console still works",
             lambda: run_command(m, "serial", "echo still here", ["\r\nstill here\r\n"], SHELL_PROMPT))
    finally:
        m.close()

    print("desktop test: " + ("all passed" if not failures else f"{failures} failed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
