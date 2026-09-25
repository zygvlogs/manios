# ManiOS

An independent operating system with **ZKT (ZygKernel Technology)** at
its core, BSD technology adopted where it legally and technically earns
its place, and a desktop environment that is genuinely its own.

ManiOS is **not** a Linux distribution and **not** a BSD or AmigaOS
clone. It does not depend on the Linux kernel.

## Status: Proposed — pre-implementation

This repository currently contains the founding architecture proposal
and repository scaffolding only. No kernel, driver, or bootloader code
has been written yet — see
[`docs/FOUNDING-PROPOSAL.md`](docs/FOUNDING-PROPOSAL.md) for the full
architecture, roadmap, and the open decisions that need sign-off before
implementation begins.

First milestone target:

> ManiOS boots on i386 and reaches a ZKT kernel console.

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
