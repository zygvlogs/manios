# tests/

- `boot_smoke_test.sh` (`make test`) — boots the kernel headlessly in
  QEMU under several RAM sizes and CPU models, including a 486, which
  faults on post-386 instructions. It requires every milestone marker
  and the shell's `manios% ` prompt on the serial port, and fails on
  any `ZKT PANIC`. The in-kernel self-tests (e.g. `zkt/mm/mm_selftest.c`)
  run during that boot, so their result is part of the gate.

- `install_test.py` (also run by `make test`; takes the build
  directory) — the native boot path and the installer (M14,
  [ADR-0006](../docs/adr/0006-native-boot-loader-and-installer.md)),
  with no `-kernel`: QEMU's BIOS boots the ISO, disks and a USB-stick
  image. It checks the ISO's structure (ISO 9660, El Torito, the hybrid
  MBR; with `xorriso` too if installed) and that it is rebuilt byte for
  byte; boots the CD; edits the loader's command line over serial and
  from the keyboard; runs `install` (its refusals, the question, the
  write) and checks the disk from outside; boots the installed disk
  with its command line and installs a clone from it; boots the ISO as
  a hard disk, a CHS-only build, and a 6 MiB machine; boots a much
  bigger build (`build/big/`: a 5 MiB file in its boot archive, a 7 MiB
  kernel) and reads the file back; and makes the loader explain too
  little memory (with the figure it works out), a damaged boot area, a
  bad header, a load address that would overwrite the loader, and a
  disk without a boot partition. The screen itself is read from
  VGA text memory (QEMU's `pmemsave`): quiet by default, the whole boot
  log with `verbose=1` (0.14.1).

- `console_test.py` (also run by `make test`) — boots the kernel with
  the serial line on stdio and drives the shell, then (after `exit`)
  the `ZKT>` monitor, two ways:
  typing over serial, and pressing keys on the emulated PS/2 keyboard
  through the QEMU monitor's `sendkey`. It checks echo, line editing
  and command output, and runs user programs (including malformed ELF
  files and a program on a FAT disk). It runs several machine
  configurations with disk images it generates (partitioned,
  partitionless FAT, none), and a 3 MiB machine whose failed self-test
  must show on the quiet boot screen. Since M18 also `/dev/sysstat`,
  `/dev/ps`, `fetch` and `top`, and `fetch`'s ANSI colours on the text
  console (read from VGA text memory: attributes, not escapes). Needs
  Python 3 and mtools, and the build's `build/bootfs/` programs.

- `net_test.py` (also run by `make test`) — the network cards, the IPv4
  stack and ZRP, on the wire. QEMU's socket netdev delivers the guest's
  Ethernet frames to the test, which speaks ARP/IPv4/ICMP/UDP and ZRP
  itself. Over each card -- NE2000, AMD PCnet, Intel e1000 -- it checks
  the guest's replies and checksums, sends it malformed packets
  (among them frames too big for one receive buffer) and floods, and runs its own ZRP server (on a lossy link) and client
  against the guest's. It connects two ManiOS machines, one serving a
  FAT volume to the other. And DHCP: against the test's own server (a
  lost DISCOVER, a stray and a malformed OFFER, a NAK), and against
  QEMU's, whose gateway must then answer pings.

- `gfx_test.py` (also run by `make test`) — the framebuffer and
  libgfx, checked on the screen itself through QEMU's `screendump`:
  colours where `gfxdemo` draws them, in each mode, and text mode
  restored pixel for pixel afterwards; on QEMU's standard adapter and on
  its VMware SVGA II (VirtualBox's VMSVGA), whose video memory is in
  another BAR.

- `desktop_test.py` (also run by `make test`) — ManiDE, driven with
  `sendkey` (Alt bindings included), `mouse_move` and `mouse_button`,
  and checked through `screendump`: text is read back off the screen by
  matching character cells against `font.txt`, and where panes are is
  worked out as `tile.c` does. It checks the status bar and the session's
  four panes (`fetch`, the welcome, `top`, a terminal), the layouts,
  focus by key and by mouse, workspaces, the run prompt and its
  completion, the menu, closing panes, `wintest` inside ManiDE, the
  terminal's scroll-back and how it follows its pane's size, and exiting
  back to the text console; and on a machine without Bochs VBE, that
  ManiDE explains and exits.

- `cluster_test.py` (also run by `make test`) — the cluster roles
  (M13): a file server, a CPU server, a terminal and a machine with the
  wrong key, connected through a hub the test runs, which is also a host
  with its own implementation of ZRP2's authentication. It checks the
  keys both ways, remote execution with the terminal's console and
  namespace (and the file server through it), exit statuses, jobs that
  cannot start (their reason reaches the terminal), a remote program's
  window on the terminal's ManiDE, and a CPU server dying under a job.

- `openbsd_test.py` (also run by `make test`) — the programs imported
  from OpenBSD (0.16, 0.17, [third_party/openbsd](../third_party/openbsd/README.md)),
  on a FAT disk of small text files (and nested directories) it
  generates: each program's whole output, including pipes, standard
  input (`-`), `yes | head` and `yes | grep -m 2` ending, regular
  expressions, `grep -r`, 64-bit `expr` arithmetic, what ManiOS can't do
  (`sed -i`, `tee FILE`, `tail -f` say so), and error messages from
  `getopt`, `err`/`warn`, `strtonum` and `regerror`; exit statuses
  through the monitor's `run` (`test`, `cmp -s`, `grep -q`); and that
  `/boot/etc/notices` holds every imported file's license. Where GNU's
  tools behave the same, the expected outputs were checked against them
  too. Needs mtools.

- `tools/mkfont.py --check` (in `make test`) — the generated font
  matches `desktop/libgfx/font.txt`.

- `userland/test/` holds the test programs the kernel runs at every
  boot (`utest` for system calls, `ctest` for libc, `gtest` for libgfx,
  `ztest` for userspace file servers, `cltest` for exports over the
  network) and those the tests start
  (`fault`, `fbtest`, `wintest`); they are in the boot archive under
  `/boot/test`.

GitHub Actions runs all of them (`make test`) before it publishes a
release ([`.github/workflows/release.yml`](../.github/workflows/release.yml));
they are not run on every push.
