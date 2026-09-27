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
- `ps2kbd.c` — PS/2 keyboard (US layout, arrows and F-keys; Alt, as a
  `ZKT_KEY_ALT` byte before the key, since M18), feeding the console,
  or `/dev/kbd` while a program has it open
- `ps2mouse.c` — PS/2 mouse on the 8042's second port
- `input.c` — devices `kbd` (raw keys) and `mouse` (text records) for
  the desktop ([M12 notes](../../docs/milestones/M12-desktop.md),
  [M18](../../docs/milestones/M18-manide.md))
- `rtc.c` — the CMOS clock, read at boot; device `time` (seconds since
  1970, UTC)
- `null.c` — device `null`
- `sysname.c` — device `sysname`: this machine's name (`sysname=`)
- `vga_text.c` — 80x25 text mode with hardware cursor (and a shadow
  copy while graphics own the display), and a subset of ANSI escape
  sequences: colours, cursor position, erasing (M18); device `vga`
- `ata.c` — ATA disks over PIO, LBA28 with a CHS fallback; devices
  `ata0`–`ata3` ([M6 notes](../../docs/milestones/M6-ata-storage.md));
  reads, and since M14 writes (followed by a cache flush)
- `mbr.c` — MBR primary partitions as block devices (`ata0p1`…)
- `pci.c` — PCI configuration space and bus scan
- `fb.c` — the framebuffer: devices `fb` and `fbctl`, Bochs VBE modes
  ([M11 notes](../../docs/milestones/M11-graphics.md)), on QEMU's and
  Bochs's adapter, VirtualBox's and VMware's SVGA II (0.14.1)
- `vga_hw.c` — VGA registers: saving/restoring text mode, mode 13h
- `ne2000.c` — NE2000-compatible ISA Ethernet (DP8390), registered with
  the network stack as interface `ne0`
  ([M10 notes](../../docs/milestones/M10-networking-zrp.md))
- `pcnet.c` — AMD PCnet PCI Ethernet (PCnet-PCI II, PCnet-FAST III:
  VirtualBox's default card), interface `pcn0`; bus-mastering, with
  descriptor rings (M15)
- `e1000.c` — Intel 8254x PRO/1000 (82540EM and relatives: VirtualBox's
  other cards, QEMU's e1000), interface `em0`; registers in memory,
  descriptor rings (M15)

M20 ([notes](../../docs/milestones/M20-drivers.md), [guide](../../docs/drivers.md)):

- `isadma.c` — the PC's two 8237 ISA DMA controllers (channels 0-3 and
  5-7), for the floppy and the Sound Blaster; DMA memory itself comes
  from `zkt/mm/dma.c`
- `floppy.c` — floppy drives on the 82077/765 controller: `fd0`, `fd1`;
  the disk's format found by trying data rates, ISA DMA, a cache of the
  last three cylinders
- `atapi.c` — CD/DVD drives, whatever carries their packets: SCSI MMC
  commands, REQUEST SENSE, discs coming and going; `cd0`…
- `ata.c` — also carries ATAPI packets over the IDE ports
- `ahci.c` — SATA on AHCI controllers: disks `sata0`… (DMA, 48-bit LBA)
  and CD drives
- `virtio.c` — legacy virtio over PCI: virtqueues; `virtio_blk.c` —
  disks `vd0`…; `virtio_net.c` — interface `vio0`
- `rtl8139.c` — RealTek RTL8139, interface `rl0`
- `tulip.c` — DEC 21143/21140/21041, interface `dc0`: serial ROM, setup
  frame
- `pcspeaker.c` — the PC speaker, device `beep`
- `audio.c` — device `audio`: the ring of fragments a sound card plays;
  `sb16.c` — Sound Blaster 16; `ac97.c` — Intel ICH AC'97
- `adlib.c` — the OPL2 FM synthesizer, device `opl`
- `uart.c` — COM2-COM4, devices `com2`… and `com2ctl`…
- `lpt.c` — the parallel port, device `lpt1`
- `nvram.c` — the CMOS memory, device `nvram` (`rtc.c` has the CMOS
  accessors)
- `acpi.c` — ACPI tables (RSDP, RSDT, FADT, the DSDT's `\_S5`): device
  `power`, off and reboot

The console device `cons` lives in `zkt/kernel/kconsole.c`. See
[`docs/FOUNDING-PROPOSAL.md` §2.7](../../docs/FOUNDING-PROPOSAL.md#27-driver-framework).
