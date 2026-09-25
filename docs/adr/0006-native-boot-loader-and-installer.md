# ADR-0006: A native boot loader, a hybrid ISO, and an installer

**Status:** Accepted
**Date:** 2026-09-25
**Supersedes:** the "native bootloader later" part of
[ADR-0001](0001-bootloader-strategy.md); the kernel stays a Multiboot 1
kernel.

## Context
Until M13, ManiOS booted only through `qemu-system-i386 -kernel` or
GRUB, as ADR-0001 planned for the early milestones. To publish ManiOS
as something people can run -- a CD image, a USB stick, a hard disk
installation -- it needs a boot path of its own. There were two ways to
make a bootable ISO:

- **Ship GRUB** (`grub-mkrescue`). Quick, but GRUB is GPLv3: every
  release would carry GPL binaries with their source-offer obligations,
  and ManiOS's release artifacts would depend on a Linux-world tool
  chain at build time. The founding proposal (§9.3) keeps GPL code out
  of ManiOS's tree; shipping GRUB in every release would bring it back
  through the release itself. (GCC and binutils build ManiOS but are not
  part of what is shipped.)
- **Write the loader ADR-0001 deferred.** The kernel above it is now
  stable (M1-M13, with their tests), which was ADR-0001's condition for
  starting it.

## Decision
ManiOS gets its own boot loader, image format and installer:

1. **The boot area** (`boot/bootarea.h`): one file, `MANIOS.BIN`,
   holding a 2 KiB header (magic `ZKTBOOT1`, offsets and lengths, a
   CRC-32 of everything after the header, the version, and a 256-byte
   kernel command line), the MBR boot code, stage 2 and the stripped
   kernel, each 2 KiB-aligned. The same file is what the CD boots, what
   a USB stick boots, and what `install` copies to a disk.
2. **Three boot entries, one stage 2.** `mbr.S` (446 bytes; hard disks
   and USB sticks) finds the first partition of type **0xDA** and loads
   stage 2 from the boot area at its start. `cdboot.S` (an El Torito
   "no emulation" boot image) loads it from the ISO, where `mkiso.py`
   patched the boot area's location into it. Stage 2 is the same binary
   in both cases.
3. **Stage 2** (`stage2_entry.S` in real mode, `stage2.c` in protected
   mode) reads the memory map (E820, then E801, then AH=88h), shows the
   command line and waits 3 seconds for a key (keyboard or COM1) to
   edit it, copies the boot area above 1 MiB with INT 15h AH=87h (the
   one BIOS way to reach high memory from real mode, available on 286s
   and up), enables A20, checks the CRC, loads the kernel's ELF segments
   and starts it **as a Multiboot 1 loader would** (EAX = 0x2BADB002,
   EBX = the info block with the memory map, command line, loader name
   and the boot area as a module). The kernel needs no second entry
   point, and still boots under QEMU `-kernel` and GRUB.
4. **Old BIOSes.** Disk reads use the LBA extensions when the BIOS has
   them and CHS otherwise, in both the MBR and stage 2. Stage 2 fits in
   the first 640 KiB and needs only a 386. A test build (`FORCE_CHS`)
   exercises the CHS path, since QEMU's BIOS always has the extensions.
5. **The ISO is hybrid** (`tools/mkiso.py`): ISO 9660 with El Torito for
   CDs, and an MBR in its first sector whose partition of type 0xDA
   points at `MANIOS.BIN` inside the ISO, so the same file written raw
   to a USB stick boots through `mbr.S`. It is built reproducibly
   (`SOURCE_DATE_EPOCH`).
6. **The installer** (`/bin/install`) runs on ManiOS itself: the kernel
   exposes the boot area it was started from as `/dev/bootarea`, and
   `install ata0 sysname=... ip=...` writes it to the disk at 1 MiB,
   then the MBR (boot code and one active 0xDA partition, size rounded
   up to a MiB), reads everything back to check it, and asks for "yes"
   before writing anything. The words after the disk become the
   installed system's command line. The kernel gained ATA PIO writes
   (with a cache flush) and block-device writes through `/dev` for this.

## Alternatives considered
- **GRUB on the ISO.** Rejected above (licensing, and it would make the
  release depend on GRUB's tooling).
- **Floppy-emulation El Torito.** Simpler loader (a plain 512-byte boot
  sector), but limited to 1.44/2.88 MB images and slower BIOS paths;
  no-emulation is what every BIOS from the late 1990s supports.
- **A filesystem for the installed system** (e.g. FAT with the kernel
  as a file). Needs a FAT reader in real mode or in stage 2; the raw
  partition holding the boot area is enough until ManiOS has a root
  filesystem worth installing. The partition is ordinary MBR, so a
  later installer can add more partitions after it.
- **Keeping the loader in the kernel image** (a "self-booting" kernel).
  Would tie the kernel's layout to the BIOS; the Multiboot boundary
  keeps them independent, as ADR-0001 wanted.

## Consequences
- ManiOS releases are one ISO, built only from ManiOS's own sources and
  its own tools; no GPL component is shipped.
- The loader is more real-mode code to keep working. It is covered by
  `tests/install_test.py`: CD, hard disk, USB-stick, CHS and 8 MiB
  boots, the command-line prompt from both keyboard and serial, and the
  loader's error messages (too little memory, damaged boot area, bad
  header, no partition).
- Real hardware remains untested; QEMU's SeaBIOS is the only BIOS the
  tests use. Reports from old machines are wanted.
- The installed system is the live system: an installed disk boots the
  same kernel and boot archive as the CD, with its own command line.
  There is no writable root filesystem yet.

## References
- `docs/FOUNDING-PROPOSAL.md` §4 (i386 Boot Strategy), §9.3
- `docs/install.md` (how to use it), `docs/milestones/M14-installer-release.md`
- El Torito Bootable CD-ROM Format Specification 1.0 (Phoenix/IBM, 1995)
- Multiboot Specification 0.6.96
