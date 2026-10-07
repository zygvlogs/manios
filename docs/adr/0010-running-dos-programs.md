# ADR-0010: Running DOS programs on ManiOS

**Status:** Accepted
**Date:** 2026-10-07

## Context

ManiDOS (`docs/dos.md`, ADR-0008) is a DOS-like shell: drive letters,
batch files, `DIR`, and ManiOS programs. It deliberately does not run
DOS programs, and refuses a `.COM` or `.EXE` by name. A user who wants
to run DOS software -- a game, a tool -- has nothing to run it with.

The question is how to run DOS programs on ManiOS. The candidates are
an emulator (DOSBox), a DOS to run inside one (MS-DOS, FreeDOS), or an
emulator written for ManiOS.

The constraints are the founding proposal's licensing rule (no
GPL/LGPL/AGPL in anything kernel-adjacent, and no proprietary code
copied from), and ManiOS's shape: a 32-bit protected-mode kernel, a
small libc, no C++ runtime, no SDL, and a deliberately small syscall
surface.

## Decision

Write a small 8086 real-mode interpreter for ManiOS, in userspace, and
serve a DOS program's `INT 21h` and BIOS calls from ManiOS. It is
`userland/dosrun/` (`docs/dosrun.md`).

The MS-DOS 4.0 source, which Microsoft published under the **MIT
License** (not the proprietary license an earlier reading assumed), is
used as a *reference* for the DOS functions' behavior -- the dispatch
table in `v4.0/src/DOS/DISPATCH.ASM` and the PSP layout in
`v4.0/src/INC/PDB.INC` -- not as code to copy.

## Alternatives considered

- **DOSBox.** Rejected: GPL-2.0, which the founding proposal forbids
  kernel-adjacent, and it assumes a POSIX host (SDL, C++, threads,
  signals) that ManiOS doesn't have. It also emulates a whole PC to run
  one program, when ManiOS already has the CPU, disk, graphics and
  timer.
- **FreeDOS.** Rejected: GPL-2.0.
- **MS-DOS binaries (`MSDOS.SYS`, `COMMAND.COM`).** Rejected as the
  *runtime*: they are 16-bit real-mode binaries that need an emulator
  to execute, so they don't remove the need for one. The MIT source is
  usable as a reference (and, later, could be booted inside the
  emulator -- a separate decision).
- **Booting MS-DOS instead of ManiOS.** Rejected: it isn't ManiOS.

## Consequences

- A DOS program's files are ManiOS's, through the same drive letters
  ManiDOS uses, so the two see the same files.
- Mode 13h is ManiOS's VGA mode 13h; the console is the terminal.
- The interpreter is 8086 only: a program that needs a 386, a DOS
  extender, EMS/XMS, sound, a mouse, or TSRs won't run. Those are
  future work, and each is a separate decision.
- ManiOS now has a second, larger userspace program, and a place for
  the 8086 interpreter to grow.
- ManiDOS's refusal message stays: ManiDOS still doesn't run DOS
  programs; `dosrun` does.

## References

- `docs/dosrun.md` -- what `dosrun` runs and how.
- `docs/dos.md`, ADR-0008 -- ManiDOS, which anticipated this.
- `docs/FOUNDING-PROPOSAL.md` §3.4 -- the licensing rule.
- `https://github.com/microsoft/MS-DOS` -- the MIT-licensed MS-DOS
  source, used as a reference.
