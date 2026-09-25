# M14 — Own Boot Loader, Bootable ISO, Installer, Releases

**Status:** Achieved (2026-09-25). The native boot loader ADR-0001
deferred, and what it takes to publish ManiOS: decisions in
[ADR-0006](../adr/0006-native-boot-loader-and-installer.md); the user's
guide is [docs/install.md](../install.md). Version **0.14.0** (the
`VERSION` file) is the first release.

```
$ make                         # the kernel, and build/manios.iso
$ qemu-system-i386 -cpu 486 -m 32 -cdrom build/manios.iso -boot d -hda disk.img

ManiOS boot loader 0.14.0
command line:
Booting in 3 s; press a key to edit the command line.

loading......................
starting the kernel
ManiOS 0.14.0 / ZKT (ZygKernel Technology)
Milestone M1: kernel console reached.
bootmod: /dev/bootarea, 678 KiB
...
manios% install ata0 sysname=box
...
ManiOS 0.14.0 is installed on ata0. Remove the CD and restart the machine.
```

## What M14 delivers

| Piece | Files | Summary |
|---|---|---|
| Boot area format | `boot/bootarea.h` | Header (magic, parts, CRC-32, version, command line), MBR code, stage 2, kernel; 2 KiB-aligned |
| MBR | `boot/mbr.S` | 446 bytes: the 0xDA partition, LBA extensions or CHS, error messages on screen and COM1 |
| CD boot | `boot/cdboot.S` | El Torito no-emulation image (one 2 KiB sector); the boot area's place patched in by `mkiso.py` |
| Stage 2 | `boot/stage2_entry.S`, `boot/stage2.c` | Memory map (E820/E801/88h), the 3-second command-line prompt (keyboard or COM1), loading above 1 MiB (INT 15h AH=87h), A20, CRC check, ELF loading, Multiboot 1 handoff with the boot area as a module |
| Image tools | `tools/mkbootarea.py`, `tools/mkiso.py`, `tools/mkdisk.py` | The boot area; the hybrid ISO (reproducible); installer-style disk images |
| Kernel | `zkt/kernel/multiboot.c`, `bootmod.c`, `zkt/mm/pmm.c` | Multiboot modules as devices (`/dev/bootarea`); the frame bitmap kept off reserved regions; the version in the banner |
| Disk writes | `zkt/drivers/ata.c`, `device.c`, `mbr.c`, `zkt/fs/devfs.c` | ATA PIO writes with a cache flush; block devices writable at any offset through `/dev` |
| Installer | `userland/bin/install.c` | `install [-y] DISK [KEY=VALUE...]`: checks, asks, writes the boot area then the MBR, reads both back |
| `dd` | `userland/bin/dd.c` | `if= of= bs= count= skip= seek=`, "N+M records in/out" |
| Releases | `.github/workflows/release.yml`, `make release`, `docs/releases/` | A `v*` tag builds, runs `make test`, and publishes the ISO |
| Fixes | `userland/bin/cpud.c`, `cpu.c`, `desktop/apps/term.c` | A CPU job that can't start says why on the terminal; the desktop terminal scrolls back (PgUp/PgDn) |
| Tests | `tests/install_test.py`, `cluster_test.py`, `desktop_test.py` | See below |

## Design decisions

ADR-0006 has the main ones: no GRUB in the release; one boot area file
for CD, stick and disk; a Multiboot 1 handoff so the kernel is
unchanged; LBA or CHS; a raw 0xDA partition rather than a filesystem.
Further:

- **Stage 2 runs from 0x8000 and copies the boot area to 4 MiB** in
  32 KiB pieces through a buffer at 0x20000, with INT 15h AH=87h. The
  kernel is loaded from there to its physical addresses (1 MiB up), so
  the loader needs 6 MiB of memory at least, and says so otherwise.
- **The CRC covers everything but the header**, so the command line in
  the header can be changed (by `install`, or `mkdisk.py`) without
  recomputing it; stage 2 still checks the header's own fields.
- **The command-line prompt accepts the serial line too.** A headless
  machine (or the test) can edit the command line over COM1, as over
  the keyboard. Keys are read from INT 16h first, then from COM1.
- **The CD path trusts LBA.** SeaBIOS doesn't report the extensions
  (INT 13h AH=41h) for the CD drive it just booted from, though AH=42h
  reads from it work; with 2048-byte sectors stage 2 uses them without
  asking (found in testing: the first CD boot said "disk error"). A CD
  boot needs them anyway: El Torito no-emulation drives have no CHS.
- **`.bss` is cleared before anything is saved in it.** An early stage 2
  stored the drive number and sector size, then cleared `.bss` — and
  divided by a sector size of zero (a hang at "loading").
- **The module's name is streamed**, not copied: QEMU's `-initrd` passes
  long host paths, which an early 64-byte buffer cut off.
- **The frame bitmap moves past reserved regions.** With modules in the
  memory map as reserved regions, the PMM's bitmap, placed just after
  the kernel, could land on one; it now moves past any it overlaps.
- **`install` writes the MBR last.** An install interrupted while the
  boot area is being written leaves the disk's old MBR, never a new one
  pointing at a half-written boot area (and a half-overwritten old
  ManiOS fails the loader's CRC check rather than starting). Then
  everything is read back and compared.
- **`install` refuses what would surprise:** a partition instead of a
  disk, a disk too small, words that aren't `KEY=VALUE`, a
  `/dev/bootarea` that is missing or fails its CRC; and asks for "yes".
- **A job's start-up errors travel back** (the M13 known limit). `cpud`
  gives the job runner a pipe as standard error until the program
  starts; if the runner exits with 125, the first line it wrote becomes
  `N/wait`'s answer, `error WHY`, and `cpu` prints `cpu: HOST: WHY` and
  exits with 125. A program's own exit status 125 is left alone (the
  pipe is closed by then, and the runner's status is the program's).
- **The terminal keeps 256 lines** that scroll off the top in a ring.
  PgUp/PgDn move half a screen; while scrolled back, the top row shows
  "N lines back (PgDn)"; any other key, or output, comes back to the
  live screen.

## Verification

- **`tests/install_test.py`** (new, in `make test`, 9 checks), with no
  `-kernel`: SeaBIOS boots ManiOS's own loader.
  - The ISO read back by the test's own parser: primary volume
    descriptor, El Torito record, the boot catalog's checksum and entry,
    the boot image's patched pointer to `MANIOS.BIN`, the hybrid MBR's
    partition over the same bytes, the root directory's files; `xorriso`
    agrees; and rebuilding gives the same bytes.
  - The CD boots to the shell; the banner, the loader's steps and
    `/dev/bootarea` (its checksum equals the file's) are there.
  - The prompt edits the command line over serial (with Backspace), and
    from the keyboard (Escape clearing what the serial line typed).
  - `install`: usage; a partition, a missing disk, a word that isn't
    `KEY=VALUE`, a 1 MiB disk refused; "no" writes nothing (the MBR
    read back with `dd` is still zeros); "yes" installs. Then from
    outside: the MBR code, the partition entry, the boot area with the
    command line, its CRC. `dd` writes a block, and 200 bytes across a
    block boundary, leaving the bytes around them as they were.
  - The installed disk boots with its command line (`sysname=`, and
    `rc=` runs its script) and installs a clone on another disk, which
    boots with its own.
  - The ISO written to a disk (a USB stick) boots through the MBR.
  - A CHS-only build of the MBR and stage 2 boots.
  - An 8 MiB machine boots the CD.
  - The loader explains a 4 MiB machine, a damaged boot area, a bad
    header, and a disk without a boot partition.
- **`cluster_test.py`** gained a step (11 checks): the host, as a
  terminal, starts a job naming an export that doesn't exist, and
  `N/wait` answers `error cannot reach the terminal's namespace ...`; a
  real terminal whose `/dev/net` (in its own namespace) gives an address
  where nothing answers gets `cpu: udp!10.0.0.2: cannot reach the
  terminal's namespace (udp!10.0.0.99): ...`, and status 125.
- **`desktop_test.py`** gained a step: two listings of `/bin` in the
  terminal, PgUp (12 lines back, the note on the top row, read off the
  screen), PgUp again, PgDn twice back to the live screen, and typing
  returning to it. And a fix: the clock step read the date from the
  first screendump in which the time was complete, but a window's first
  frame reaches the screen in pieces (the compositor shows each write of
  its image as it arrives, and 200x70 pixels take four), so the date
  line could still be half drawn — which happened twice in a row late
  in this milestone (`????-??-?? ???`: only the one-row `-` and the
  space matched). It now waits for the date line itself.
- **GitHub Actions** runs the release workflow on `ubuntu-24.04` (the
  same QEMU 8.2.2 as the development machine): the cross toolchain
  builds in about 12 minutes and is cached; the build and all of
  `make test` take under 4 minutes there. A trial run by hand (without
  publishing) passed every suite first. **0.14.0 was published** by a
  run by hand with "publish" (this development environment may push
  only its branch, not tags), which created the tag `v0.14.0` on the
  tested commit. The published ISO was then downloaded, checked against
  `SHA256SUMS`, booted, installed on a blank disk, and the disk booted
  with its command line.
- `make test`: 190 checks (179 at M13), none failing — locally, and in
  GitHub Actions.
- **Negative controls** (temporary sabotage, reverted afterward):

  | Sabotage | Caught by |
  |---|---|
  | Stage 2 skips the CRC check | The damaged boot area is not refused |
  | `install` accepts a partition | The refusal check; then the test's input goes astray and the installed-disk boot fails too |
  | devfs writes a partial block's bytes at the block's start | **Not caught at first**: the only partial write began at a block's first byte, where the sabotage changes nothing. The test now writes 200 bytes at byte 1000 over a patterned disk; caught ("dd didn't write 200 bytes at byte 1000") |
  | `mkiso.py` doesn't patch the boot image's pointer | The ISO check (the boot image doesn't point at the boot area), and every CD boot |
  | `cpud` drops the reason | The new cluster step (`N/wait` said `exit 125`) |
  | The terminal ignores PgUp/PgDn | The new desktop step (never 12 lines back) |

## Numbers

- The ISO is 736 KiB (753,664 bytes); `MANIOS.BIN`, the boot area, is
  678 KiB — the stripped kernel (669 KiB, with the 550 KiB boot archive
  inside it), stage 2 (2,964 bytes), the MBR code, and the header.
- An installed disk uses its first 2 MiB: the MBR, the gap up to 1 MiB,
  and a 1 MiB partition.
- `install` is 16 KB stripped, `dd` 14 KB.
- Stage 2 fits between 0x8000 and 0x20000 with room to spare (the
  linker script refuses more); the MBR's code is 446 bytes by
  definition.

## Known limits

- **Real hardware is untested.** SeaBIOS is the only BIOS the tests
  use; the CHS path is exercised only through a build that skips the
  LBA check (`FORCE_CHS`).
- The installed system is the live system: no writable root
  filesystem; `install` takes the whole disk (one partition, the rest
  unpartitioned).
- BIOS only; no UEFI. PIO disks only; no ATAPI (the CD is read by the
  BIOS, not by ZKT).
- The prompt waits 3 seconds; there is no menu of configurations.
- A window's new frame can be seen half drawn for a moment: the
  compositor shows each write of a window's image as it arrives, not
  whole frames.
- Still no signals: a program can't be interrupted with ^C.
