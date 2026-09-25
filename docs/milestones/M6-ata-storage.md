# M6 — ATA Storage

**Status:** Achieved (2026-09-25). Roadmap M6 ("ATA/IDE PIO block
driver"), plus MBR partitions.

## What M6 delivers

| Piece | Files | Summary |
|---|---|---|
| ATA driver | `zkt/drivers/ata.c` | PIO reads on both legacy IDE channels, master and slave; devices `ata0`–`ata3` |
| Partitions | `zkt/drivers/mbr.c` | MBR primary partitions as block devices `ata0p1`… |
| Monitor | `read DEVICE BLOCK` | Hex dump of one block |
| Port I/O | `zkt/arch/i386/io.h` | `inw` / `outw` |

## Design decisions

- **Polled PIO with the drive's interrupt disabled (`nIEN`).** This
  works on every IDE controller back to the ISA era. It holds a
  per-channel *mutex*, not interrupts-off, so while a thread waits on
  a slow disk, other threads keep being scheduled and preempted.
  Interrupt-driven and DMA transfers are later optimizations.
- **Detection by IDENTIFY.** A status of `0xFF` means a floating bus
  (no controller), `0x00` means no drive, and a signature in the LBA
  registers means ATAPI or SATA, which is skipped. Every wait has a
  5 s deadline, so an absent or wedged device can't hang boot.
- **CHS fallback for pre-LBA drives.** Drives from the 386/486 era,
  ManiOS's first target, may not support LBA. The driver uses CHS when
  IDENTIFY word 49 lacks the LBA bit, with the drive's *current*
  translated geometry (words 54–56, which a BIOS may have changed)
  when valid, otherwise its default geometry (words 1/3/6). QEMU can't
  emulate a CHS-only drive, so on every LBA-capable drive the CHS path
  is **cross-checked** at boot: 4-sector reads across a head boundary
  and across a cylinder boundary, compared with LBA reads of the same
  sectors.
- **Block counts are 32-bit** (LBA28 caps at 128 GiB anyway), because
  `kprintf` and block arithmetic avoid 64-bit division; see M5.
- **Is sector 0 an MBR?** A partitionless FAT disk ("superfloppy")
  also ends sector 0 in `55 AA`. The first version decided by status
  bytes: all four entries 00/80, as Linux's msdos parser does. A test
  broke that assumption: `mformat`'s boot sector has zeros there, so it
  passed, and only happened to contain no plausible entry. An adversarial
  image with a plausible entry 1 produced a phantom `ata0p1`. The
  sector is now first checked for a valid FAT BIOS Parameter Block
  (512-byte sectors, power-of-two cluster size, at least 1 reserved
  sector, 1–2 FATs, a valid media byte), which an MBR can't have
  because boot code occupies those bytes.
- **Read-only.** Nothing writes yet (M7's filesystem starts read-only),
  so no untested write path can damage a real disk.

## Verification

`tests/console_test.py` now runs three machine configurations, each
with a generated disk image:

- **Partitioned disk:**
  - boot must report the model, size and geometry, *CHS cross-check
    passed*, and the partition;
  - `devices` shows the right sizes;
  - `read ata0 1` and `read ata0p1 5` return the sectors' own
    "ZKT disk/part sector N" text, which proves partition-relative
    addressing;
  - reading past the end gives *beyond end of device*;
  - bad arguments print the usage message.

  The gap before the partition is filled with per-sector text, so a
  misaddressed read, including in the CHS cross-check, can't pass by
  comparing zeros.
- **Partitionless FAT disk with a plausible fake entry:** no partitions
  may be registered.
- **No disk:** boot is unaffected.

Checked by hand:
- a disk as primary *slave* plus one on the secondary channel give
  `ata1`, `ata1p1`, `ata3` and `ata3p1`;
- a GRUB CD (ATAPI, secondary master) plus a disk boots, and the ATAPI
  drive is correctly skipped.

**Negative controls:**
- CHS sector numbering off by one gives *CHS cross-check FAILED*;
- without the BPB check, the adversarial superfloppy gets a phantom
  partition.

## Known limits

- Read-only; no DMA; no interrupt-driven transfers.
- LBA28 only (disks beyond 128 GiB are truncated); no LBA48.
- No extended/logical partitions and no GPT.
- No ATAPI (CD-ROM) support.
- The CHS path is validated only through the cross-check above, not on
  a genuine CHS-only drive.
