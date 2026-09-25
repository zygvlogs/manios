# ADR-0001: Boot on Multiboot first, native ManiOS bootloader later

**Status:** Proposed
**Date:** 2026-09-25

## Context
ManiOS needs a working i386 boot path to reach the M1 milestone ("ManiOS
boots on i386 and reaches a ZKT kernel console"). A bootloader is a
first-class architecture layer (layer 1) and "our own bootloader" is part
of the project's long-term identity, but real-mode BIOS bootloader code
(disk I/O via `int 0x13`, A20 handling, protected-mode transition) is
slow to get right and easy to confuse with genuine kernel bugs during
early bring-up.

## Decision
Target the **Multiboot 1** specification for the ZKT kernel binary in
the first milestones. GRUB (dev machines) or `qemu-system-i386 -kernel`
(CI/inner loop) perform real-mode setup and the jump into 32-bit
protected mode; ZKT's entry point begins already in protected mode with
a Multiboot info structure describing the memory map. A native ManiOS
bootloader (stage1 MBR + stage2) is scheduled as its own later milestone,
once the kernel above it is stable.

## Alternatives considered
- **Write a native ManiOS bootloader from day one.** Rejected for M1:
  maximizes "it's all ours" but couples two large sources of early bugs
  (bootloader real-mode code and kernel protected-mode code) together,
  slowing the very first milestone the founding prompt asks for.

## Consequences
- Faster path to a demonstrable, CI-testable M1 milestone.
- Introduces a (temporary, well-understood, removable) dependency on
  GRUB/Multiboot tooling for development and CI.
- Layer 1 (bootloader) is designed from the start as a replaceable
  component (§1.1, §4.1 of the founding proposal), so this is not
  expected to require rework of ZKT itself when the native bootloader
  milestone begins — only a new implementation of layer 1.

## References
- `docs/FOUNDING-PROPOSAL.md` §4 (i386 Boot Strategy), §11 (D2)
