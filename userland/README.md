# userland/

User programs, linked with `libc/` by `userland/user.ld` and packed
into the boot archive, which the kernel mounts at `/boot`
([M8](../docs/milestones/M8-userspace.md) and
[M9](../docs/milestones/M9-libc-shell.md) notes). Every `.c` file is one
program:

- `bin/` — installed in `/boot/bin`, bound at `/bin`: the shell `sh`
  (which ManiOS boots into) and the coreutils — `cat`, `echo`, `ls`,
  `wc`, `sum`, `sleep`, `pwd`, `bind`, `unbind`, `uptime`, `true`,
  `false` — plus `mount` (ZRP, M10), `gfxdemo` (M11) and `hello`
- `test/` — test programs, installed in `/boot/test`: `utest` (system
  calls), `ctest` (libc) and `gtest` (libgfx), run at every boot; their
  helpers `fault`, `isotest`, `nstest`; and `fbtest` (the framebuffer
  device, run by `tests/gfx_test.py`)
- `etc/` — plain files, installed in `/boot/etc`
