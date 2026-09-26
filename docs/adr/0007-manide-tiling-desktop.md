# ADR-0007: ManiDE, a tiling desktop; the desktop gives windows their size

**Status:** Accepted; implemented at M18 ([design](../desktop/DESIGN.md), [notes](../milestones/M18-manide.md))
**Date:** 2026-09-26

## Context
The M12 desktop was a floating window manager: windows had the size
their programs asked for, cascaded, and were moved by their title bars.
The project's direction for the desktop is ManiDE, shown in a mock-up:
panes that tile the screen with no overlap, workspaces, a status bar of
facts (host, kernel, uptime, CPU, memory, network, time), a dark theme,
and panes that are mostly text -- system information, a process viewer,
terminals. Tiling needs something M12 did not have: the window manager,
not the program, decides a window's size. The window system is a file
server (ADR-0004), shared by programs that may be slow, remote (M13) or
written before this change.

## Decision
The desktop becomes **ManiDE**, a tiling window manager with nine
workspaces and three layouts (grid, tall, mono), driven from the
keyboard with Alt and usable with the mouse. **ManiDE offers each window
the size of its pane with an event, `r W H`; the program takes it by
writing `size W H` to the window's `ctl` and redrawing.** Until it does,
ManiDE shows the image it has, at the pane's top-left. `libwin` takes the
offer for its programs: it resizes their canvas before returning
`WIN_RESIZE`. `move` goes away. The kernel reports Alt as a prefix byte,
`ZKT_KEY_ALT`, on `/dev/kbd`, and gains the facts the status bar and the
text panes show: `/dev/sysstat`, `/dev/ps`, and per-process CPU time.

## Alternatives considered
- **Keep floating windows and add a tiling mode.** Two window managers'
  worth of rules (stacking, dragging, placement) for one screen of 800x600
  pixels, where overlapping windows mostly hide each other.
- **Let ManiDE resize the image itself when it retiles.** Simple for
  ManiDE, but a program's image writes in flight would land at offsets
  for the old width and scramble the picture, and the program would draw
  at a size it doesn't know. Making the program acknowledge (`size`)
  orders the change among its own writes on the one ZRP connection.
- **Scale images to their panes.** Integer-only scaling of text is
  unreadable; programs drawing at the real size look right.
- **A different modifier (a "super" key).** PS/2 keyboards of the
  target machines don't all have one, and VirtualBox and QEMU pass Alt
  through by default.

## Consequences
- Every `libwin` program must redraw on `WIN_RESIZE` (the four in ManiOS
  do). A program that writes `/dev/wsys` itself and never takes the
  offer keeps working: its image shows at the top-left of its pane.
- A window's size changes more often than before, so image traffic
  matters more; the kernel now moves writes in 16 KiB pieces instead of
  512 bytes, and ManiDE waits a moment for the rest of an image before
  composing.
- Terminals are now any size, so `term` understands the ANSI escape
  sequences full-screen programs use, and answers the size query
  (`ESC [ 18 t`).
- The console had no Alt; `ZKT_KEY_ALT` is only on `/dev/kbd`, and a
  program reading the console sees just the key.

## References
- [ADR-0004](0004-window-system-as-a-file-server.md): the window system as a file server.
- [docs/desktop/DESIGN.md](../desktop/DESIGN.md): ManiDE's design.
- `zkt/abi/zkt_abi.h`: `ZKT_KEY_ALT`; `zkt/kernel/sysstat.h`: `/dev/sysstat`, `/dev/ps`.
