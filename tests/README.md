# tests/

- `boot_smoke_test.sh` (`make test`) — boots the kernel headlessly in
  QEMU under several RAM sizes and CPU models, including a 486, which
  faults on post-386 instructions. It requires every milestone marker
  and the shell's `manios% ` prompt on the serial port, and fails on
  any `ZKT PANIC`. The in-kernel self-tests (e.g. `zkt/mm/mm_selftest.c`)
  run during that boot, so their result is part of the gate.

- `console_test.py` (also run by `make test`) — boots the kernel with
  the serial line on stdio and drives the shell, then (after `exit`)
  the `ZKT>` monitor, two ways:
  typing over serial, and pressing keys on the emulated PS/2 keyboard
  through the QEMU monitor's `sendkey`. It checks echo, line editing
  and command output, and runs user programs (including malformed ELF
  files and a program on a FAT disk). It runs several machine
  configurations with disk images it generates (partitioned,
  partitionless FAT, none). Needs Python 3 and mtools, and the build's
  `build/bootfs/` programs.

- `userland/test/` holds the test programs the kernel runs at every
  boot (`utest` for system calls, `ctest` for libc) and that the
  console test starts (`fault`); they are in the boot archive under
  `/boot/test`.

Not yet wired into CI (GitHub Actions); see
[`docs/FOUNDING-PROPOSAL.md` §12](../docs/FOUNDING-PROPOSAL.md#12-m1-status-achieved).
