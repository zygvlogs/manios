# M20 — Drivers: disks, CDs, network cards, sound, ports, power

**Status:** Achieved (2026-09-27). Released as **0.20.0**. Asked for as
"we need to write drivers, write all opensourced, obscure ... drivers
that work". Sixteen drivers, from the SATA disks and virtio devices of
today's machines and hypervisors to the floppy controller, the Sound
Blaster 16, the AdLib and the DEC Tulip. Each is ManiOS's own code
(BSD 2-Clause), written from the hardware's public documentation --
data sheets, programming guides, specifications -- and none ships
without a test that drives QEMU's model of the hardware. The guide is
[docs/drivers.md](../drivers.md).

## What M20 delivers

| Piece | Files | Summary |
|---|---|---|
| DMA memory | `zkt/mm/dma.c`, `pmm.c`, `zkt/drivers/isadma.c` | Physically contiguous frames below 16 MiB (where the kernel's boot mapping reaches them and ISA DMA can), within one 64 KiB block for ISA DMA; the two 8237 DMA controllers |
| Floppy drives | `floppy.c` | The 82077/765 controller: `fd0`, `fd1`; the disk's format found by trying each data rate the drive takes; ISA DMA, a cylinder at a time; the last three cylinders kept; write protection; a new disk noticed |
| CD/DVD drives | `atapi.c`, `ata.c`, `ahci.c` | SCSI MMC packets over the IDE ports or an AHCI port: `cd0`…; discs coming, going and changing |
| ISO 9660 | `zkt/fs/iso9660.c` | CDs and `.iso` images, with Rock Ridge or Joliet names; a CD drive's `/n/cd0` follows its disc |
| SATA | `ahci.c` | AHCI controllers: disks `sata0`… by DMA with 48-bit LBA, and CD drives |
| Virtio | `virtio.c`, `virtio_blk.c`, `virtio_net.c` | Legacy virtio PCI: disks `vd0`…, network card `vio0` |
| Network cards | `rtl8139.c`, `tulip.c` | RealTek 8139 (`rl0`); DEC 21143/21140/21041 (`dc0`) |
| Sound | `pcspeaker.c`, `audio.c`, `sb16.c`, `ac97.c`, `adlib.c` | `/dev/beep`; `/dev/audio` (44.1 kHz 16-bit stereo) on a Sound Blaster 16 or AC'97; `/dev/opl`, the OPL2 FM synthesizer |
| Ports | `uart.c`, `lpt.c` | COM2-COM4 with `comNctl` for the speed; the parallel port `lpt1` |
| The rest | `nvram.c`, `acpi.c` | `/dev/nvram` (the CMOS memory); `/dev/power`: ACPI off and reboot |
| Programs | `userland/bin/` | `beep`, `play` (WAV files and tones), `fm`, `poweroff`, `reboot` |
| ManiDOS | `userland/dos/drives.c` | B: the floppy; C: on the hard disks (IDE, SATA, virtio); then the CD drives; `VOL` gives a disc's name |
| The installer | `userland/bin/install.c` | Installs on SATA and virtio disks too (`install sata0`); refuses any partition, not only `ata`'s |

## Design decisions

**From the documents, tested against models.** Every driver's first
comment names what it was written from: Intel's 82077AA and 8237A data
sheets, the AHCI 1.3 specification, T10's MMC and SPC, ECMA-119 and the
Rock Ridge and Joliet specifications, the virtio specification's legacy
interface, RealTek's RTL8139 data sheet, Digital's 21143 manual,
Creative's Sound Blaster programming guide, Intel's AC'97 programmer's
reference, Yamaha's YM3812 manual, National's PC16550D data sheet, IEEE
1284, the ACPI specification. QEMU's models of the same hardware are
the test bench; where a model differs from the documents, the driver
follows the documents and the difference is written down (the Tulip,
below).

**DMA memory below 16 MiB.** The kernel keeps physical memory below
16 MiB mapped from boot, so a buffer there has a kernel address and a
physical one at no cost -- and it is also all that ISA DMA reaches.
`pmm_alloc_contiguous` finds runs of free frames under a limit, not
crossing a boundary if asked (64 KiB for ISA DMA). Disks move data
through one bounce buffer per controller rather than DMA to and from
callers' buffers, which the heap doesn't keep physically contiguous: a
copy, for simplicity, one request at a time.

**Polled where it is simple, interrupts where it matters.** AHCI and
virtio-blk issue one command and wait for it, yielding the processor
meanwhile; the floppy controller, the network cards and the sound cards
interrupt, as their work comes when it comes. Legacy virtio asks the
device not to interrupt for what is polled, so that an unanswered
interrupt line can't jam.

**The floppy finds its disk's format.** A 1.44 MB drive may hold a
720 KB disk, which is read at another data rate with 9 sectors a track
instead of 18. The driver tries the drive's formats in turn -- the data
rate, then the last sector of the first cylinder -- and the first that
reads is the disk's. Reads fetch a whole cylinder (both heads, one
command), and the last three are kept: file systems read a block at a
time, and FAT goes back to the FAT, on cylinder 0, between clusters.

**CDs: a layer for the commands, transports for the packets.** A CD
drive speaks SCSI whether it sits on the IDE ports or on a SATA port;
`atapi.c` knows the commands (and that after an error a drive must be
asked REQUEST SENSE, or it answers everything with that error), and
`ata.c` and `ahci.c` each carry the 12-byte packets. A drive reports a
new disc as UNIT ATTENTION; the driver counts the change, and the ISO
9660 root mounted at `/n/cd0` -- asked on every look whether the disc
changed -- mounts the new one. Files open on the old disc fail rather
than read the new one.

**ISO 9660 names.** Rock Ridge (Unix names in each record's System Use
area) is preferred, then Joliet (a second directory tree of UCS-2
names, shown as UTF-8), then the ISO names, which are upper case with a
version number: shown in lower case without it, and found in any case,
as FAT names are.

**One sound format.** `/dev/audio` takes 16-bit signed stereo at
44,100 Hz, what CDs hold; the programs convert (`play` does WAV files of
any rate, 8 or 16 bits, mono or stereo). The audio layer gives a sound
card a ring of four fragments of 23 ms, which the card plays round and
round from the moment the first is written, interrupting after each; a
fragment played is silenced, so one the writer didn't fill in time plays
as silence rather than as the last sound again. The Sound Blaster plays
the ring by auto-initialised 16-bit ISA DMA, AC'97 by a list of 32
buffer descriptors, four times round the ring, whose last valid index
is kept just behind the current one.

**ACPI without AML.** Turning a machine off needs the SLP_TYP values of
the sleep state S5, which firmware gives in the DSDT's `\_S5` object --
AML byte code. ManiOS has no AML interpreter, so `acpi.c` finds the
object by its encoding (NameOp, "_S5_", PackageOp) and reads the
package's first two elements, as other small systems do. Rebooting uses
the FADT's reset register when the firmware has one, then the keyboard
controller's reset line, then a triple fault.

**Text where it is enough.** `/dev/beep` takes lines "FREQUENCY
MILLISECONDS", `comNctl` takes "b9600" (Plan 9's serial control
files), `/dev/power` takes "off" and "reboot", and each says what it
is when read.

## Bugs found on the way

- The floppy's first cache held one cylinder, and reading a 300 KB file
  took 35 seconds: FAT went to the FAT on cylinder 0 between clusters,
  and every cluster read the file's cylinder again. Three cylinders,
  least recently used first: 1 second.
- The RTL8139 took a 2,000-byte frame (the network test sends one) for a
  sign of a corrupt ring, and "restarted" receiving by turning it off
  and on, which doesn't move the card's write pointer: after that it
  read garbage. Frames too long for Ethernet are now skipped; a header
  that makes no sense resets the card.
- A new disc was never seen: after a disc change, QEMU's drive answered
  every command with UNIT ATTENTION, and kept on -- as SCSI says a drive
  should until REQUEST SENSE fetches the error. `atapi.c` now always
  asks.
- ManiDOS started on the (empty) CD drive of a machine without a hard
  disk -- C:, since CDs have letters now. It starts on the first hard
  disk, or A:.
- The AdLib's notes measured an octave high. The driver was right: the
  instrument's modulator made harmonics that the test's zero-crossing
  count took for the pitch. `fm` plays a pure tone now, and the test
  measures it exactly.
- QEMU's tulip model, given a frame under 14 bytes or over its 2 KiB
  buffer, asks QEMU to hold on to it instead of dropping it, and then
  receives nothing more, whatever the driver does (both frames on their
  own stall it). A real 21143 drops such frames; the network test leaves
  those two out for this card, and says why.

## Verification

- **`tests/driver_test.py`** (new, in `make test`): 33 steps over
  twelve QEMU machines -- seven for the disks (among them q35, whose CD
  drive is on SATA, and four to install ManiOS on a SATA and a virtio
  disk and boot it), three for sound and two for the rest; see
  [tests/README.md](../../tests/README.md). Sound is checked by
  recording it (QEMU's `wav` audio backend) and measuring each tone:
  440 Hz from the PC speaker for 600 ms, 1000 Hz and 660 Hz from the
  Sound Blaster, 523 Hz and 262 Hz from the AdLib, 440 Hz from AC'97,
  and three WAV files (880 Hz at 22,050 Hz mono, 330 Hz at 11,025 Hz
  8-bit stereo, 1200 Hz at 48,000 Hz) as `play` converts them.
- **`tests/net_test.py`**: its whole suite (ARP, ICMP, UDP, malformed
  frames, floods, the ZRP server and client, DHCP) runs on the three new
  cards too -- seven cards; DHCP from QEMU's server on all six PCI ones.
- **`tests/console_test.py`**, **`dos_test.py`**: QEMU's PC has a CD
  drive, a floppy drive, a PC speaker, a parallel port, CMOS and ACPI,
  so their devices are in every machine's list now, and the CD drive is
  a ManiDOS drive.
- The memory manager's self-test checks `pmm_alloc_contiguous`.
- `make test`: 499 checks (397 at 0.19), none failing.
- **Negative controls**, each caught:

  | Sabotage | Caught by |
  |---|---|
  | The floppy's cylinder cache not updated by a write | the floppy read after a write (a first try, writing a file read from the floppy itself, pushed the cylinders out of the cache and caught nothing: the test now writes from the boot archive) |
  | Only the drive's own format tried | the 720 KB disk |
  | Write protection reported as an I/O error | the write-protected floppy |
  | Rock Ridge not looked for | the Rock Ridge disc |
  | The Joliet descriptor not recognised | the Joliet disc |
  | ISO names keep their ";1" | the plain ISO disc |
  | No REQUEST SENSE after an error | the discs changed (IDE) |
  | AHCI: the sense key not taken from PxTFD | the disc changed (SATA, q35) |
  | virtio-blk's read-only feature ignored | the read-only virtio disk |
  | CD drives lettered before hard disks | ManiDOS's DRIVES |
  | The installer's partition check only for `ata` names | `install sata0p1` |
  | The PC speaker's divisor from half the clock | the speaker's 440 Hz (it played 878) |
  | A 16-bit ISA DMA channel's address in bytes, not words | the Sound Blaster's tones |
  | The Sound Blaster's rate bytes swapped | the Sound Blaster's tones |
  | AC'97's last valid index not moved on | the WAV files (the channel stops after 32 fragments) |
  | The OPL2's data written before its register's address | the AdLib's notes |
  | `play`: mono samples not doubled | the mono WAV file |
  | COM2-4: no receive interrupts | COM2's read |
  | LPT1: STROBE never pulsed | the printer's file |
  | NVRAM offsets from the clock's registers | `/dev/nvram` |
  | ACPI: `\_S5`'s element count taken for SLP_TYPa | poweroff |
  | RTL8139: CAPR without the card's 16-byte offset | `net_test` over `rl0` |
  | virtio-net: receive buffers not given back | `net_test` over `vio0` |
  | Tulip: no setup frame | `net_test` over `dc0` |

## Known limits

- No USB, Intel HD Audio, NVMe or SCSI adapters yet.
- One AHCI controller, used with one command at a time; the disks'
  data goes through a bounce buffer.
- A floppy's FAT is mounted if a disk is in at boot; a disk changed
  later is read correctly by `/dev/fd0` but not remounted.
- ISO 9660: files of one extent (up to 4 GiB); no Rock Ridge symbolic
  links; directories Rock Ridge relocated (deeper than eight levels on
  some discs) are not shown. CDs are read only; audio CDs aren't read.
- `/dev/audio` plays one format; no mixer or volume control; no
  recording.
- ACPI: power off and reboot only; no power button, sleep or battery.
- The network stack uses the first card it finds.
