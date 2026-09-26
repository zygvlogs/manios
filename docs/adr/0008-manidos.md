# ADR-0008: ManiDOS, a DOS written for ManiOS -- not MS-DOS

**Status:** Accepted; implemented at M19 ([guide](../dos.md), [notes](../milestones/M19-manidos.md))
**Date:** 2026-09-26

## Context
The project wants a `dos` command that puts the user in "some sort of
DOS, a disk operating system" -- and, explicitly, **not MS-DOS**. ManiOS
runs on i386 PCs, has a FAT12/16 file system (read-only), per-process
namespaces, pipes, and a Unix-like shell. It has no virtual-8086 mode
support, no BIOS services for programs, and its file systems can't be
written yet. The project takes no GPL or LGPL code.

## Decision
ManiDOS is **a DOS-style command interpreter written for ManiOS from
nothing**, run as an ordinary program (`/bin/dos`): drive letters
mapped onto the namespace (A: the boot disk, C: onwards the FAT
volumes, Z: all of ManiOS), the classic internal commands with their
familiar output, batch files with `AUTOEXEC.BAT`, pipes and
redirection, and disk tools (`VOL`, `CHKDSK`) that read FAT volumes
from the disk itself. It runs ManiOS programs, not DOS programs.
`shell=dos` on the boot command line makes ManiOS start in it.

## Alternatives considered
- **MS-DOS.** Excluded by the project: proprietary, and not wanted.
- **FreeDOS.** GPL-licensed (the kernel and most of its programs), which
  ManiOS's licensing rules exclude.
- **A DOS program runner** (an 8086 emulator, or virtual-8086 mode,
  with an INT 21h layer). It would run real DOS programs, but it is a
  far larger piece of work -- a CPU emulator or kernel support for V86
  tasks, BIOS and DOS services -- and there are no DOS programs in
  ManiOS to run: none can be shipped. It can come later on top of
  ManiDOS, which already refuses `.COM` and `.EXE` files by name.
- **A DOS mode in ManiOS's `sh`.** `sh` is a Unix-like shell; mixing DOS
  syntax into it would make both worse.

## Consequences
- ManiOS gains a second way to work, familiar to DOS users, that sees
  the same files as the first (a FAT volume is C:\ and `/n/ata0p1`).
- Because the drives are read-only, the commands that write (`DEL`,
  `REN`, `MD`, `RD`, `COPY` to a file) say so; they can follow ManiOS
  when it can write files.
- `CHKDSK` reads the disk directly, so it checks what is on the disk,
  independently of ManiOS's FAT driver.

## References
- [docs/dos.md](../dos.md): ManiDOS's guide.
- ADR-0003: namespaces (drive letters are places in one).
