# desktop/

The ManiOS Desktop Environment: compositor/window manager, desktop
shell, application launcher, panel/taskbar, file manager, settings
application, notification system, terminal, theme system, and
application/window APIs. Native, with its own visual identity — never a
repackaging of an existing desktop environment.

The desktop is **ManiDE**: a tiling window manager with workspaces and
a status bar. Design: [`docs/desktop/DESIGN.md`](../docs/desktop/DESIGN.md),
[ADR-0004](../docs/adr/0004-window-system-as-a-file-server.md) — the
window system is a file server, `/dev/wsys` — and
[ADR-0007](../docs/adr/0007-manide-tiling-desktop.md) — tiling, and
ManiDE gives windows their size. The first desktop was milestone M12
([notes](../docs/milestones/M12-desktop.md)); ManiDE is M18
([notes](../docs/milestones/M18-manide.md)).

- `libgfx/` — the 2D graphics library everything here draws with:
  canvases, clipping, primitives, blending, text in ManiOS's own font
  (`font.txt`), and output to the screen (`/dev/fb`)
  ([M11 notes](../docs/milestones/M11-graphics.md))
- `manide/` — `/bin/manide`: the window manager, compositor, status
  bar, menu and run prompt, in one process (`main.c` start-up, the
  session and the main loop, `wsys.c` the `/dev/wsys` file server,
  `tile.c` workspaces, layouts and focus, `bar.c` the status bar's
  facts, `draw.c` composition, `input.c` keys and the mouse)
- `libwin/` — the window library programs link: `win_open`,
  `win_flush`, `win_next` (with `WIN_RESIZE`), `win_resize`,
  `win_close`
- `apps/` — one program per file, installed in `/bin`: `term` (a
  terminal of any size running `sh`, with ANSI colours), `welcome`,
  `clock`, `about`
