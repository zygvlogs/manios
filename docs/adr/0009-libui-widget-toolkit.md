# ADR-0009: libui, a widget toolkit in userland, over libwin and libgfx

**Status:** Accepted; implemented at M23 ([guide](../gui.md), [notes](../milestones/M23-gui-software.md))
**Date:** 2026-10-06

## Context
ManiOS has had graphical programs since M12, but each one drew its own
pixels. `welcome`, `clock` and the terminal call `win_open`, fill a
canvas with libgfx, and read raw key and pointer events; none shares code
with the others beyond the drawing library, and none has a button, a text
field or a list. Writing the next program meant writing its layout, its
focus, its mouse handling and its look from the beginning, so there were
no programs to use besides the demonstrations: the desktop could show
windows but had nothing of its own to put in them. The request was that
ManiOS officially support graphical software: that there be a toolkit
to write it with, programs written with it, and documentation that says
how.

The constraints are the desktop's (DESIGN.md §1): an i386 with no
floating-point unit and tens of megabytes of memory, integer arithmetic
only, no GPU. The window system is a file server, `/dev/wsys` (ADR-0004),
and ManiDE tiles its windows and chooses their size (ADR-0007), so a
toolkit cannot assume a window keeps the size it asked for. ManiOS
writes files only on a FAT volume (M22, 0.23.0); the root, the boot
archive, CDs and ZRP mounts are read-only, so what a program can save is
partly the kernel's to decide, not the toolkit's.

## Decision
**libui is a retained-mode widget toolkit in userland, a library linked
into each graphical program, built on libwin (windows and events) and
libgfx (drawing).** A program builds a tree of widgets (boxes, grids,
labels, buttons, check boxes, entries, lists, text areas, progress bars,
and widgets of its own), gives callbacks for what the user does, and runs
`ui_run`. libui lays the tree out in the size ManiDE gives, draws it in
ManiDE's palette, and turns keys and the pointer into calls. The window
system and the kernel are unchanged: libui speaks only the files libwin
already used. A `ui` can also exist with no window (`ui_new`), drawing
on a canvas of its own and fed events by hand, which is how the toolkit is
tested.

Programs are built from `desktop/apps/*.c` into `/bin` as before. The
programs that ship with it are a file browser (`files`), a viewer
(`view`), a calculator (`calc`), a sketchpad (`paint`), a gallery of every
widget (`widgets`) and the smallest example (`greet`); the guide is
[docs/gui.md](../gui.md).

## Alternatives considered
- **Leave each program to draw for itself.** That is what M12 and M18
  did, and the reason there was nothing to run. A toolkit pays for itself
  with the second program, and a program's look must stay consistent with
  the desktop's: the palette and the focus rules are written once.
- **Port an existing toolkit** (GTK, Qt, Tk, X11 clients). They assume
  floating point, POSIX in a depth ManiOS's libc doesn't have, a window
  system speaking X or Wayland, and megabytes of library; none would run on
  the target machines, and ManiOS's own window system would have to
  imitate theirs. Not a continuation of ManiOS's design, which is small,
  integer-only and made from files.
- **Widgets in the window system**, with ManiDE drawing buttons and lists
  and programs sending descriptions of them, as a web page does. It makes every widget a protocol feature, puts
  every program's bugs inside the one process that owns the screen (a bad
  list would take the desktop down), and ties widgets to ManiDE's
  release. Toolkit code in the program is checked, replaced and
  crashed independently, and works the same on a CPU server whose window
  is drawn on another machine (M13).
- **Immediate-mode drawing** (draw the whole interface from the program
  every frame). Simple to write, but every redraw is the whole window
  through the kernel's pipe to ManiDE: libui keeps what changed and
  sends only that, which matters at 4 bytes a pixel on an i386.
- **A declarative layout language** (a text file describing a window).
  Another parser and another format to specify, for programs whose
  windows are a dozen widgets; boxes and grids in C are enough and can be
  checked by the compiler.
- **Absolute positions.** Not with tiling: ManiDE changes a window's size
  whenever another opens or closes, and a program can't predict its
  pane. Boxes that give widgets their natural size, share the rest by
  weight and shrink when short follow the size they are given.

## Consequences
- **A GUI program is a short C file.** `greet` is 34 lines; the file
  browser, with navigation, sizes and a location box, is under 300. A new program
  reuses the focus, the keyboard conventions and the look.
- **libui is the place to fix and extend the look.** Every widget
  follows ManiDE's palette through one table, `ui_theme`; a change to the
  desktop's colours is one edit there (and DESIGN.md §6).
- **The toolkit has a conformance test that needs no desktop**
  (`userland/test/uitest.c`, 113 checks run from the shell), and the
  programs have one that does (`tests/gui_test.py`, through QEMU's keyboard
  and mouse). Both are in `make test`. The second mirrors libui's layout
  arithmetic, so a layout that drifts from its documentation fails.
- **A window's pixels are held twice** (the program's canvas and
  ManiDE's copy), as before; libui adds the widgets and a few kilobytes of
  state per window. A full-screen 800x600 program is about 4 MiB.
- **The programs here don't save.** The toolkit has an editable text
  area and an entry, and ManiOS can now write a FAT volume, but saving
  needs a place the user picks and a name they type: a file chooser and a
  question dialog, which libui doesn't have yet. So nothing ships that
  edits a file (`view` is a viewer, `paint` a sketchpad); a text editor
  and a paint program that saves are the first programs after those.
- **Left out on purpose, not forgotten:** menus and tabs, question
  dialogs and a file chooser, the clipboard, drag and drop, images, other
  fonts, floating windows, accessibility, and right-to-left or non-ASCII
  text. Each wants either a decision on ManiDE's side (floating windows, a
  clipboard that is a file, as the rest is) or the filesystem's; none
  changes libui's shape. They are listed in the guide's limits, and a later
  ADR takes each that needs deciding.
- **The toolkit is one ui and one thread per program.** A program that
  must wait for something slow starts a helper process, as the terminal
  does (DESIGN.md §2), and polls its pipe, not libui's.

## References
- [docs/gui.md](../gui.md): the guide; [M23 notes](../milestones/M23-gui-software.md)
- [ADR-0003](0003-plan9-namespaces-and-resource-protocol.md), [ADR-0004](0004-window-system-as-a-file-server.md),
  [ADR-0007](0007-manide-tiling-desktop.md); [docs/desktop/DESIGN.md](../desktop/DESIGN.md)
