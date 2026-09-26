# ManiOS

An independent operating system with **ZKT (ZygKernel Technology)** at
its core, BSD technology adopted where it legally and technically earns
its place, and a desktop environment that is genuinely its own.

ManiOS is **not** a Linux distribution and **not** a BSD or AmigaOS
clone. It does not depend on the Linux kernel.

ManiOS is a continuation of [ZygOS](https://github.com/zygvlogs/zygos):
ZKT (ZygKernel Technology) is ZygOS as a kernel, and was also a ZygOS
project. [ZygHosting.online](https://zyghosting.online/) and
[Manikineko.nl](https://manikineko.nl/) decided to make ManiOS; its code
is written anew for it.

## Status

| Milestone | State |
|---|---|
| M1 — boots on i386 to a ZKT kernel console | Achieved |
| M2 — physical/virtual memory, higher-half kernel, kernel heap | Achieved ([notes](docs/milestones/M2-memory-management.md)) |
| M3 — hardware IRQs, PIT timer | Achieved ([notes](docs/milestones/M3-interrupts-and-timer.md)) |
| M4 — multitasking: kernel threads, cooperative + preemptive scheduling | Achieved ([notes](docs/milestones/M4-multitasking.md)) |
| M5 — driver framework, keyboard, interactive `ZKT>` monitor | Achieved ([notes](docs/milestones/M5-driver-framework.md)) |
| M6 — ATA storage (PIO, CHS fallback, MBR partitions) | Achieved ([notes](docs/milestones/M6-ata-storage.md)) |
| M7 — VFS, Plan 9-style namespaces and union directories, FAT12/16, devfs | Achieved ([notes](docs/milestones/M7-vfs-namespaces.md)) |
| M8 — userspace: ring 3 processes, ELF loader, syscalls | Achieved ([notes](docs/milestones/M8-userspace.md)) |
| M9 — libc, versioned syscall ABI, coreutils, shell | Achieved ([notes](docs/milestones/M9-libc-shell.md)) |
| M10 — networking (NE2000, IPv4/UDP), and ZRP (the resource protocol) over the network | Achieved ([notes](docs/milestones/M10-networking-zrp.md), [protocol](docs/zrp.md)) |
| M11 — graphics: linear framebuffer (Bochs VBE, VGA 13h), 2D library, own font | Achieved ([notes](docs/milestones/M11-graphics.md)) |
| M12 — desktop environment MVP: compositor, window system as files, panel, launcher, terminal | Achieved ([notes](docs/milestones/M12-desktop.md), [design](docs/desktop/DESIGN.md)) |
| M13 — cluster roles: file server, CPU server, terminal; authenticated ZRP2 | Achieved ([notes](docs/milestones/M13-cluster-roles.md), [ADR-0005](docs/adr/0005-cluster-roles-and-authentication.md)) |
| M14 — own boot loader, bootable hybrid ISO, installer, releases | Achieved ([notes](docs/milestones/M14-installer-release.md), [ADR-0006](docs/adr/0006-native-boot-loader-and-installer.md)) |
| M15 — network cards for VirtualBox (AMD PCnet, Intel PRO/1000), DHCP | Achieved ([notes](docs/milestones/M15-network-cards-dhcp.md)) |
| M16 — first BSD imports: OpenBSD's text tools on ManiOS's libc | Achieved ([notes](docs/milestones/M16-openbsd-tools.md), [imports](third_party/openbsd/README.md)) |

## Download and install

Each release on the [Releases](https://github.com/zygvlogs/manios/releases)
page has a bootable ISO, `manios-VERSION.iso`. It boots from a CD, from
a USB stick it is written to, or in QEMU or VirtualBox:

```
qemu-system-i386 -cpu 486 -m 32 -cdrom manios-0.16.0.iso -boot d
```

and `install ata0` puts ManiOS on a hard disk. ManiOS boots with its own
boot loader, which offers to change the boot options for 3 seconds;
then it tests itself and gives you the shell (`verbose=1` shows the
whole boot log). See [docs/install.md](docs/install.md). ManiOS has only
been tested in emulators so far; reports from real (old) PCs are
welcome.

The full architecture and roadmap are in
[`docs/FOUNDING-PROPOSAL.md`](docs/FOUNDING-PROPOSAL.md).

## Build and run

```
make toolchain   # once: builds the i686-elf cross-compiler (~15 min)
make             # builds build/manios-zkt.elf and the ISO, build/manios.iso
make run         # boots the kernel in QEMU (486 CPU model), serial on the terminal
make test        # headless boot tests, plus interactive console tests
make release     # dist/: the ISO, the kernel, SHA256SUMS
```

Pushing a tag `vVERSION` (matching the `VERSION` file), or running the
release workflow by hand with "publish" ticked, makes GitHub Actions
build, run `make test` and publish a release
([workflow](.github/workflows/release.yml)).

Requires `qemu-system-i386`, Python 3 and mtools (for the tests; xorriso
optionally), plus
the usual GCC build dependencies (GMP, MPFR, MPC, texinfo, bison, flex)
for `make toolchain`.

ManiOS boots into its shell, `manios% `. The programs are in `/bin`
(`ls /bin`; the shell's `help` lists its builtins). For example,
`ls -l /dev` lists devices, and with a FAT disk attached
(`qemu-system-i386 ... -drive file=disk.img,format=raw`),
`cd /n/ata0p1; ls` lists its files, while `bind -a /n/ata0p2 /n/ata0p1`
makes a union of two volumes. `newns` gives the shell a private
namespace. `exit` leaves the shell for the `ZKT>` kernel monitor, a
debugging console with its own `help`; `run /bin/sh` goes back.

Machines share files over ZRP, ManiOS's 9P-style protocol, and take
Plan 9's roles (M13). ManiOS drives NE2000, AMD PCnet (VirtualBox's
default) and Intel PRO/1000 (e1000) network cards, and without `ip=` on
the boot line asks a DHCP server for its address; `make run` attaches
an NE2000 to QEMU's user network. Give every machine of a cluster the same `key=SECRET` on its
boot line; then:

- a **file server** booted with `ip=10.0.0.1/24 export=/n/ata0p1`
  serves that directory, and on another machine `mount udp!10.0.0.1 /n`
  puts it at `/n`;
- a **CPU server** booted with `ip=10.0.0.2/24 rc=/boot/etc/rc.cpu`
  runs `cpud`;
- on a **terminal**, `cpu udp!10.0.0.2` gives a shell on the CPU server
  that sees the terminal's console and files (at `/mnt/term`), and
  `cpu udp!10.0.0.2 clock`, typed in the desktop's terminal, opens a
  window drawn by the CPU server.

With a key, both ends of every mount prove they know it; messages are
not encrypted, so use ZRP on networks you trust
([protocol](docs/zrp.md)). `tests/cluster_test.py` runs four machines
this way.

`gfxdemo` shows the graphics (640x480; `gfxdemo 800 600`, or
`gfxdemo vga` for 320x200 on any VGA card) and returns to text on
Enter.

![The ManiOS desktop](docs/desktop/screenshot.png)

`desktop` starts the ManiOS desktop (800x600; `desktop 640 480`). F1
opens its menu — Terminal, Clock, About ManiOS, Exit desktop — and F2
brings the bottom window up; the mouse focuses, raises, drags and
closes windows. The window system is a file server: in a terminal,
`ls /dev/wsys` lists the windows, and `cat /dev/wsys/1/ctl` describes
one ([design](docs/desktop/DESIGN.md)). The shell does pipelines and
redirection (`ls /bin | wc`, `cat < FILE`, `echo x > /dev/null`).

Besides ManiOS's own programs, `/bin` has text tools from OpenBSD,
built from OpenBSD's source unmodified: `head`, `cut`, `paste`, `comm`,
`uniq`, `rev`, `fold`, `expand`, `basename`, `dirname` and `yes`
(`ls /bin | head -n 5`; [third_party/openbsd](third_party/openbsd/README.md)).
Their licenses are in `/boot/etc/notices`.

## Repository layout

```
boot/         the ManiOS boot loader: MBR, CD boot image, stage 2 (ADR-0006)
zkt/          ZygKernel Technology — the kernel
  arch/i386/  first target architecture (only place with #ifdef/asm)
  mm/         physical + virtual memory management, kernel heap
  scheduler/  processes, threads, run queues
  ipc/        inter-process communication
  drivers/    driver framework + in-tree drivers
  fs/         VFS + filesystem implementations
  net/        network stack and ZRP
  kernel/     init, panic, logging, processes, syscalls, ELF loader
  abi/        the system call ABI header shared with userspace
libc/         ManiOS's own C library (stdio, malloc, strings, file servers, ...)
desktop/      the desktop environment: libgfx/ (2D graphics), libwin/ (windows),
              wm/ (the compositor), apps/ (terminal, clock, about)
userland/     user programs (bin/), test programs (test/), boot files (etc/)
tools/        cross-toolchain build script, boot area / ISO / disk image builders,
              QEMU script
third_party/  BSD-derived source (openbsd/: tools and libc functions) + the
              license notices ledger
docs/         architecture, roadmap, and ADRs
tests/        boot smoke tests, console, network, graphics, desktop, cluster
              and install tests
.github/      the release workflow
build/        build output (git-ignored); dist/: release files (git-ignored)
```

## Read next

- [`docs/FOUNDING-PROPOSAL.md`](docs/FOUNDING-PROPOSAL.md) — architecture,
  BSD integration strategy, boot strategy, roadmap, risks, and licensing.
- [`docs/adr/`](docs/adr/) — Architecture Decision Records for individual
  major decisions.
