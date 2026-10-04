# M22 — writing to a FAT volume: the kernel's own FAT writer

**Status:** Achieved (2026-10-04), after 0.22.0; not yet released.
Written for better DOS compatibility: until M22 ManiOS could read a FAT
volume but not write one, so ManiDOS's `DEL`, `MD`, `RD`, `REN` and
`COPY` onto a file answered "Access denied", `> FILE` refused, libc's
`open(O_CREAT)`, `unlink`, `mkdir`, `remove` and `mkstemp` were stubs
that only ever returned EROFS, and OpenBSD's `sed -i` and `tee FILE`
said "read-only file system". The writes now come from the kernel's own
FAT writer: every change reaches the device before the call returns
(there is no dirty cache to flush), so an image file can be read with
mtools straight after a run -- which is how `tests/dos_test.py` checks
them.

## What M22 delivers

| Piece | Files | Summary |
|---|---|---|
| The syscalls | `zkt/abi/zkt_abi.h`, `zkt/kernel/syscall.c` | `SYS_UNLINK` 26, `SYS_MKDIR` 27, `SYS_RMDIR` 28, `SYS_RENAME` 29, and `O_CREAT` (0x0200), `O_TRUNC` (0x0400), `O_EXCL` (0x0800) in `SYS_OPEN`'s mode; ABI version 2, the oldest the kernel still runs being 1 |
| The VFS layer | `zkt/fs/vfs.c`, `zkt/fs/vfs.h` | `vfs_create`, `vfs_unlink`, `vfs_rmdir`, `vfs_rename`, and `vfs_open` taking the new flags: one directory reached two ways is a rename, two directories are `EXDEV`; `O_TRUNC` empties a file only, so a device or a directory opened for writing is untouched (`EISDIR` for the directory); a name that isn't there is `ENOENT` before the file system's own answer |
| The FAT writer | `zkt/fs/fat.c` | ~1,070 lines: a chain allocated and linked, entries placed at the end marker or in a deleted slot, removed, and renamed (the target's entry and chain dropped first, long-name slots beside either gone), both copies of the FAT written one after the other, `dir_is_empty` for `RD`, and `ENOSPC` when the disk is full |
| Read-only still | `zkt/fs/fat.c` (`fat_ro_ops`) | A volume whose device has no `write` (a CD, a write-protected floppy, a floppy without a disk) mounts read-only: its nodes get no create/remove/rename, so `open(O_CREAT)` and friends are `EROFS` there |
| Errors | `zkt/abi/zkt_abi.h`, `zkt/kernel/kerrno.c`, `libc/string.c` | `EXDEV` 18, `ENOSPC` 28, `ENOTEMPTY` 39, with their kernel and libc strings |
| libc | `libc/syscalls.c`, `libc/posix.c`, `libc/stat.c`, `libc/stdio.c`, headers | raw `unlink`, `rmdir`, `rename`, `mkdir`; POSIX `open` passing `O_CREAT`/`O_TRUNC`/`O_EXCL` through to the kernel; `remove()`; `mkstemp()`; `fopen`/`freopen`/`fdopen` truncating for `"w"` and seeking to the end for `"a"` |
| ManiDOS | `userland/dos/commands.c`, `userland/dos/main.c` | `DEL`/`ERASE`, `MD`/`MKDIR`, `RD`/`RMDIR`, `REN`/`RENAME`, `COPY` onto a file (and one file alone copied into the current directory), `> FILE` and `>> FILE`, each with DOS's own words; one `denied()` for "Access denied - that drive is read-only", "Insufficient disk space" and the rest |
| Tests | `tests/dos_test.py`, `tests/openbsd_test.py` | A write step at the prompt, and what was written checked on the image afterwards; `sed -i` and `tee FILE` writing |

## Design decisions

**Only FAT12/16, and only where the device can be written.** `ramfs`,
`bootfs`, `iso9660` and ZRP have no create, remove or rename, so they
keep answering `EROFS` -- the kernel's own tree and the boot archive are
not a place for a program to make files. Everything that rested on that
still holds: ctest's read-only cases, `dd` onto a device that takes no
writes (`driver_test`: "read-only file system" from the device, not
from the file system), and ZRP's `EROFS` on a read-only mount
(`net_test`). A FAT volume whose device has no `write` mounts with
`fat_ro_ops` and `fs->read_only` set, so A:, a CD and a write-protected
floppy all say the same thing: `Access denied - that drive is
read-only`. Devices and pipes still take writes as they always did:
`fopen("/dev/cons", "w")` is not affected by any of this.

**Write-through.** Each change is written to the device before the call
returns, both FAT copies one after the other. That keeps the writer
small (no cache coherence, no flush on close or on `sync`) and makes the
result inspectable from outside: `dos_test.py` reads the image after
ManiDOS exits. A crash between the two copies leaves them differing,
which `CHKDSK` already reports as a fault -- and the test checks they
match.

**8.3 on disk, no long-name entries written.** A name that doesn't fit
is made its numeric alias (`MANIOS~1.TXT`), which is what FAT itself
does with a long name it has no room for; a name that does fit is
written as it is. Lookups fall back to the alias, so a long name made
by another system still opens through its short form; renaming or
removing an entry drops the long-name slots it had. The cost is
documented in [docs/dos.md](../dos.md#differences-from-dos): two long
names that share their first six letters and their extension are told
apart when they are made (`~1`, `~2`) but not when they are asked for.

**POSIX order of errors.** A name that isn't there gives `ENOENT`
before the file system is asked whether it could write (a read-only
file system says `ENOENT` for a name it doesn't have, as POSIX has it);
`mkdir` on a name that is there gives `EEXIST`; `rmdir` of a file gives
`ENOTDIR` and of a directory holding files `ENOTEMPTY`. A rename takes
one directory: two routes to the same directory (two walks hand out two
vnodes) compare equal by path as well as by pointer, and only then is
`EXDEV` right.

**COPY's destination is opened when there is something to put in it.**
A source that isn't there leaves no stray file behind, and several
sources onto one file name are written one after the other, as DOS
appends them.

**No FORMAT.** `FORMAT`, `FDISK`, `LABEL` and `SYS` still aren't in
ManiDOS: they would write a disk's own structures. `CHKDSK /F` checks
and says it has no repair rather than claiming the drive is read-only,
which is no longer the reason.

## Bugs found on the way

- `new_directory` made the "." entry as `'.'`, a NUL and spaces (the
  fill started at byte 2), so `dir_is_empty`'s test for a dot entry
  never matched: **every `RD` of a directory ManiDOS had just made said
  it wasn't empty**, while mtools read the same directory as empty. The
  new test caught it by reading the image after the machine exited --
  the directory was still in the root, with its files gone.
- `dos_to_manios` kept a drive root's trailing separator, so
  `COPY README.TXT` (one name: copy into the current directory) built
  `/n/ata0p1//readme.txt`. The path is cleaned by the kernel, so it
  opened the *source* for writing: the "file cannot be copied onto
  itself" check missed, and the file was emptied and copied onto itself
  (0 bytes). Caught by the new test expecting that message.
- `vfs_unlink` on a name that isn't there gave `EROFS` rather than
  `ENOENT` on a read-only file system, which is what ctest has always
  checked (`unlink("/boot/etc/none") == ENOENT`). Fixed in the VFS: a
  directory whose file system has no remove of its own is asked whether
  the name is there first, and only then answers `EROFS`.

## Verification

- **`tests/dos_test.py`** (changed): a step at the prompt that copies,
  makes, renames and deletes -- each command with its errors too (a
  directory as a `DEL` target, a second `MD`, `RD` of a non-empty
  directory, a rename onto a name in use, a missing name) -- plus
  `DIR > LIST.TXT` and `>> FILE` appending; and then, after the machine
  has exited, the image is read directly: both copies of the FAT byte
  for byte, the names that were deleted absent and `LIST.TXT` there,
  its bytes holding what was redirected into it, `README.TXT` unchanged,
  `DOCS\README.TXT` (copied into the directory and deleted) gone, and
  the free space `CHKDSK` reported while the disk was mounted equal to
  the one the image's own FAT holds.
- **`tests/openbsd_test.py`** (changed): `sed -i` -- `mkstemp` in the
  working directory, then `rename` over the original -- and
  `tee FILE`, both writing the FAT volume, with the result read back.
- `tests/console_test.py` (`ctest`, `utest`) is unchanged and passes:
  the read-only answers it checks for the boot archive, and the `EROFS`
  a device gives, are what M22 keeps.
- `make test` stops at `gfx_test`, whose two VGA mode-13h font-restore
  checks fail (`font not restored`, `text differs at the end`) -- on a
  pristine 0.22.0 checkout as well, verified by stashing these changes,
  rebuilding and running the test there: 10 pass, the same two fail
  with and without them. The suites `make test` never reaches were run
  by hand, all passing: `vbe_test`, `desktop_test`, `dos_test`,
  `driver_test` (its read-only floppy and virtio disk still say
  "read-only file system") and `cluster_test`; `console_test`,
  `openbsd_test` and `net_test` passed in the same run before
  `gfx_test`.

## Known limits

- FAT12 and FAT16 only, and only a volume ManiOS has mounted: FAT32
  isn't mounted at all (`fat_mount` refuses it), so there is nothing to
  write there.
- A:, Z: and the CD drives take no writes: the boot archive, ManiOS's
  own tree and an ISO disc have no create/remove/rename. A floppy in
  B: is writable when it isn't write-protected.
- No long-name entries are written (above), no `FORMAT`, `FDISK`,
  `LABEL` or `SYS`, no `CHKDSK /F` repair, no `DEL /P /S /Q`, and no
  wildcards in `REN`.
- A file is renamed only within one directory: a rename across
  directories gives `EXDEV`, as DOS's `REN` does.
- Write-through means no write caching: every change is a device write
  (fine for what ManiOS does; a buffered writer is future work, and
  would have to keep both FAT copies in step on flush).
- Files have no times, so `DIR` shows no dates and a file ManiDOS
  writes gets the epoch, as one made by `mformat` does.
