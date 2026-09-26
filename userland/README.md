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
  M10), `gfxdemo` (M11), `cpu` and `cpud` (the terminal's and the CPU
  server's halves of remote execution, M13), `install` (puts ManiOS on
  a hard disk, M14, [docs/install.md](../docs/install.md)), `dd`
  (copies blocks, e.g. to and from disks), `hello`, `fetch` (the
  system at a glance), `top` (the processes) and `desktop` (ManiDE by
  its old name). ManiDE and its programs (`manide`, `term`, `welcome`,
  `clock`, `about`) are built from `desktop/` into `/bin` too,
  and so are the programs imported from OpenBSD (0.16, 0.17): `grep`,
  `sed`, `expr`, `test`, `head`, `tail`, `cut`, `paste`, `join`, `comm`,
  `cmp`, `uniq`, `tr`, `nl`, `fmt`, `fold`, `column`, `colrm`, `col`,
  `lam`, `expand`, `unexpand`, `rev`, `tee`, `tsort`, `vis`, `unvis`,
  `basename`, `dirname` and `yes`, from
  [`third_party/openbsd/`](../third_party/openbsd/README.md).
- `test/` — test programs, installed in `/boot/test`: `utest` (system
  calls), `ctest` (libc), `gtest` (libgfx), `ztest` (a program
  serving files over a pipe, M12) and `cltest` (exports and ZRP over
  the loopback network, M13), run at every boot; their helpers
  `fault`, `isotest`, `nstest`; `fbtest` (the framebuffer device, run by
  `tests/gfx_test.py`); and `wintest` (the window system's files, run
  inside ManiDE by `tests/desktop_test.py`)
- `etc/` — plain files, installed in `/boot/etc`: `motd`, `manide`
  (ManiDE's session: the programs it starts), and `rc.cpu` (the boot
  script of a CPU server, `rc=/boot/etc/rc.cpu`)
