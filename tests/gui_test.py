#!/usr/bin/env python3
"""GUI test (M23): ManiOS's graphical programs and their toolkit, libui,
driven like a user would drive them -- keys through QEMU's `sendkey`, the
pointer through `mouse_move` and `mouse_button` -- and checked on the
screen itself (`screendump`) and on the serial line.

First without a desktop: libui's own conformance test (/boot/test/uitest,
which feeds a windowless ui events by hand), the calculator's arithmetic
(`calc -t`), and that a GUI program started with no desktop says so. Then
on ManiDE, one program at a time: the calculator (keys, buttons, errors,
following its pane's size), the widget gallery (every kind of widget),
the file browser opening the viewer, and the paint program.

Where a widget is on screen comes from the same arithmetic as
desktop/libui/layout.c (`box` and `cell` below), as desktop_test.py's
`grid` mirrors tile.c; text is read back off the screen with the
glyphs of desktop/libgfx/font.txt.

Usage: tests/gui_test.py [path-to-kernel-elf]
"""
import os
import sys
import tempfile
import time

from console_test import SHELL_PROMPT, Machine, TestFailure, run_command
import desktop_test as dt
from desktop_test import (ACCENT, BAR_H, CELL_H, CELL_W, DIM, FOCUS, TEXT, area, check, content,
                          grid, read_text, title)

# desktop/libui/ui.c: the toolkit's colours.
BG = (0x0F, 0x13, 0x19)
PANEL = (0x1A, 0x20, 0x2A)
FIELD = (0x0A, 0x0D, 0x12)
EDGE = (0x2A, 0x31, 0x3C)
SELECT = (0x1F, 0x3A, 0x45)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

MENU = ["Terminal", "System info", "Processes", "ManiDOS", "Files", "Calculator", "Paint",
        "Welcome", "Clock", "About ManiOS", None, "Exit ManiDE"]


# --- libui's arithmetic (desktop/libui/layout.c) ---

def box(rect, horizontal, pad, spacing, items):
    """The rectangles of a box's children. `items` are (want_w, want_h,
    expand); there must be room for all of them at their natural size."""
    x, y, w, h = rect
    ix, iy, iw, ih = x + pad, y + pad, w - 2 * pad, h - 2 * pad
    room = (iw if horizontal else ih) - spacing * (len(items) - 1)
    sizes = [it[0] if horizontal else it[1] for it in items]
    extra = room - sum(sizes)
    check(extra >= 0, f"the test's box has room for its children ({extra})")
    left, weights = extra, sum(it[2] for it in items)
    for k, it in enumerate(items):
        if it[2]:
            share = left * it[2] // weights
            sizes[k] += share
            left -= share
            weights -= it[2]
    rects, at = [], ix if horizontal else iy
    for size in sizes:
        rects.append((at, iy, size, ih) if horizontal else (ix, at, iw, size))
        at += size + spacing
    return rects


def cell(start, length, n, spacing, i):
    room = length - (n - 1) * spacing
    return start + room * i // n + i * spacing, room * (i + 1) // n - room * i // n


def cells(rect, cols, count, spacing):
    """A grid's cells (x, y, w, h), row-major."""
    x, y, w, h = rect
    rows = (count + cols - 1) // cols
    out = []
    for i in range(count):
        cx, cw = cell(x, w, cols, spacing, i % cols)
        cy, ch = cell(y, h, rows, spacing, i // cols)
        out.append((cx, cy, cw, ch))
    return out


def centre(r):
    return r[0] + r[2] // 2, r[1] + r[3] // 2


def text_h(lines=1, scale=1):
    return lines * CELL_H * scale


def button_size(label, scale=1):
    return len(label) * CELL_W * scale + 16, text_h(1, scale) + 8


class Gui:
    """ManiDE with `manide -n`, and helpers to start and look at programs."""

    def __init__(self, m, tmpdir):
        self.m = m
        self.sc = dt.Scenario(m, tmpdir)
        self.d = self.sc.d
        self.windows = 0  # numbered by ManiDE as they are made

    def start_desktop(self):
        self.m.type_serial("manide -n\r")
        self.m.expect("serving /dev/wsys")
        self.d.wait(lambda s: s.w == 800 and read_text(s, 6, dt.TEXT_Y, 6, ACCENT) == "ManiDE",
                    "the status bar")
        self.park()

    def screen(self):
        return self.d.screen()

    def start(self, command, name, panes=None):
        """Runs `command` at the run prompt (Alt+d) and waits for its window."""
        self.d.key("alt-d")
        self.d.wait(lambda s: read_text(s, dt.FACTS_X, dt.TEXT_Y, 5, ACCENT) == "run:", "the run prompt")
        self.d.type(command + "\n")
        self.windows += 1
        self.m.expect(f'window {self.windows} "{name}"')

    def menu(self, label):
        """Picks `label` from the F1 menu."""
        self.d.key("f1")
        self.d.wait(lambda s: read_text(s, 11, dt.BAR_H + 2 + 5, 8, dt.DARK) == "Terminal", "the menu")
        for _ in range(MENU.index(label)):
            self.d.key("down")
        self.d.key("ret")
        self.windows += 1
        self.m.expect(f'window {self.windows} "{label}"')

    def close(self):
        self.d.key("alt-q")
        self.m.expect("exited (status 0)")

    def point(self, x, y):
        """Moves the pointer to (x, y), in steps of at most 200 pixels: QEMU
        hands the guest a long move as a burst of PS/2 packets in an instant,
        and part of a long one is lost on the way."""
        px, py = self.sc.pointer
        while (px, py) != (x, y):
            dx, dy = max(-200, min(200, x - px)), max(-200, min(200, y - py))
            self.d.move(dx, dy)
            px, py = px + dx, py + dy
        self.sc.pointer = (x, y)

    def park(self):
        """The pointer to the corner, out of the way of what is read."""
        self.point(799, 599)

    def click(self, x, y):
        self.point(x, y)
        self.d.button(1)
        self.d.button(0)
        self.park()

    def double_click(self, x, y):
        """Two clicks in one go: well inside a double click's time."""
        self.point(x, y)
        self.m.monitor.sendall(b"mouse_button 1\nmouse_button 0\nmouse_button 1\nmouse_button 0\n")
        time.sleep(0.3)
        self.park()

    def drag(self, points, button=1):
        """Presses `button` at the first point, moves through the others, releases."""
        self.point(*points[0])
        self.d.button(button)
        for p in points[1:]:
            self.point(*p)
        self.d.button(0)
        self.park()


def is_win(win, x, y):
    """Window coordinates to screen coordinates, for a window in `pane`."""
    return win[0] + x, win[1] + y


# --- without a desktop ---

def headless(m):
    run_command(m, "serial", "/boot/test/uitest", ["uitest: all ", " checks passed", "!FAIL"], SHELL_PROMPT)
    run_command(m, "serial", "calc -t", ["calc: all ", " checks passed", "!FAIL"], SHELL_PROMPT)
    for program in ["calc", "files", "view", "paint", "widgets", "greet"]:
        run_command(m, "serial", program, [f"{program}: no window"], SHELL_PROMPT)
    run_command(m, "serial", "calc x", ["usage: calc"], SHELL_PROMPT)


# --- the calculator ---

class Calculator:
    # desktop/apps/calc.c: a vertical box (padding 6, spacing 6) of the pending
    # line, the display (scale 2, at least 36 high) and a grid of 4 x 5 buttons.
    LABELS = ["C", "Del", "%", "/", "7", "8", "9", "*", "4", "5", "6", "-",
              "1", "2", "3", "+", "+/-", "0", ".", "="]

    def __init__(self, g, pane):
        self.g, self.pane = g, pane
        self.win = content(pane)
        _, _, w, h = self.win
        self.pending, self.display, self.grid = box(
            (0, 0, w, h), False, 6, 6, [(0, text_h(), 0), (0, 36, 0), (0, 0, 1)])
        self.buttons = cells(self.grid, 4, 20, 4)

    def button(self, label):
        bx, by = centre(self.buttons[self.LABELS.index(label)])
        return is_win(self.win, bx, by)

    def shown(self, s, color=TEXT):
        """The display's text: right-aligned, at scale 2."""
        dx, dy, dw, _ = self.display
        cells_n = (dw - 12) // (CELL_W * 2)
        x0 = dx + 6 + (dw - 12) % (CELL_W * 2)
        ox, oy = self.win[0], self.win[1]
        return read_text(s, ox + x0, oy + dy + 7, cells_n, color, scale=2).strip()

    def pending_text(self, s):
        px, py, pw, _ = self.pending
        n = (pw) // CELL_W
        x0 = px + pw % CELL_W
        return read_text(s, self.win[0] + x0, self.win[1] + py, n, DIM).strip()

    def wait_shows(self, text, color=TEXT, what=None):
        return self.g.d.wait(lambda s: self.shown(s, color) == text, what or f"the display showing {text!r}")

    def press(self, *labels):
        for label in labels:
            self.g.click(*self.button(label))

    def run(self):
        g = self.g
        self.wait_shows("0", what="the calculator, showing 0")
        s = g.screen()
        # The look: operators cyan, = amber, the buttons the panel colour.
        bx, by, bw, bh = self.buttons[self.LABELS.index("5")]
        check(s.pixel(self.win[0] + bx + 3, self.win[1] + by + 3) == PANEL, "a button's body is the panel colour")
        check(s.pixel(self.win[0] + self.display[0] + 2, self.win[1] + self.display[1] + 2) == FIELD,
              "the display is a sunken field")
        g.d.type("12*34")
        self.wait_shows("34")
        check(self.pending_text(g.screen()) == "12 *", "the pending line shows 12 *")
        g.d.type("=")
        self.wait_shows("408", what="12 * 34 = 408 typed on the keyboard")
        check(self.pending_text(g.screen()) == "", "nothing pending after =")
        g.d.key("esc")
        self.wait_shows("0", what="Escape clearing")
        g.d.type("1/3=")
        self.wait_shows("0.333333", what="1 / 3 to six decimals")
        g.d.type("c1/0=")
        self.wait_shows("Divide by 0", ACCENT, "an error, in the accent colour")
        g.d.type("5")
        self.wait_shows("5", what="a digit clearing the error and starting a number")
        g.d.key("backspace")
        self.wait_shows("0", what="Backspace")
        # The mouse: 7 * 6 =, then + 8 +/- =.
        g.d.type("c")
        self.press("7", "*", "6", "=")
        self.wait_shows("42", what="7 * 6 = 42 with the mouse")
        self.press("+", "8", "+/-", "=")
        self.wait_shows("34", what="42 + (-8) = 34 with the mouse")
        self.press("C", "2", ".", "5", "*", "4", "=")
        self.wait_shows("10", what="2.5 * 4 = 10")
        self.press("%")
        self.wait_shows("0.1", what="the % button")
        self.press("Del")
        self.wait_shows("0.", what="the Del button taking the 1 off 0.1")
        self.press("Del")
        self.wait_shows("0", what="the Del button taking the point off")
        g.d.type("c")

    def resize(self):
        """A second window halves the calculator's pane; its buttons follow."""
        g = self.g
        g.d.key("alt-ret")
        g.windows += 1
        g.m.expect(f'window {g.windows} "Terminal"')
        g.d.wait(lambda s: dt.edge(s, grid(2)[1]) == FOCUS, "the terminal in the second pane")
        calc = Calculator(g, grid(2)[0])
        calc.wait_shows("0", what="the calculator redrawn in half the width")
        s = g.screen()
        bx, by, bw, bh = calc.buttons[Calculator.LABELS.index("=")]
        check(bw < self.buttons[0][2] and calc.win[2] < self.win[2], "the pane is narrower than it was")
        check(s.pixel(calc.win[0] + bx + 2, calc.win[1] + by + 2) == PANEL
              and s.pixel(calc.win[0] + bx + bw + 2, calc.win[1] + by) in (BG, FIELD),
              "the buttons follow the pane's size")
        g.click(*calc.button("9"))
        g.click(*calc.button("+"))
        g.click(*calc.button("1"))
        g.click(*calc.button("="))
        calc.wait_shows("10", what="9 + 1 = 10 in the narrower pane")
        g.d.key("alt-q")  # (the focused window: the calculator, which was clicked)
        g.m.expect("exited (status 0)")
        g.d.key("alt-q")
        g.m.expect("exited (status 0)")


# --- the widget gallery ---

class Gallery:
    # desktop/apps/widgets.c: title (scale 2), two columns, a help line.
    def __init__(self, g, pane):
        self.g = g
        self.win = content(pane)
        w, h = self.win[2], self.win[3]
        title_r, cols, help_r = box((0, 0, w, h), False, 6, 6, [(0, text_h(1, 2), 0), (0, 0, 1), (0, text_h(), 0)])
        left_want = max(button_size("Click me")[0], 15 * CELL_W, 11 + 6 + 20 * CELL_W, 100, 20 * CELL_W + 8, 6 * CELL_W)
        right_want = max(120, 20 * CELL_W, 160, 22 * CELL_W)
        self.left, self.right = box(cols, True, 0, 10, [(left_want, 0, 1), (right_want, 0, 1)])
        self.help_r = help_r
        lw, lh = self.left[2], self.left[3]
        (self.button, self.clicked, self.sep1, self.check, self.bar, self.sep2, self.entry,
         self.echo, _) = box(self.left, False, 0, 6, [
             (0, 19, 0), (0, text_h(), 0), (0, 3, 0), (0, 15, 0), (0, 15, 0), (0, 3, 0),
             (0, 19, 0), (0, text_h(), 0), (0, 0, 1)])
        self.list, self.picked, self.text, self.lines = box(self.right, False, 0, 6, [
            (0, 3 * 13 + 2, 2), (0, text_h(), 0), (0, 4 * CELL_H + 4, 2), (0, text_h(), 0)])

    def at(self, r, dx=0, dy=0):
        return self.win[0] + r[0] + dx, self.win[1] + r[1] + dy

    def label(self, s, r, text_len, color=DIM):
        x, y = self.at(r)
        return read_text(s, x, y, text_len, color)

    def row(self, i):
        """Where list row i is (its top-left, in screen coordinates)."""
        return self.at(self.list, 1, 1 + i * 13)

    def run(self):
        g = self.g
        s = g.d.wait(lambda s: self.label(s, self.clicked, 15) == "clicked 0 times"
                     and self.label(s, self.picked, 11) == "selected: -", "the widget gallery")
        x, y = self.at(self.button)
        check(s.pixel(x, y + 5) == FOCUS, "the first button has the focus: its edge is cyan")
        check(read_text(s, self.win[0] + 6, self.win[1] + 6, 14, ACCENT, scale=2) == "libui widgets",
              "the title, at scale 2, in the accent colour")
        # Buttons: click three times, then Space on the focused button.
        for _ in range(3):
            g.click(*centre_on(self.win, self.button))
        g.d.wait(lambda s: self.label(s, self.clicked, 17) == "clicked 3 times", "three clicks counted")
        g.d.key("spc")
        g.d.wait(lambda s: self.label(s, self.clicked, 17) == "clicked 4 times", "Space pressing the focused button")
        # Tab to the check box; Space checks it; the progress bar runs.
        g.d.key("tab")
        cx, cy = self.at(self.check)
        g.d.wait(lambda s: s.pixel(cx, cy + 7) == FOCUS, "Tab moving the focus to the check box")
        g.d.key("spc")
        bx, by = self.at(self.bar)
        g.d.wait(lambda s: s.pixel(bx + 3, by + 7) == ACCENT, "the progress bar filling once the box is checked")
        g.d.key("spc")
        g.d.wait(lambda s: s.pixel(bx + 3, by + 7) in (FIELD, ACCENT), "the box unchecked")
        # The entry: a click focuses it; typing echoes; Enter shows a message.
        g.click(*centre_on(self.win, self.entry))
        g.d.type("hello world")
        g.d.wait(lambda s: self.label(s, self.echo, 17) == "echo: hello world", "typing echoed by the label")
        g.d.key("backspace")
        g.d.wait(lambda s: self.label(s, self.echo, 16) == "echo: hello worl", "Backspace in the entry")
        g.d.key("ret")
        ex, ey = self.win[0] + 2, self.win[1] + 2
        s = g.d.wait(lambda s: s.pixel(ex, ey) not in (BG, FIELD), "Enter showing a message that dims the window")
        g.d.key("ret")
        g.d.wait(lambda s: s.pixel(ex, ey) == BG, "Enter closing the message")
        # The list: a click selects, the keys move, a double click activates.
        rx, ry = self.row(3)
        g.click(rx + 40, ry + 6)
        g.d.wait(lambda s: self.label(s, self.picked, 16) == "selected: Damson", "a click selecting a row")
        s = g.screen()
        check(s.pixel(rx + 100, ry + 6) == SELECT, "the selected row has the selection colour")
        g.d.key("down")
        g.d.wait(lambda s: self.label(s, self.picked, 20) == "selected: Elderberry", "Down selecting the next row")
        g.double_click(rx + 40, self.row(5)[1] + 6)
        g.d.wait(lambda s: s.pixel(ex, ey) not in (BG, FIELD), "a double click activating a row: a message")
        g.d.key("esc")
        g.d.wait(lambda s: s.pixel(ex, ey) == BG, "Escape closing it")
        picked = self.label(g.screen(), self.picked, 20)
        check(picked == "selected: Fig", f"the double click selected the row too: {picked!r}")
        # The text area: a click puts the caret on the second line (the
        # status line follows edits, not moves); End and typing, Enter, more.
        tx, ty = self.at(self.text)
        g.click(tx + 60, ty + 40)
        g.d.key("end")
        g.d.type("!")
        g.d.wait(lambda s: self.label(s, self.lines, 22) == "line 2, column 10 of 2",
                 "a click, End and a character: the caret on line 2, after 'edit me.!'")
        g.d.key("ret")
        g.d.type("more")
        g.d.wait(lambda s: self.label(s, self.lines, 21) == "line 3, column 5 of 3", "Enter and typing: a third line")
        s = g.screen()
        first = read_text(s, tx + 4, ty + 2, 12, TEXT)
        third = read_text(s, tx + 4, ty + 2 + 2 * CELL_H, 4, TEXT)
        check(first == "A text area:" and third == "more", f"the text shown: {first!r}, {third!r}")
        g.close()


def centre_on(win, rect):
    cx, cy = centre(rect)
    return win[0] + cx, win[1] + cy


# --- the file browser and the viewer ---

class Files:
    # desktop/apps/files.c: the location entry, the list, the status line.
    def __init__(self, g, pane):
        self.g = g
        self.win = content(pane)
        w, h = self.win[2], self.win[3]
        self.where, self.list, self.status = box((0, 0, w, h), False, 6, 6,
                                                 [(0, 19, 0), (0, 0, 1), (0, text_h(), 0)])

    def at(self, r, dx=0, dy=0):
        return self.win[0] + r[0] + dx, self.win[1] + r[1] + dy

    def rows(self, s, n=8, cells_n=24):
        x, y = self.at(self.list, 1 + 6, 1)
        out = []
        for i in range(n):
            # folders are cyan, files light, the ".." row cyan too
            texts = [read_text(s, x, y + i * 13 + 1, cells_n, c) for c in (FOCUS, TEXT, DIM)]
            out.append(next((t for t in texts if t), ""))
        return out

    def path(self, s):
        x, y = self.at(self.where, 4, (19 - CELL_H) // 2)
        return read_text(s, x, y, 30, TEXT)

    def status_text(self, s, n=30):
        x, y = self.at(self.status)
        return read_text(s, x, y, n, DIM)

    def run(self):
        g = self.g
        # (A screendump can catch a frame half drawn, top to bottom: wait for the last row.)
        s = g.d.wait(lambda s: self.path(s) == "/" and self.rows(s)[0] == "bin/"
                     and self.status_text(s) == "bin: folder", "the root directory listed")
        rows = self.rows(s)
        check(rows[:5] == ["bin/", "boot/", "dev/", "mnt/", "n/"], f"the root's folders: {rows[:5]}")
        check(self.status_text(s) == "bin: folder",
              f"the status line describes the selected row: {self.status_text(s)!r}")
        rx, ry = self.at(self.list, 1, 1)
        check(s.pixel(rx + 100, ry + 6) == SELECT, "the first row is selected")
        # Down, Enter: into a folder, with .. at the top.
        g.d.key("down")
        g.d.wait(lambda s: self.status_text(s) == "boot: folder", "Down selecting the next row")
        g.d.key("ret")
        s = g.d.wait(lambda s: self.path(s) == "/boot" and self.rows(s)[0] == ".."
                     and self.status_text(s).endswith(" items"), "Enter opening the folder")
        rows = self.rows(s)
        check(rows[:5] == ["..", "bin/", "etc/", "test/", "autoexec.bat"], f"/boot's contents under ..: {rows}")
        g.d.key("backspace")
        s = g.d.wait(lambda s: self.path(s) == "/" and self.status_text(s) == "boot: folder",
                     "Backspace going up, the folder we left selected")
        # The location box: Tab to it, type a path, Enter.
        g.d.key("tab")
        g.d.wait(lambda s: s.pixel(*self.at(self.where, 0, 6)) == FOCUS, "Tab moving to the location box")
        g.d.type("/boot/etc\n")
        s = g.d.wait(lambda s: self.path(s) == "/boot/etc" and "motd" in self.rows(s)
                     and self.status_text(s).endswith(" items"), "a typed path opened")
        rows = self.rows(s)
        check(rows[0] == ".." and "motd" in rows, f"/boot/etc lists motd: {rows}")
        # Back to the list; select motd; Enter opens the viewer.
        index = rows.index("motd")
        for _ in range(index):
            g.d.key("down")
        g.d.wait(lambda s: self.status_text(s).startswith("motd: "), "motd selected, with its size")
        check(self.status_text(g.screen()).split(": ")[1].endswith(" B"), "a file's size in bytes")
        g.d.key("ret")
        g.windows += 1
        g.m.expect(f'window {g.windows} "View"')
        files_pane, view_pane = grid(2)
        s = g.d.wait(lambda s: title(s, view_pane, FOCUS, 20) == "View /boot/etc/motd", "the viewer's title")
        v = View(g, view_pane)
        v.run()
        # q closed the viewer (Files started it, so ManiDE logs its window, not its exit);
        # Files fills the screen again.
        g.m.expect(f"window {g.windows} closed")
        g.d.wait(lambda s: dt.edge(s, area()) in (FOCUS, EDGE) and self.path(s) == "/boot/etc", "Files alone again")
        # A message for what cannot be opened.
        g.close()


class View:
    def __init__(self, g, pane):
        self.g = g
        self.win = content(pane)

    def run(self):
        g = self.g
        w, h = self.win[2], self.win[3]
        bar, text, status = box((0, 0, w, h), False, 6, 6, [(0, 19, 0), (0, 0, 1), (0, text_h(), 0)])
        ox, oy = self.win[0], self.win[1]
        s = g.d.wait(lambda s: read_text(s, ox + text[0] + 1 + 3, oy + text[1] + 2, 5, DIM).strip() == "1",
                     "the line number in the margin")
        body = read_text(s, ox + text[0] + 1 + 3 + 5 * CELL_W + 4, oy + text[1] + 2, 18, TEXT)
        check(body == "Welcome to ManiOS.", f"the file's text: {body!r}")
        # (the entry follows the label "File" and the 6 pixels between)
        name = read_text(s, ox + bar[0] + 4 * CELL_W + 6 + 4, oy + bar[1] + 4, 14, TEXT)
        check(name == "/boot/etc/motd", f"the file's name in the box: {name!r}")
        g.d.key("q")


# --- greet ---

class Greet:
    # desktop/apps/greet.c, the guide's first example: a label and a button.
    def __init__(self, g, pane):
        self.g = g
        self.win = content(pane)
        w, h = self.win[2], self.win[3]
        self.label, self.button = box((0, 0, w, h), False, 6, 6,
                                      [(0, text_h(), 0), (button_size("Click me")[0], 19, 0)])

    def run(self):
        g = self.g
        lx, ly = self.win[0] + self.label[0], self.win[1] + self.label[1]
        g.d.wait(lambda s: read_text(s, lx, ly, 13, TEXT) == "Hello, ManiOS", "the greeting")
        g.click(*centre_on(self.win, self.button))
        g.d.wait(lambda s: read_text(s, lx, ly, 16, TEXT) == "clicked 1 time", "one click, singular")
        g.click(*centre_on(self.win, self.button))
        g.d.wait(lambda s: read_text(s, lx, ly, 17, TEXT) == "clicked 2 times", "two clicks, plural")
        g.close()


# --- paint ---

class Paint:
    # desktop/apps/paint.c: a row of the tools and the page.
    def __init__(self, g, pane):
        self.g = g
        self.win = content(pane)
        w, h = self.win[2], self.win[3]
        (self.row,) = box((0, 0, w, h), False, 6, 6, [(0, 0, 1)])
        tools_w = max(2 * 24 - 4, 7 * CELL_W, 2 * 22 + 4, button_size("Clear")[0])
        self.tools, self.page = box(self.row, True, 0, 8, [(tools_w, 0, 0), (120, 120, 1)])
        self.palette, self.brush, self.sizes, self.clear, _ = box(self.tools, False, 0, 6, [
            (44, 188, 0), (0, text_h(), 0), (0, 19, 0), (0, 19, 0), (0, 0, 1)])

    def at(self, r, dx=0, dy=0):
        return self.win[0] + r[0] + dx, self.win[1] + r[1] + dy

    def swatch(self, i):
        return self.at(self.palette, i % 2 * 24 + 10, i // 2 * 24 + 10)

    def page_at(self, dx, dy):
        return self.at(self.page, dx, dy)

    def run(self):
        g = self.g
        s = g.d.wait(lambda s: s.pixel(*self.page_at(20, 20)) == WHITE, "a white page")
        check(s.pixel(*self.swatch(0)) == BLACK and s.pixel(*self.swatch(4)) == (0xCC, 0x22, 0x22),
              "the palette's swatches")
        px, py = self.at(self.palette)
        check(s.pixel(px, py + 10) == FOCUS, "black, the first colour, is outlined as the chosen one")
        brush = read_text(s, *self.at(self.brush, (self.brush[2] - 7 * CELL_W) // 2), 7, TEXT)
        check(brush == "Brush 4", f"the brush size shown: {brush!r}")
        # A stroke with the left button, in black.
        g.drag([self.page_at(30, 40), self.page_at(130, 40)])
        g.d.wait(lambda s: s.pixel(*self.page_at(80, 40)) == BLACK, "a black line along the drag")
        s = g.screen()
        check(s.pixel(*self.page_at(80, 60)) == WHITE and s.pixel(*self.page_at(200, 40)) == WHITE,
              "...and only along it")
        check(s.pixel(*self.page_at(80, 42)) == BLACK and s.pixel(*self.page_at(80, 43)) == WHITE
              and s.pixel(*self.page_at(80, 37)) == WHITE, "a brush of 4 is 5 pixels thick")
        # Pick red; draw across.
        g.click(*self.swatch(4))
        sx, sy = self.at(self.palette, 0, 2 * 24 + 10)  # swatch 4: left column, third row
        g.d.wait(lambda s: s.pixel(sx, sy) == FOCUS and s.pixel(px, py + 10) != FOCUS,
                 "the red swatch outlined as chosen, black no longer")
        g.drag([self.page_at(60, 20), self.page_at(60, 80)])
        g.d.wait(lambda s: s.pixel(*self.page_at(60, 60)) == (0xCC, 0x22, 0x22), "a red line")
        check(g.screen().pixel(*self.page_at(60, 40)) == (0xCC, 0x22, 0x22),
              "the red line is over the black one where they cross")
        # The right button erases.
        g.drag([self.page_at(20, 60), self.page_at(100, 60)], button=2)
        g.d.wait(lambda s: s.pixel(*self.page_at(60, 60)) == WHITE, "the right button erasing")
        check(g.screen().pixel(*self.page_at(80, 40)) == BLACK, "...only where it went")
        # The brush: + and ]; a bigger brush draws a thicker line.
        bigger = (self.sizes[0] + 22 + 4, self.sizes[1], 22, self.sizes[3])  # the second of two buttons
        g.click(*centre_on(self.win, bigger))
        shown = lambda s: read_text(s, *self.at(self.brush, (self.brush[2] - 7 * CELL_W) // 2), 7, TEXT)
        g.d.wait(lambda s: shown(s) == "Brush 6", "the + button growing the brush from 4 to 6")
        g.d.key("bracket_right")
        g.d.wait(lambda s: shown(s) == "Brush 8", "] growing it to 8")
        g.drag([self.page_at(150, 100), self.page_at(250, 100)])
        s = g.d.wait(lambda s: s.pixel(*self.page_at(200, 100)) == (0xCC, 0x22, 0x22), "the thicker line")
        check(s.pixel(*self.page_at(200, 104)) == (0xCC, 0x22, 0x22) and s.pixel(*self.page_at(200, 106)) == WHITE,
              "a brush of 8 is 9 pixels thick")
        # Clear wipes the page.
        g.click(*centre_on(self.win, self.clear))
        g.d.wait(lambda s: s.pixel(*self.page_at(80, 40)) == WHITE and s.pixel(*self.page_at(200, 100)) == WHITE,
                 "Clear wiping the page")
        g.close()


def main():
    args = sys.argv[1:]
    only = None
    if "--only" in args:  # tests/gui_test.py KERNEL --only gallery: just the steps naming it
        only = args[args.index("--only") + 1]
        del args[args.index("--only"):args.index("--only") + 2]
    kernel = args[0] if args else "build/manios-zkt.elf"
    tmpdir = tempfile.mkdtemp(prefix="zkt-gui-")
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
        if not only:
            step("libui's conformance test, calc's arithmetic, and no desktop: no window",
                 lambda: headless(m))
        g = Gui(m, tmpdir)
        pane = area()

        def calculator():
            g.menu("Calculator")
            calc = Calculator(g, pane)
            calc.run()
            calc.resize()

        steps = [
            ("the F1 menu starts the calculator; keys and buttons calculate; its buttons follow its pane",
             calculator),
            ("greet, the guide's first example: a label and a button",
             lambda: (g.start("greet", "Greet"), Greet(g, pane).run())),
            ("the widget gallery: buttons, check box, entry, list, text, messages",
             lambda: (g.start("widgets", "Widgets"), Gallery(g, pane).run())),
            ("the file browser lists, opens folders and files; the viewer shows a file",
             lambda: (g.menu("Files"), Files(g, pane).run())),
            ("paint: strokes, colours, the eraser, the brush, Clear",
             lambda: (g.menu("Paint"), Paint(g, pane).run())),
        ]
        if step("ManiDE starts", g.start_desktop):
            for name, fn in steps:
                if only and only not in name:
                    continue
                if not step(name, fn):
                    break
            else:
                if not only:
                    step("Alt+Shift+Q exits, restoring the text console", lambda: (
                        g.d.key("alt-shift-q"), m.expect("manide: bye"), m.expect(SHELL_PROMPT)))
    finally:
        m.close()

    print("gui test: " + ("all passed" if not failures else f"{failures} failed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
