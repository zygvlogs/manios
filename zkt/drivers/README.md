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
- `ps2kbd.c` — PS/2 keyboard (US layout, arrows and F-keys), feeding
  the console, or `/dev/kbd` while a program has it open
- `ps2mouse.c` — PS/2 mouse on the 8042's second port
- `input.c` — devices `kbd` (raw keys) and `mouse` (text records) for
  the desktop ([M12 notes](../../docs/milestones/M12-desktop.md))
- `rtc.c` — the CMOS clock, read at boot; device `time` (seconds since
  1970, UTC)
- `null.c` — device `null`
- `sysname.c` — device `sysname`: this machine's name (`sysname=`)
- `vga_text.c` — 80x25 text mode with hardware cursor (and a shadow
  copy while graphics own the display); device `vga`
- `ata.c` — ATA disks over PIO, LBA28 with a CHS fallback; devices
  `ata0`–`ata3` ([M6 notes](../../docs/milestones/M6-ata-storage.md))
- `mbr.c` — MBR primary partitions as block devices (`ata0p1`…)
- `pci.c` — PCI configuration space and bus scan
- `fb.c` — the framebuffer: devices `fb` and `fbctl`, Bochs VBE modes
  ([M11 notes](../../docs/milestones/M11-graphics.md))
- `vga_hw.c` — VGA registers: saving/restoring text mode, mode 13h
- `ne2000.c` — NE2000-compatible ISA Ethernet (DP8390), registered with
  the network stack as interface `ne0`
  ([M10 notes](../../docs/milestones/M10-networking-zrp.md))

The console device `cons` lives in `zkt/kernel/kconsole.c`. See
[`docs/FOUNDING-PROPOSAL.md` §2.7](../../docs/FOUNDING-PROPOSAL.md#27-driver-framework).
