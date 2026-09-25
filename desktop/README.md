# desktop/

The ManiOS Desktop Environment: compositor/window manager, desktop
shell, application launcher, panel/taskbar, file manager, settings
application, notification system, terminal, theme system, and
application/window APIs. Native, with its own visual identity — never a
repackaging of an existing desktop environment.

- `libgfx/` — the 2D graphics library everything here draws with:
  canvases, clipping, primitives, blending, text in ManiOS's own font
  (`font.txt`), and output to the screen (`/dev/fb`)
  ([M11 notes](../docs/milestones/M11-graphics.md))

The desktop itself is milestone M12
([`docs/FOUNDING-PROPOSAL.md` §6](../docs/FOUNDING-PROPOSAL.md#6-development-roadmap)).
