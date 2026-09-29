# ManiOS's drivers

Every driver in ManiOS is its own code, BSD-licensed like the rest,
written from the hardware's public documentation: data sheets,
programming guides and specifications (each driver's first comment says
which). None is taken from another system. All of them are tested in
QEMU, on its models of the hardware, by `make test`; none has been tried
on real machines yet, and reports are welcome.

`ls /dev` shows the devices a machine has, and the boot log (the boot
option `verbose=1` shows it) what each driver found.

## Disks

| Hardware | Devices | Driver | Notes |
|---|---|---|---|
| IDE hard disks (PIO) | `ata0`–`ata3`, partitions `ata0p1`… | `ata.c` | LBA28, or CHS on old disks; reads and writes |
| IDE CD/DVD drives (ATAPI) | `cd0`, `cd1`… | `ata.c`, `atapi.c` | Read only; discs changed while running are noticed |
| SATA disks (AHCI controllers: Intel ICH/PCH and compatibles) | `sata0`…, `sata0p1`… | `ahci.c` | 48-bit LBA, DMA; reads and writes |
| SATA CD/DVD drives | `cd0`… | `ahci.c`, `atapi.c` | As on IDE |
| Floppy drives (82077/765 controller) | `fd0`, `fd1` | `floppy.c` | 360 KB, 720 KB, 1.2 MB, 1.44 MB, 2.88 MB; the disk's format found by itself; ISA DMA; reads and writes |
| Virtio disks (QEMU, KVM, other hypervisors) | `vd0`…, `vd0p1`… | `virtio_blk.c` | Legacy/transitional virtio PCI |

A disk's file system appears under `/n`, named after its device:

- **FAT12/16** volumes (hard disk partitions, whole disks, floppies):
  `/n/ata0p1`, `/n/sata0p1`, `/n/vd0p1`, `/n/fd0`;
- **ISO 9660** (CDs, DVDs, `.iso` images): `/n/cd0`, with the long names
  of **Rock Ridge** or **Joliet** when the disc has them, else the ISO
  names in lower case (`readme.txt`, not `README.TXT;1`). An ISO image
  written to a disk or a USB stick is found there too (`/n/ata0`).

A CD drive's `/n/cd0` follows the disc in the drive: empty when there is
none, and the new disc's files after a change (a disc taken out while
its files are open makes them fail with an I/O error). A floppy's FAT is
mounted if a disk is in at boot.

```
fd0: 3.5" 1.44 MB drive, 1.44 MB disk, on the 82077 controller
cd0: QEMU DVD-ROM, ATAPI, disc of 2 MiB
sata0: QEMU HARDDISK, 16 MiB, on AHCI port 0
cd0: ISO 9660, Rock Ridge, 2 MiB, TEST_rr
cd0: the disc in the drive, mounted at /n/cd0
fd0: FAT12, 1440 KiB, mounted at /n/fd0
sata0p1: FAT12, 15 MiB, mounted at /n/sata0p1
```

In ManiDOS ([docs/dos.md](dos.md)) the floppy is B:, the hard disks'
volumes C: on, then the CD drives.

## Network cards

| Hardware | Interface | Driver |
|---|---|---|
| NE2000 and compatibles (ISA) | `ne0` | `ne2000.c` |
| AMD PCnet-PCI II, PCnet-FAST III (VirtualBox's default) | `pcn0` | `pcnet.c` |
| Intel PRO/1000 8254x: 82540EM (QEMU's e1000, VirtualBox's MT Desktop), 82543GC, 82545EM… | `em0` | `e1000.c` |
| RealTek RTL8139 (8139C/D) | `rl0` | `rtl8139.c` |
| Virtio network cards | `vio0` | `virtio_net.c` |
| DEC 21143 "Tulip" (and 21140, 21041) | `dc0` | `tulip.c` |

ManiOS gives an address to the first card it finds; `cat /dev/net`
(or `net` at `ZKT>`) shows the cards and their counters. Without `ip=` on the boot line
the card asks a DHCP server for an address.

## Sound

| Hardware | Device | Driver |
|---|---|---|
| The PC speaker | `/dev/beep` | `pcspeaker.c` |
| Creative Sound Blaster 16 (0x220, IRQ 5, DMA 5) | `/dev/audio` | `sb16.c` |
| Intel ICH AC'97 (VirtualBox's default sound card) | `/dev/audio` | `ac97.c` |
| Yamaha OPL2 FM synthesizer (AdLib, and the Sound Blaster's) at 0x388 | `/dev/opl` | `adlib.c` |

- **`/dev/beep`** takes lines `FREQUENCY MILLISECONDS`, and `beep` writes
  them: `beep` (440 Hz for 200 ms), `beep 880 100 0 50 880 100`.
- **`/dev/audio`** takes sound as 16-bit signed little-endian stereo at
  44,100 Hz; reading it says which card plays it and how many times the
  writer fell behind. `play FILE.WAV` plays WAV files (PCM, 8 or 16 bits,
  mono or stereo, any rate: converted as they play); `play -t 440 500`
  plays a tone. The first sound card found has `/dev/audio`.
- **`/dev/opl`** takes pairs of bytes, a register and its value; `fm`
  plays notes on it (`fm` alone plays a scale; `fm 440 500`).

## Other devices

| Hardware | Device | Driver |
|---|---|---|
| Serial ports COM2–COM4 (8250/16450/16550) | `com2`… and `com2ctl`… | `uart.c` |
| The parallel port (LPT1, 0x378) | `lpt1` | `lpt.c` |
| The CMOS memory beside the clock | `nvram` | `nvram.c` |
| ACPI power control | `power` | `acpi.c` |

- **COM2–COM4** read and write bytes (COM1 is the console,
  `serial.c`). `echo b115200 > /dev/com2ctl` sets the speed (9600 at
  first); reading `com2ctl` gives the speed and the modem lines.
- **`lpt1`** sends what is written to the printer; it fails if the
  printer stays busy for 10 seconds or is out of paper.
- **`nvram`** is the 114 bytes of CMOS memory after the clock's
  registers, as a file (`dd` reads and writes it); writing the BIOS's
  summed bytes keeps its checksum right.
- **`power`**: `poweroff` (ACPI's sleep state S5, found in the firmware's
  tables) and `reboot` (ACPI's reset register, or the keyboard
  controller); `cat /dev/power` says how.

## Graphics on real hardware

`manide` and `gfxdemo` need a display adapter with the Bochs VBE
registers (QEMU's, VirtualBox's, VMware's SVGA II) -- emulators, so
far, not real cards. `gfx=auto` on the boot line has the boot loader
itself ask the display's own VESA BIOS for a linear framebuffer before
ManiOS starts (M21, [docs/install.md](install.md#gfxauto-a-real-adapters-own-linear-framebuffer)):

```
graphics: VESA 1024x768 found
```

which `fb_init()` then uses if there is no Bochs VBE adapter to prefer.
Left out, nothing about a boot changes.

## Also

The keyboard and mouse (PS/2), the VGA text console, the framebuffer
(Bochs VBE, VirtualBox's and VMware's SVGA II, VGA mode 13h, and since
M21 a real adapter's own VESA linear framebuffer), the clock (CMOS) and
COM1 are the older drivers; `zkt/drivers/README.md` lists every file.

## In emulators

What QEMU and VirtualBox offer that ManiOS drives. The tests use QEMU
(8.2); VirtualBox's column is from its documentation, and ManiOS hasn't
been tried on VirtualBox's models of these devices yet.

| | QEMU | VirtualBox |
|---|---|---|
| Disks | `-hda`, `-cdrom`, `-fda`; `-device ahci` + `ide-hd`/`ide-cd`; `-M q35` (SATA); `virtio-blk-pci` | IDE, SATA (AHCI), floppy |
| Network | `ne2k_isa`, `pcnet`, `e1000`, `rtl8139`, `virtio-net-pci`, `tulip` | PCnet-PCI II/FAST III, PRO/1000 MT Desktop/T Server/MT Server, Paravirtualized Network (virtio) |
| Sound | `-device sb16`, `AC97`, `adlib`, and `-machine pcspk-audiodev=…` (with an `-audiodev`) | ICH AC97, SoundBlaster 16 |

Not supported yet: USB, Intel HD Audio, NVMe, SCSI adapters, more than
one network card at a time, and writing to CDs.
