# desktop/

The ManiOS Desktop Environment: compositor/window manager, desktop
shell, application launcher, panel/taskbar, file manager, settings
application, notification system, terminal, theme system, and
application/window APIs. Native, with its own visual identity — never a
repackaging of an existing desktop environment.

Design: [`docs/desktop/DESIGN.md`](../docs/desktop/DESIGN.md) and
[ADR-0004](../docs/adr/0004-window-system-as-a-file-server.md) — the
window system is a file server, `/dev/wsys`. The MVP is milestone M12
([notes](../docs/milestones/M12-desktop.md)).

- `libgfx/` — the 2D graphics library everything here draws with:
  canvases, clipping, primitives, blending, text in ManiOS's own font
  (`font.txt`), and output to the screen (`/dev/fb`)
  ([M11 notes](../docs/milestones/M11-graphics.md))
- `wm/` — `/bin/desktop`: the compositor, window manager, panel and
  launcher, in one process (`main.c` start-up and the main loop,
  `wsys.c` the `/dev/wsys` file server, `draw.c` composition,
  `input.c` keyboard and mouse)
- `libwin/` — the window library applications link: `win_open`,
  `win_flush`, `win_next`, `win_close`
- `apps/` — one program per file, installed in `/bin`: `term` (a
  terminal running `sh`), `clock`, `about`
