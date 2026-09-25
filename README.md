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
| M5 — driver framework, keyboard | Next |

The full architecture and roadmap are in
[`docs/FOUNDING-PROPOSAL.md`](docs/FOUNDING-PROPOSAL.md).

## Build and run

```
make toolchain   # once: builds the i686-elf cross-compiler (~15 min)
make             # builds build/manios-zkt.elf
make run         # boots it in QEMU (486 CPU model), serial on the terminal
make test        # headless boot tests under several CPU/RAM configurations
```

Requires `qemu-system-i386`, plus the usual GCC build dependencies
(GMP, MPFR, MPC, texinfo, bison, flex) for `make toolchain`.

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
  kernel/     init, panic, logging, syscall dispatch
libc/         ManiOS's own minimal C library
userland/     init, shell, coreutils
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
