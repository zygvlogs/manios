# ManiOS

An independent operating system with **ZKT (ZygKernel Technology)** at
its core, BSD technology adopted where it legally and technically earns
its place, and a desktop environment that is genuinely its own.

ManiOS is **not** a Linux distribution and **not** a BSD or AmigaOS
clone. It does not depend on the Linux kernel.

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
| M12 — desktop environment MVP: compositor, shell, launcher | Next |

The full architecture and roadmap are in
[`docs/FOUNDING-PROPOSAL.md`](docs/FOUNDING-PROPOSAL.md).

## Build and run

```
make toolchain   # once: builds the i686-elf cross-compiler (~15 min)
make             # builds build/manios-zkt.elf
make run         # boots it in QEMU (486 CPU model), serial on the terminal
make test        # headless boot tests, plus interactive console tests
```

Requires `qemu-system-i386`, Python 3 and mtools (for the tests), plus
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

Machines share files over ZRP, ManiOS's 9P-style protocol. `make run`
attaches an NE2000 to QEMU's user network. A machine booted with
`-append "ip=10.0.0.1/24 export=/n/ata0p1"` serves that directory, and
on another, `mount udp!10.0.0.1 /n` puts it at `/n`
(`tests/net_test.py` connects two machines this way). ZRP v1 has no
authentication: use it only on trusted networks.

`gfxdemo` shows the graphics (640x480; `gfxdemo 800 600`, or
`gfxdemo vga` for 320x200 on any VGA card) and returns to text on
Enter.

## Repository layout

```
boot/         bootloader
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
libc/         ManiOS's own C library (stdio, malloc, strings, ...)
desktop/      the desktop environment; libgfx/, the 2D graphics library
userland/     user programs (bin/), test programs (test/), boot files (etc/)
desktop/      ManiOS Desktop Environment
tools/        cross-toolchain build scripts, image builder, QEMU scripts
third_party/  vendored BSD-derived source + license notices ledger
docs/         architecture, roadmap, and ADRs
tests/        boot smoke tests, unit tests
build/        build output (git-ignored)
```

## Read next

- [`docs/FOUNDING-PROPOSAL.md`](docs/FOUNDING-PROPOSAL.md) — architecture,
  BSD integration strategy, boot strategy, roadmap, risks, and licensing.
- [`docs/adr/`](docs/adr/) — Architecture Decision Records for individual
  major decisions.
