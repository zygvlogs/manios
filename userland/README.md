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
  system at a glance), `top` (the processes), `desktop` (ManiDE by
  its old name), and since M20 `beep` (the PC speaker), `play` (WAV
  files and tones on the sound card), `fm` (notes on the OPL2 FM
  synthesizer), `poweroff` and `reboot`
  ([docs/drivers.md](../docs/drivers.md)). ManiDE and its programs (`manide`, `term`, `welcome`,
  `clock`, `about`, and since M23 the graphical programs `files`,
  `view`, `calc`, `paint`, `widgets` and `greet`, written with libui;
  [docs/gui.md](../docs/gui.md)) are built from `desktop/` into `/bin` too,
  and so are the programs imported from OpenBSD (0.16, 0.17): `grep`,
  `sed`, `expr`, `test`, `head`, `tail`, `cut`, `paste`, `join`, `comm`,
  `cmp`, `uniq`, `tr`, `nl`, `fmt`, `fold`, `column`, `colrm`, `col`,
  `lam`, `expand`, `unexpand`, `rev`, `tee`, `tsort`, `vis`, `unvis`,
  `basename`, `dirname` and `yes`, from
  [`third_party/openbsd/`](../third_party/openbsd/README.md).
- `dos/` — ManiDOS (`/bin/dos`), a disk operating system of ManiOS's
  own ([docs/dos.md](../docs/dos.md)): `main.c` the command line,
  pipelines and running programs, `drives.c` drive letters and DOS
  paths, `commands.c` the internal commands, `batch.c` batch files,
  `fat.c` FAT disks read raw (VOL, CHKDSK); and `autoexec.bat`,
  installed at the boot disk's root (`/boot/autoexec.bat`, A:\AUTOEXEC.BAT)
- `test/` — test programs, installed in `/boot/test`: `utest` (system
  calls), `ctest` (libc), `gtest` (libgfx), `ztest` (a program
  serving files over a pipe, M12) and `cltest` (exports and ZRP over
  the loopback network, M13), run at every boot; their helpers
  `fault`, `isotest`, `nstest`; `fbtest` (the framebuffer device, run by
  `tests/gfx_test.py`); and `wintest` (the window system's files, run
  inside ManiDE by `tests/desktop_test.py`); and `uitest` (the widget
  toolkit's conformance test, run by `tests/gui_test.py`)
- `etc/` — plain files, installed in `/boot/etc`: `motd`, `manide`
  (ManiDE's session: the programs it starts), and `rc.cpu` (the boot
  script of a CPU server, `rc=/boot/etc/rc.cpu`)
