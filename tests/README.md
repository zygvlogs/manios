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

- `net_test.py` (also run by `make test`) — the NE2000, the IPv4 stack
  and ZRP, on the wire. QEMU's socket netdev delivers the guest's
  Ethernet frames to the test, which speaks ARP/IPv4/ICMP/UDP and ZRP
  itself. It checks the guest's replies and checksums, sends it
  malformed packets and floods, and runs its own ZRP server (on a lossy
  link) and client against the guest's. Then it connects two ManiOS
  machines, one serving a FAT volume to the other.

- `gfx_test.py` (also run by `make test`) — the framebuffer and
  libgfx, checked on the screen itself through QEMU's `screendump`:
  colours where `gfxdemo` draws them, in each mode, and text mode
  restored pixel for pixel afterwards.

- `desktop_test.py` (also run by `make test`) — the desktop, driven
  with `sendkey`, `mouse_move` and `mouse_button`, and checked through
  `screendump`: text is read back off the screen by matching character
  cells against `font.txt`. It opens the menu, runs commands in a
  terminal window, runs `wintest` inside the desktop, drags, focuses and
  closes windows, and exits back to the text console; and on a machine
  without Bochs VBE, checks the desktop explains and exits.

- `cluster_test.py` (also run by `make test`) — the cluster roles
  (M13): a file server, a CPU server, a terminal and a machine with the
  wrong key, connected through a hub the test runs, which is also a host
  with its own implementation of ZRP2's authentication. It checks the
  keys both ways, remote execution with the terminal's console and
  namespace (and the file server through it), exit statuses, a remote
  program's window on the terminal's desktop, and a CPU server dying
  under a job.

- `tools/mkfont.py --check` (in `make test`) — the generated font
  matches `desktop/libgfx/font.txt`.

- `userland/test/` holds the test programs the kernel runs at every
  boot (`utest` for system calls, `ctest` for libc, `gtest` for libgfx,
  `ztest` for userspace file servers, `cltest` for exports over the
  network) and those the tests start
  (`fault`, `fbtest`, `wintest`); they are in the boot archive under
  `/boot/test`.

Not yet wired into CI (GitHub Actions); see
[`docs/FOUNDING-PROPOSAL.md` §12](../docs/FOUNDING-PROPOSAL.md#12-m1-status-achieved).
