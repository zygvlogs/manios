# userland/

User programs, linked with `libc/` by `userland/user.ld` and packed
into the boot archive, which the kernel mounts at `/boot`
([M8](../docs/milestones/M8-userspace.md) and
[M9](../docs/milestones/M9-libc-shell.md) notes). Every `.c` file is one
program:

- `bin/` — installed in `/boot/bin`, bound at `/bin`: the shell `sh`
  (which ManiOS boots into; pipelines and redirection since M12) and
  the coreutils — `cat`, `echo`, `ls`, `wc`, `sum`, `sleep`, `pwd`,
  `bind`, `unbind`, `uptime`, `true`, `false` — plus `mount` (ZRP,
  M10), `gfxdemo` (M11) and `hello`. The desktop's programs (`desktop`,
  `term`, `clock`, `about`) are built from `desktop/` into `/bin` too.
- `test/` — test programs, installed in `/boot/test`: `utest` (system
  calls), `ctest` (libc), `gtest` (libgfx) and `ztest` (a program
  serving files over a pipe, M12), run at every boot; their helpers
  `fault`, `isotest`, `nstest`; `fbtest` (the framebuffer device, run by
  `tests/gfx_test.py`); and `wintest` (the window system's files, run
  inside the desktop by `tests/desktop_test.py`)
- `etc/` — plain files, installed in `/boot/etc`
