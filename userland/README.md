# userland/

User programs, linked with `libc/` by `userland/user.ld` and packed
into the boot archive, which the kernel mounts at `/boot`
([M8 notes](../docs/milestones/M8-userspace.md)). Every `.c` file is one
program:

- `bin/` — programs, installed in `/boot/bin` (bound at `/bin`)
- `test/` — test programs, installed in `/boot/test`: `utest` (the
  system call conformance test run at every boot), `fault`, `isotest`,
  `nstest`
- `etc/` — plain files, installed in `/boot/etc`
