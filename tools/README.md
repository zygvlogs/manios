# tools/

Development tooling that is not part of the shipped OS:

- `toolchain/build-i686-elf-toolchain.sh` — builds the pinned `i686-elf`
  cross-compiler (binutils 2.42 + GCC 13.2.0, C only, no host libc) into
  `toolchain/i686-elf/` (git-ignored). Run it with `make toolchain`. See
  [`docs/FOUNDING-PROPOSAL.md` §4.2](../docs/FOUNDING-PROPOSAL.md#42-toolchain).
- `qemu-run.sh` — boots the built kernel in `qemu-system-i386` on a 486
  CPU model, with the serial console attached to the terminal (`make run`).
- `mkbootarea.py` — packs the MBR boot code, stage 2 and the stripped
  kernel into a boot area (`build/manios.bin`, the ISO's `MANIOS.BIN`;
  format in `boot/bootarea.h`), with its CRC-32 and command line.
- `mkiso.py` — the bootable, hybrid ISO (`make iso`, `build/manios.iso`):
  ISO 9660 with an El Torito no-emulation boot image, and an MBR whose
  0xDA partition points at `MANIOS.BIN`, so the same file boots from a
  USB stick. Deterministic: a fixed date (`SOURCE_DATE_EPOCH`) and no
  host metadata.
- `mkdisk.py` — a raw hard disk image laid out as `install` lays one out
  (for tests, and to try an installed system without installing).
- `mkfont.py` — generates the desktop's font from `font.txt`.
- `mknotices.py` — collects the license header of every file under
  `third_party/` into `/boot/etc/notices` (and the release's
  `NOTICES.txt`); fails if an imported file lacks ManiOS's note.
- ADR-0006 and [`docs/install.md`](../docs/install.md) describe the
  boot path these build.
