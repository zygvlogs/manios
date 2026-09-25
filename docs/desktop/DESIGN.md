# The ManiOS Desktop Environment — design

**Status:** the MVP described here is milestone M12
([notes](../milestones/M12-desktop.md)); later sections are direction,
not commitments.
**Date:** 2026-09-25
**Decision record:** [ADR-0004](../adr/0004-window-system-as-a-file-server.md)

The founding proposal (§1.4) deferred the desktop to its own design
document "when the project reaches that milestone". This is that
document. It follows the proposal's shape: what we want, the principles
that decide trade-offs, the architecture, what the MVP contains, and what
is explicitly left for later.

![The ManiOS desktop at M12: a terminal, the clock and About ManiOS](screenshot.png)

*The M12 desktop in QEMU. The windows were placed by writing `move X Y`
to their `ctl` files from the terminal's shell.*

## 1. Goals

1. **A graphical desktop that is ManiOS's own.** Not a port or a theme of
   an existing environment. Its look (§6) and its structure (§3) are
   designed here.
2. **Runs where ManiOS runs.** An i386 with no FPU, a Bochs/VBE or
   compatible linear framebuffer, a PS/2 keyboard and mouse, and tens of
   megabytes of memory. Integer arithmetic only; no GPU.
3. **Consistent with the rest of the system (ADR-0003).** Windows,
   input and the screen are reached as files in a namespace, through the
   same protocol (ZRP) that reaches another machine's files. A program
   drawing a window uses `open`, `read` and `write`; so could a program
   on another node, once M13 lets it import the terminal's namespace.
4. **Small enough to understand.** The MVP is one compositor process,
   one small client library, and three applications.

Non-goals for the MVP: hardware acceleration, resizable windows,
multiple screens, fonts other than the ManiOS font, drag and drop,
clipboard, notifications, a file manager and a settings application
(§8 orders these).

## 2. Principles

- **The window system is a file server.** The compositor serves a small
  tree, `/dev/wsys`, and applications are its clients. There are no
  window system calls in the kernel and no special IPC: the kernel only
  learned to carry ZRP over a pipe (`mountfd`, M12), which any program
  can use to serve files.
- **Processes as event sources.** A client that must wait for several
  things (keys and a shell's output, say) runs a small helper process per
  blocking source, as Plan 9's libevent does, and polls pipes. The kernel
  does not need to know which remote files would block.
- **The compositor owns the hardware.** Only it opens `/dev/fb`,
  `/dev/kbd` and `/dev/mouse`; applications see only their window.
- **Text in, text out.** Control and event messages are short text lines,
  readable with `cat` and writable with `echo`, like the rest of the
  system's device files. Pixels are the one binary format.
- **Integer-only, damage-driven drawing.** Nothing is redrawn that did not
  change; everything is recomposed from the window images on demand.

## 3. Architecture

```
  +--------------+   +------------+   +-------------+
  | term         |   | clock      |   | about       |   applications (libwin)
  +------+-------+   +-----+------+   +------+------+
         | open/read/write /dev/wsys/...     |
         v                  v                v
  ---------------- kernel VFS + ZRP client (channel) ----------------
         |  one pipe, many requests in flight
         v
  +---------------------------------------------------------------+
  | desktop  (the compositor, window manager, panel and launcher) |
  |   zsrv file server  |  compositor  |  input  |  panel, menu   |
  +---------------------------------------------------------------+
         |                 |                  |
     /dev/fbctl, /dev/fb   /dev/kbd       /dev/mouse, /dev/time
```

- **`desktop`** is a single process. It switches the display to a
  graphics mode, forks its namespace, and serves `/dev/wsys` over a pipe:
  a helper it starts mounts the pipe's other end *after* `/dev` (a union,
  ADR-0003), so `/dev/cons` and friends stay where they are and
  `/dev/wsys` appears beside them — for the desktop and every program it
  starts, and for no one else.
- **Composition** is done in a back buffer in memory (32-bit pixels),
  limited to the damaged rectangle, then copied to `/dev/fb` (which
  converts to the display's format — `libgfx`'s `gfx_present`). Layers,
  bottom to top: background, windows in stacking order (frame, title
  bar, image), panel, menu, pointer.
- **Input.** Keys from `/dev/kbd` go to the focused window, except the
  desktop's own shortcuts. The pointer is drawn by the desktop; mouse
  reports from `/dev/mouse` move it, and clicks focus, raise, move and
  close windows, or reach the application inside its window.
- **Applications** link `libwin`, which opens a window, keeps a canvas
  for it, sends damaged rows to the window's image, and turns the event
  file into structured events.

## 4. The window system's files

Mounted at `/dev/wsys` in the desktop's namespace:

| File | Operations |
|------|------------|
| `new` | A clone file. Write `WIDTH HEIGHT TITLE` to make a window (content size in pixels); read to get its number. The window lives as long as this file stays open: closing it — or the program exiting — closes the window. |
| `N/ctl` | Read: `N X Y WIDTH HEIGHT FOCUSED TITLE` (the content's screen position). Write: `title TEXT`, `top` (raise and focus), `move X Y`. |
| `N/image` | Write-only pixels: 4 bytes each, `0xAARRGGBB` little-endian (alpha ignored), row-major, at byte offset `(y × WIDTH + x) × 4`. The window shows them at once. |
| `N/event` | Read: blocks until there are events, then returns whole lines: `k CODE` (a key: ASCII, or `ZKT_KEY_*`), `m X Y BUTTONS` (pointer in window coordinates, while inside the window or while a button pressed inside it is held), `f 1` / `f 0` (focus gained or lost), `c` (the close box was pressed; the application decides). End of file once the window is gone. |

Keys are delivered as the kernel's `/dev/kbd` bytes, so an application
sees exactly what a text program would (Ctrl+letter as a control
character). The desktop keeps F1 (the menu) and F2 (the next window).

Why these choices:

- A clone file ties a window's life to an open file, so a crashed
  application's window disappears with it — the kernel clunks every fid a
  dying process held (verified by the M12 self-test's fid accounting).
- Offset-addressed image writes need no protocol of their own: the
  kernel's VFS already splits large writes into messages and a program can
  send only the rows it changed. A drawing protocol (Plan 9's `/dev/draw`)
  would cut bandwidth and is the obvious next step (§8), but it is a
  larger design than the MVP needs.
- Event reads are *deferred* by the server (`zsrv`) until there is
  something to say. The kernel's channel transport keeps any number of
  requests in flight, so a blocked event read does not stop the same
  application's image writes, or anyone else's.

## 5. The MVP's components

| Component | Source | What it does |
|-----------|--------|--------------|
| `desktop` | `desktop/wm/` | Mode set, `/dev/wsys` server, compositor, window manager (focus, raise, move, close box), panel (menu button, a button per window, clock), launcher menu, pointer. `desktop [WIDTH HEIGHT]`, 800×600 by default. |
| `libwin` | `desktop/libwin/` | `win_open`, `win_flush`, `win_next` (events, with a timeout), `win_close`, and the event helper process. |
| `term` | `desktop/apps/term.c` | A terminal: 80×24 cells running `sh` through pipes, with local line editing (the kernel's line discipline only serves the console). |
| `clock` | `desktop/apps/clock.c` | The time (UTC) from `/dev/time`, updated every second. |
| `about` | `desktop/apps/about.c` | What this system is: name, kernel, milestone, uptime. |

The launcher is the panel's **ManiOS** menu: *Terminal*, *Clock*,
*About ManiOS*, and *Exit desktop*, which closes every window, waits
(briefly) for the applications to finish, restores the text console with
everything printed meanwhile (M11), and gives the keyboard back to it.

The shell of the desktop — in the proposal's sense of the thing a user
starts programs from — is the panel and its menu for now, with `term`'s
`sh` for everything else.

## 6. Visual identity

A dark, calm base with one warm accent, drawn entirely with the ManiOS
font and flat shapes; no bitmaps other than the pointer.

| Element | Colour |
|---------|--------|
| Background | vertical gradient `#1B3A4B` → `#0E1A24`, the word "ManiOS" in large low-contrast type |
| Panel | `#1E2430`, a 1-pixel `#E07A2E` (ManiOS amber) rule under it |
| Focused title bar | amber `#E07A2E`, dark text |
| Other title bars | `#5A6270`, light text |
| Frame | `#0B0F14` |
| Menu | `#262D3A`, highlighted item amber |

Geometry: panel 22 px; title bar 18 px; 1-pixel frame; a 12×12 close box
at the title bar's right; new windows cascade from (60, 50) in steps of
26 px.

## 7. Risks and limits

| Risk / limit | Mitigation |
|---|---|
| Pixel traffic: a full 480×264 terminal repaint is ~500 KB through the kernel | Clients send only changed rows; the compositor recomposes only damage. A drawing protocol (§8) is the real fix. |
| One compositor process: a bug there takes every window down | Applications see end of file and exit; the text console comes back when the desktop exits. |
| Old hardware without VBE | The desktop needs a linear framebuffer of at least 640×480; on VGA alone it says so and exits (the VGA 320×200 mode stays available to programs through `libgfx`). |
| A client that stops reading events | Events queue per window up to a limit; beyond it the oldest pointer moves are dropped, keys never are (until the queue is full). |

## 8. After the MVP

In rough order: resizable windows and a resize event; a `/dev/draw`-style
protocol (images held by the server, draw operations sent instead of
pixels); a settings application and theme files; notifications; a file
manager; and — with M13 — running applications on another node whose
windows appear here, because the window system is just files in the
namespace a remote CPU server imports.
