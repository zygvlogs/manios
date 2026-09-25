# boot/

ManiOS's own boot loader (M14,
[ADR-0006](../docs/adr/0006-native-boot-loader-and-installer.md)). It
loads ZKT as a Multiboot 1 loader would, so the kernel also still boots
under `qemu-system-i386 -kernel` and GRUB
([ADR-0001](../docs/adr/0001-bootloader-strategy.md)).

- `bootarea.h` — the boot area: the file (`MANIOS.BIN`) holding a
  header, the MBR code, stage 2 and the kernel; shared with the kernel
  (`/dev/bootarea`), `install` and the tools. Also the loader's memory
  layout.
- `mbr.S` — the 446-byte master boot record of hard disks and USB
  sticks: finds the partition of type 0xDA and loads stage 2 from it
  (LBA extensions, or CHS).
- `cdboot.S` — the El Torito "no emulation" boot image of the ISO: loads
  stage 2 from the boot area, whose location `tools/mkiso.py` patches in.
- `stage2_entry.S` — stage 2 in real mode: memory map, the command-line
  prompt (keyboard or COM1), loading the boot area above 1 MiB, A20,
  protected mode.
- `stage2.c` — stage 2 in protected mode: the CRC check, loading the
  kernel's ELF segments, the Multiboot information, the jump.
- `sector.ld`, `stage2.ld` — flat binaries at 0x7C00 and 0x8000.

Built with `-DFORCE_CHS`, the MBR and stage 2 use CHS reads even when
the BIOS has LBA extensions (`build/manios-chs.bin`, for
`tests/install_test.py`).
