# Installing ManiOS

ManiOS is published as one file, `manios-VERSION.iso`, on the
[Releases](https://github.com/zygvlogs/manios/releases) page. It boots
from a CD, from a USB stick, or in an emulator (QEMU, VirtualBox). From
there, `install` puts ManiOS on a hard disk. The boot and install steps
on this page are what `tests/install_test.py` does, in QEMU; ManiOS has
not been tried on real hardware yet (see "What to expect", below).

## What it runs on

- An i386-compatible PC with a BIOS (not UEFI-only machines; on UEFI
  machines, turn on "legacy" or "CSM" boot). ZKT is built for the 386
  instruction set with no FPU instructions.
- At least **6 MiB of memory** (the loader checks; 8 MiB is tested).
- An IDE/ATA hard disk, to install on (PIO mode; LBA or CHS).
- Optional: an NE2000-compatible ISA network card at I/O 0x300, IRQ 9;
  for the desktop (640x480 or more), a display adapter with the Bochs
  VBE registers: QEMU's and Bochs's standard one, VirtualBox's graphics
  controllers, or VMware's SVGA II. `gfxdemo vga` runs at 320x200 on any
  VGA card.

## Checking the download

Each release has a `SHA256SUMS` file:

```
sha256sum -c SHA256SUMS --ignore-missing
```

The ISO itself carries no build-time timestamps (`mkiso.py` uses a
fixed date), so the same boot area always gives the same ISO.

## Trying it in QEMU

```
qemu-system-i386 -cpu 486 -m 32 -cdrom manios-0.14.1.iso -boot d
```

Add `-serial stdio` to use the serial console from your terminal, and
`-netdev user,id=n0 -device ne2k_isa,netdev=n0,iobase=0x300,irq=9` for
the network. For the desktop, QEMU's default display adapter is the
right one. To try the installer, give it an empty disk:

```
qemu-img create -f raw disk.img 64M
qemu-system-i386 -cpu 486 -m 32 -cdrom manios-0.14.1.iso -boot d -hda disk.img
```

and after `install ata0`, boot the disk alone with `-hda disk.img -boot c`.

## Trying it in VirtualBox

Make a new virtual machine of type "Other" / "Other/Unknown", give it
32 MiB of memory or more and an IDE hard disk to install on, and put
the ISO in its CD drive. ManiOS knows VirtualBox's three graphics
controllers for the desktop: VMSVGA is VMware's SVGA II, which the tests
cover through QEMU's copy of it; VBoxVGA and VBoxSVGA are untested so
far -- if `desktop` says there is no framebuffer, try VMSVGA. VirtualBox
has no NE2000 network card, so there is no network there yet.

## The boot loader

The loader prints its version and the boot options -- the kernel's
command line -- then waits 3 seconds:

```
ManiOS boot loader 0.14.1
Boot options: (none)
Press any key within 3 seconds to change them.
```

Most of the time there is nothing to do: it starts ManiOS by itself.
Press any key (on the keyboard, or on COM1) to change the options for
this boot:

```
Type the boot options, then press Enter to start ManiOS.
Backspace deletes a character; Escape clears the line.
boot: sysname=box ip=10.0.0.5/24
```

The options are words of the form `KEY=VALUE`:

| Key | Meaning |
|---|---|
| `sysname=NAME` | the machine's name (`cat /dev/sysname`) |
| `ip=ADDR/PREFIX` | the network address, e.g. `ip=10.0.0.5/24` |
| `gw=ADDR` | the default gateway |
| `key=SECRET` | the cluster key: every machine of a cluster needs the same one ([ADR-0005](adr/0005-cluster-roles-and-authentication.md)) |
| `export=PATH` | serve PATH to other machines over ZRP (a file server) |
| `rc=PATH` | a boot script to run before the shell, e.g. `rc=/boot/etc/rc.cpu` for a CPU server |
| `verbose=1` | show the whole boot log on the screen (see below) |

## Starting up

ManiOS tests itself at every boot, and the screen shows what it checks:

```
ManiOS 0.14.1 / ZKT (ZygKernel Technology)
Self-tests: memory, interrupts, threads, devices, disks, files, programs,
            C library, network, graphics, windows, cluster. All passed.
ManiOS 0.14.1 is ready. Try ls /bin (programs), help (the shell), desktop.
manios%
```

If a check fails, ManiOS stops with a message saying which. The full log
-- every self-test, including test programs that crash on purpose to
check that ManiOS survives them ("killed: Page fault", "assertion
failed") -- always goes to the serial port (COM1), and with `verbose=1`
to the screen too.

If the loader cannot go on, it says why (after `ManiOS boot loader: `)
and stops:

| Message | Meaning |
|---|---|
| `not enough memory: ManiOS needs 6 MiB` | as it says |
| `the boot area is damaged (its checksum is wrong)` | the CD, stick or disk has bad data: write it again |
| `the boot area's header is damaged` | likewise |
| `disk error` | the BIOS could not read the disk |
| `cannot enable the A20 line` | the machine won't address memory above 1 MiB |

Before the loader, a hard disk's boot sector can say:

| Message | Meaning |
|---|---|
| `ManiOS: no boot partition` | the disk has no partition of type 0xDA |
| `ManiOS: bad boot area` | that partition doesn't hold a ManiOS boot area |
| `ManiOS: disk error` | the BIOS could not read the disk |

## A USB stick

The ISO is also a hard disk image: write it to the whole stick (not a
partition of it), and the stick boots on BIOSes that boot from USB as a
hard disk. **This destroys everything on the stick.** On Linux, with the
stick at `/dev/sdX`:

```
sudo dd if=manios-0.14.1.iso of=/dev/sdX bs=1M conv=fsync
```

## Installing on a hard disk

Boot the CD (or stick) on the machine, then, at the `manios% ` prompt:

```
manios% install ata0 sysname=box ip=10.0.0.5/24
ManiOS 0.14.1 will be installed on ata0 (512 MiB).
EVERYTHING ON ata0 WILL BE LOST.
The installed system's command line: "sysname=box ip=10.0.0.5/24"
Type yes to go on: yes
writing the boot area (678 KiB)...
checking what was written...
ManiOS 0.14.1 is installed on ata0. Remove the CD and restart the machine.
```

- `ata0` is the first IDE disk (primary master), `ata1` the second, and
  so on; `ls /dev` lists them. Name the whole disk, not a partition
  (`ata0p1`).
- The words after the disk become the installed system's command line;
  the loader still offers to edit it at every boot.
- `install -y` skips the question, for scripts.
- The disk gets a new MBR with one active partition of type 0xDA,
  starting at 1 MiB and just large enough for ManiOS (under 1 MiB, rounded
  up to a MiB). The rest of the disk is left unpartitioned. Nothing else
  on the disk survives: the old partition table is replaced.
- `install` reads everything back and says so if the disk didn't keep
  what was written.

An installed ManiOS can install itself again, on another disk
(`install ata1`): `/dev/bootarea` is the boot area the machine started
from, whichever medium that was.

## What an installation is, today

The installed system is the live system: the same kernel and the same
boot archive (`/boot`, `/bin`) as the CD, read-only, with its own command
line. There is no writable root filesystem yet; FAT volumes on other
partitions or disks are mounted at `/n/ataXpY` as before. What you
change at run time -- binds, mounts, the desktop -- lasts until you
restart.

## What to expect

ManiOS is young. Real hardware has not been tested; QEMU's SeaBIOS is
the only BIOS the tests use (ManiOS 0.14.0 was also seen booting in
VirtualBox, whose BIOS is different), and the loader's CHS path (for
BIOSes without the LBA extensions) is tested with a build that pretends
the extensions are missing. If you try it on an old machine, a report of
what happened -- the machine, the BIOS date, and the last line on the
screen -- is very welcome as an issue.
