# zkt/drivers/

The driver framework and the in-tree PC drivers. Design and
verification: [M5 notes](../../docs/milestones/M5-driver-framework.md).
Drivers run in the kernel for now, but they are written to the
framework interface so that individual drivers can later move to
userspace servers (ADR-0002).

- `device.c` / `device.h` — the registry: named character and block
  devices, each with an operations table
- `drivers.c` — starts the drivers and registers their devices at boot
- `serial.c` — COM1: polled early/panic output; IRQ-driven RX and
  buffered TX; device `com1`
- `ps2kbd.c` — PS/2 keyboard (US layout), feeding the console
- `vga_text.c` — 80x25 text mode with hardware cursor; device `vga`
- `ata.c` — ATA disks over PIO, LBA28 with a CHS fallback; devices
  `ata0`–`ata3` ([M6 notes](../../docs/milestones/M6-ata-storage.md))
- `mbr.c` — MBR primary partitions as block devices (`ata0p1`…)

The console device `cons` lives in `zkt/kernel/kconsole.c`. See
[`docs/FOUNDING-PROPOSAL.md` §2.7](../../docs/FOUNDING-PROPOSAL.md#27-driver-framework).
